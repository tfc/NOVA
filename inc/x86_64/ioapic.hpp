/*
 * I/O Advanced Programmable Interrupt Controller (IOAPIC)
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

#include "lock_guard.hpp"
#include "macros.hpp"
#include "pci.hpp"
#include "vectors.hpp"

class Ioapic final : public List<Ioapic>
{
    private:
        uintptr_t   const       reg_base;
        unsigned    const       gsi_base;
        uint8_t     const       id;                             // Enumeration ID
        pci_t                   pci         { 0 };              // PCI S:B:D:F
        Spinlock                lock;

        static Slab_cache cache;                                    // IOAPIC Slab Cache
        static inline constinit Ioapic *  list { nullptr };         // IOAPIC List
        static inline constinit uintptr_t mmap { MMAP_GLB_APIC };   // IOAPIC Memory Map Pointer

        // Direct Registers
        enum class Reg8 : unsigned
        {
            IND     = 0x0,                  // Index Register
        };

        // Direct Registers
        enum class Reg32 : unsigned
        {
            DAT     = 0x10,                 // Data Register
            PAR     = 0x20,                 // Pin Assertion Register
            EOI     = 0x40,                 // EOI Register
        };

        // Indirect Registers
        enum class Ind32 : uint8_t
        {
            ID      = 0x0,                  // ID Register
            VER     = 0x1,                  // Version Register
            ARB     = 0x2,                  // Arbitration ID Register
            BCFG    = 0x3,                  // Boot Configuration Register
            RTE     = 0x10,                 // Redirection Table Entry Register
        };

        auto read  (Reg32 r) const      { return *reinterpret_cast<uint32_t volatile *>(reg_base + std::to_underlying (r)); }

        void write (Reg8  r, uint8_t  v) const { *reinterpret_cast<uint8_t  volatile *>(reg_base + std::to_underlying (r)) = v; }
        void write (Reg32 r, uint32_t v) const { *reinterpret_cast<uint32_t volatile *>(reg_base + std::to_underlying (r)) = v; }

        void index (Ind32 r) const
        {
            write (Reg8::IND, std::to_underlying (r));
        }

        uint32_t read (Ind32 r)
        {
            Lock_guard <Spinlock> guard { lock };
            index (r);
            return read (Reg32::DAT);
        }

        void write (Ind32 r, uint32_t v)
        {
            Lock_guard <Spinlock> guard { lock };
            index (r);
            write (Reg32::DAT, v);
        }

        void init();

    public:
        explicit Ioapic (uint64_t, uint8_t, unsigned);

        static void init_all()
        {
            for (auto l { list }; l; l = l->next)
                l->init();
        }

        static bool claim_dev (pci_t p, uint8_t i)
        {
            for (auto l { list }; l; l = l->next)
                if (l->pci == 0 && l->id == i) {
                    l->pci = p;
                    return true;
                }

            return false;
        }

        auto src() const { return Pci::bdf (pci); }
        auto mre() { return read (Ind32::VER) >> 16 & BIT_RANGE (7, 0); }
        auto ver() { return read (Ind32::VER)       & BIT_RANGE (7, 0); }

        ALWAYS_INLINE
        void set_dst (unsigned gsi, uint32_t v)
        {
            auto const rte { gsi - gsi_base };
            write (Ind32 (std::to_underlying (Ind32::RTE) + 2 * rte + 1), v);
        }

        ALWAYS_INLINE
        void set_cfg (unsigned gsi, bool msk = true, bool trg = false, bool pol = false)
        {
            auto const rte { gsi - gsi_base };
            write (Ind32 (std::to_underlying (Ind32::RTE) + 2 * rte), msk << 16 | trg << 15 | pol << 13 | ((VEC_GSI + gsi) & BIT_RANGE (7, 0)));
        }

        [[nodiscard]] static void *operator new (size_t) noexcept
        {
            return cache.alloc();
        }
};
