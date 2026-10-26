/* PaperS3 memory backend for libjpeg-turbo. No filesystem backing store. */
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jmemsys.h"
#include "ReaderJpegMemory.h"
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

typedef union {
  struct { ReaderJpegBudget* budget; size_t bytes; } allocation;
  long double alignment;
} AllocationHeader;

void* reader_jpeg_alloc(ReaderJpegBudget* budget, size_t bytes) {
  if (!budget || bytes > (size_t)-1 - sizeof(AllocationHeader)) return NULL;
  size_t charge = bytes + sizeof(AllocationHeader);
  if (budget->used > budget->limit || charge > budget->limit - budget->used) return NULL;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  AllocationHeader* header = heap_caps_malloc(charge, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  AllocationHeader* header = malloc(charge);
#endif
  if (!header) return NULL;
  header->allocation.budget = budget;
  header->allocation.bytes = charge;
  budget->used += charge;
  return header + 1;
}
void reader_jpeg_free(void* memory) {
  if (!memory) return;
  AllocationHeader* header = (AllocationHeader*)memory - 1;
  header->allocation.budget->used -= header->allocation.bytes;
  free(header);
}
GLOBAL(void*) jpeg_get_small(j_common_ptr cinfo, size_t bytes) {
  return reader_jpeg_alloc((ReaderJpegBudget*)cinfo->client_data, bytes);
}
GLOBAL(void*) jpeg_get_large(j_common_ptr cinfo, size_t bytes) {
  return reader_jpeg_alloc((ReaderJpegBudget*)cinfo->client_data, bytes);
}
GLOBAL(void) jpeg_free_small(j_common_ptr cinfo, void* memory, size_t bytes) {
  (void)cinfo; (void)bytes; reader_jpeg_free(memory);
}
GLOBAL(void) jpeg_free_large(j_common_ptr cinfo, void* memory, size_t bytes) {
  (void)cinfo; (void)bytes; reader_jpeg_free(memory);
}
GLOBAL(size_t) jpeg_mem_available(j_common_ptr cinfo, size_t minimum, size_t maximum, size_t allocated) {
  ReaderJpegBudget* budget = (ReaderJpegBudget*)cinfo->client_data;
  (void)minimum; (void)allocated;
  size_t remaining = budget->limit > budget->used ? budget->limit - budget->used : 0;
  return remaining < maximum ? remaining : maximum;
}
GLOBAL(void) jpeg_open_backing_store(j_common_ptr cinfo, backing_store_ptr info, long bytes) {
  (void)info; (void)bytes; ERREXIT(cinfo, JERR_NO_BACKING_STORE);
}
GLOBAL(long) jpeg_mem_init(j_common_ptr cinfo) {
  return (long)((ReaderJpegBudget*)cinfo->client_data)->limit;
}
GLOBAL(void) jpeg_mem_term(j_common_ptr cinfo) { (void)cinfo; }
