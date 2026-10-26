# PaperS3 libjpeg-turbo integration

Vendored from the official libjpeg-turbo **3.2.0** release:
https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0

Source archive SHA-256: `980dd81f425082aa6d7c9e47fef27554ce7a9ffc8e2f6e863b97d263c5c50858`.

This local PlatformIO library contains the portable C decompressor, required
headers/templates and upstream 12-bit helper variants referenced internally by
the codec. The reader accepts only 8-bit lossy JPEG input, including sequential,
progressive, grayscale, RGB/YCbCr and CMYK/YCCK images. Encoder tools, TurboJPEG,
SIMD, arithmetic coding and lossless decoding are excluded.

Local changes:

- `jconfig.h`, `jconfigint.h` and `jversion.h` configure the portable build.
- `jmorecfg.h` disables `D_LOSSLESS_SUPPORTED`.
- `precision12/` contains unchanged upstream wrapper translation units.
- `library.json` selects decoder sources; included C templates are not compiled
  separately.
- `jmemreader.c` replaces the system memory backend. Every codec allocation is
  charged to the caller's budget and uses PSRAM on ESP32. There is no backing
  store and no fallback that consumes internal RAM when PSRAM is exhausted.
- `ReaderJpegMemory.h` exposes the same budgeted allocation for the reader's
  scanline and output buffers.

The portable adapter in `../ReaderImages/src/ReaderJpeg.c` handles fatal errors
with a C-only setjmp boundary, returns grayscale output at screen size, yields
through a progress callback, rejects warning-level corruption, limits scan count
and releases allocations on both success and failure. Its codec/image memory
budget is capped at 4 MiB; the device additionally reserves 512 KiB of free PSRAM.

Keep this directory's license files with redistributed source and include the
following attribution in documentation for firmware distributions:

This software is based in part on the work of the Independent JPEG Group.
