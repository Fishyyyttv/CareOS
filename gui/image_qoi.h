/* gui/image_qoi.h — QOI decoder (qoiformat.org spec) */
#ifndef IMAGE_QOI_H
#define IMAGE_QOI_H
#ifndef HOST_TEST
#include "kernel.h"   /* provides u8/u32; under HOST_TEST the .c typedefs them first */
#endif
/* Decode QOI bytes into a u32 pixel buffer (0x00RRGGBB per pixel, w*h entries).
 * out_cap is the buffer size in BYTES. Returns 0 on success, -1 on error. */
int qoi_decode(const u8 *in, u32 in_len, u32 *out_px, u32 out_cap,
               u32 *out_w, u32 *out_h);
#endif
