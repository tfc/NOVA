/*
 * System Memory Management Unit (ARM SMMUv2)
 *
 * Copyright (C) 2019-2024 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "bits.hpp"
#include "hip.hpp"
#include "interrupt.hpp"
#include "lock_guard.hpp"
#include "lowlevel.hpp"
#include "smmu.hpp"
#include "space_dma.hpp"
#include "space_hst.hpp"
#include "stdio.hpp"

INIT_PRIORITY (PRIO_SLAB) Slab_cache Smmu::cache { sizeof (Smmu), alignof (Smmu) };

Smmu::Smmu (Board::Smmu const &brd) : List (list), board (brd)
{
    // Map first SMMU page
    Hptp::master_map (mmap, board.mmio, 0,
                      Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::dev());

    // This facilitates access to the GR0 register space only
    mmio_base_gr0 = mmap;

    auto const idr0 { read (GR0_Reg32::IDR0) };
    auto const idr1 { read (GR0_Reg32::IDR1) };
    auto const idr2 { read (GR0_Reg32::IDR2) };
    auto const idr7 { read (GR0_Reg32::IDR7) };

    // Determine SMMU capabilities
    mode      = idr0 & BIT (27) ? Mode::STREAM_MATCHING : Mode::STREAM_INDEXING;
    sidx_bits = idr0 & BIT (8) ? 16 : idr0 >> 9 & BIT_RANGE (3, 0);
    page_size = idr1 & BIT (31) ? BIT (16) : BIT (12);
    num_smg   = BIT_RANGE (7, 0) & idr0;
    num_ctx   = BIT_RANGE (7, 0) & idr1;
    ias       = BIT_RANGE (3, 0) & idr2;
    oas       = BIT_RANGE (3, 0) & idr2 >> 4;

    // Treat DPT as noncoherent if at least one SMMU requires it
    Dpt::noncoherent |= !(idr0 & BIT (14));

    // Determine total size of the SMMU
    auto const smmu_pnum { BIT ((idr1 >> 28 & BIT_RANGE (2, 0)) + 1) };
    auto const smmu_size { page_size * smmu_pnum * 2 };

    // Map all SMMU pages
    Hptp::master_map (mmap, board.mmio, static_cast<unsigned>(bit_scan_msb (smmu_size)) - PAGE_BITS,
                      Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::dev());

    // This facilitates access to the GR1 and CTX register spaces
    mmio_base_gr1 = mmio_base_gr0 + page_size;
    mmio_base_ctx = mmio_base_gr0 + page_size * smmu_pnum;

    // Allocate configuration table
    config = new Config;

    trace (TRACE_SMMU, "SMMU: %#010lx %#x r%up%u S1:%u S2:%u N:%u C:%u SMG:%u CTX:%u SID:%u-bit Mode:%u",
           board.mmio, smmu_size, idr7 >> 4 & BIT_RANGE (3, 0), idr7 & BIT_RANGE (3, 0),
           !!(idr0 & BIT (30)), !!(idr0 & BIT (29)), !!(idr0 & BIT (28)), !!(idr0 & BIT (14)),
           num_smg, num_ctx, sidx_bits, std::to_underlying (mode));

    // Reserve MMIO region
    Space_hst::access_ctrl (board.mmio, smmu_size, Paging::NONE);

    // Advance memory map pointer
    mmap += smmu_size;

    Hip::set_feature (Hip_arch::Feature::SMMU);
}

void Smmu::init()
{
    // Configure global fault interrupts
    for (unsigned i { 0 }; i < sizeof (board.glb) / sizeof (*board.glb); i++)
        if (board.glb[i].flg)
            Interrupt::conf_spi (board.glb[i].spi, false, board.glb[i].flg & BIT_RANGE (3, 2), Cpu::id);

    // Configure context fault interrupts
    for (unsigned i { 0 }; i < sizeof (board.ctx) / sizeof (*board.ctx); i++)
        if (board.ctx[i].flg)
            Interrupt::conf_spi (board.ctx[i].spi, false, board.ctx[i].flg & BIT_RANGE (3, 2), Cpu::id);

    // Configure CTXs
    for (uint8_t ctx { 0 }; ctx < num_ctx; ctx++)
        write (ctx, GR1_Arr32::CBAR, BIT (17));       // Generate "invalid context" fault

    // Configure SMGs
    for (uint8_t smg { 0 }; smg < num_smg; smg++)
        if (!conf_smg (smg))
            write (smg, GR0_Arr32::S2CR, BIT (17));   // Generate "invalid context" fault

    write (GR0_Reg32::CR0, BIT (21) | BIT_RANGE (12, 11) | BIT (10) | BIT_RANGE (5, 4) | BIT_RANGE (2, 1));
}

bool Smmu::conf_smg (uint8_t smg)
{
    // Obtain SMG configuration
    auto const dma { config->entry[smg].dma };
    auto const sid { config->entry[smg].sid };
    auto const msk { config->entry[smg].msk };
    auto const ctx { config->entry[smg].ctx };

    if (!dma)
        return false;

    auto const sdid { dma->get_sdid() };

    // Disable CTX during configuration
    write (ctx, Ctx_Arr32::SCTLR, 0);

    // Invalidate stale TLB entries for SDID
    tlb_invalidate (sdid);

    // Configure CTX as VA64 stage-2
    write (ctx, GR1_Arr32::CBA2R, BIT (0));
    write (ctx, GR1_Arr32::CBAR,  sdid & BIT_RANGE (7, 0));

    // Determine input size and number of levels
    auto const isz { Dpt::pas (ias) };
    auto const lev { Dpt::lev (isz) };

    // Configure and enable CTX
    write (ctx, Ctx_Arr32::TCR,   oas << 16 | TCR_TG0_4K | TCR_SH0_INNER | TCR_ORGN0_WB_RW | TCR_IRGN0_WB_RW | (lev - 2) << 6 | (64 - isz));
    write (ctx, Ctx_Arr64::TTBR0, Kmem::ptr_to_phys (dma->get_ptab (lev - 1)));
    write (ctx, Ctx_Arr32::SCTLR, BIT_RANGE (6, 5) | BIT (0));

    // Disable SMG during configuration
    write (smg, GR0_Arr32::SMR, 0);

    // Configure and enable SMG
    write (smg, GR0_Arr32::S2CR, BIT (27) | ctx);
    write (smg, GR0_Arr32::SMR,  BIT (31) | msk << 16 | sid);

    return true;
}

bool Smmu::configure (Space_dma *dma, uintptr_t dad)
{
    auto const sid { static_cast<uint16_t>(dad) };
    auto const msk { static_cast<uint16_t>(dad >> 16) };
    auto       smg { static_cast<uint8_t> (dad >> 32) };
    auto const ctx { static_cast<uint8_t> (dad >> 40) };

    // When using stream indexing, the maximum SID size is 7 bits and selects the SMG directly
    if (mode == Mode::STREAM_INDEXING)
        smg = static_cast<uint8_t>(sid);

    if (!config || (sid | msk) >= BIT (sidx_bits) || smg >= num_smg || ctx >= num_ctx)
        return false;

    trace (TRACE_SMMU, "SMMU: SID:%#06x MSK:%#06x SMG:%#04x CTX:%#04x assigned to Domain %u", sid, msk, smg, ctx, static_cast<unsigned>(dma->get_sdid()));

    Lock_guard <Spinlock> guard { cfg_lock };

    // Remember SMG configuration for suspend/resume
    config->entry[smg].dma = dma;
    config->entry[smg].sid = sid;
    config->entry[smg].msk = msk;
    config->entry[smg].ctx = ctx;

    return conf_smg (smg);
}

void Smmu::fault()
{
    auto const gfsr { read (GR0_Reg32::GFSR) };

    if (gfsr & BIT_RANGE (8, 0)) {

        auto const syn { read (GR0_Reg32::GFSYNR0) };

        trace (TRACE_SMMU, "SMMU: GLB Fault (M:%u UUT:%u P:%u E:%u CA:%u UCI:%u UCB:%u SMC:%u US:%u IC:%u) at %#010lx (%c%c%c) SID:%#x",
               !!(gfsr & BIT (31)), !!(gfsr & BIT (8)), !!(gfsr & BIT (7)), !!(gfsr & BIT (6)), !!(gfsr & BIT (5)),
               !!(gfsr & BIT (4)),  !!(gfsr & BIT (3)), !!(gfsr & BIT (2)), !!(gfsr & BIT (1)), !!(gfsr & BIT (0)),
               read (GR0_Reg64::GFAR),
               syn & BIT (3) ? 'I' : 'D',       // Instruction / Data
               syn & BIT (2) ? 'P' : 'U',       // Privileged / Unprivileged
               syn & BIT (1) ? 'W' : 'R',       // Write / Read
               read (GR0_Reg32::GFSYNR1) & BIT_RANGE (15, 0));

        write (GR0_Reg32::GFSR, gfsr);
    }

    for (unsigned ctx { 0 }; ctx < num_ctx; ctx++) {

        auto const fsr { read (ctx, Ctx_Arr32::FSR) };

        if (fsr & BIT_RANGE (8, 1)) {

            auto const syn { read (ctx, Ctx_Arr32::FSYNR0) };

            trace (TRACE_SMMU, "SMMU: C%02u Fault (M:%u SS:%u UUT:%u AS:%u LK:%u MC:%u E:%u P:%u A:%u T:%u) at %#010lx (%c%c%c) LVL:%u",
                   ctx, !!(fsr & BIT (31)), !!(fsr & BIT (30)),
                   !!(fsr & BIT (8)), !!(fsr & BIT (7)), !!(fsr & BIT (6)), !!(fsr & BIT (5)),
                   !!(fsr & BIT (4)), !!(fsr & BIT (3)), !!(fsr & BIT (2)), !!(fsr & BIT (1)),
                   read (ctx, Ctx_Arr64::FAR),
                   syn & BIT (6) ? 'I' : 'D',   // Instruction / Data
                   syn & BIT (5) ? 'P' : 'U',   // Privileged / Unprivileged
                   syn & BIT (4) ? 'W' : 'R',   // Write / Read
                   syn & BIT_RANGE (1, 0));

            write (ctx, Ctx_Arr32::FSR, fsr);
        }
    }
}

/*
 * TLB Invalidate by IPA
 */
void Smmu::tlb_invalidate (unsigned ctx, uint64_t ipa)
{
    // Post TLB maintenance operation
    write (ctx, Ctx_Arr64::TLBIIPAS2, ipa >> 12);

    // Ensure completion
    tlb_sync_ctx (ctx);
}

/*
 * TLB Invalidate by VMID
 */
void Smmu::tlb_invalidate (Sdid vmid)
{
    // Post TLB maintenance operation
    write (GR0_Reg32::TLBIVMID, vmid & BIT_RANGE (15, 0));

    // Ensure completion
    tlb_sync_glb();
}

/*
 * Ensure completion of one or more posted TLB invalidate operations
 * accepted in the specified translation context bank only.
 */
void Smmu::tlb_sync_ctx (unsigned ctx)
{
    Lock_guard <Spinlock> guard { inv_lock };

    write (ctx, Ctx_Arr32::TLBSYNC, 0);

    while (read (ctx, Ctx_Arr32::TLBSTATUS) & BIT (0))
        pause();
}

/*
 * Ensure completion of one or more posted TLB invalidate operations
 * accepted in the global address space or in any translation context bank.
 */
void Smmu::tlb_sync_glb()
{
    Lock_guard <Spinlock> guard { inv_lock };

    write (GR0_Reg32::TLBGSYNC, 0);

    while (read (GR0_Reg32::TLBGSTATUS) & BIT (0))
        pause();
}
