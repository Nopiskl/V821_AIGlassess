# V821 AI Glass

Language: English | [中文](README_CN.md)

This repository provides hardware designs, applications, and SDK patches for the `v821-aiglass` V821 AI glasses configuration.

## Project Overview

`v821-aiglass` is a board-level configuration for AI glasses with an optical waveguide display.

The system design covers:

- LLM integration and image recognition through a companion mobile app.
- `640 × 480` image and text output on a MicroOLED display.
- One-button photo capture and video recording, dynamic image stabilization, and high-resolution imaging.
- JL7016 standby Bluetooth management and music playback with DSP audio processing; V821 UI rendering and management in continuously powered UI mode.
- Application downloads, file uploads, and device management between the phone and glasses.
- An LVGL glasses interface and a phone companion.

These features describe the system design. Implementation and validation scope are documented per module.
The phone companion in this repository targets uni-app H5; its store provides local tools and templates,
without glasses application installation.

Hardware validation covers the testboard and modules. Glasses hardware design files are available in [`Hardware/V821_ai_glasses`](Hardware/V821_ai_glasses/).

## Repository Layout

### Hardware

#### V821_ai_glasses

[`Hardware/V821_ai_glasses`](Hardware/V821_ai_glasses/) contains the optical waveguide glasses hardware project:

- [EDA project](Hardware/V821_ai_glasses/ProPrj_光波导AR眼镜_2026-10-26.epro2): schematic and PCB design files in `.epro2` format.
- [Schematic PDFs](Hardware/V821_ai_glasses/PDF_光波导AR眼镜.zip): a ZIP archive of schematic documents.
- [Images](Hardware/V821_ai_glasses/Image/): hardware photos and PCB, FPC, and 3D design previews.

**Hardware Gallery**

| Glasses | PCB Design | Hardware Components |
| :---: | :---: | :---: |
| <a href="Hardware/V821_ai_glasses/Image/ai_glass.png"><img src="Hardware/V821_ai_glasses/Image/ai_glass.png" width="240" alt="Optical waveguide glasses"></a> | <a href="Hardware/V821_ai_glasses/Image/PCB_design.png"><img src="Hardware/V821_ai_glasses/Image/PCB_design.png" width="240" alt="Glasses PCB design"></a> | <a href="Hardware/V821_ai_glasses/Image/item_overview.png"><img src="Hardware/V821_ai_glasses/Image/item_overview.png" width="240" alt="Glasses hardware components"></a> |

| Frame and Waveguide Module | Debug Log | Waveguide Display |
| :---: | :---: | :---: |
| <a href="Hardware/AI_glass_avaota_suit/Image/1.png"><img src="Hardware/AI_glass_avaota_suit/Image/1.png" width="240" alt="Frame and optical waveguide module"></a> | <a href="Hardware/AI_glass_avaota_suit/Image/debug.png"><img src="Hardware/AI_glass_avaota_suit/Image/debug.png" width="240" alt="Debug log display"></a> | <a href="Hardware/AI_glass_avaota_suit/Image/2.png"><img src="Hardware/AI_glass_avaota_suit/Image/2.png" width="240" alt="Optical waveguide display output"></a> |

#### AI_glass_avaota_suit

[`Hardware/AI_glass_avaota_suit`](Hardware/AI_glass_avaota_suit/) contains the AvaotaF1-based testboard for `v821-aiglass` module testing and functional validation. Files include an EDA project, schematic PDF, Gerber archive, STEP model, and test images.

<p align="center">
  <a href="Hardware/AI_glass_avaota_suit/Image/testboard-horizontal.svg"><img src="Hardware/AI_glass_avaota_suit/Image/testboard-horizontal.svg" width="350" alt="Testboard horizontal view"></a>
</p>

### Application

[`Application/aiglass-apps`](Application/aiglass-apps/README.md) provides the controller-driven mode in which JL controls the device and V821 does not display a UI. It contains AGLink support and seven applications: normal, photo, audio, download, OTA, video, and livestream.

[`Application/lvgl-home`](Application/lvgl-home/README.md) provides a green outline glasses UI
for LVGL 8.3/8.4, with a default landscape resolution of `640 × 480`.
It includes AI chat, translation, phone communication, recording, camera, files, notes, and settings,
using Linux interfaces and configurable HTTP services. The module plugs into an existing LVGL
application. In the continuously powered UI mode, photo, video, and audio capture call V821 MPP
directly through [`Application/mpp-media`](Application/mpp-media/README.md), with configurable local
audio devices and channels. This path does not require AGLink. See [runtime modes](Application/RUNTIME_MODES.md)
for ownership, startup, and protocol boundaries. The host application supplies LVGL, and the module has a
separate build entry from the seven controller-driven applications. Development validation uses native
Linux host builds and integration tests; operation on the V821 target device is unverified.

[`Application/mobile-app`](Application/mobile-app/README.md) is a separate uni-app / Vue 3 phone companion,
targeting H5. It provides first-use pairing and Home, Features, Store, and Me tabs.
The phone pairs using the six-digit code shown by LVGL, then exchanges actual files, notes, and messages
with the running process through an HTTP bridge. The phone displays an offline state when the service is unavailable.

[`Application/host-debug`](Application/host-debug/README.md) provides Linux host integration with a shared preview
port for the actual LVGL display and phone UI. Builds, dependencies, logs, and test data stay in the workspace.
Host mode excludes camera, microphone, backlight, real network provisioning, and JL access; AI services require
separate configuration. See the [host simulator architecture (Chinese)](Application/host-debug/ARCHITECTURE.md)
for source reuse, process and thread design, interfaces, pairing, storage, and testing boundaries.

The `Application/Makefile` provides a common build entry for V821 applications. A complete Tina V821 BSP, cross toolchain, and built dependencies are required; `SDK/TinaLinux 5.0` in this repository contains patches only. The phone project uses separate npm commands.

```bash
make -C Application list
make -C Application check AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
make -C Application all AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
make -C Application package AIGLASS_BSP_ROOT=/absolute/path/to/tina-v821-release
```

See the [application guide](Application/aiglass-apps/README.md) for dependency preparation, rootfs installation, and BSP source integration. Build and package outputs stay under `Application/aiglass-apps/build`, `output`, and `dist` and are ignored by Git.

### SDK/TinaLinux 5.0

Contains SDK patches and board-level configuration for `v821-aiglass`.

## How to Use the Patches

Copy the patch files to the corresponding SDK directories. Refer to the [Allwinner V821 documentation](https://developers.allwinnertech.com/docs) for board-level development.

## Acknowledgments

The hardware design in `Hardware/V821_ai_glasses` draws on [sgsax's PCB project on OSHWHub](https://oshwhub.com/sgsax/project_tiqnqiyt). We thank the original author for sharing the design files.
