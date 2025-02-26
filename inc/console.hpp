/*
 * Generic Console
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

#include <stdarg.h>
#include "acpi_gas.hpp"
#include "debug.hpp"
#include "initprio.hpp"
#include "list.hpp"
#include "spinlock.hpp"

class Console : public List<Console>
{
    private:
        enum class Mode
        {
            FLAGS       = 0,
            WIDTH       = 1,
            PRECS       = 2,
        };

        enum Flags
        {
            SIGNED      = BIT (0),
            ALT_FORM    = BIT (1),
            ZERO_PAD    = BIT (2),
        };

        static inline constinit Console *dormant { nullptr };
        static inline constinit Console *enabled { nullptr };
        static inline constinit Spinlock lock;

        [[nodiscard]] virtual bool outc (char) const = 0;

        static void putc (char c)
        {
            for (Console *e { enabled }, *n; e; e = n) {

                n = e->next;

                if (EXPECT_FALSE (!e->outc (c)))
                    e->disable();
            }
        }

        static void print_num (uint64_t, unsigned, unsigned, unsigned);
        static void print_str (char const *, unsigned, unsigned);

        FORMAT (1,0)
        static void vprintf (char const *, va_list);

    protected:
        Console() : List { dormant } {}

        [[nodiscard]] virtual bool init() const { return true; }
        [[nodiscard]] virtual bool fini() const { return true; }

        [[nodiscard]] virtual bool match_dbgp (Debug::Type, Debug::Subtype) const { return false; }
        [[nodiscard]] virtual bool using_regs (Acpi_gas const &) const            { return false; }
        [[nodiscard]] virtual bool setup_regs (Acpi_gas const &)                  { return false; }

        void enable()
        {
            remove (dormant);
            insert (enabled);
        }

        void disable()
        {
            remove (enabled);
            insert (dormant);
        }

    public:
        FORMAT (1,2)
        static void print (char const *, ...);

        static void flush()
        {
            for (Console *e { enabled }, *n; e; e = n) {

                n = e->next;

                if (EXPECT_FALSE (!e->fini()))
                    e->disable();
            }
        }

        static void bind (Debug::Type t, Debug::Subtype s, Acpi_gas const &r)
        {
            if (EXPECT_FALSE (!r.addr || r.bits < 8))
                return;

            for (auto e { enabled }; e; e = e->next)
                if (e->using_regs (r))
                    return;

            for (auto d { dormant }; d; d = d->next)
                if (d->using_regs (r))
                    return;

            for (auto d { dormant }; d; d = d->next)
                if (d->match_dbgp (t, s) && d->setup_regs (r))
                    return;
        }
};
