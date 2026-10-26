#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { size_t used, limit; } ReaderJpegBudget;
void* reader_jpeg_alloc(ReaderJpegBudget* budget, size_t bytes);
void reader_jpeg_free(void* memory);

#ifdef __cplusplus
}
#endif
