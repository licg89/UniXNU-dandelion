/**
  MT6765 / MT6762G GPIO block description.

  Derived from MT6768Pkg/Library/GpioImplLib/GpioImplLib.c. Every value below
  is unchanged from MT6768Pkg, because for once they all turned out to be
  correct for MT6765 as well - see the per-field notes.

  What is NOT here: the Pinctrl base address. GpioDxe locates the "Pinctrl"
  memory region through the platform MemoryMapLib (0x10005000 on both MT6768
  and MT6765: vendor DT node gpio@10005000, LK mt_reg_base.h GPIO_BASE), so a
  platform that reuses lancelot's memory map gets the right base for free.

  [VERIFIED] MaxPin = 179. MT6765 LK has the identical limit:
    platform/mt6765/include/platform/gpio_const.h:40
      #define MAX_GPIO_PIN 179
  (LK rejects pin >= MAX_GPIO_PIN, GpioDxe rejects pin > MaxPin, so this is
  one pin more permissive than LK - harmless.)

  [VERIFIED] Register grouping. The MT6765 vendor kernel drives the eMMC pin
  mux through MSDC0_GPIO_MODE0 = Pinctrl + 0x3F0 and MODE1 = Pinctrl + 0x400
  (drivers/mmc/host/mediatek/ComboA/mt6765/msdc_cust.h), and GpioDxe computes
  mode register = ModeOffset + (pin / 8) * 0x10. 0x300 + (122/8)*0x10 = 0x3F0
  and 0x300 + (130/8)*0x10 = 0x400, i.e. the same 8-pins-per-register,
  4-bits-per-pin layout that MT6768Pkg assumes, for the same pin range
  (GPIO122..133, see MsdcImplLib.c).

  TODO(MT6765): nothing known. If a pin above 179 is ever needed on MT6765
  (SD card detect, etc.), re-derive MaxPin from pinctrl-mt6765.c / the DT
  gpio-ranges property rather than raising this number by guesswork.
**/

#include <Library/GpioImplLib.h>

MTK_GPIO_PLATFORM_INFO gPlatformInfo = {
  .SetOffset     = 0x4,
  .ResetOffset   = 0x8,
  .DirOffset     = 0x0,
  .DataOutOffset = 0x100,
  .DataInOffset  = 0x200,
  .ModeOffset    = 0x300,
  .MaxPin        = 179,   // [VERIFIED] LK gpio_const.h:40, same as MT6768
};
