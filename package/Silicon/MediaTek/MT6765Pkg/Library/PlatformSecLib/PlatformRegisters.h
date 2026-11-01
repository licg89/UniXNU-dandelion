#ifndef _PLATFORM_REGISTERS_H_
#define _PLATFORM_REGISTERS_H_

//
// Display OVL Registers
//
// [INHERITED] From MT6768Pkg, unchanged.
// TODO(MT6765): verify against dandelion's display hardware before trusting it.
//   The Display OVL block base comes from the platform MemoryMapLib entry
//   "Display OVL"; the only value that has to be measured is the *offset*.
//   On lancelot (MT6768) the map places OVL at 0x1400B000 inside the DDP
//   window at 0x14000000. dandelion's vendor DT only describes the DDP block
//   (ddp@14000000), not the OVL sub-block, so what must be measured is:
//     1. the OVL base inside the DDP window (probe +0x0000..+0x2000 for the
//        OVL "DISP_OVL" register signature, or read it from LK's
//        platform/mt6765 display tables if present), and
//     2. whether the constant-blend enable bit is really BIT28 of
//        OVL_PITCH_OFFSET(n) on MT6765 as it is on MT6768.
//   If either is wrong the failure mode is benign (no blending / no display),
//   it does not hang the boot.
//
#define OVL_PITCH_OFFSET(x)           (0x44 + 0x20 * (x))

//
// Display OVL Bits
//
// [INHERITED] Not verified on MT6765 - see the TODO above.
//
#define OVL_CONST_BLEND                BIT28

//
// WatchDog Timer Values
//
// [VERIFIED] WDT_MODE_KEY = 0x22000000, from the MT6765 vendor LK
//   (platform/mt6765/include/platform/mtk_wdt.h: MTK_WDT_MODE_KEY) and the
//   vendor kernel mtk_wdt_v2.c. Every write to WDT_MODE must OR this key in or
//   the write is ignored. Writing only the key with ENABLE (bit 0) clear -
//   which is what this value does, since the whole register is overwritten
//   with 0x22000000 - also disables the watchdog. Same on MT6768.
//
#define WDT_MODE_KEY                   0x22000000

//
// [VERIFIED] Register offsets, from the same vendor header
//   (drivers/watchdog/mediatek/wdt/common/wdt_v2/mtk_wdt.h):
//     MTK_WDT_MODE   = TOPRGU + 0x0000
//     MTK_WDT_LENGTH = TOPRGU + 0x0004
//   TOPRGU itself is the MemoryMapLib region "WatchDog Timer" (0x10007000),
//   so nothing here hardcodes the base.
//
#define WDT_MODE_OFFSET                0x0000
#define WDT_LENGTH_OFFSET              0x0004

//
// [VERIFIED] WDT_MODE bits (mtk_wdt.h): ENABLE 0x0001, EXT_POL 0x0002,
//   EXTEN 0x0004, IRQ 0x0008, AUTO_RESTART 0x0010, IRQ_LEVEL_EN 0x0020,
//   DUAL_MODE 0x0040, DDR_RESERVE 0x0080.
//   EXTEN makes expiry pull the external reset line (a real SoC reset)
//   instead of only raising the interrupt, which is what a beacon needs.
//
#define WDT_MODE_ENABLE                0x0001
#define WDT_MODE_EXTEN                 0x0004

//
// [VERIFIED] WDT_LENGTH: the timeout sits in bits 15:5 and the low 5 bits must
//   carry LENGTH_KEY or the write is ignored (mtk_wdt.h:
//   MTK_WDT_LENGTH_TIME_OUT 0xffe0, MTK_WDT_LENGTH_KEY 0x0008).
//   mtk_wdt_v2.c::mtk_wdt_set_timeout() encodes N seconds as
//       timeout = N * (1 << 6);  timeout <<= 5;
//       writel (timeout | MTK_WDT_LENGTH_KEY, MTK_WDT_LENGTH);
//   i.e. the field is N * 64, so the longest representable timeout is
//   2047 / 64 ~= 31 s. Do not ask for more than 31.
//
#define WDT_LENGTH_KEY                 0x0008
#define WDT_LENGTH_MAX_SECONDS         31

#endif /* _PLATFORM_REGISTERS_H_ */
