/*
 * Floating Point Unit (FPU)
 * Streaming SIMD Extensions (SSE)
 * Advanced Vector Extensions (AVX)
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

#include "arch.hpp"
#include "cpu.hpp"
#include "cr.hpp"
#include "hazard.hpp"
#include "patch.hpp"
#include "slab.hpp"

class Fpu final
{
    private:
        // State Components
        enum Component
        {
            APX_F           = BIT (19),             // XCR (enabled)
            XTILEDATA       = BIT (18),             // XCR (enabled)
            XTILECFG        = BIT (17),             // XCR (enabled)
            HWP             = BIT (16),             // XSS (managed)
            LBR             = BIT (15),             // XSS (managed)
            UINTR           = BIT (14),             // XSS (managed)
            HDC             = BIT (13),             // XSS (managed)
            CET_S           = BIT (12),             // XSS (managed)
            CET_U           = BIT (11),             // XSS (managed)
            PASID           = BIT (10),             // XSS (managed)
            PKRU            = BIT (9),              // XCR (managed)
            PT              = BIT (8),              // XSS (managed)
            AVX512          = BIT_RANGE (7, 5),     // XCR (enabled)
            MPX             = BIT_RANGE (4, 3),     // XCR (enabled)
            AVX             = BIT (2),              // XCR (enabled)
            SSE             = BIT (1),              // XCR (managed)
            X87             = BIT (0),              // XCR (managed)
        };

        // Legacy Region: x87 State, SSE State
        struct Legacy
        {
            uint16_t    fcw             { 0x37f };  // x87 FPU Control Word
            uint16_t    fsw             { 0 };      // x87 FPU Status Word
            uint16_t    ftw             { 0xffff }; // x87 FPU Tag Word
            uint16_t    fop             { 0 };      // x87 FPU Opcode
            uint64_t    fip             { 0 };      // x87 FPU Instruction Pointer Offset
            uint64_t    fdp             { 0 };      // x87 FPU Instruction Data Pointer Offset
            uint32_t    mxcsr           { 0x1f80 }; // SIMD Control/Status Register
            uint32_t    mxcsr_mask      { 0 };      // SIMD Control/Status Register Mask Bits
            uint64_t    mmx[8][2]       {{0}};      //  8  80bit MMX registers
            uint64_t    xmm[16][2]      {{0}};      // 16 128bit XMM registers
            uint64_t    unused[6][2]    {{0}};      //  6 128bit reserved
        };

        static_assert (__is_standard_layout (Legacy) && sizeof (Legacy) == 512);

        // XSAVE Header
        struct Header
        {
            uint64_t    xstate          { 0 };
            uint64_t    xcomp           { static_cast<uint64_t>(compact) << 63 };
            uint64_t    unused[6]       { 0 };
        };

        static_assert (__is_standard_layout (Header) && sizeof (Header) == 64);

        // XSAVE Area
        Legacy  legacy;
        Header  header;

    public:
        struct State_xsv
        {
            uint64_t    xcr             { Component::X87 };
            uint64_t    xss             { 0 };

            /*
             * Switch XSAVE state between guest/host
             *
             * VMM-provided guest state was sanitized by constrain_* functions below
             *
             * @param o     Old live state
             * @param n     New live state
             */
            ALWAYS_INLINE
            static inline void make_current (State_xsv const &o, State_xsv const &n)
            {
                if (EXPECT_FALSE (o.xcr != n.xcr))
                    Cr::set_xcr (0, n.xcr);

                if (EXPECT_FALSE (o.xss != n.xss))
                    Msr::write (Msr::Reg64::IA32_XSS, n.xss);
            }

            /*
             * Constrain XCR0 value to ensure XSETBV does not fault
             *
             * @param v     XCR0 value provided by VMM
             * @return      Constrained value
             */
            ALWAYS_INLINE
            static inline uint64_t constrain_xcr (uint64_t v)
            {
                // Setting any AVX512 bit requires all AVX512 bits and the AVX bit
                if (v & Component::AVX512)
                    v |= Component::AVX512 | Component::AVX;

                // Setting the AVX bit requires the SSE bit
                if (v & Component::AVX)
                    v |= Component::SSE;

                // The X87 bit is always required
                v |= Component::X87;

                // Constrain to bits that are manageable
                return hst_xsv.xcr & v;
            }

            /*
             * Constrain XSS value to ensure WRMSR does not fault
             *
             * @param v     XSS value provided by VMM
             * @return      Constrained value
             */
            ALWAYS_INLINE
            static inline uint64_t constrain_xss (uint64_t v)
            {
                // Constrain to bits that are manageable
                return hst_xsv.xss & v;
            }
        };

        static State_xsv hst_xsv CPULOCAL;

        // XSAVE area format: XSAVES/compact (true), XSAVE/standard (false)
        static inline constinit bool compact { true };

        // XSAVE context size
        static inline constinit size_t size { sizeof (Legacy) + sizeof (Header) };

        // XSAVE context alignment
        static constexpr size_t alignment { 64 };

        // XSAVE state components managed by NOVA
        static constexpr uint64_t managed { Component::AVX512 | Component::AVX | Component::SSE | Component::X87 };

        /*
         * Load FPU state from memory into registers
         *
         * The default method uses XRSTORS and compacted format (if supported)
         * Patched alternative uses XRSTOR and standard format (see Patch::init)
         *
         * @param this  XSAVE area
         */
        ALWAYS_INLINE
        inline void load() const
        {
            asm volatile (EXPAND (PATCH (xrstors64 %0, xrstor64 %0, PATCH_XSAVES)) : : "m" (*this), "d" (static_cast<uint32_t>(managed >> 32)), "a" (static_cast<uint32_t>(managed)));
        }

        /*
         * Save FPU state from registers into memory
         *
         * The default method uses XSAVES and compacted format (if supported)
         * Patched alternative uses XSAVE and standard format (see Patch::init)
         *
         * @param this  XSAVE area
         */
        ALWAYS_INLINE
        inline void save()
        {
            asm volatile (EXPAND (PATCH (xsaves64 %0, xsave64 %0, PATCH_XSAVES)) : "=m" (*this) : "d" (static_cast<uint32_t>(managed >> 32)), "a" (static_cast<uint32_t>(managed)));
        }

        /*
         * Disable FPU and clear FPU hazard
         */
        ALWAYS_INLINE
        static inline void disable()
        {
            Cr::set_cr0 (Cr::get_cr0() | CR0_TS);

            Cpu::hazard &= ~Hazard::FPU;
        }

        /*
         * Enable FPU and set FPU hazard
         */
        ALWAYS_INLINE
        static inline void enable()
        {
            asm volatile ("clts");

            Cpu::hazard |= Hazard::FPU;
        }

        /*
         * Allocate XSAVE area
         *
         * @param cache FPU slab cache
         * @return      Pointer to XSAVE area
         */
        [[nodiscard]] ALWAYS_INLINE
        static inline void *operator new (size_t, Slab_cache &cache) noexcept
        {
            return cache.alloc();
        }

        /*
         * Deallocate XSAVE area
         *
         * @param ptr   Pointer to XSAVE area
         * @param cache FPU slab cache
         */
        ALWAYS_INLINE
        static inline void operator delete (void *ptr, Slab_cache &cache)
        {
            if (EXPECT_TRUE (ptr))
                cache.free (ptr);
        }

        static void init();
        static void fini();
};

static_assert (__is_standard_layout (Fpu) && sizeof (Fpu) == 576);
