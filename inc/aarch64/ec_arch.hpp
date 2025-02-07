/*
 * Execution Context (EC)
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

#include "assert.hpp"
#include "ec.hpp"
#include "extern.hpp"
#include "mtd.hpp"
#include "utcb.hpp"

class Ec_arch final : private Ec
{
    friend class Ec;

    private:
        static constexpr auto needs_pio { false };

        // Constructor: Kernel Thread
        Ec_arch (Refptr<Space_obj> &, Refptr<Space_hst> &, Refptr<Space_pio> &, cpu_t, cont_t);

        // Constructor: HST EC
        Ec_arch (bool, Fpu *, Refptr<Space_obj> &, Refptr<Space_hst> &, Refptr<Space_pio> &, cpu_t, unsigned long, uintptr_t, uintptr_t, void *);

        // Constructor: GST EC
        Ec_arch (bool, Fpu *, Refptr<Space_obj> &, Refptr<Space_hst> &, Vmcb *, cpu_t, unsigned long, uintptr_t);

        void collect() override final
        {
            trace (TRACE_DESTROY, "KOBJ: EC %p collected", static_cast<void *>(this));
        }

        static void handle_irq_kern() asm ("handle_irq_kern");

        [[noreturn]]
        static void handle_irq_user() asm ("handle_irq_user");

        [[noreturn]]
        static void handle_exc_kern (Exc_regs *) asm ("handle_exc_kern");

        [[noreturn]]
        static void handle_exc_user (Exc_regs *) asm ("handle_exc_user");

        [[noreturn]]
        static void ret_user_hypercall (Ec *);

        [[noreturn]]
        static void ret_user_exception (Ec *);

        [[noreturn]]
        static void ret_user_vmexit (Ec *);

        [[noreturn]]
        static void set_vmm_regs (Ec *);

        ALWAYS_INLINE
        inline void state_load (Ec *const self, Mtd_arch mtd)
        {
            assert (cont == ret_user_vmexit || cont == ret_user_exception);

            self->get_utcb()->arch()->load (mtd, cpu_regs());
        }

        ALWAYS_INLINE
        inline bool state_save (Ec *const self, Mtd_arch mtd)
        {
            assert (cont == ret_user_vmexit || cont == ret_user_exception);

            return self->get_utcb()->arch()->save (mtd, cpu_regs(), self->regs.get_obj());
        }

        [[noreturn]] ALWAYS_INLINE
        inline void make_current()
        {
            uintptr_t dummy;

            // Reset stack
            asm volatile ("adrp %0, %1; mov sp, %0" : "=&r" (dummy) : "S" (&DSTK_TOP) : "memory");

            // Become current EC and invoke continuation
            (*cont)(current = this);

            UNREACHED;
        }
};
