/*
 * Generic Interrupt Controller: Physical CPU Interface (GICC/ICC)
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

#pragma once

#include "barrier.hpp"
#include "intid.hpp"
#include "memory.hpp"
#include "sysreg.hpp"
#include "types.hpp"

class Gicc final : private Intid
{
    friend class Acpi_table_madt;

    private:
        enum class Reg32 : unsigned
        {
            CTLR        = 0x0000,   // Control Register
            PMR         = 0x0004,   // Priority Mask Register
            BPR         = 0x0008,   // Binary Point Register
            IAR         = 0x000c,   // Interrupt Acknowledge Register
            EOIR        = 0x0010,   // End Of Interrupt Register
            RPR         = 0x0014,   // Running Priority Register
            HPPIR       = 0x0018,   // Highest Priority Pending Interrupt Register
            ABPR        = 0x001c,   // Aliased Binary Point Register
            AIAR        = 0x0020,   // Aliased Interrupt Acknowledge Register
            AEOIR       = 0x0024,   // Aliased End Of Interrupt Register
            AHPPIR      = 0x0028,   // Aliased Highest Priority Pending Interrupt Register
            APRn        = 0x00d0,   // Active Priorities Registers
            NSAPRn      = 0x00e0,   // Non-Secure Active Priorities Registers
            IIDR        = 0x00fc,   // Implementer Identification Register
            DIR         = 0x1000,   // Deactivate Interrupt Register
        };

        static inline constinit uint64_t phys { Board::gic[2].mmio };

        static auto read  (Reg32 r)      { return *reinterpret_cast<uint32_t volatile *>(MMAP_GLB_GICC + std::to_underlying (r)); }
        static void write (Reg32 r, uint32_t v) { *reinterpret_cast<uint32_t volatile *>(MMAP_GLB_GICC + std::to_underlying (r)) = v; }

        SYSREG32_RW (el1_pmr,       S3_0_C4_C6_0)
        SYSREG32_WO (el1_dir,       S3_0_C12_C11_1)
        SYSREG64_WO (el1_sgi1r,     S3_0_C12_C11_5)
        SYSREG32_RO (el1_iar1,      S3_0_C12_C12_0)
        SYSREG32_WO (el1_eoir1,     S3_0_C12_C12_1)
        SYSREG32_RW (el1_bpr1,      S3_0_C12_C12_3)
        SYSREG32_RW (el1_ctlr,      S3_0_C12_C12_4)
        SYSREG32_RW (el1_igrpen1,   S3_0_C12_C12_7)
        SYSREG32_RW (el2_sre,       S3_4_C12_C9_5)

        static void mmap_mmio();
        static void init_mmio();
        static void init_regs();
        static void init_mode();

        static void send_sgi (uint64_t v)
        {
            /*
             * Ensure all earlier stores are observable in the ISH domain
             * before the SGI gets sent. Because the SGIR write is a system
             * register operation, we also need to ensure store completion.
             */
            Barrier::wsb (Barrier::Domain::ISH);

            set_el1_sgi1r (v);

            // Ensure system register write executed
            Barrier::isb();
        }

    public:
        enum class Mode : unsigned
        {
            MMIO,
            REGS,
        };

        static Mode mode CPULOCAL;

        static void init();

        static uint32_t ack()
        {
            // Reads of ICC_IAR1_EL1 are self-synchronizing when PSTATE.I == 1
            if (mode == Mode::REGS)
                return get_el1_iar1();
            else
                return read (Reg32::IAR);
        }

        static void eoi (uint32_t val)
        {
            if (mode == Mode::REGS) {
                set_el1_eoir1 (val);
                Barrier::isb();
            } else
                write (Reg32::EOIR, val);
        }

        static void dir (uint32_t val)
        {
            if (mode == Mode::REGS) {
                set_el1_dir (val);
                Barrier::isb();
            } else
                write (Reg32::DIR, val);
        }

        static void send_cpu (unsigned, cpu_t);
        static void send_exc (unsigned);
};
