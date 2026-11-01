# dandelion hardware, as measured on the device

Recorded from the phone itself (LineageOS 20, Android 13, `lineage_blossom-userdebug
13 TQ3A.230901.001`) with `adb root`, and from the tree that ran during the previous
attempt.  These are observations, not datasheet values.

## Storage

`/dev/block/by-name/` (all 21 entries read; the ones that matter here):

| Name | Node | Note |
| --- | --- | --- |
| `boot` | `mmcblk0p33` | |
| `recovery` | `mmcblk0p2` | |
| `lk` / `lk2` | `mmcblk0p31` / `p32` | the firmware's little kernel |
| `flashinfo` | `mmcblk0p43` | carries the ESP and the kernel log block |
| `userdata` | `mmcblk0p41` | **52 GB total, 20 GB free** - the natural root volume |
| `cache` | `mmcblk0p40` | 362 MB |
| `metadata` | `mmcblk0p14` | |
| `super` | `mmcblk0p38` | dynamic partitions: system, vendor, product |
| `vbmeta` / `vbmeta_system` / `vbmeta_vendor` | `p39` / `p8` / `p9` | |

Controller: MSDC0, PIO mode works (previous attempt).

## Display

- Physical size 720x1600, density 320 (`wm size`, `wm density`)
- LCM bias supply: TPS65132 over I2C (`[LCM] tps65132_probe`)
- Primary display orientation 0 (`ro.surface_flinger.primary_display_orientation`)
- DSI0 base `0x14014000`, stride 2944, framebuffer `0x7ec50000` as brought up by the LK in the
  previous attempt

## Touch

```
[NVT-ts] nvt_driver_init: TP info: [Vendor]HELITAI [IC]nt36525b
[NVT-ts] nvt_parse_dt: novatek,irq-gpio=329
```

So the panel-side touch is a Novatek NT36525B (vendor HELITAI) with its interrupt on GPIO 329.
A `himax_hx83102` driver also initialises (`[HXTP] himax_hx83102_init`), which suggests either a
second controller or a compatibility probe; the Novatek one is the one that reports a panel.

## Reading this data again

`dmesg`, `/proc/partitions` and `/sys/class/block/*/size` are all `Permission denied` without
root; `adb root` is available on this build, after which all three read normally.