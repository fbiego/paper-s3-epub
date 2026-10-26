#include "ReaderJpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <jerror.h>
#include <ReaderJpegMemory.h>

typedef struct {
  struct jpeg_error_mgr base;
  jmp_buf jump;
  char message[JMSG_LENGTH_MAX];
} ReaderError;
typedef struct {
  ReaderJpegBudget budget;
  struct jpeg_decompress_struct jpeg;
  ReaderError error;
  struct jpeg_progress_mgr progress;
  ReaderJpegYield yield;
  uint8_t* row;
  uint8_t* pixels;
} DecodeContext;

static void fatal(j_common_ptr jpeg) {
  ReaderError* error = (ReaderError*)jpeg->err;
  if (jpeg->err->msg_code == JERR_OUT_OF_MEMORY || jpeg->err->msg_code == JERR_NO_BACKING_STORE)
    snprintf(error->message, sizeof(error->message), "JPEG exceeds available image memory");
  else (*jpeg->err->format_message)(jpeg, error->message);
  longjmp(error->jump, 1);
}
static void quiet(j_common_ptr jpeg) { (void)jpeg; }
static void emit(j_common_ptr jpeg, int level) {
  /* A truncated/corrupt image is a failure, rather than a partial success. */
  if (level < 0) fatal(jpeg);
}
static void progress(j_common_ptr jpeg) {
  DecodeContext* context = (DecodeContext*)jpeg->client_data;
  if (((j_decompress_ptr)jpeg)->input_scan_number > 128) {
    snprintf(context->error.message, sizeof(context->error.message), "JPEG exceeds 128 scan limit");
    longjmp(context->error.jump, 1);
  }
  if (context->yield) context->yield();
}
static void release(DecodeContext* context) {
  jpeg_destroy_decompress(&context->jpeg);
  reader_jpeg_free(context->row);
  reader_jpeg_free(context->pixels);
  free(context);
}
void readerFreeJpeg(ReaderJpegImage* image) {
  if (!image) return;
  if (image->owner) release((DecodeContext*)image->owner);
  memset(image, 0, sizeof(*image));
}
int readerDecodeJpeg(const uint8_t* data, size_t size, unsigned width, unsigned height,
                     size_t memoryLimit, ReaderJpegYield yield,
                     ReaderJpegImage* image, char* error, size_t errorSize) {
  if (error && errorSize) error[0] = 0;
  if (!image) return 0;
  memset(image, 0, sizeof(*image));
  if (!data || !size || size > 2u*1024*1024 || !width || !height ||
      width > 4096 || height > 4096 || (uint64_t)width*height > 4u*1024*1024) {
    if (error && errorSize) snprintf(error,errorSize,"Invalid JPEG input or output dimensions");
    return 0;
  }
  DecodeContext* context = calloc(1,sizeof(*context));
  if (!context) {
    if (error && errorSize) snprintf(error,errorSize,"Not enough memory for JPEG decoder");
    return 0;
  }
  context->budget.limit = memoryLimit < 4u*1024*1024 ? memoryLimit : 4u*1024*1024;
  context->yield = yield;
  context->jpeg.client_data = context; /* budget is the first member */
  context->jpeg.err = jpeg_std_error(&context->error.base);
  context->error.base.error_exit = fatal;
  context->error.base.output_message = quiet;
  context->error.base.emit_message = emit;
  if (setjmp(context->error.jump)) {
    if (error && errorSize) snprintf(error,errorSize,"%s",context->error.message);
    release(context);
    return 0;
  }
  jpeg_create_decompress(&context->jpeg);
  context->progress.progress_monitor = progress;
  context->jpeg.progress = &context->progress;
  jpeg_mem_src(&context->jpeg,data,(unsigned long)size);
  jpeg_read_header(&context->jpeg,TRUE);
  struct jpeg_decompress_struct* jpeg = &context->jpeg;
  if (jpeg->data_precision != 8 || !jpeg->image_width || !jpeg->image_height ||
      jpeg->image_width > 4096 || jpeg->image_height > 4096 ||
      (uint64_t)jpeg->image_width*jpeg->image_height > 4u*1024*1024 ||
      (jpeg->num_components != 1 && jpeg->num_components != 3 && jpeg->num_components != 4))
    ERREXIT(jpeg,JERR_BAD_PRECISION);
  const int cmyk = jpeg->jpeg_color_space == JCS_CMYK || jpeg->jpeg_color_space == JCS_YCCK;
  jpeg->out_color_space = cmyk ? JCS_CMYK : JCS_GRAYSCALE;
  jpeg->dct_method = JDCT_ISLOW;
  jpeg->do_fancy_upsampling = FALSE;
  /* Select the smallest IDCT output that still covers the target screen. */
  for (unsigned denominator=8; denominator; denominator/=2) {
    jpeg->scale_num = 1; jpeg->scale_denom = denominator;
    jpeg_calc_output_dimensions(jpeg);
    if (jpeg->output_width >= width && jpeg->output_height >= height) break;
  }
  context->pixels = reader_jpeg_alloc(&context->budget,(size_t)width*height);
  if (!context->pixels) ERREXIT1(jpeg,JERR_OUT_OF_MEMORY,0);
  jpeg_start_decompress(jpeg);
  context->row = reader_jpeg_alloc(&context->budget,(size_t)jpeg->output_width*jpeg->output_components);
  if (!context->row) ERREXIT1(jpeg,JERR_OUT_OF_MEMORY,0);
  unsigned targetY = 0;
  while (jpeg->output_scanline < jpeg->output_height) {
    const unsigned sourceY = jpeg->output_scanline;
    JSAMPROW row = context->row;
    if (jpeg_read_scanlines(jpeg,&row,1) != 1) ERREXIT(jpeg,JERR_INPUT_EOF);
    while (targetY < height && (uint64_t)targetY*jpeg->output_height/height == sourceY) {
      for (unsigned x=0;x<width;++x) {
        unsigned sourceX = (uint64_t)x*jpeg->output_width/width;
        if (!cmyk) context->pixels[(size_t)targetY*width+x] = row[sourceX];
        else {
          const uint8_t* pixel = row + sourceX*4;
          int r,g,b;
          if (jpeg->saw_Adobe_marker) {
            r=pixel[0]*pixel[3]/255; g=pixel[1]*pixel[3]/255; b=pixel[2]*pixel[3]/255;
          } else {
            r=(255-pixel[0])*(255-pixel[3])/255;
            g=(255-pixel[1])*(255-pixel[3])/255;
            b=(255-pixel[2])*(255-pixel[3])/255;
          }
          context->pixels[(size_t)targetY*width+x]=(77*r+150*g+29*b)>>8;
        }
      }
      ++targetY;
    }
    if (yield) yield();
  }
  jpeg_finish_decompress(jpeg);
  jpeg_destroy_decompress(jpeg);
  reader_jpeg_free(context->row); context->row=NULL;
  image->pixels=context->pixels; image->width=width; image->height=height; image->owner=context;
  return 1;
}
