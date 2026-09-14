<!--
© OpenShot Studios, LLC

SPDX-License-Identifier: LGPL-3.0-or-later
-->

## Hardware Acceleration

Hardware acceleration in libopenshot allows FFmpeg to use platform-specific GPU
APIs for video decode and encode when available. In practice, this means some of
the work that would otherwise be done by the CPU can be offloaded to the GPU or
to dedicated media blocks on the GPU.

This document focuses on what hardware acceleration in libopenshot does today,
how it fits into the current processing pipeline, and what users and developers
should expect from it.

## Backend Overview

The following table summarizes the historically supported hardware-acceleration
backends in libopenshot. Actual behavior still depends on FFmpeg build options,
driver availability, operating system support, and the runtime environment.

|                    | Linux Decode | Linux Encode | macOS Decode | macOS Encode | Windows Decode | Windows Encode | Notes |
|--------------------|:------------:|:------------:|:------------:|:------------:|:--------------:|:--------------:|-------|
| VA-API             |      ✔️      |      ✔️      |      -       |      -       |       -        |       -        | Linux only |
| VDPAU              |   ✔️ <sup>1</sup> | ✅ <sup>2</sup> |      -       |      -       |       -        |       -        | Linux only |
| CUDA (NVDEC/NVENC) |   ❌ <sup>3</sup> |      ✔️      |      -       |      -       |       -        |      ✔️        | Backend availability depends on the FFmpeg build |
| VideoToolbox       |      -       |      -       |      ✔️       | ❌ <sup>4</sup> |       -        |       -        | macOS only |
| DXVA2              |      -       |      -       |      -       |      -       | ❌ <sup>3</sup> |       -        | Windows only |
| D3D11VA            |      -       |      -       |      -       |      -       | ❌ <sup>3</sup> |       -        | Windows only |
| QSV                |   ❌ <sup>3</sup> |      ❌      |      ❌       |      ❌       |       ❌        |       ❌        | Backend availability depends on the FFmpeg build |

#### Notes

1. VDPAU historically needed a card number one higher than expected.
2. VDPAU is decode-only.
3. Historically associated with failed transfers, corrupt frames, or unusable output on some setups.
4. Historically unstable.

This table should be read as a support map, not a guarantee that every backend
is fully validated on every current OS/driver combination.

## Why Hardware Acceleration Exists

Hardware acceleration is useful for two main reasons:

* It can reduce CPU load during decode and encode.
* It can improve throughput for some media, especially on systems with strong
  hardware video support.

However, hardware acceleration is not automatically faster for every file or on
every system. The real result depends on codec support, driver quality, stream
format, pixel format, resolution, frame rate, and how much CPU-side work still
needs to happen after decode.

## What libopenshot Uses Hardware Acceleration For

Today, hardware acceleration in libopenshot is primarily used for:

* video decode
* video encode

It is not currently used to keep the entire edit/render pipeline on the GPU.
Decoded frames usually still need to be copied back into system memory for
colorspace conversion, scaling, caching, effect processing, compositing, and
timeline rendering.

That detail is important because it explains why hardware decode does not always
produce a speedup.

## Decode Flow in libopenshot

The current decode flow looks roughly like this:

1. A hardware decoder may be requested through `Settings::HARDWARE_DECODER`.
2. FFmpeg opens the requested hardware decode path if the backend and driver
   support it.
3. The decoder produces a frame, either:
   * directly as a software-readable frame, or
   * as a hardware frame that must be transferred to system memory.
4. libopenshot converts that frame into the CPU-side image representation used
   by the rest of the pipeline.

If hardware decode fails during startup decode or frame transfer, libopenshot
falls back to software decode for that reader instead of returning corrupt,
green, or black frames.

## Fallback Behavior

Hardware decode is best-effort, not all-or-nothing.

If a hardware decoder is requested and one of the following happens early in the
decode path:

* repeated startup decode failures
* failed hardware-frame transfer
* invalid transferred frame data
* failed software conversion of a transferred frame

libopenshot reopens that reader in software decode mode and continues decoding.

This behavior is intentionally conservative. The priority is correctness and
stability:

* valid frames are better than corrupt frames
* software fallback is better than black or green output
* a file that cannot be decoded by one hardware backend should still decode if
  CPU decoding can handle it

For diagnostics and UI checks, this means there is a difference between:

* decode succeeded
* hardware decode actually produced frames
* hardware decode failed and software fallback was used

`FFmpegReader::HardwareDecodeSuccessful()` exists to expose that distinction.

## Performance Expectations

Hardware decode is not guaranteed to be faster than software decode.

In libopenshot's current pipeline, decoded frames are brought back to
system memory immediately after decode. That introduces costs that can erase or
outweigh the raw decode benefit:

* hardware device setup overhead
* frame transfer overhead between GPU and CPU memory
* colorspace conversion and scaling after decode
* caching and image wrapping in CPU memory
* container/seek behavior and stream structure

Because of that, hardware decode performance is workload-dependent.

General guidance:

* some files benefit from hardware decode
* some files are effectively neutral
* some files are slower with hardware decode
* files with similar codec and resolution can still behave differently

Hardware acceleration should be treated as a capability that may help, not as a
guarantee of better performance.

## Why Some Files Fail on Hardware Decode

A file can fail on hardware decode for several reasons:

* unsupported codec profile
* unsupported chroma format or pixel format
* unsupported bit depth or color range
* driver/backend limitations
* FFmpeg/backend integration quirks on a specific platform

For example, consumer hardware decode paths often handle H.264 4:2:0 very well,
but may not support H.264 4:2:2 decode reliably. In those cases, software decode
may work perfectly while hardware decode fails.

## Supported FFmpeg Versions

* Hardware acceleration support requires FFmpeg versions new enough to expose the
  relevant hardware APIs to libopenshot.
* In practice, decode support in libopenshot relies on FFmpeg's modern send/receive
  decode API and hardware-frame APIs.
* Actual backend availability depends on how FFmpeg was compiled on the target system.

Older historical note:

* Some Ubuntu/FFmpeg/NVIDIA combinations behaved differently between FFmpeg 3.x
  and FFmpeg 4.x, especially for NVIDIA decode support.

Because backend support has changed over time, always validate against the
actual FFmpeg build and driver stack in use.

## OpenShot Settings

The following settings are used by libopenshot to enable, disable, and control
hardware acceleration features.

```cpp
/// Use video codec for faster video decoding (if supported)
int HARDWARE_DECODER = 0;

/* 0 - No acceleration
   1 - Linux VA-API
   2 - NVIDIA NVDEC
   3 - Windows D3D9
   4 - Windows D3D11
   5 - macOS / VideoToolbox
   6 - Linux VDPAU
   7 - Intel QSV */

/// Number of threads of OpenMP
int OMP_THREADS = 12;

/// Number of threads that FFmpeg uses
int FF_THREADS = 8;

/// Maximum rows that hardware decode can handle
int DE_LIMIT_HEIGHT_MAX = 1100;

/// Maximum columns that hardware decode can handle
int DE_LIMIT_WIDTH_MAX = 1950;

/// Which GPU to use to decode (0 is the first, Linux only)
int HW_DE_DEVICE_SET = 0;

/// Which GPU to use to encode (0 is the first, Linux only)
int HW_EN_DEVICE_SET = 0;
```

## Platform Notes

### Linux / VA-API

VA-API is one of the primary Linux hardware-decode paths used by libopenshot.
On supported Intel and AMD systems it can work well, but not every file format,
codec profile, or pixel format is supported by every driver.

### Linux / VDPAU

VDPAU support exists historically, but behavior can vary with driver and FFmpeg
stack. Treat it as backend-dependent rather than universally reliable.

### NVIDIA

NVIDIA hardware encode support has historically been more reliable than decode
support in libopenshot, depending on FFmpeg build and driver stack. Validate the
actual runtime environment before assuming support.

### macOS / VideoToolbox

VideoToolbox support exists, but stability and feature coverage should be tested
carefully on the target FFmpeg/macOS version.

### Windows / DXVA2 / D3D11VA

Windows decode backends are highly dependent on FFmpeg build options and device
support. They should be treated as runtime-validated features, not assumptions.

## Multiple Graphics Cards

If the computer has multiple graphics cards installed, libopenshot can choose
which device should be used for decode and encode. This is currently practical
mainly on Linux, where FFmpeg expects device names such as `/dev/dri/renderD128`.

Contributions are welcome for improving cross-platform device enumeration and
selection.

## Testing and Validation

When validating hardware decode, check both:

* correctness of the decoded output
* whether hardware decode actually succeeded

A frame that looks correct is not enough to prove that hardware acceleration
worked, because software fallback may have rescued the decode.

Recommended validation:

* compare output against a software-decode baseline
* track whether hardware decode actually produced frames
* test both a known-good hardware sample and a known-failing fallback sample

## Future Improvements

The biggest architectural limitation today is that decoded frames are generally
copied back to CPU memory for the rest of the pipeline.

Longer-term improvements could include:

* better hardware capability probing
* better device enumeration for users
* broader backend validation across platforms
* keeping more of the pipeline on GPU memory
* GPU-native effects/compositing paths

Avoiding repeated GPU-to-CPU and CPU-to-GPU copies would make hardware
acceleration much more effective for end-to-end editing and export workflows.

## Help Improve This Document

Hardware acceleration support changes with FFmpeg, drivers, operating systems,
and GPU generations. If you find incorrect information or validate a backend on
a newer stack, please update this document.

## Credit

A big thanks to Peter M (https://github.com/eisneinechse) for all his work on
integrating hardware acceleration into libopenshot. The community thanks you for
this major contribution.
