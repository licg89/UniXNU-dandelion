APRIORI DXE {
  #
  # Diagnostic driver first in the dispatch order (2026-10-28 21:0x).
  #
  # It draws its own screen markers before anything else can, so its output is
  # the proof that the DXE dispatcher reached this point at all.  Everything
  # else in this list runs after it.
  #
  INF dandelionPkg/Drivers/PlatformProgressDxe/PlatformProgressDxe.inf
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF MediaTekPkg/Drivers/GpioDxe/GpioDxe.inf
  INF MediaTekPkg/Drivers/ClockDxe/ClockDxe.inf
  INF MediaTekPkg/Drivers/PmicWrapperDxe/PmicWrapperDxe.inf
  INF MediaTekPkg/Drivers/MT6358PmicDxe/MT6358PmicDxe.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  #
  # 2026-10-29 20:3x -- MsdcDxe is GONE, not reordered.
  #
  # Everything above was about *where* it ran.  The question was wrong: running
  # it at all is what costs the data path.  Its bring-up resets the controller,
  # sends CMD0 and re-initialises the card, and a card re-initialised that way
  # answers every command and then never drives DAT0..7 (IMG_5760: four bus
  # modes, identical I=0100 / ps=EF/FF / k=14999).  bootshim/msdc.c gets reads
  # and writes out of the same controller on the same phone by inheriting LK's
  # state and touching only the clock divider -- see docs/EMMC-LOG.md.
  #
  # So MSDC0 must be untouched when StorageBeaconDxe runs, and StorageBeaconDxe
  # is now the eMMC driver (InheritProbe) rather than a reporter on top of one.
  # Its Depex is TRUE, so it needs nothing from here to dispatch.
  #
# INF MediaTekPkg/Drivers/MsdcDxe/MsdcDxe.inf

  INF MdeModulePkg/Bus/Sd/EmmcDxe/EmmcDxe.inf
  INF MdeModulePkg/Bus/Sd/SdDxe/SdDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  #
  # BdsDxe is deliberately NOT in this list any more (2026-10-28 20:2x).
  #
  # BdsEntry never returns: it is the boot manager, it waits for input.  While
  # it sat here, the moment a real console satisfied its dependency it was
  # dispatched and took over -- and every driver still queued in the FV body
  # (PlatformProgressDxe, MsdcDxe, FatPkg, StorageBeaconDxe) never ran.  That
  # is why a build with the console in APRIORI showed the console's own
  # SELFTEST lines and nothing else.
  #
  # It is still in DXE.inc, at the end of the FV, which is where a boot manager
  # belongs: everything the platform needs is dispatched before it.
  #
# INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 0
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif
  INF SiliciumPkg/Drivers/KeypadDeviceDxe/KeypadDeviceDxe.inf
  INF SiliciumPkg/Drivers/KeypadDxe/KeypadDxe.inf

  #
  # SimpleTextInOutSerial is deliberately gone.  It was inherited from the
  # A04BR3 tablet, which has a UART; dandelion has no UART pads at all.  What
  # it did here was add a phantom 80x25 text device to ConSplitter -- visible
  # in PlatformProgressDxe's dump as "COLS 0050 ROWS 0019" -- which satisfied
  # BdsDxe's console dependency before any real framebuffer console existed,
  # so BDS printed its messages into the bit bucket while the panel showed
  # whichever 720-pitch renderer happened to be attached.
  #
  # With it gone, ConSplitter has exactly one text child: DandelionConsoleDxe.
  #
# INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  #
  # Our framebuffer console has to be dispatched in THIS pass, after
  # ConSplitterDxe, and therefore before BdsDxe gets its retry.
  #
  # Measured 2026-10-28 19:4x: with the console only in the FV body, the ConOut
  # probe in PlatformProgressDxe printed nothing while a direct call to the same
  # OutputString printed fine.  So ConSplitter had no framebuffer child when
  # ConOut was used: BdsDxe's dependency was already satisfied by
  # SimpleTextInOutSerial (an 80x25 serial console), so BDS ran first and its
  # messages -- "Volume Up", "boot manager", "failed to find an operating
  # system" -- went to the serial port and never to the panel.
  #
  INF dandelionPkg/Drivers/DandelionConsoleDxe/DandelionConsoleDxe.inf
  INF dandelionPkg/Drivers/DandelionEmmcDxe/DandelionEmmcDxe.inf
  #
  # Storage connect sweep -- after the console on purpose (2026-10-29 01:0x).
  #
  # This driver does the ConnectController sweep that BDS would normally do, and
  # EmmcDxe is started from inside that sweep.  EmmcDxe explains its failures
  # only through DEBUG(), so the sweep has to happen after a console exists or
  # the explanation is thrown away -- measured: with this driver before the
  # console, "EmmcDxe looked at us and sent commands" (cells 16/17) was all we
  # could get, with no idea which step failed.
  #
  # Dispatch is safe to leave past the console: the console's own entry point
  # now paints a bar at y 1384 when it returns, and on hardware that bar appears,
  # so the dispatcher keeps going.
  #
  INF dandelionPkg/Drivers/StorageBeaconDxe/StorageBeaconDxe.inf
  #
  # M5: the last thing this firmware does.  BdsDxe is not in the list at all, so
  # this driver is the boot policy: try every FAT volume for \UniXNU.efi (and two
  # fallback names), then fall back to the payload embedded in the FV, then paint
  # blue/green/red at y 1570 to say which of those happened.
  #
  # It costs nothing to place it last: with no payload present it paints red and
  # returns, which is exactly the state the board is in before M4.
  #
  INF dandelionPkg/Drivers/PayloadBootDxe/PayloadBootDxe.inf
  #
  # GraphicsConsoleDxe is deliberately NOT here any more.
  #
  # Commenting it out in DXE.inc is NOT enough, and neither is removing it from
  # the DSC [Components] list: this APRIORI list is what actually puts a module
  # into the FV.  Measured 2026-10-28 by searching the built FVMAIN for the
  # driver's file GUID (CCCB0C28-4B24-11d5-9A5A-0090273FC14D):
  #
  #   line present -> GUID found in FVMAIN   (both consoles shipped, so
  #                   ConSplitter aggregated ours and the sheared one)
  #   line removed -> GUID absent
  #
  # Replaced by dandelionPkg/Drivers/DandelionConsoleDxe, which draws with the
  # real 2944-byte stride.
  #
# INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
}
