/*
 * Virtual-Memory Layout
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

#pragma once

#include "macros.hpp"

#define LOAD_ADDR       0x400000

#define PTE_BPL         9
#define PAGE_BITS       12
#define PAGE_SIZE(L)    BITN ((L) * PTE_BPL + PAGE_BITS)
#define OFFS_MASK(L)    (PAGE_SIZE (L) - 1)

// Space-Local Area           [--PTE--]---      // ^39 ^30 ^21 ^12
#define MMAP_SPC_PIO_E  0xffffffffc0002000      // 511 511 000 002
#define MMAP_SPC_PIO    0xffffffffc0000000      // 511 511 000 000
#define MMAP_SPC        0xffffffffc0000000      // 511 511 000 000

// CPU-Local Area             [--PTE--]---      // ^39 ^30 ^21 ^12
#define MMAP_CPU_DATA   0xffffffffbffff000      // 511 510 511 511    4K
#define MMAP_CPU_DSTT   0xffffffffbff87000      // 511 510 511 391  Data Stack Top
#define MMAP_CPU_DSTB   0xffffffffbff86000      // 511 510 511 390  Data Stack Base
#define MMAP_CPU_ISTT   0xffffffffbff85000      // 511 510 511 389  Intr Stack Top
#define MMAP_CPU_ISTB   0xffffffffbff84000      // 511 510 511 388  Intr Stack Base
#define MMAP_CPU_DSHT   0xffffffffbff82ff8      // 511 510 511 387  Data Shadow Stack Token
#define MMAP_CPU_DSHB   0xffffffffbff82000      // 511 510 511 386  Data Shadow Stack Base
#define MMAP_CPU_ISHT   0xffffffffbff80ff8      // 511 510 511 385  Intr Shadow Stack Token
#define MMAP_CPU_ISHB   0xffffffffbff80000      // 511 510 511 384  Intr Shadow Stack Base
#define MMAP_CPU_APIC   0xffffffffbff00000      // 511 510 511 256    4K
#define MMAP_CPU        0xffffffffbfe00000      // 511 510 511 000    2M

// Global Area                [--PTE--]---      // ^39 ^30 ^21 ^12
#define MMAP_GLB_MAP1   0xffffffffbe800000      // 511 510 500 000    4M + gap
#define MMAP_GLB_MAP0   0xffffffffbe000000      // 511 510 496 000    4M + gap
#define MMAP_GLB_APIC   0xffffffffbd000000      // 511 510 488 000   16M
#define MMAP_GLB_UART   0xffffffffbc000000      // 511 510 480 000   16M
#define MMAP_GLB_SMMU   0xffffffffb8000000      // 511 510 448 000   64M
#define LINK_ADDR       0xffffffff80000000      // 511 510 000 000  896M

#define MMAP_GLB_CPUS   0xffffffff40000000      // 511 509 000 000    1G (262144 CPUs)
#define MMAP_GLB_PCIE   0xffffffc000000000      // 511 256 000 000
#define MMAP_GLB_PCIS   0xffffff8000000000      // 511 000 000 000  256G (1024 PCI Segment Groups)
#define BASE_ADDR       0xffffff8000000000      // 511 000 000 000

#define OFFSET          (LINK_ADDR - LOAD_ADDR)
