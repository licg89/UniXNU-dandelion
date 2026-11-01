/**
  MT6765 / MT6762G PMIC wrapper (PWRAP) register map.

  Derived from MT6768Pkg/Library/PmicWrapperImplLib/PmicWrapperImplLib.c.

  PWRAP base is not here: PmicWrapperDxe locates the "PMIC Wrapper" memory
  region through the platform MemoryMapLib (0x1000D000 for MT6765 - vendor DT
  node pwrap@1000d000, LK mt_reg_base.h PWRAP_BASE). Same as MT6768.

  ---------------------------------------------------------------------------
  SOC_TYPE == 1 (MT6762G, dandelion)

  [VERIFIED] against the MT6765 LK register map,
    platform/mt6765/include/platform/reg_PMIC_WRAP.h:
       PMIC_WRAP_WACS2_CMD    = PMIC_WRAP_BASE + 0xC20
       PMIC_WRAP_WACS2_RDATA  = PMIC_WRAP_BASE + 0xC24
       PMIC_WRAP_WACS2_VLDCLR = PMIC_WRAP_BASE + 0xC28
       PMIC_WRAP_INIT_DONE2   = PMIC_WRAP_BASE + 0xA0
  and the same four offsets are byte-identical in the MT6768 LK map
  (platform/mt6768/include/platform/reg_PMIC_WRAP.h), so this block would be
  correct for either SoC.

  [BUG IN UPSTREAM] MT6768Pkg uses 0x094 for InitDone2. In the MT6765/MT6768
  register map 0x094 is PMIC_WRAP_WACS1_EN, not INIT_DONE2 (0x0A0). The
  inherited value is kept in the #else branch so the diff against upstream
  stays visible, but the MT6762G branch uses the correct 0x0A0: with 0x094 the
  "wait for init" read samples an unrelated enable bit, which on this hardware
  reads back non-zero and makes the wait pass by accident.

  [VERIFIED] .ArbCapabilities = FALSE is right for MT6765: the MT6765 LK
  wrapper issues plain WACS2 commands with
    wacs_cmd = (write << 31) | ((adr >> 1) << 16) | wdata
  and decodes the FSM as (RDATA >> 16) & 0x7, which is exactly what
  PmicWrapperDxe does when ArbCapabilities is FALSE. (MT6768Pkg also has
  FALSE; MT6789Pkg sets TRUE because MT6789-class wrappers have the ARB
  block in use. MT6765 has the HARB registers at 0x64..0x78 but LK does not
  drive the SWINF2 path.)

  ---------------------------------------------------------------------------
  TODO(MT6765): the PMIC *chip* is MT6357, not MT6358, and none of its
  registers live in this file. They live in the shared driver
  MediaTekPkg/Drivers/MT6358PmicDxe/MT6358PmicDxe.c (mRegulators[]), which
  MediaTekPkg.dsc.inc builds unconditionally. On a dandelion build that table
  is wrong in every address, e.g.

      regulator        MT6358 (upstream table)   MT6357 (LK upmu_hw.h)
      ldo_vemc.Con0    0x1B1C                    0x1988
      ldo_vmch.Con0    0x1CD8                    0x1AC4
      ldo_vmc.Con0     0x1CC4                    0x1AB0
      TOPSTATUS        0x28                      0x24

  (MT6357 addresses from platform/mt6765/include/platform/upmu_hw.h:
   MT6357_LDO_VEMC_CON0 = 0x1988, MT6357_LDO_VMCH_CON0 = 0x1AC4,
   MT6357_LDO_VMC_CON0 = 0x1AB0; MT6357 confirmed as the PMIC by
   platform/mt6765/mt_pmic.c and the vendor DT.) Writing MT6358 addresses
  through an MT6357 is not a no-op: it lands on unrelated PMIC registers.
  Before any dandelion build touches storage power, MT6358PmicDxe must be
  forked into an MT6357 driver (or its table made selectable), including the
  per-regulator VOSEL shift/mask (MT6357: VEMC mask 0x7 shift 8,
  VMCH mask 0x7 shift 8, VMC mask 0xF shift 8).
**/

#include <Library/PmicWrapperImplLib.h>

//
// See ClockImplLib.c: SOC_TYPE has to reach the compiler or the MT6762G
// register map below silently compiles out and the inherited (incorrect)
// InitDone2 offset is used instead.
//
#if !defined (SOC_TYPE)
#error "MT6765Pkg needs -DSOC_TYPE=<n>; see MT6765Pkg.dsc.inc [BuildOptions]."
#endif

MTK_PMIC_WRAPPER_PLATFORM_INFO gPlatformInfo = {
#if SOC_TYPE == 1
  // MT6765 / MT6762G - LK reg_PMIC_WRAP.h for platform/mt6765.
  .RegMap = {
    [PmicWrapperInitDone2]   = 0x0A0,
    [PmicWrapperWacs2Cmd]    = 0xC20,
    [PmicWrapperWacs2RData]  = 0xC24,
    [PmicWrapperWacs2VldClr] = 0xC28,
  },
  .ArbCapabilities = FALSE,
#else
  // MT6768/MT6769T - inherited from MT6768Pkg, unchanged.
  // NOTE: InitDone2 = 0x094 is PMIC_WRAP_WACS1_EN in the real register map
  //       (INIT_DONE2 is 0xA0). Left as-is on purpose so the upstream diff
  //       stays reviewable; SOC_TYPE 1 above carries the fix.
  .RegMap = {
    [PmicWrapperInitDone2]   = 0x094,
    [PmicWrapperWacs2Cmd]    = 0xC20,
    [PmicWrapperWacs2RData]  = 0xC24,
    [PmicWrapperWacs2VldClr] = 0xC28,
  },
  .ArbCapabilities = FALSE,
#endif
};
