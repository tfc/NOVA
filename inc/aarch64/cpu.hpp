/*
 * Central Processing Unit (CPU)
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

#include "arch.hpp"
#include "atomic.hpp"
#include "kmem.hpp"
#include "spinlock.hpp"
#include "std.hpp"
#include "types.hpp"

class Cpu final
{
    private:
        static constexpr auto hyp0_cptr { 0 };

        static constexpr auto hyp1_cptr { CPTR_TAM      |   // Trap activity monitor: AMCFGR, AMCGCR, AMCNTEN*, AMCR, AMEVCNTR*, AMEVTYPER*, AMUSERENR
                                          CPTR_TTA      |   // Trap trace registers
                                          CPTR_TZ };        // Trap ZCR

        static constexpr auto hyp0_mdcr { MDCR_E2TB     |   // Trap trace buffer controls: TRB*
                                          MDCR_E2PB };      // Trap profiling buffer control: PMB*

        static constexpr auto hyp1_mdcr { MDCR_TDCC     |   // Trap debug comms channel: OSDTRRX, OSDTRTX, MDCCSR, MDCCINT, DBGDTR, DBGDTRRX, DBGDTRTX
                                          MDCR_TTRF     |   // Trap trace filter: TRFCR
                                          MDCR_TPMS     |   // Trap performance monitor sampling: PMS*
                                          MDCR_TDE      |   // Trap all of TDRA+TDOSA+TDA
                                          MDCR_TPM };       // Trap performance monitor access: PMCR, PM*

        static constexpr auto hyp0_hcr  { HCR_ATA       |   // Trap GCR, RGSR, TFSR*
                                          HCR_ENSCXT    |   // Trap SCXTNUM
                                          HCR_FIEN      |   // Trap ERXPFG*
                                          HCR_NV2       |
                                          HCR_NV1       |
                                          HCR_NV        |
                                          HCR_APK       |   // Trap APDAKey*, APDBKey*, APGAKey*, APIAKey*, APIBKey*
                                          HCR_E2H       |
                                          HCR_ID        |
                                          HCR_CD        |
                                          HCR_TGE       |
                                          HCR_DC };

        static constexpr auto hyp1_hcr  { HCR_TID5      |   // Trap GMID
                                          HCR_TERR      |   // Trap ERRIDR, ERRSELR, ERXADDR, ERXCTLR, ERXFR, ERXMISC*, ERXSTATUS
                                          HCR_TLOR      |   // Trap LORC, LOREA, LORID, LORN, LORSA
                                          HCR_TSW       |   // Trap DC ISW/CSW/CISW
                                          HCR_TACR      |   // Trap ACTLR
                                          HCR_TIDCP     |   // Trap S3_* implementation defined registers
                                          HCR_TSC       |   // Trap SMC
                                          HCR_TID3      |   // Trap ID_AA64AFR*,ID_AA64DFR*, ID_AA64ISAR*, ID_AA64MMFR*, ID_AA64PFR*, ID_AA64ZFR*, ID_AFR0, ID_DFR0, ID_ISAR*, ID_MMFR, ID_PFR, MVFR*
                                          HCR_TID1      |   // Trap AIDR, REVIDR
                                          HCR_TID0      |   // Trap JIDR
                                          HCR_TWE       |   // Trap WFE
                                          HCR_TWI       |   // Trap WFI
                                          HCR_BSU_INNER |
                                          HCR_FB        |
                                          HCR_AMO       |
                                          HCR_IMO       |
                                          HCR_FMO       |
                                          HCR_PTW       |
                                          HCR_SWIO      |
                                          HCR_VM };

        static constexpr auto hyp0_hcrx { 0 };
        static constexpr auto hyp1_hcrx { 0 };

        static uint64_t ptab                CPULOCAL;           // EL2 Page Table Root
        static uint64_t midr                CPULOCAL;           // Main ID Register
        static uint64_t mpidr               CPULOCAL;           // Multiprocessor Affinity Register

        static uint64_t res0_hcr            CPULOCAL;           // RES0 bits in HCR
        static uint64_t res0_hcrx           CPULOCAL;           // RES0 bits in HCRX

        static uint64_t feat_cpu64[3]       CPULOCAL;           // ID_AA64PFRx
        static uint64_t feat_dbg64[2]       CPULOCAL;           // ID_AA64DFRx
        static uint64_t feat_isa64[4]       CPULOCAL;           // ID_AA64ISARx
        static uint64_t feat_mem64[5]       CPULOCAL;           // ID_AA64MMFRx
        static uint64_t feat_sme64[1]       CPULOCAL;           // ID_AA64SMFRx
        static uint64_t feat_sve64[1]       CPULOCAL;           // ID_AA64ZFRx

        static uint32_t feat_cpu32[3]       CPULOCAL;           // ID_PFRx
        static uint32_t feat_dbg32[2]       CPULOCAL;           // ID_DFRx
        static uint32_t feat_isa32[7]       CPULOCAL;           // ID_ISARx
        static uint32_t feat_mem32[6]       CPULOCAL;           // ID_MMFRx
        static uint32_t feat_mfp32[3]       CPULOCAL;           // MVFRx

        static inline constinit Spinlock boot_lock asm ("__boot_lock");

        static void enumerate_features();

    public:
        enum class Cpu_feature : unsigned
        {
            EL0         =  0,   // EL0                    AArch64 only (1), AArch64+AArch32 (2)
            EL1         =  1,   // EL1                    AArch64 only (1), AArch64+AArch32 (2)
            EL2         =  2,   // EL2 Unimplemented (0), AArch64 only (1), AArch64+AArch32 (2)
            EL3         =  3,   // EL3 Unimplemented (0), AArch64 only (1), AArch64+AArch32 (2)
            FP          =  4,   // Floating Point (0), FEAT_FP16 (1), Unsupported (15)
            ADVSIMD     =  5,   // Advanced SIMD (0), FEAT_FP16 (1), Unsupported (15)
            GIC         =  6,   // GIC System Registers v3.0/4.0 (1), v4.1 (3)
            RAS         =  7,   // FEAT_RAS (1), FEAT_RASv1p1 (2), FEAT_RASv2 (3)
            SVE         =  8,   // FEAT_SVE (1)
            SEL2        =  9,   // FEAT_SEL2 (1)
            MPAM        = 10,   // FEAT_MPAM (1)
            AMU         = 11,   // FEAT_AMUv1 (1), FEAT_AMUv1p1 (2)
            DIT         = 12,   // FEAT_DIT (1)
            RME         = 13,   // FEAT_RME (1)
            CSV2        = 14,   // FEAT_CSV2 (1), FEAT_CSV2_2 (2), FEAT_CSV2_3 (3)
            CSV3        = 15,   // FEAT_CSV3 (1)
            BT          = 16,   // FEAT_BTI (1)
            SSBS        = 17,   // FEAT_SSBS (1), FEAT_SSBS2 (2)
            MTE         = 18,   // FEAT_MTE (1), FEAT_MTE2 (2), FEAT_MTE3 (3)
            RAS_FRAC    = 19,   // FEAT_RASv1p1 (1)
            MPAM_FRAC   = 20,   // FEAT_MPAM_frac (1)
            SME         = 22,   // FEAT_SME (1), FEAT_SME2 (2)
            RNDR_TRAP   = 23,   // FEAT_RNG_TRAP (1)
            CSV2_FRAC   = 24,   // FEAT_CSV2_1p1 (1), FEAT_CSV2_1p2 (2)
            NMI         = 25,   // FEAT_NMI (1)
            MTE_FRAC    = 26,
            GCS         = 27,   // FEAT_GCS (1)
            THE         = 28,   // FEAT_THE (1)
            MTEX        = 29,   // FEAT_MTE4 (1)
            DF2         = 30,   // FEAT_DoubleFault2 (1)
            PFAR        = 31,   // FEAT_PFAR (1)
            MTEPERM     = 32,   // FEAT_MTE_PERM (1)
            MTESTORE    = 33,   // FEAT_MTE4 (1)
            MTEFAR      = 34,   // FEAT_MTE4 (1)
            FPMR        = 40,   // FEAT_FPMR (1)
        };

        enum class Dbg_feature : unsigned
        {
            DEBUGVER    =  0,   // FEAT_Debugv8p0 (6), FEAT_Debugv8p1 (7), FEAT_Debugv8p2 (8), FEAT_Debugv8p4 (9), FEAT_Debugv8p8 (10), FEAT_Debugv8p9 (11)
            TRACEVER    =  1,   // Trace Support
            PMUVER      =  2,   // FEAT_PMUv3 (1), FEAT_PMUv3p1 (4), FEAT_PMUv3p4 (5), FEAT_PMUv3p5 (6), FEAT_PMUv3p7 (7), FEAT_PMUv3p8 (8), FEAT_PMUv3p9 (9)
            BRPS        =  3,   // Number of Breakpoints - 1
            PMSS        =  4,   // FEAT_PMUv3_SS (1)
            WRPS        =  5,   // Number of Watchpoints - 1
            SEBEP       =  6,   // FEAT_SEBEP (1)
            CTX_CMPS    =  7,   // Number of Context-Aware Breakpoints - 1
            PMSVER      =  8,   // FEAT_SPE (1), FEAT_SPEv1p1 (2), FEAT_SPEv1p2 (3), FEAT_SPEv1p3 (4), FEAT_SPEv1p4 (5)
            DOUBLELOCK  =  9,   // FEAT_DoubleLock (0), Unsupported (15)
            TRACEFILT   = 10,   // FEAT_TRF (1)
            TRACEBUFFER = 11,   // FEAT_TRBE (1)
            MTPMU       = 12,   // FEAT_MTPMU (1)
            BRBE        = 13,   // FEAT_BRBE (1), FEAT_BRBEv1p1 (2)
            EXTTRCBUFF  = 14,   // FEAT_TRBE_EXT (1)
            HPMN0       = 15,   // FEAT_HPMN0 (1)
        };

        enum class Isa_feature : unsigned
        {
            AES         =  1,   // FEAT_AES (1), FEAT_PMULL (2)
            SHA1        =  2,   // FEAT_SHA1 (1)
            SHA2        =  3,   // FEAT_SHA256 (1), FEAT_SHA512 (2)
            CRC32       =  4,   // FEAT_CRC (1)
            ATOMIC      =  5,   // FEAT_LSE (2), FEAT_LSE128 (3)
            TME         =  6,   // FEAT_TME (1)
            RDM         =  7,   // FEAT_RDM (1)
            SHA3        =  8,   // FEAT_SHA3 (1)
            SM3         =  9,   // FEAT_SM3 (1)
            SM4         = 10,   // FEAT_SM4 (1)
            DP          = 11,   // FEAT_DotProd (1)
            FHM         = 12,   // FEAT_FHM (1)
            TS          = 13,   // FEAT_FlagM (1), FEAT_FlagM2 (2)
            TLB         = 14,   // FEAT_TLBIOS (1,2), FEAT_TLBIRANGE (2)
            RNDR        = 15,   // FEAT_RNG (1)
            DPB         = 16,   // FEAT_DPB (1), FEAT_DPB2 (2)
            APA         = 17,   // FEAT_PAuth (1), FEAT_EPAC (2), FEAT_PAuth2 (3), FEAT_FPAC (4), FEAT_FPACCOMBINE (5)
            API         = 18,   // FEAT_PAuth (1), FEAT_EPAC (2), FEAT_PAuth2 (3), FEAT_FPAC (4), FEAT_FPACCOMBINE (5)
            JSCVT       = 19,   // FEAT_JSCVT (1)
            FCMA        = 20,   // FEAT_FCMA (1)
            LRCPC       = 21,   // FEAT_LRCPC (1), FEAT_LRCPC2 (2), FEAT_LRCPC3 (3)
            GPA         = 22,   // FEAT_PACQARMA5 (1)
            GPI         = 23,   // FEAT_PACIMP (1)
            FRINTTS     = 24,   // FEAT_FRINTTS (1)
            SB          = 25,   // FEAT_SB (1)
            SPECRES     = 26,   // FEAT_SPECRES (1), FEAT_SPECRES2 (2)
            BF16        = 27,   // FEAT_BF16 (1), FEAT_EBF16 (2)
            DGH         = 28,   // FEAT_DGH (1)
            I8MM        = 29,   // FEAT_I8MM (1)
            XS          = 30,   // FEAT_XS (1)
            LS64        = 31,   // FEAT_LS64 (1), FEAT_LS64_V (2), FEAT_LS64_ACCDATA (3)
            WFXT        = 32,   // FEAT_WFxT (1)
            RPRES       = 33,   // FEAT_RPRES (1)
            GPA3        = 34,   // FEAT_PACQARMA3 (1)
            APA3        = 35,   // FEAT_PAuth (1), FEAT_EPAC (2), FEAT_PAuth2 (3), FEAT_FPAC (4), FEAT_FPACCOMBINE (5)
            MOPS        = 36,   // FEAT_MOPS (1)
            BC          = 37,   // FEAT_HBC (1)
            PAC_FRAC    = 38,   // FEAT_CONSTPACFIELD (1)
            CLRBHB      = 39,   // FEAT_CLRBHB (1)
            SYSREG_128  = 40,   // FEAT_SYSREG128 (1)
            SYSINS_128  = 41,   // FEAT_SYSINSTR128 (1)
            PRFMSLC     = 42,   // FEAT_PRFMSLC (1)
            RPRFM       = 44,   // FEAT_RPRFM (1)
            CSSC        = 45,   // FEAT_CSSC (1)
            LUT         = 46,   // FEAT_LUT (1)
            ATS1A       = 47,
            CPA         = 48,   // FEAT_CPA (1), FEAT_CPA2 (2)
            FAMINMAX    = 49,   // FEAT_FAMINMAX (1)
            TLBIW       = 50,   // FEAT_TLBIW (1)
            PACM        = 51,   // FEAT_PAuth_LR (1,2)
        };

        enum class Mem_feature : unsigned
        {
            PARANGE     =  0,   // FEAT_LPA/FEAT_LPA2 (6), FEAT_D128 (7)
            ASIDBITS    =  1,   // Number of ASID Bits
            BIGEND      =  2,   // Mixed Endian Support
            SNSMEM      =  3,   // Secure/Non-Secure Memory Distinction
            BIGEND_EL0  =  4,   // Mixed Endian Support at EL0
            TGRAN16     =  5,   // Stage-1 Granule 16KB
            TGRAN64     =  6,   // Stage-1 Granule 64KB
            TGRAN4      =  7,   // Stage-1 Granule  4KB
            TGRAN16_2   =  8,   // FEAT_GTG Stage-2 Granule 16KB
            TGRAN64_2   =  9,   // FEAT_GTG Stage-2 Granule 64KB
            TGRAN4_2    = 10,   // FEAT_GTG Stage-2 Granule  4KB
            EXS         = 11,   // FEAT_ExS (1)
            FGT         = 14,   // FEAT_FGT (1), FEAT_FGT2 (2)
            ECV         = 15,   // FEAT_ECV (1,2)
            HAFDBS      = 16,   // FEAT_HAFDBS (A:1 A+D:2)
            VMIDBITS    = 17,   // FEAT_VMID16 (2)
            VH          = 18,   // FEAT_VHE (1)
            HPDS        = 19,   // FEAT_HPDS (1), FEAT_HPDS2 (2)
            LO          = 20,   // FEAT_LOR (1)
            PAN         = 21,   // FEAT_PAN (1), FEAT_PAN2 (2), FEAT_PAN3 (3)
            SPECSEI     = 22,   // RAS SError on speculative reads/fetches
            XNX         = 23,   // FEAT_XNX (1)
            TWED        = 24,   // FEAT_TWED (1)
            ETS         = 25,   // FEAT_ETS (1)
            HCX         = 26,   // FEAT_HCX (1)
            AFP         = 27,   // FEAT_AFP (1)
            NTLBPA      = 28,   // FEAT_nTLBPA (1)
            TIDCP1      = 29,   // FEAT_TIDCP1 (1)
            CMOW        = 30,   // FEAT_CMOW (1)
            ECBHB       = 31,   // FEAT_ECBHB (1)
            CNP         = 32,   // FEAT_TTCNP (1)
            UAO         = 33,   // FEAT_UAO (1)
            LSM         = 34,   // FEAT_LSMAOC (1)
            IESB        = 35,   // FEAT_IESB (1)
            VARANGE     = 36,   // FEAT_LVA (1)
            CCIDX       = 37,   // FEAT_CCIDX (1)
            NV          = 38,   // FEAT_NV (1), FEAT_NV2 (2)
            ST          = 39,   // FEAT_TTST (1)
            AT          = 40,   // FEAT_LSE2 (1)
            IDS         = 41,   // FEAT_IDST (1)
            FWB         = 42,   // FEAT_S2FWB (1)
            TTL         = 44,   // FEAT_TTL (1)
            BBM         = 45,   // FEAT_BBM (0,1,2)
            EVT         = 46,   // FEAT_EVT (1,2)
            E0PD        = 47,   // FEAT_E0PD (1)
            TCRX        = 48,   // TCR Extension (1)
            SCTLRX      = 49,   // SCTLRX Extension (1)
            S1PIE       = 50,   // Stage-1 Permission Indirection Extension (1)
            S2PIE       = 51,   // Stage-2 Permission Indirection Extension (1)
            S1POE       = 52,   // Stage-1 Permission Overlay Extension (1)
            S2POE       = 53,   // Stage-2 Permission Overlay Extension (1)
            AIE         = 54,   // Stage-1 Attribute Index Extension (1)
            MEC         = 55,   // FEAT_MEC (1)
            D128        = 56,   // 128bit Stage-1 (1)
            D128_2      = 57,   // 128bit Stage-2 (1)
            SNERR       = 58,   // FEAT_ANERR (2)
            ANERR       = 59,   // FEAT_ANERR (2)
            SDERR       = 61,   // FEAT_ADERR (2)
            ADERR       = 62,   // FEAT_ADERR (2)
            SPEC_FPACC  = 63,
            EIESB       = 65,   // IESB before (1) / after (15) exception
            ASID2       = 66,   // FEAT_ASID2 (1)
            HACDBS      = 67,   // FEAT_HACDBS (1)
            FGWTE3      = 68,   // FEAT_FGWTE3 (1)
            NV_FRAC     = 69,
            E2H0        = 70,   // FEAT_E2H0 (0)
            E3DSE       = 73,   // FEAT_E3DSE (1)
        };

        static cpu_t            id          CPULOCAL;
        static unsigned         hazard      CPULOCAL;
        static bool             bsp         CPULOCAL;
        static uint64_t         gicr        CPULOCAL;
        static uint64_t         cptr        CPULOCAL;
        static uint64_t         mdcr        CPULOCAL;

        static inline constinit cpu_t         boot_cpu { 0 };
        static inline constinit cpu_t         count    { 0 };
        static inline constinit Atomic<cpu_t> online   { 0 };

        // Returns affinity in Aff3[31:24] Aff2[23:16] Aff1[15:8] Aff0[7:0] format
        static constexpr auto affinity_pack (uint64_t v) { return static_cast<uint32_t>((v >> 8 & BIT_RANGE (31, 24)) | (v & BIT_RANGE (23, 0))); }

        // Returns affinity in Aff3[39:32] Aff2[23:16] Aff1[15:8] Aff0[7:0] format
        static constexpr auto affinity_bits (uint64_t v) { return v & (BIT64_RANGE (39, 32) | BIT64_RANGE (23, 0)); }

        static auto remote_mpidr (cpu_t cpu) { return *Kmem::loc_to_glob (cpu, &mpidr); }

        static auto remote_ptab (cpu_t cpu) { return *Kmem::loc_to_glob (cpu, &ptab); }

        static void preemption_disable() { asm volatile ("msr daifset, #0xf" : : : "memory"); }

        static void preemption_enable() { asm volatile ("msr daifclr, #0xf" : : : "memory"); }

        static void preemption_point() { asm volatile ("msr daifclr, #0xf; msr daifset, #0xf" : : : "memory"); }

        static void halt() { asm volatile ("wfi; msr daifclr, #0xf; msr daifset, #0xf" : : : "memory"); }

        [[nodiscard]] static uint8_t feature (Cpu_feature f) { return feat_cpu64[std::to_underlying (f) / 16] >> std::to_underlying (f) % 16 * 4 & BIT_RANGE (3, 0); }
        [[nodiscard]] static uint8_t feature (Dbg_feature f) { return feat_dbg64[std::to_underlying (f) / 16] >> std::to_underlying (f) % 16 * 4 & BIT_RANGE (3, 0); }
        [[nodiscard]] static uint8_t feature (Isa_feature f) { return feat_isa64[std::to_underlying (f) / 16] >> std::to_underlying (f) % 16 * 4 & BIT_RANGE (3, 0); }
        [[nodiscard]] static uint8_t feature (Mem_feature f) { return feat_mem64[std::to_underlying (f) / 16] >> std::to_underlying (f) % 16 * 4 & BIT_RANGE (3, 0); }

        static inline auto constrain_hcr  (uint64_t v) { return (v | hyp1_hcr)  & ~(res0_hcr  | hyp0_hcr); }
        static inline auto constrain_hcrx (uint64_t v) { return (v | hyp1_hcrx) & ~(res0_hcrx | hyp0_hcrx); }

        static void init (cpu_t, unsigned);
        static void fini();

        static void set_vmm_regs (uintptr_t (&)[31], uint64_t &, uint64_t &, uint64_t &, uint32_t &);

        static void allocate (cpu_t, uint64_t, uint64_t = 0);
};
