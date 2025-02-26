/*
 * Initialization Code
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

#include "acpi.hpp"
#include "cmdline.hpp"
#include "console.hpp"
#include "cpu.hpp"
#include "extern.hpp"
#include "fdt.hpp"
#include "patch.hpp"
#include "smmu.hpp"

extern "C" uintptr_t kern_ptab_setup (cpu_t cpu)
{
    return Cpu::remote_ptab (cpu);
}

extern "C" void preinit()
{
    if (!Acpi::resume)
        Cmdline::init();
}

extern "C" unsigned init()
{
    if (Acpi::resume) {

        // Restart all cores
        for (cpu_t c { 0 }; c < Cpu::count; c++)
            Psci::boot_cpu (c, Cpu::remote_mpidr (c));

    } else {

        Buddy::init();

        for (auto func { CTORS_S }; func != CTORS_E; (*func++)()) ;

        for (auto func { CTORS_C }; func != CTORS_S; (*func++)()) ;

        // Now we're ready to talk to the world
        Console::print ("\nNOVA Microhypervisor #%07lx-%#x (%s): %s %s [%s]\n", reinterpret_cast<uintptr_t>(&GIT_VER), Patch::applied, ARCH, __DATE__, __TIME__, COMPILER_STRING);
    }

    Acpi::init() || Fdt::init();

    // If SMMUs were not enumerated by firmware, then enumerate them based on board knowledge
    if (!Smmu::avail_smg() && !Smmu::avail_ctx())
        for (unsigned i = 0; i < sizeof (Board::smmu) / sizeof (*Board::smmu); i++)
            if (Board::smmu[i].mmio)
                new Smmu (Board::smmu[i]);

    return Cpu::boot_cpu;
}
