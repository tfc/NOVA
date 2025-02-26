/*
 * Central Processing Unit (CPU)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2014 Udo Steinberg, FireEye, Inc.
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

#include "acpi.hpp"
#include "bits.hpp"
#include "cache.hpp"
#include "cmdline.hpp"
#include "cos.hpp"
#include "counter.hpp"
#include "fpu.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "lapic.hpp"
#include "mca.hpp"
#include "pconfig.hpp"
#include "space_hst.hpp"
#include "stdio.hpp"
#include "svm.hpp"
#include "timeout.hpp"
#include "tss.hpp"
#include "vmx.hpp"

bool Cpu::bsp;
cpu_t Cpu::id;
unsigned Cpu::hazard, Cpu::platform, Cpu::family, Cpu::model, Cpu::stepping, Cpu::patch;
uint32_t Cpu::cstates, Cpu::topology, Cpu::features[13];
uint64_t Cpu::csthint;
Cpu::Vendor Cpu::vendor;
Cpu::State_tsc Cpu::hst_tsc;

void Cpu::enumerate_clocks (uint32_t &clk, uint32_t &rat, scaleable_bus const *freq, unsigned i)
{
    clk = freq[i].d ? 100'000'000 * freq[i].m / freq[i].d : 0;
    rat = Msr::read (Msr::Reg64::PLATFORM_INFO) >> 8 & BIT_RANGE (7, 0);
}

void Cpu::enumerate_clocks (uint32_t &clk, uint32_t &rat)
{
    // Intel P-core >= CNL, E-core >= GLM+: CPUID[0x15] reports both clk and rat
    if (clk && rat)
        return;

    if (vendor != Vendor::INTEL || family != 0x6)
        return;

    // Use model-specific knowledge
    switch (model) {

        // P-core >= SKL: CPUID[0x15] reports rat only
        case 0xa6:              // CML-U62
        case 0xa5:              // CML-H, CML-S
        case 0x9e:              // KBL-H, KBL-S, KBL-G, KBL-X, CFL-H, CFL-S, Xeon E3v6
        case 0x8e:              // KBL-Y, KBL-U, KBL-R, AML-Y, WHL-U, CFL-U, CML-U42
        case 0x5e:              // SKL-H, SKL-S, Xeon E3v5
        case 0x4e:              // SKL-Y, SKL-U
            clk = 24'000'000;   // 24 MHz
            return;

        // P-core <= BDW: CPUID[0x15] unavailable
        case 0x6a:              // ICX
        case 0x55:              // SKX/CLX/CPX
        case 0x56:              // BDW-DE
        case 0x4f:              // BDW-E, BDW-EP, BDW-EX
        case 0x3f:              // HSW-E, HSW-EP, HSW-EX
        case 0x3e:              // IVB-E, IVB-EP, IVB-EX
        case 0x2d:              // SNB-E, SNB-EP
        case 0x47:              // BDW-H, Xeon E3v4
        case 0x3d:              // BDW-Y, BDW-U
        case 0x46:              // HSW-H, HSW-R
        case 0x45:              // HSW-Y, HSW-U
        case 0x3c:              // HSW-M, Xeon E3v3
        case 0x3a:              // IVB, Xeon E3v2, Gladden
        case 0x2a:              // SNB, Xeon E3v1
            return enumerate_clocks (clk, rat, freq_core, 5);   // 100.00 MHz

        case 0x2f:              // WSM-EX (Eagleton)
        case 0x2c:              // WSM-EP (Gulftown)
        case 0x25:              // WSM (Clarkdale, Arrandale)
        case 0x2e:              // NHM-EX (Beckton)
        case 0x1a:              // NHM-EP (Gainestown, Bloomfield)
        case 0x1f:              // NHM (Havendale, Auburndale)
        case 0x1e:              // NHM (Lynnfield, Clarksfield, Jasper Forest)
            return enumerate_clocks (clk, rat, freq_core, 1);   // 133.33 MHz

        case 0x1d:              // Dunnington
        case 0x17:              // Penryn, Wolfdale, Yorkfield, Harpertown
        case 0x0f:              // Merom, Conroe, Kentsfield, Clovertown, Woodcrest, Tigerton
            return enumerate_clocks (clk, rat, freq_core, Msr::read (Msr::Reg64::FSB_FREQ) & BIT_RANGE (2, 0));

        // E-core >= GLM: CPUID[0x15] reports rat only
        case 0x5f:              // GLM (Harrisonville: Denverton)
            clk = 25'000'000;   // 25 MHz
            return;

        case 0x5c:              // GLM (Willow Trail: Broxton, Apollo Lake)
            clk = 19'200'000;   // 19.2 MHz
            return;

        // E-core <= AMT: CPUID[0x15] unavailable
        case 0x4c:              // AMT (Cherry Trail: Cherryview, Braswell)
            return enumerate_clocks (clk, rat, freq_atom, Msr::read (Msr::Reg64::FSB_FREQ) & BIT_RANGE (3, 0));

        case 0x5d:              // SLM (Slayton: Sofia 3G)
        case 0x5a:              // SLM (Moorefield: Anniedale)
        case 0x4a:              // SLM (Merrifield: Tangier)
        case 0x37:              // SLM (Bay Trail: Valleyview)
            return enumerate_clocks (clk, rat, freq_atom, Msr::read (Msr::Reg64::FSB_FREQ) & BIT_RANGE (2, 0));

        case 0x75:              // AMT (Lightning Mountain)
        case 0x6e:              // AMT (Cougar Mountain)
        case 0x65:              // AMT (XGold XMM7272)
        case 0x4d:              // SLM (Edisonville: Avoton, Rangeley)
            return;
    }
}

void Cpu::enumerate_topology (uint32_t leaf, uint32_t &topology, uint32_t (&lvl)[4])
{
    uint32_t eax, ebx, ecx, edx;

    for (unsigned i { 0 }, s { 0 }, b; i < sizeof (lvl) / sizeof (*lvl); i++, s = b) {

        cpuid (leaf, i, eax, ebx, ecx, edx);

        if (EXPECT_TRUE (ebx)) {
            lvl[i] = ((topology = edx) & ~(~0U << (b = eax & BIT_RANGE (4, 0)))) >> s;
            continue;
        }

        if (EXPECT_TRUE (i))
            lvl[i] = topology >> s;

        break;
    }
}

void Cpu::enumerate_features (uint32_t &clk, uint32_t &rat, uint32_t (&lvl)[4], uint32_t (&name)[12])
{
    uint32_t eax, ebx, ecx, edx, rti, cpp { 1 };

    cpuid (0, eax, ebx, ecx, edx);

    size_t v { sizeof (vendor_string) / sizeof (*vendor_string) };
    while (--v)
        if (Signature::u32 (vendor_string[v] + 0) == ebx && Signature::u32 (vendor_string[v] + 4) == edx && Signature::u32 (vendor_string[v] + 8) == ecx)
            break;

    vendor = Vendor (v);

    if (vendor == Vendor::INTEL) {
        Msr::write (Msr::Reg64::IA32_BIOS_SIGN_ID, 0);
        platform = static_cast<unsigned>(Msr::read (Msr::Reg64::IA32_PLATFORM_ID) >> 50) & 7;
    }

    topology = invalid_topology;

    switch (static_cast<uint8_t>(eax)) {
        default:
            enumerate_topology (0x1f, topology, lvl);
            [[fallthrough]];
        case 0x15 ... 0x1e:
            cpuid (0x15, eax, ebx, clk, edx);
            rat = eax ? ebx / eax : 0;
            [[fallthrough]];
        case 0x10 ... 0x14:
            cpuid (0x10, 0, eax, rti, ecx, edx);
            if (rti & BIT (1)) {    // CAT/CDP L3
                cpuid (0x10, 1, eax, ebx, ecx, edx);
                Cos::hcb_l3 = (eax & BIT_RANGE (4, 0));
                Cos::cos_l3 = (edx & BIT_RANGE (6, 0)) + 1;         // max 128
                Cos::supcfg |= static_cast<uint8_t>(!!(ecx & BIT (2)) * Cos::CDP_L3);
            }
            if (rti & BIT (2)) {    // CAT/CDP L2
                cpuid (0x10, 2, eax, ebx, ecx, edx);
                Cos::hcb_l2 = (eax & BIT_RANGE (4, 0));
                Cos::cos_l2 = (edx & BIT_RANGE (5, 0)) + 1;         // max 64
                Cos::supcfg |= static_cast<uint8_t>(!!(ecx & BIT (2)) * Cos::CDP_L2);
            }
            if (rti & BIT (3)) {    // MBA
                cpuid (0x10, 3, eax, ebx, ecx, edx);
                Cos::del_mb = (eax & BIT_RANGE (11, 0)) + 1;
                Cos::cos_mb = (edx & BIT_RANGE  (5, 0)) + 1;        // max 64
            }
            [[fallthrough]];
        case 0xd ... 0xf:
            cpuid (0xd, 0, eax, ebx, ecx, edx);
            Fpu::hst_xsv.xcr = Fpu::managed & (static_cast<uint64_t>(edx) << 32 | eax);
            cpuid (0xd, 1, eax, ebx, ecx, edx);
            Fpu::hst_xsv.xss = Fpu::managed & (static_cast<uint64_t>(edx) << 32 | ecx);
            [[fallthrough]];
        case 0xb ... 0xc:
            if (topology == invalid_topology)
                enumerate_topology (0xb, topology, lvl);
            [[fallthrough]];
        case 0x7 ... 0xa:
            cpuid (0x7, 0x0, eax, features[3], features[4], features[5]);
            cpuid (0x7, 0x1, features[6], features[7], features[8], features[9]);
            cpuid (0x7, 0x2, eax, ebx, ecx, features[10]);
            [[fallthrough]];
        case 0x6:
            cpuid (0x6, features[2], ebx, ecx, edx);
            [[fallthrough]];
        case 0x5:
            cpuid (0x5, eax, ebx, ecx, cstates);
            [[fallthrough]];
        case 0x4:
            cpuid (0x4, 0x0, eax, ebx, ecx, edx);
            cpp = (eax >> 26 & BIT_RANGE (5, 0)) + 1;
            [[fallthrough]];
        case 0x1 ... 0x3:
            cpuid (0x1, eax, ebx, features[0], features[1]);
            family   = (eax >> 8 & 0xf) + (eax >> 20 & 0xff);
            model    = (eax >> 4 & 0xf) + (eax >> 12 & 0xf0);
            stepping =  eax & 0xf;
            Cache::init (8 * (ebx >> 8 & BIT_RANGE (7, 0)));
            if (topology == invalid_topology) {
                topology = ebx >> 24;
                auto const tpp { feature (Feature::HTT) ? ebx >> 16 & BIT_RANGE (7, 0) : 1 };
                auto const tpc { tpp / cpp };
                auto const c { bit_scan_msb (cpp - 1) + 1 };
                auto const t { bit_scan_msb (tpc - 1) + 1 };
                lvl[2] = topology >> (c + t);
                lvl[1] = topology >> t & ~(~0U << c);
                lvl[0] = topology      & ~(~0U << t);
            }
            [[fallthrough]];
        case 0x0:
            break;
    }

    patch = static_cast<unsigned>(Msr::read (Msr::Reg64::IA32_BIOS_SIGN_ID) >> 32);

    cpuid (0x80000000, eax, ebx, ecx, edx);

    if (eax & 0x80000000) {
        switch (static_cast<uint8_t>(eax)) {
            default:
                cpuid (0x8000000a, Vmcb::svm_version, ebx, ecx, Vmcb::svm_feature);
                [[fallthrough]];
            case 0x4 ... 0x9:
                cpuid (0x80000004, name[8], name[9], name[10], name[11]);
                [[fallthrough]];
            case 0x3:
                cpuid (0x80000003, name[4], name[5], name[6], name[7]);
                [[fallthrough]];
            case 0x2:
                cpuid (0x80000002, name[0], name[1], name[2], name[3]);
                [[fallthrough]];
            case 0x1:
                cpuid (0x80000001, eax, ebx, features[11], features[12]);
                [[fallthrough]];
            case 0x0:
                break;
        }
    }

    if (EXPECT_FALSE (Cmdline::nodl))
        defeature (Feature::TSC_DEADLINE);

    if (EXPECT_FALSE (Cmdline::nopcid))
        defeature (Feature::PCID);

    enumerate_clocks (clk, rat);
}

void Cpu::setup_msr()
{
    if (EXPECT_TRUE (feature (Feature::ACPI)))
        Msr::write (Msr::Reg64::IA32_THERM_INTERRUPT, 0x10);

    if (EXPECT_TRUE (feature (Feature::SEP)))
        Msr::write (Msr::Reg64::IA32_SYSENTER_CS, 0);

    if (EXPECT_TRUE (feature (Feature::LM))) {
        Msr::write (Msr::Reg64::IA32_STAR,  hst_sys.star);
        Msr::write (Msr::Reg64::IA32_LSTAR, hst_sys.lstar);
        Msr::write (Msr::Reg64::IA32_FMASK, hst_sys.fmask);
        Msr::write (Msr::Reg64::IA32_KERNEL_GS_BASE, hst_sys.kernel_gs_base);
    }

    if (EXPECT_TRUE (feature (Feature::RDPID) || feature (Feature::RDTSCP)))
        Msr::write (Msr::Reg64::IA32_TSC_AUX, hst_tsc.tsc_aux = Cpu::id);

    if (EXPECT_TRUE (feature (Feature::TME)) && bsp) {

        trace (TRACE_CPU, "TMEE: Split:%u/%u Keys:%u Algo:%#x", Memattr::kbits, Memattr::obits, Memattr::kimax, Memattr::crypt);

        // FIXME: Check for valid PCONFIG targets
        if (EXPECT_TRUE (feature (Feature::PCONFIG) && Memattr::crypt)) {

            Pconfig::Encrypt e { static_cast<uint8_t>(BIT (bit_scan_msb (Memattr::crypt))) };

            for (uint16_t i { 0 }; i < Memattr::kimax; i++)
                (Cmdline::nomktme ? Pconfig::key_clr : Pconfig::key_rnd)(i + 1, e);
        }
    }

#if 0
    // Disable C1E on AMD Rev.F and beyond because it stops LAPIC clock
    if (vendor == Vendor::AMD)
        if (family > 0xf || (family == 0xf && model >= 0x40))
            Msr::write (Msr::Reg64::AMD_IPMR, Msr::read (Msr::Reg64::AMD_IPMR) & ~(3UL << 27));
#endif
}

void Cpu::setup_cst()
{
    if (Cmdline::noccst || vendor != Vendor::INTEL)
        return;

    if (!feature (Feature::MONITOR) || !feature (Feature::ARAT))
        return;

    // Unsupported states are demoted to the next lower supported state
    csthint = 0xf0;
    for (unsigned i { 0 }; i < 7; i++) {
        auto const s { supports (Cstate { 8 * (i + 1) }) };
        csthint |= (s ? i << 4 | (s - 1) : csthint >> 8 * i & BIT_RANGE (7, 0)) << 8 * (i + 1);
    }

    auto const ctl { Msr::read (Msr::Reg64::POWER_CTL) };
    auto const cfg { Msr::read (Msr::Reg64::CST_CONFIG) & ~(BIT (31) | BIT (16)) };

    // Disable timed MWAIT and ACC
    Msr::write (Msr::Reg64::CST_CONFIG, cfg);

    trace (TRACE_CPU, "CCST:%s%s%s%s%s%s%s (%#lx:%#lx)",
           supports (Cstate::C10) ? " C10" : "", supports (Cstate::C9) ? " C9"  : "", supports (Cstate::C8) ? " C8"  : "",
           supports (Cstate::C7)  ? " C7"  : "", supports (Cstate::C6) ? " C6"  : "", supports (Cstate::C3) ? " C3"  : "",
           supports (Cstate::C1)  ? " C1"  : "", ctl, cfg);
}

void Cpu::setup_pst()
{
    if (vendor != Vendor::INTEL)
        return;

    if (feature (Feature::HWP)) {

        // Enable HWP
        Msr::write (Msr::Reg64::IA32_PM_ENABLE, BIT (0));

        // Additional HWP MSRs may only be accessed after HWP is enabled
        auto const msr { Msr::read (Msr::Reg64::IA32_HWP_CAPABILITIES) };
        auto const min { static_cast<uint8_t>(msr >> 24) };     // Lowest
        auto const gtd { static_cast<uint8_t>(msr >>  8) };     // Currently Guaranteed
        auto const max { static_cast<uint8_t>(msr) };           // Highest
        auto const des { Cmdline::nocpst ? gtd : max };

        // Set desired level
        Msr::write (Msr::Reg64::IA32_HWP_REQUEST, des << 16 | max << 8 | min);

        trace (TRACE_CPU, "CPST: %u-%u-%u using %u (HWP)", max, gtd, min, des);

    } else if (feature (Feature::EIST)) {

        // Enable EIST
        Msr::write (Msr::Reg64::IA32_MISC_ENABLE, Msr::read (Msr::Reg64::IA32_MISC_ENABLE) | BIT (16));

        auto const msr { Msr::read (Msr::Reg64::PLATFORM_INFO) };
        auto const min { static_cast<uint8_t>(msr >> 48) };
        auto const gtd { static_cast<uint8_t>(msr >>  8) };
        auto const max { feature (Feature::TURBO_BOOST) ? static_cast<uint8_t>(Msr::read (Msr::Reg64::TURBO_RATIO_LIMIT)) : gtd };
        auto const des { Cmdline::nocpst ? gtd : max };

        // Set desired ratio and (dis)engage turbo mode
        Msr::write (Msr::Reg64::IA32_PERF_CTL, (Msr::read (Msr::Reg64::IA32_PERF_CTL) & ~(BIT64 (32) | BIT64_RANGE (15, 8))) | (feature (Feature::TURBO_BOOST) && Cmdline::nocpst ? BIT64 (32) : 0) | des << 8);

        trace (TRACE_CPU, "CPST: %u-%u-%u using %u (EIST)", max, gtd, min, des);
    }
}

void Cpu::init()
{
    if (Acpi::resume)
        hazard = 0;

    else {
        for (auto func { CTORS_L }; func != CTORS_C; (*func++)()) ;

        Gdt::build();
        Tss::build();
    }

    // Initialize exception handling
    Gdt::load();
    Idt::load();
    Tss::load();

    uint32_t clk { 0 }, rat { 0 }, lvl[4] { 0 }, name[12] { 0 };

    enumerate_features (clk, rat, lvl, name);

    Lapic::init (clk, rat);

    if (!Acpi::resume) {
        Hpt::OAddr phys; unsigned o; Memattr ma;
        Space_hst::nova.loc[id] = Hptp::current();
        Space_hst::nova.loc[id].lookup (MMAP_CPU_DATA, phys, o, ma);
        Hptp::master_map (MMAP_GLB_CPUS + id * PAGE_SIZE (0), phys, 0, Paging::Permissions (Paging::G | Paging::W | Paging::R), ma);
    }

    setup_msr();
    setup_cst();
    setup_pst();

    Cr::set_cr4 (Cr::get_cr4() | feature (Feature::SMAP)  * CR4_SMAP    |
                                 feature (Feature::SMEP)  * CR4_SMEP    |
                                 feature (Feature::XSAVE) * CR4_OSXSAVE |
                                 feature (Feature::PCID)  * CR4_PCIDE   |
                                 feature (Feature::UMIP)  * CR4_UMIP    |
                                 feature (Feature::MCE)   * CR4_MCE);

    Cos::init();
    Fpu::init();
    Mca::init();

    Vmcb::init();
    Vmcs::init();

    trace (TRACE_CPU, "CORE: %02u:%02u.%u %x:%x:%x:%x [%x] %.48s", lvl[2], lvl[1], lvl[0], family, model, stepping, platform, patch, reinterpret_cast<char *>(name));

    boot_lock.unlock();
}

void Cpu::fini()
{
    auto const s { Acpi::get_transition() };

    if (s.state() > 1) {
        Fpu::fini();
        Vmcs::fini();
    }

    Acpi::fini (s);
}

void Cpu::halt()
{
    if (EXPECT_FALSE (!csthint)) {
        asm volatile ("sti; hlt; cli");
        return;
    }

    // Pick a C-state based on predicted idle residency and exit latency
    auto const i { Timeout::idle() };
    auto const c { i < 1 ? Cstate::C0 : i < 80 ? Cstate::C1 : i < 120 ? Cstate::C3 : i < 151 ? Cstate::C6 : i < 256 ? Cstate::C7 : i < 339 ? Cstate::C8 : i < 1034 ? Cstate::C9 : Cstate::C10 };
    auto const h { csthint >> std::to_underlying (c) & BIT_RANGE (7, 0) };

    asm volatile ("monitor" : : "a" (MMAP_CPU_DSTB), "c" (0), "d" (0));
    asm volatile ("sti; mwait; cli" : : "a" (h), "c" (0));
}

void Cpu::allocate (apic_t i)
{
    if (EXPECT_FALSE (count == NUM_CPU))
        panic ("Platform has more than %u CPUs", NUM_CPU);

    Lapic::id[count++] = i;
}
