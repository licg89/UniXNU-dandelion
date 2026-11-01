# MT6765Pkg — MediaTek MT6765 / MT6762G support package (dandelion)

Status: **skeleton / scaffolding. Never built, never run.** Nothing in this
directory has been compiled, let alone executed on a phone. Read
[§7](#7-what-remains-unbuilt-and-unvalidated) before planning anything on top
of it.

Target: Xiaomi Redmi 9A (`dandelion`, MT6762G, 8× Cortex-A53, GICv3).

---

## 1. Derivation

Copied file-for-file from the only MT676x package in
[`mouseos/uefi-a04br3`](https://github.com/mouseos/uefi-a04br3), then renamed:

| upstream | this package |
|---|---|
| `Silicon/MediaTek/MT6768Pkg/MT6768Pkg.dsc.inc` | `MT6765Pkg/MT6765Pkg.dsc.inc` |
| `Library/{PlatformSecLib,GpioImplLib,ClockImplLib,MsdcImplLib,PmicWrapperImplLib}` | same paths, unchanged |

The structure is deliberately identical, and every changed line keeps the
original next to it (`#if SOC_TYPE == 1` … `#else` … `#endif`) so a diff against
upstream stays one-to-one and reviewable:

```
diff -ru  D:/aios/uefi-a04br3/Silicon/MediaTek/MT6768Pkg \
          D:/aios/dandelion-xnu/Silicon/MediaTek/MT6765Pkg
```

`SOC_TYPE` follows the plan in
[`docs/UEFI-MT6765PKG-DERIVATION.md`](../../../docs/UEFI-MT6765PKG-DERIVATION.md):

| `SOC_TYPE` | SoC | Notes |
|---|---|---|
| 0 | MT6765 (Helio P22) | inherited branch, essentially upstream MT6768 numbers, unverified |
| **1** | **MT6762G (Helio G25) — dandelion** | **the branch this port is for** |

Upstream's `SOC_TYPE 1` was MT6769T (Helio G80); that payload has been replaced
by MT6762G. MT6769T belongs to `MT6768Pkg`, not here.

### Tree

```
MT6765Pkg/
├── MT6765Pkg.dsc.inc                       ← the only file with absolute addresses
├── README.md
└── Library/
    ├── ClockImplLib/{ClockImplLib.c, ClockImplLib.inf}
    ├── GpioImplLib/{GpioImplLib.c, GpioImplLib.inf}
    ├── MsdcImplLib/{MsdcImplLib.c, MsdcImplLib.inf}
    ├── PlatformSecLib/{PlatformSec.c, PlatformRegisters.h, PlatformSecLib.inf,
    │                   AArch64/PlatformHelper.S}
    └── PmicWrapperImplLib/{PmicWrapperImplLib.c, PmicWrapperImplLib.inf}
```

---

## 2. The one PCD that must never be copied from MT6768Pkg

```ini
gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0C100000   # MT6765 / MT6762G
#                         upstream MT6768Pkg says 0x0C040000 — WRONG HERE
```

Agreed by three independent sources:

* vendor device tree `mt6765.dts`: `arm,gic-v3`, `reg = <0 0x0c000000 0 0x40000>,
  <0 0x0c100000 0 0x200000>, <0 0x10200a80 0 0x50>` — distributor, redistributor,
  polarity block;
* Android dmesg: `GICv3: CPU0: found redistributor 0 region 0:0x000000000c100000`;
* the dandelion shim's probe: `DANDELION_GICR_BASE` in
  [`pexpert/pexpert/arm64/DANDELION.h`](../../../pexpert/pexpert/arm64/DANDELION.h).

The failure mode with the upstream value is the nasty one: the firmware boots
and looks alive, but the redistributor it initialises is not the one this CPU is
attached to, so the arch-timer interrupt never arrives. It presents as "the
timer setup hangs", not as an address error.

**The PCD is only half of it.** `ArmGicV3` reaches the redistributors through
the platform `MemoryMapLib` descriptor, so the platform must also map
`0x0C100000`. Copying lancelot's
`Platforms/Xiaomi/lancelotPkg/Library/MemoryMapLib/MemoryMapLib.c` verbatim
(which says `{"GIC Redistributors", 0x0C040000, 0x00200000, ...}`) silently
defeats the corrected PCD. Fix both or neither.

---

## 3. What is actually in the libraries

Worth knowing before reading the code: **there are no peripheral base
addresses in any of the five libraries** — the only absolute addresses in the
whole package are the two GIC PCDs in the `.dsc.inc`. Everything else is a
*register offset* resolved against a region looked up by name at runtime:

| region name | base | looked up by |
|---|---|---|
| `Top Ck Gen` | `0x10000000` | ClockDxe |
| `Infra Cfg AO` | `0x10001000` | ClockDxe |
| `AP Mixed` | `0x1000C000` | ClockDxe |
| `Pinctrl` | `0x10005000` | GpioDxe |
| `PMIC Wrapper` | `0x1000D000` | PmicWrapperDxe |
| `MSDC-0` / `MSDC-1` | `0x11230000` / `0x11240000` | MsdcDxe |
| `Display OVL`, `WatchDog Timer` | platform map | PlatformSecLib |

All of those bases are **identical on MT6765 and MT6768** (checked against the
vendor DT and lancelot's memory map), so the plan's assumption that "MSDC base
`0x11230000` differs" turned out to be wrong: it is the same value. The real
memory-map differences for a dandelion platform are:

| entry | lancelot (MT6768) | dandelion (MT6765) |
|---|---|---|
| GIC Redistributors | `0x0C040000` | **`0x0C100000`** |
| MSDC Top-0 (`msdc0_top`) | `0x11C90000` | **`0x11CD0000`** (vendor DT `msdc0_top@11cd0000`) |
| System memory base | `0x40000000` | **`0x48000000`** (shim-measured; platform PCD, not this package) |

---

## 4. Verified vs TODO

Tags: **[V]** verified against a source that was read locally (vendor kernel
`dandelion-q-oss` @ `8974b91`, MT6765 LK, or the vendor DT) or measured on the
device; **[U]** inherited from MT6768Pkg, unverified.

### GIC / CPU / timer

| item | value | status |
|---|---|---|
| GICD base | `0x0C000000` | **[V]** same as MT6768; `GICD_IIDR=0x1143b` read there |
| GICR base | `0x0C100000`, 2 MiB | **[V]** DT + dmesg + shim probe |
| cores / clusters | 8 / 2 (affinity `0x00-0x03`, `0x100-0x103`) | **[V]** same as MT6768 |
| arch-timer PPIs | 29 / **30** / 27 / 26 | **[V]** DT `GIC_PPI 13/14/11/10` = INTID +16 |
| timer frequency | 13 MHz | **[V]** DT `clock-frequency = <13000000>`; used only as the `CNTFRQ_EL0 == 0` fallback |
| `WDT_MODE_KEY` | `0x22000000` | **[V]** LK `platform/mt6765/include/platform/mtk_wdt.h` |

### MSDC / eMMC

| item | value | status |
|---|---|---|
| MSDC0 base | `0x11230000` | **[V]** DT, LK `mt_reg_base.h`, shim `bootshim/msdc.c` |
| MSDC0 pin group | GPIO122…133 | **[V]** LK `mt_gpio_init.c` ("skip msdc pins init GPIO122~133") |
| individual DAT/CMD/CLK pin assignment | 122=CMD, 124=CLK, 123/128/125/132/126/129/127/130=DAT0-7 | **[U]** inherited; self-consistent with the 122-133 range but unproven |
| `MSDC_PAD_TUNE0` offset | `0xf0` | **[V]** vendor `msdc_reg.h` |
| `TuningStep`, `UseTop`, `AsyncFifo`, … | `{32,64}`, TRUE, TRUE, … | **[U]** MT6768-class quirks |
| clock gates `INFRA_MSDC0/1`, `INFRA_FAES_FDE`, `INFRA_MSDC0/1_SRC` | `0x88/0x8c/0x94` bit 2/4, `0xa4…` bit 29, `0xc0/0xc4/0xc8` bit 9/10 | **[V]** identical in `clk-mt6765.c` |
| top muxes `TOP_MSDC50_0_SEL`, `…_HCLK_SEL`, `TOP_MSDC30_1_SEL` | `CLK_CFG_3` `0x70/0x74/0x78` | **[V]** identical |
| parent tables | all three arrays match MT6768Pkg entry for entry | **[V]** `clk-mt6765.c:473-500` |

### GPIO

| item | value | status |
|---|---|---|
| Pinctrl base | `0x10005000` | **[V]** DT `gpio@10005000`, LK `GPIO_BASE` |
| `MaxPin` | 179 | **[V]** LK `gpio_const.h:40` `MAX_GPIO_PIN 179` — identical to MT6768Pkg |
| register grouping (`0x0/0x100/0x200/0x300` + `(pin/8)*0x10`) | | **[V]** consistent with LK/vendor `MODE0=0x3F0`, `MODE1=0x400` |

### PMIC

| item | value | status |
|---|---|---|
| PMIC chip | **MT6357** (the plan said "unconfirmed") | **[V]** MT6765 LK `upmu_hw.h` is the MT6357 map; `mt_pmic.c` logs "MT6357 CHIP Code" |
| PWRAP base | `0x1000D000` | **[V]** DT `pwrap@1000d000`, LK `PWRAP_BASE` |
| `WACS2_CMD/RDATA/VLDCLR` | `0xC20 / 0xC24 / 0xC28` | **[V]** LK `reg_PMIC_WRAP.h` (MT6765) |
| `INIT_DONE2` | **`0x0A0`** | **[V]** LK — and `0x094` (upstream's value) is `WACS1_EN`, see §6 |
| `ArbCapabilities` | FALSE | **[V]** LK uses the plain WACS2 encoding `(w<<31) \| ((adr>>1)<<16) \| data` |
| regulator register map | — | **TODO, blocking** — lives in `MediaTekPkg/Drivers/MT6358PmicDxe` and is MT6358's |

### Clocks

| item | status |
|---|---|
| controller bases, infracfg_ao gate banks, topckgen muxes, MSDC parent tables | **[V]** identical to MT6768Pkg |
| `AP_MSDCPLL` (CON0 `0x027C`, CON3 `0x0288`, CON1 `0x0280`, no rst_bar) | **[V]** `clk-mt6765.c` — now selected by `#if SOC_TYPE == 1` |
| the other 9 apmixed PLLs | **TODO** — MT6768 offsets still in place; MT6765 map is tabulated in `ClockImplLib.c`'s header |
| `FMax`/`FMin` per PLL | **TODO** — 3800/1500 MHz inherited, MT6762G tops out near 2 GHz |
| everything non-MSDC (display, MIPI, camera, audio, SSPM, CCIF) | **TODO** — not compared at all |

### Blocking TODOs, in the order they will bite

1. **MT6357 PMIC driver.** `MediaTekPkg.dsc.inc` builds
   `MediaTekPkg/Drivers/MT6358PmicDxe/MT6358PmicDxe.inf` unconditionally. Its
   `mRegulators[]` table is MT6358's, and the addresses do not overlap with
   MT6357's:

   | regulator | MT6358 (upstream table) | MT6357 (LK `upmu_hw.h`) |
   |---|---|---|
   | `ldo_vemc.Con0` | `0x1B1C` | `0x1988` |
   | `ldo_vmch.Con0` | `0x1CD8` | `0x1AC4` |
   | `ldo_vmc.Con0` | `0x1CC4` | `0x1AB0` |
   | `TOPSTATUS` | `0x28` | `0x24` |

   `MsdcImplLib::PowerControl()` goes through exactly those names, so as it
   stands an eMMC bring-up on a dandelion build would write MT6358 addresses
   into an MT6357 — not a no-op, it lands on unrelated registers. A
   `PcdPmicType` switch or a forked `MT6357PmicDxe` is required before storage
   power is ever touched.
2. **`msdc0_top` at `0x11CD0000`** in the platform memory map (else every
   pad-tune write goes nowhere).
3. **The 9 remaining apmixed PLLs**, if anything but eMMC clocks is enabled.
4. **GPIO signal↔pin mapping** for the eMMC bus (see `MsdcImplLib.c` for how to
   check it on silicon: read `Pinctrl+0x3F0` / `+0x400` after `InitGpio()`).
5. **Display OVL** offsets in `PlatformRegisters.h` are MT6768 guesses; wrong
   values cost blending, not a hang.

---

## 5. Deviations from MT6768Pkg

Everything that differs, with the reason:

| # | deviation | reason |
|---|---|---|
| 1 | `MT6768Pkg.dsc.inc` → `MT6765Pkg.dsc.inc`; 5 × `LibraryClasses` paths; `MT6768_CLOCK_ID` → `MT6765_CLOCK_ID` | rename (instruction 1). That typedef was the only `MT6768` token inside any source file |
| 2 | `PcdGicRedistributorsBase` `0x0C040000` → `0x0C100000` | measured; §2 |
| 3 | `SOC_TYPE 1` payload MT6769T → MT6762G: ACPI `OemTableId` `0x202032363736544D` (`"MT6762  "`) / `OemRevision 0x6762`, SMBIOS "Helio G25" / "MT6762G" | dandelion identity. `SOC_TYPE 0` SMBIOS part number → `MT6765` / "Helio P22" |
| 4 | `-DSOC_TYPE=$(SOC_TYPE)` added to `AARCH64_CC_FLAGS` | upstream never exposed `SOC_TYPE` to C, so the `#if SOC_TYPE` splits in the libraries could not work. Same technique as `Qualcomm/PakalaPkg` |
| 5 | `-march=armv8.2-a+crypto+rcpc` → `-march=armv8-a+crc` | MT6762G/MT6765 are 8× Cortex-A53 = ARMv8.0-A; ARMv8.1 LSE atomics / ARMv8.2 RCpc would fault. Precedent: `MT8167Pkg` (Cortex-A35) |
| 6 | explicit `PcdArmArchTimer{Sec,}IntrNum` 29/30/27/26 | documents the measured PPI numbers; these are also ArmPkg's defaults, so no behaviour change |
| 7 | `PcdTimerFreqOverwrite = 13000000` | DT says 13 MHz; `Timer.patch` only consults it when `CNTFRQ_EL0` reads 0, i.e. pure safety net |
| 8 | `PlatformHelper.S`: `#if SOC_TYPE == 1` block programming `CNTHCTL_EL2 = 3`, `CNTVOFF_EL2 = 0`, `CPTR_EL2 = 0x33FF`, `VPIDR_EL2`/`VMPIDR_EL2` | with `USE_PHYSICAL_TIMER=1`, EL1 timer accesses trap to EL2 unless `CNTHCTL_EL2.EL1PCTEN/EL1PCEN` are set, and there is no EL2 vector — a silent hang. Copied from the only MTK port in this tree that has booted (MT8167Pkg). **Unvalidated here**, and dead code if the payload is entered at EL1 (which is what the current shim does) |
| 9 | `PmicWrapperImplLib.c`: `SOC_TYPE 1` uses `INIT_DONE2 = 0x0A0` | upstream's `0x094` is `WACS1_EN`, not `INIT_DONE2`; §6 |
| 10 | `ClockImplLib.c`: `#if SOC_TYPE == 1` around `AP_MSDCPLL`'s offsets + `MT6765_APMIXED_*` defines + header table | MSDCPLL moved from `0x33C` to `0x027C`; it is the clock the eMMC path reads |
| 11 | `MsdcImplLib.c`: one `DEBUG_INFO` line under `SOC_TYPE == 1` | says out loud that the pin assignment is unverified; no functional change |
| 12 | comment blocks in every file, `TODO(MT6765):` markers | provenance + instructions, no functional change |
| 13 | `.inf` files re-created with a header; `BASE_NAME`/`FILE_GUID` **unchanged** | upstream `MT6789Pkg` already reuses `MT6768Pkg`'s GUIDs, and only one MediaTek package is ever linked per platform. Do not include two MTK packages in one DSC |

Not deviated: section order, PCD set (except additions above), `USE_PHYSICAL_TIMER`,
`PcdCoreCount`/`PcdClusterCount`, and all `MTK_*_PLATFORM_INFO` values except the
`INIT_DONE2` fix.

---

## 6. Upstream bug found while deriving

`MT6768Pkg/Library/PmicWrapperImplLib/PmicWrapperImplLib.c` sets
`[PmicWrapperInitDone2] = 0x094`. In the MT6768 **and** MT6765 LK register maps
(`platform/mt67xx/include/platform/reg_PMIC_WRAP.h`) `0x094` is
`PMIC_WRAP_WACS1_EN`; `PMIC_WRAP_INIT_DONE2` is `0x0A0`. So upstream's
"wait until the wrapper is initialised" polls an unrelated enable bit that
happens to read back non-zero. `MT6765Pkg` keeps the wrong value in the `#else`
branch (so the diff stays honest) and uses `0x0A0` for `SOC_TYPE 1`.
The same fix probably belongs in `MT6768Pkg` and `MT6789Pkg` upstream.

---

## 7. What remains unbuilt and unvalidated

* **Nothing here has been compiled.** No EDK2 build was attempted while writing
  this package (that was explicitly out of scope), so treat the tree as
  *syntactically reviewed*, not *build-clean*.
* `dandelion-xnu` has no EDK2 tree, no `build_uefi.py`, and no `Mu_Basecore` /
  `Silicium-ACPI` submodules — these files cannot be built from this repository
  at all. They are sources to be dropped into a UEFI workspace (§8).
* **No platform package exists yet.** `Platforms/Xiaomi/dandelionPkg`
  (`.dsc` / `.fdf` / `.dec` / `DeviceBuild.py` / `MemoryMapLib` / ACPI tables) is
  a prerequisite and is not part of this task.
* **No MTK phone has ever booted UEFI from this code lineage.** Upstream's own
  status page lists the MTK groups as *Inactive*: `MT6768Pkg`'s only target
  (lancelot) reached Windows PE with **"One Core only"**; `MT6789Pkg` (stone)
  has all features `?`. The single MediaTek device that has booted anything is
  the Benesse TAB-A04-BR3 (`MT8167Pkg`, GICv2, Cortex-A35) — a different
  interrupt controller and a different CPU generation, so it validates the
  *approach*, not this package.
* **The 4 KiB / 16 KiB page-granule problem** (`docs/MMU_GRANULE.md`) is
  untouched by anything here.
* No UART on dandelion: nothing in this package provides console output, so all
  bring-up evidence has to come from the eMMC log channel (`docs/EMMC-LOG.md`).

---

## 8. How this package is meant to be built

It is an EDK2 *package include*, not a platform: nothing builds it by itself.
The build lives in a writable clone of `uefi-a04br3` (the copy at
`D:\aios\uefi-a04br3` is a read-only reference and must not be modified).

### 8.1 One-time wiring

1. Copy this directory into the workspace's package search path:

   ```powershell
   Copy-Item -Recurse D:\aios\dandelion-xnu\Silicon\MediaTek\MT6765Pkg `
                          <workspace>\Silicon\MediaTek\MT6765Pkg
   ```

   `Silicon/MediaTek` is already in `PackagesPath` for the MediaTek platforms
   (`Platforms/Xiaomi/lancelotPkg/DeviceBuild.py`), so no search-path edit is
   needed.

2. Create `Platforms\Xiaomi\dandelionPkg\` modelled on `lancelotPkg`, with
   `dandelion.dsc` containing at minimum:

   ```ini
   [Defines]
     PLATFORM_NAME                  = dandelion
     PLATFORM_GUID                  = <new guid>
     OUTPUT_DIRECTORY               = Build/dandelionPkg
     FLASH_DEFINITION               = dandelionPkg/dandelion.fdf
     SOC_TYPE                       = 1          # MT6762G
   
   !include MT6765Pkg/MT6765Pkg.dsc.inc
   
   [PcdsFixedAtBuild]
     gArmTokenSpaceGuid.PcdSystemMemoryBase|0x48000000       # shim-measured
     gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x48000000
     gMediaTekPkgTokenSpaceGuid.PcdStorageIsEMMC|TRUE
     gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|720      # 720x1600 NT36525B
     gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1600
     ...
   [LibraryClasses]
     MemoryMapLib|dandelionPkg/Library/MemoryMapLib/MemoryMapLib.inf
   ```

   and a `MemoryMapLib` whose register-region list uses **GICR `0x0C100000`**,
   **`MSDC Top-0` `0x11CD0000`**, and DRAM from `0x48000000`. Also needed:
   `dandelion.fdf`, `dandelion.dec`, `DeviceBuild.py` (copy lancelot's, change
   `PackagesSupported`/`GetName`/`GetPlatformDscAndConfig`/`PRODUCT_NAME`), an
   `AcpiTables.inf` (or drop the component), and `Resources\Configs\dandelion.toml`
   (FD base/size, payload/boot-image config) plus `Resources\DTBs\dandelion.dts`
   if the config references one.

3. Build. From the workspace root, with Python 3.11+, `edk2toolext`,
   `coloredlogs`, and clang/LLVM available (`TOOL_CHAIN_TAG=CLANGPDB` is set by
   `DeviceBuild.py`), and all submodules initialised (`Common/Mu`,
   `Common/Mu_OEM_Sample`, `Mu_Basecore`, `Silicium-ACPI`, `Binaries`):

   ```powershell
   python build_uefi.py -d dandelion -m 0 -r DEBUG -c
   ```

   (`-d dandelion` resolves `Platforms\*\dandelionPkg` + `Resources\Configs\dandelion.toml`;
   `-m 0` is the model index; `-c` cleans.) Release: `-r RELEASE`.
   Expected artefacts: `Build/dandelionPkg/DEBUG_CLANGPDB/FV/*.fd`, and a boot
   image from `patch_boot.py`, per `docs/UEFI-MT6765PKG-DERIVATION.md` §5.

### 8.2 Compile-smoke-test before `dandelionPkg` exists (M1 gate)

To find out whether this package compiles at all, without a dandelion platform:
in a *scratch* clone, point lancelot's DSC at this package and build — the
resulting FD is for the wrong board (wrong memory map, wrong SMBIOS, wrong
DRAM base) and **must not be flashed**, it is a compiler check only:

```powershell
# scratch clone only. lancelot.dsc already has SOC_TYPE = 1, which is our
# MT6762G branch, so this compiles the MT6765 code paths (not the SOC_TYPE 0 ones).
(Get-Content Platforms\Xiaomi\lancelotPkg\lancelot.dsc) `
  -replace '^!include MT6768Pkg/MT6768Pkg.dsc.inc','!include MT6765Pkg/MT6765Pkg.dsc.inc' |
  Set-Content Platforms\Xiaomi\lancelotPkg\lancelot.dsc

python build_uefi.py -d lancelot -m 0 -r DEBUG -c
```

M1 is met when the build completes and the FD is ≤ 4 MiB
(`docs/UEFI-MT6765PKG-DERIVATION.md` §4). Note that
`Platforms/Xiaomi/lancelotPkg/Library/MemoryMapLib/MemoryMapLib.c` will still
describe GICR `0x0C040000` in this smoke test — harmless for a compile, fatal
for a boot.

### 8.3 Things that will break the build, and are expected to

* `ClockImplLib.c`, `PmicWrapperImplLib.c` and `PlatformHelper.S` open with
  `#error` when `SOC_TYPE` is not defined. That is deliberate: without
  `-DSOC_TYPE`, every `#if SOC_TYPE == 1` compiles out and the package silently
  ships MT6768 register data (wrong `INIT_DONE2`, wrong `MSDCPLL` offsets,
  no EL2 timer setup). The error fires if these libraries are ever pulled in
  from a DSC that does not include `MT6765Pkg/MT6765Pkg.dsc.inc`.
* `PcdTimerFreqOverwrite` needs `Resources/MuPatches/Timer.patch` applied to
  `Mu_Basecore` (build_uefi.py does this automatically) — without the patch the
  PCD is simply unused, which is not an error.
* `-march=armv8-a+crc` will fail loudly (not silently) if any module assumes
  ARMv8.1 atomics. That is the intended behaviour: better a compile error than
  an illegal-instruction fault on an A53.
* If the platform also includes `MT6768Pkg`, both packages define the same
  `PlatformSecLib`/`ClockImplLib`/… `BASE_NAME`s and `FILE_GUID`s; EDK2 will
  either shadow one or emit duplicate-GUID modules. Include exactly one MTK
  package per platform.

---

## 9. Sources used for every claim above

All read locally (paths are workspace-relative to `D:\aios`):

| what | where |
|---|---|
| MT6765 vendor kernel (DT, clock driver), branch `dandelion-q-oss` @ `8974b91` | `research/refs/mt6765-vendor/` — `arch/arm64/boot/dts/mediatek/mt6765.dts`, `drivers/clk/mediatek/clk-mt6765.c` |
| MT6765 LK (`platform/mt6765`) | `research/refs/lk-svoboda18/platform/mt6765/` — `include/platform/{mt_reg_base.h,reg_PMIC_WRAP.h,gpio_const.h,upmu_hw.h,mtk_wdt.h}`, `mt_gpio_init.c`, `mt_pmic.c`, `mt_pmic_wrap_init.c` |
| upstream UEFI package | `uefi-a04br3/Silicon/MediaTek/{MT6768Pkg,MediaTekPkg,MT8167Pkg}` |
| device facts (GICR, MSDC, page size, DRAM) | `dandelion-xnu/pexpert/pexpert/arm64/DANDELION.h`, `dandelion-xnu/bootshim/{gic_probe.c,msdc.c}`, `dandelion-xnu/docs/*` |
| register-level background for MT6765 | `research/mtk6765-drivers.md` |
