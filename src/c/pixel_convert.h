/** @file pixel_convert.h
 *  @brief Small dependency-free pixel format converters (used by v4l2sink).
 *
 *  Input is packed, interleaved 8-bit pixels with 1, 3 (RGB) or 4 (RGBA) channels
 *  (or BGR/BGRA when swapRB is set). YUV output uses BT.601 limited range integer math.
 */
#ifndef PIXEL_CONVERT_H_INCLUDED
#define PIXEL_CONVERT_H_INCLUDED

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Output formats understood by the converters. */
enum PixelOutputFormat
{
    PIXOUT_GREY = 0, ///< 1 byte per pixel (luma)
    PIXOUT_RGB24,    ///< 3 bytes per pixel, R G B
    PIXOUT_BGR24,    ///< 3 bytes per pixel, B G R
    PIXOUT_YUYV,     ///< 4:2:2 packed, Y0 U Y1 V (width rounded up to even)
    PIXOUT_YU12      ///< 4:2:0 planar, Y plane, then U, then V (I420)
};

/** @return Bytes needed for one output image, 0 for an unknown format or empty size. */
size_t pixelOutputSize(enum PixelOutputFormat fmt, unsigned int width, unsigned int height);

/** @return Bytes per output line (the V4L2 bytesperline; for YU12 the luma line). */
unsigned int pixelOutputBytesPerLine(enum PixelOutputFormat fmt, unsigned int width);

/**
 * @brief Converts one image.
 * @param dst pixelOutputSize(fmt,width,height) bytes.
 * @param src width*height*channels bytes.
 * @param channels 1, 3 or 4 (any other count >= 1 is read as grey: the first channel).
 * @param swapRB Source is BGR(A) instead of RGB(A).
 * @return 1 on success, 0 on bad arguments.
 */
int convertPixels(unsigned char *dst, enum PixelOutputFormat fmt,
                  const unsigned char *src, unsigned int width, unsigned int height,
                  unsigned int channels, int swapRB);

#ifdef __cplusplus
}
#endif

#endif
