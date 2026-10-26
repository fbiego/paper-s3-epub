#pragma once
#include <stdint.h>

// Original PNG served to browsers only; device screens use ProjectLogo.h.
extern const uint8_t projectLogoStart[] asm("_binary_papers3_library_png_start");
extern const uint8_t projectLogoEnd[] asm("_binary_papers3_library_png_end");
extern const uint8_t epubPrepareStart[] asm("_binary_web_epub_prepare_js_start");
extern const uint8_t epubPrepareEnd[] asm("_binary_web_epub_prepare_js_end");
extern const uint8_t fflateStart[] asm("_binary_web_vendor_fflate_js_start");
extern const uint8_t fflateEnd[] asm("_binary_web_vendor_fflate_js_end");
