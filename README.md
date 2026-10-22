# V821 AI Glass

Language: English | [中文](README_CN.md)

This repository contains two V821 AI glasses related solutions:

1. `v821-aiglass`
2. `v821-aiglass_fastboot`

## Project Overview

### v821-aiglass

`v821-aiglass` is a board-level configuration solution for a complete AI Glass product form. It uses an optical waveguide module and is intended for a more complete professional AI glasses design.

Currently supported capabilities:

- Large model API integration and image recognition
- `320 x 640` image display and text display
- One-click photo capture, video recording, and related functions

Due to product size constraints, this solution has completed testboard-level validation and module feasibility validation. Full device assembly is still in progress.

<p>
  <a href="Application/Image/1.png"><img src="Application/Image/1.png" width="180" alt="v821-aiglass preview 1"></a>
  <a href="Application/Image/debug.png"><img src="Application/Image/debug.png" width="180" alt="v821-aiglass debug preview"></a>
  <a href="Application/Image/2.png"><img src="Application/Image/2.png" width="180" alt="v821-aiglass preview 2"></a>
</p>

### v821-aiglass_fastboot

`v821-aiglass_fastboot` is a fast boot solution based on fastboot. Because fastboot is tightly coupled with the SoC, development flexibility is relatively limited, and most of the configuration comes from the vendor reference solution.

This solution refers to the Allwinner public `board` and the Sochip V821 AI glasses development board. It does not follow a traditional Linux boot flow. With fastboot, it can perform fast image capture, photo capture, and video recording, with the overall flow controlled within 1 second.

Currently supported capabilities:

- Large model API integration and speech recognition
- AI calling, photo forwarding, and intelligent analysis
- One-click photo capture, video recording, and related functions

<p>
  <a href="Application/Image/ai_glass.png"><img src="Application/Image/ai_glass.png" width="260" alt="v821-aiglass fastboot preview"></a>
</p>

## Repository Layout

### Hardware_for_test

Hardware test related content, using the AvaotaF1 design form factor as a reference.

A testboard is currently provided for module testing and functional validation, and it is intended for `v821-aiglass`. Smaller PCB designs will continue to be explored so the hardware can fit into a glasses structure.

<p>
  <a href="Application/Image/testboard.png"><img src="Application/Image/testboard.png" width="180" alt="testboard preview"></a>
</p>

### Application

[`Application/aiglass-apps`](Application/aiglass-apps/README.md) contains the C/C++ application sources, AGLink support, and seven applications: normal, photo, audio, download, OTA, video, and livestream.

[`Application/lvgl-home`](Application/lvgl-home/) provides a `320 × 320` glasses home
screen for LVGL 8.x, showing the date, weekday, time, and seconds using text and outlines.
It draws no background image and plugs into an existing LVGL application. LVGL itself is
not bundled, and the UI is separate from the seven applications above.

The `Application/Makefile` provides a common build entry and defaults to the `aiglass_fastboot` board, whose OpenWrt configuration already enables these applications. A complete Tina V821 BSP, cross toolchain, and built dependencies are required; `SDK/TinaLinux 5.0` in this repository contains patches only.

```bash
make -C Application list
make -C Application check AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
make -C Application all AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
make -C Application package AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
```

See the [application guide](Application/aiglass-apps/README.md) for dependency preparation, rootfs installation, and BSP source integration. Build and package outputs stay under `Application/aiglass-apps/build`, `output`, and `dist` and are ignored by Git.

### SDK/TinaLinux 5.0

SDK patches and board-level configuration content, mainly including:

- `v821-aiglass`
- `v821-aiglass_fastboot`

## How to Use the Patches

Copy the patch files to the corresponding SDK directories. For further board-level development, refer to the Allwinner online documentation:

https://docs.aw-ol.com/docs/soc/v821/
