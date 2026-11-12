/** @file pixel_convert.c
 *  @brief See pixel_convert.h
 */
#include "pixel_convert.h"

#include <string.h>

static inline unsigned char clamp8(int v) { return (unsigned char) ((v < 0) ? 0 : (v > 255) ? 255 : v); }

/* BT.601 limited range, 8 bit fixed point */
static inline unsigned char rgbToY(int r, int g, int b) { return clamp8((( 66 * r + 129 * g +  25 * b + 128) >> 8) +  16); }
static inline unsigned char rgbToU(int r, int g, int b) { return clamp8(((-38 * r -  74 * g + 112 * b + 128) >> 8) + 128); }
static inline unsigned char rgbToV(int r, int g, int b) { return clamp8(((112 * r -  94 * g -  18 * b + 128) >> 8) + 128); }

/* Reads pixel x of a line as r,g,b whatever the channel count */
static inline void fetchRGB(const unsigned char *line, unsigned int x, unsigned int channels, int swapRB,
                            int *r, int *g, int *b)
{
    const unsigned char *p = line + (size_t) x * channels;
    if (channels >= 3 && channels <= 4)
    {
        *r = swapRB ? p[2] : p[0];
        *g = p[1];
        *b = swapRB ? p[0] : p[2];
    } else
    {
        *r = *g = *b = p[0];
    }
}

size_t pixelOutputSize(enum PixelOutputFormat fmt, unsigned int width, unsigned int height)
{
    if (width == 0 || height == 0) { return 0; }
    size_t w = width, h = height;
    switch (fmt)
    {
        case PIXOUT_GREY:  return w * h;
        case PIXOUT_RGB24:
        case PIXOUT_BGR24: return w * h * 3;
        case PIXOUT_YUYV:  return ((w + 1) & ~(size_t) 1) * 2 * h;
        case PIXOUT_YU12:  return w * h + 2 * (((w + 1) / 2) * ((h + 1) / 2));
    }
    return 0;
}

unsigned int pixelOutputBytesPerLine(enum PixelOutputFormat fmt, unsigned int width)
{
    switch (fmt)
    {
        case PIXOUT_GREY:  return width;
        case PIXOUT_RGB24:
        case PIXOUT_BGR24: return width * 3;
        case PIXOUT_YUYV:  return ((width + 1) & ~1u) * 2;
        case PIXOUT_YU12:  return width;
    }
    return 0;
}

int convertPixels(unsigned char *dst, enum PixelOutputFormat fmt,
                  const unsigned char *src, unsigned int width, unsigned int height,
                  unsigned int channels, int swapRB)
{
    if (dst == NULL || src == NULL || width == 0 || height == 0 || channels == 0) { return 0; }
    const size_t srcStride = (size_t) width * channels;
    int r, g, b;

    switch (fmt)
    {
    case PIXOUT_GREY:
        for (unsigned int y = 0; y < height; y++)
        {
            const unsigned char *line = src + y * srcStride;
            unsigned char *out = dst + (size_t) y * width;
            if (channels == 1) { memcpy(out, line, width); continue; }
            for (unsigned int x = 0; x < width; x++)
            {
                fetchRGB(line, x, channels, swapRB, &r, &g, &b);
                out[x] = (unsigned char) ((77 * r + 150 * g + 29 * b + 128) >> 8); /* full range luma */
            }
        }
        return 1;

    case PIXOUT_RGB24:
    case PIXOUT_BGR24:
    {
        /* Output BGR when exactly one of (source is BGR, output is BGR) holds */
        int outSwap = (fmt == PIXOUT_BGR24) ^ (swapRB != 0);
        if (channels == 3 && !outSwap) { memcpy(dst, src, (size_t) width * height * 3); return 1; }
        unsigned char *out = dst;
        for (unsigned int y = 0; y < height; y++)
        {
            const unsigned char *line = src + y * srcStride;
            for (unsigned int x = 0; x < width; x++)
            {
                fetchRGB(line, x, channels, 0, &r, &g, &b); /* raw channel order */
                if (channels < 3) { out[0] = out[1] = out[2] = (unsigned char) r; }
                else if (outSwap) { out[0] = (unsigned char) b; out[1] = (unsigned char) g; out[2] = (unsigned char) r; }
                else              { out[0] = (unsigned char) r; out[1] = (unsigned char) g; out[2] = (unsigned char) b; }
                out += 3;
            }
        }
        return 1;
    }

    case PIXOUT_YUYV:
    {
        const size_t dstStride = pixelOutputBytesPerLine(PIXOUT_YUYV, width);
        for (unsigned int y = 0; y < height; y++)
        {
            const unsigned char *line = src + y * srcStride;
            unsigned char *out = dst + (size_t) y * dstStride;
            for (unsigned int x = 0; x < width; x += 2)
            {
                int r0, g0, b0, r1, g1, b1;
                fetchRGB(line, x, channels, swapRB, &r0, &g0, &b0);
                if (x + 1 < width) { fetchRGB(line, x + 1, channels, swapRB, &r1, &g1, &b1); }
                else               { r1 = r0; g1 = g0; b1 = b0; } /* odd width: repeat last pixel */
                int ra = (r0 + r1 + 1) >> 1, ga = (g0 + g1 + 1) >> 1, ba = (b0 + b1 + 1) >> 1;
                out[0] = rgbToY(r0, g0, b0);
                out[1] = rgbToU(ra, ga, ba);
                out[2] = rgbToY(r1, g1, b1);
                out[3] = rgbToV(ra, ga, ba);
                out += 4;
            }
        }
        return 1;
    }

    case PIXOUT_YU12:
    {
        const unsigned int cw = (width + 1) / 2, ch = (height + 1) / 2;
        unsigned char *Y = dst;
        unsigned char *U = dst + (size_t) width * height;
        unsigned char *V = U + (size_t) cw * ch;
        for (unsigned int y = 0; y < height; y++)
        {
            const unsigned char *line = src + y * srcStride;
            for (unsigned int x = 0; x < width; x++)
            {
                fetchRGB(line, x, channels, swapRB, &r, &g, &b);
                Y[(size_t) y * width + x] = rgbToY(r, g, b);
            }
        }
        for (unsigned int cy = 0; cy < ch; cy++)
        {
            for (unsigned int cx = 0; cx < cw; cx++)
            {
                int rs = 0, gs = 0, bs = 0, n = 0;
                for (unsigned int dy = 0; dy < 2; dy++)
                {
                    unsigned int y = cy * 2 + dy;
                    if (y >= height) { continue; }
                    for (unsigned int dx = 0; dx < 2; dx++)
                    {
                        unsigned int x = cx * 2 + dx;
                        if (x >= width) { continue; }
                        fetchRGB(src + y * srcStride, x, channels, swapRB, &r, &g, &b);
                        rs += r; gs += g; bs += b; n++;
                    }
                }
                rs = (rs + n / 2) / n; gs = (gs + n / 2) / n; bs = (bs + n / 2) / n;
                U[(size_t) cy * cw + cx] = rgbToU(rs, gs, bs);
                V[(size_t) cy * cw + cx] = rgbToV(rs, gs, bs);
            }
        }
        return 1;
    }
    }
    return 0;
}
