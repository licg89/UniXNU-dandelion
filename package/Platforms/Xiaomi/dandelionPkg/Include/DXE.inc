  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf
  # M2 progress beacon: first driver in the FV that we control, so the 2 s
  # watchdog re-arm happens as early as the dispatcher allows.
  INF dandelionPkg/Drivers/PlatformProgressDxe/PlatformProgressDxe.inf
  INF dandelionPkg/Drivers/DandelionConsoleDxe/DandelionConsoleDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
# INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf   # dandelion has no UART; see APRIORI.inc
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
#   INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf   # replaced: GraphicsConsoleDxe shears every row by 16 px here
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF MediaTekPkg/Drivers/GpioDxe/GpioDxe.inf
  INF MediaTekPkg/Drivers/ClockDxe/ClockDxe.inf
  INF MediaTekPkg/Drivers/PmicWrapperDxe/PmicWrapperDxe.inf
  # MT6358PmicDxe stays for now: removing it from the FV made the whole DXE chain stop
  # before DandelionConsoleDxe (the bars and SELFTEST disappeared), so something earlier
  # in the list locates the PMIC protocol and trips over its absence.  Keep the driver,
  # but never let MsdcImplLib write through it -- see PowerControl().
  INF MediaTekPkg/Drivers/MT6358PmicDxe/MT6358PmicDxe.inf

  #
  # MsdcDxe is deliberately NOT in the firmware volume any more (2026-10-29
  # 20:3x).  Two reasons, both measured:
  #
  #   1. Its bring-up is the thing that breaks the data path.  It resets the
  #      controller (MSDC_CFG.RST), sends CMD0, walks the card up from IDLE and
  #      sets the host bus width -- while the card's own bus width lives in
  #      EXT_CSD[183] and survives CMD0.  After that every command answers and
  #      no data ever arrives (IMG_5757..IMG_5760: I=0100, ps=EF/FF, k=14999).
  #   2. bootshim/msdc.c reads and writes the same controller from bare metal on
  #      the same phone precisely by NOT doing any of that -- it inherits LK's
  #      state and only slows the clock (docs/EMMC-LOG.md has the verified
  #      result: 5159 bytes written and read back from flashinfo).
  #
  # StorageBeaconDxe now carries that inherit path instead (see its InheritProbe)
  # and its Depex is TRUE, so it dispatches on its own.  EmmcDxe/SdDxe stay in
  # the volume but have no pass-through producer to bind to, which is what we
  # want until the inherit path produces a BlockIo of its own.
  #
# INF MediaTekPkg/Drivers/MsdcDxe/MsdcDxe.inf
  # M4 eMMC, before the beacon so the beacon has a BlockIo handle to report on.
  INF dandelionPkg/Drivers/DandelionEmmcDxe/DandelionEmmcDxe.inf
  # M4 beacon: now self-contained -- it is the eMMC driver, not a reporter on
  # top of one.
  INF dandelionPkg/Drivers/StorageBeaconDxe/StorageBeaconDxe.inf
  #
  # M5 loader.  This file is the FV content list; APRIORI.inc only orders the
  # dispatch.  Both are needed -- measured 2026-10-29: PayloadBootDxe compiled,
  # was listed in APRIORI.inc, and did not appear in the firmware volume at all
  # until this line existed (FVMAIN.inf is the ground truth: grep its file list).
  #
  INF dandelionPkg/Drivers/PayloadBootDxe/PayloadBootDxe.inf

  INF MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf
  INF MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 0
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif
  INF SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
  INF SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
