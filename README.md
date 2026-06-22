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
  <a href="test_image/1.png"><img src="test_image/1.png" width="180" alt="v821-aiglass preview 1"></a>
  <a href="test_image/debug.png"><img src="test_image/debug.png" width="180" alt="v821-aiglass debug preview"></a>
  <a href="test_image/2.png"><img src="test_image/2.png" width="180" alt="v821-aiglass preview 2"></a>
</p>

### v821-aiglass_fastboot

`v821-aiglass_fastboot` is a fast boot solution based on fastboot. Because fastboot is tightly coupled with the SoC, development flexibility is relatively limited, and most of the configuration comes from the vendor reference solution.

This solution refers to the Allwinner public `board` and the Sochip V821 AI glasses development board. It does not follow a traditional Linux boot flow. With fastboot, it can perform fast image capture, photo capture, and video recording, with the overall flow controlled within 1 second.

Currently supported capabilities:

- Large model API integration and speech recognition
- AI calling, photo forwarding, and intelligent analysis
- One-click photo capture, video recording, and related functions

<p>
  <a href="test_image/ai_glass.png"><img src="test_image/ai_glass.png" width="260" alt="v821-aiglass fastboot preview"></a>
</p>

## Repository Layout

### Hardware_for_test

Hardware test related content, using the AvaotaF1 design form factor as a reference.

A testboard is currently provided for module testing and functional validation, and it is intended for `v821-aiglass`. Smaller PCB designs will continue to be explored so the hardware can fit into a glasses structure.

<p>
  <a href="test_image/testboard.png"><img src="test_image/testboard.png" width="180" alt="testboard preview"></a>
</p>

### Application

Application related content for `v821-aiglass`.

LVGL / Qt demo applications are currently provided for future in-depth development. The demos are mainly used for functional validation at this stage. They are relatively general-purpose, but they have not yet been fully adapted to the hardware.

### SDK/TinaLinux 5.0

SDK patches and board-level configuration content, mainly including:

- `v821-aiglass`
- `v821-aiglass_fastboot`

## How to Use the Patches

Copy the patch files to the corresponding SDK directories. For further board-level development, refer to the Allwinner online documentation:

https://docs.aw-ol.com/docs/soc/v821/
