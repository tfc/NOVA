/*
 * Initialization Code
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
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
#include "compiler.hpp"
#include "extern.hpp"
#include "ioapic.hpp"
#include "interrupt.hpp"
#include "patch.hpp"
#include "pic.hpp"
#include "stdio.hpp"
#include "string.hpp"

extern "C" uintptr_t kern_ptab_setup (apic_t)
{
    Hptp hptp;

    // Share global kernel memory
    hptp.share_from_master (BASE_ADDR, MMAP_CPU);

    // Allocate and map cpu page
    hptp.update (MMAP_CPU_DATA, Kmem::ptr_to_phys (Buddy::alloc (0, Buddy::Fill::BITS0)), 0, Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    // Allocate and map kernel data stack
    hptp.update (MMAP_CPU_DSTB, Kmem::ptr_to_phys (Buddy::alloc (0, Buddy::Fill::BITS0)), 0, Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    // Allocate and map kernel intr stack
    hptp.update (MMAP_CPU_ISTB, Kmem::ptr_to_phys (Buddy::alloc (0, Buddy::Fill::BITS0)), 0, Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    return hptp.root_addr();
}

extern "C" void preinit()
{
    Patch::detect();
}

extern "C" void init()
{
    Patch::init();
    Buddy::init();

    // Setup 0-page and 1-page
    memset (reinterpret_cast<void *>(&PAGE_0),  0,  PAGE_SIZE (0));
    memset (reinterpret_cast<void *>(&PAGE_1), ~0u, PAGE_SIZE (0));

    for (auto func { CTORS_S }; func != CTORS_E; (*func++)()) ;

    Cmdline::init();

    for (auto func { CTORS_C }; func != CTORS_S; (*func++)()) ;

    // Now we're ready to talk to the world
    Console::print ("\fNOVA Microhypervisor v%d-%07lx-%#x (%s): %s %s [%s]\n", CFG_VER, reinterpret_cast<uintptr_t>(&GIT_VER), Patch::applied, ARCH, __DATE__, __TIME__, COMPILER_STRING);

    Interrupt::setup();
    Acpi::setup();

    Pic::init();

    Ioapic::init_all();

    Interrupt::init_all();
}
