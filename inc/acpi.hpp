/*
 * Advanced Configuration and Power Interface (ACPI)
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

#include "acpi_arch.hpp"
#include "acpi_fixed.hpp"
#include "acpi_table_dbg2.hpp"
#include "acpi_table_facs.hpp"
#include "acpi_table_fadt.hpp"
#include "acpi_table_madt.hpp"
#include "acpi_table_mcfg.hpp"
#include "acpi_table_rsdp.hpp"
#include "acpi_table_spcr.hpp"
#include "acpi_table_srat.hpp"
#include "acpi_table_tpm2.hpp"
#include "acpi_table_xsdt.hpp"
#include "atomic.hpp"

class Acpi final : public Acpi_arch
{
    friend class Acpi_table_fadt;

    private:
        static inline constinit uint32_t fflg { 0 };    // Feature Flags

        static inline constinit Atomic<Acpi_fixed::Transition> trans { Acpi_fixed::Transition { 0 } };

    public:
        static inline constinit uint64_t resume { 0 };

        static inline Acpi_fixed::Transition get_transition() { return trans; }

        static inline bool set_transition (Acpi_fixed::Transition t)
        {
            Acpi_fixed::Transition o { 0 };

            return trans.compare_exchange (o, t);
        }

        static inline void clr_transition()
        {
            Acpi_fixed::wake_clr();

            trans = Acpi_fixed::Transition { 0 };
        }

        static bool init();
        static void fini (Acpi_fixed::Transition);
};
