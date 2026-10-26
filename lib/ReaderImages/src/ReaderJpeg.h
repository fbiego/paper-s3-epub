#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t* pixels; /* Row-major 8-bit grayscale. */
  unsigned width, height;
  void* owner;
} ReaderJpegImage;
typedef void (*ReaderJpegYield)(void);
/* Decode into an exact target size. All codec/image buffers share a budget. */
int readerDecodeJpeg(const uint8_t* data, size_t size, unsigned width, unsigned height,
                     size_t memoryLimit, ReaderJpegYield yield,
                     ReaderJpegImage* image, char* error, size_t errorSize);
void readerFreeJpeg(ReaderJpegImage* image);

#ifdef __cplusplus
}
#endif
