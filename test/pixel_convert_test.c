/* Unit test for src/c/pixel_convert.c: known colours -> expected BT.601 values.
 * Build/run: gcc -Wall -O2 -I src/c test/pixel_convert_test.c src/c/pixel_convert.c -o test/bin/pixel_convert_test && test/bin/pixel_convert_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pixel_convert.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)
static int near(int a, int b) { return abs(a - b) <= 1; }

int main(void)
{
    /* 2x1 RGB: white, black -> YUYV */
    unsigned char wb[6] = { 255,255,255, 0,0,0 };
    unsigned char out[64];
    CHECK(pixelOutputSize(PIXOUT_YUYV, 2, 1) == 4, "yuyv size");
    CHECK(convertPixels(out, PIXOUT_YUYV, wb, 2, 1, 3, 0), "yuyv convert");
    CHECK(near(out[0], 235) && near(out[2], 16) && near(out[1], 128) && near(out[3], 128),
          "white/black yuyv = %u %u %u %u", out[0], out[1], out[2], out[3]);

    /* pure red pair: Y~82 U~90 V~240 */
    unsigned char red[6] = { 255,0,0, 255,0,0 };
    convertPixels(out, PIXOUT_YUYV, red, 2, 1, 3, 0);
    CHECK(near(out[0], 82) && near(out[1], 90) && near(out[3], 240), "red yuyv = %u %u %u %u", out[0], out[1], out[2], out[3]);

    /* same pixels given as BGR with swapRB must match */
    unsigned char redBGR[6] = { 0,0,255, 0,0,255 }, out2[4];
    convertPixels(out2, PIXOUT_YUYV, redBGR, 2, 1, 3, 1);
    CHECK(memcmp(out, out2, 4) == 0, "swapRB yuyv mismatch");

    /* odd width: 3 px -> 2 macropixels */
    CHECK(pixelOutputSize(PIXOUT_YUYV, 3, 2) == 16, "odd yuyv size %zu", pixelOutputSize(PIXOUT_YUYV, 3, 2));
    unsigned char odd[18]; memset(odd, 200, sizeof(odd));
    CHECK(convertPixels(out, PIXOUT_YUYV, odd, 3, 2, 3, 0), "odd convert");

    /* YU12 of a 2x2 red image: 4 Y + 1 U + 1 V */
    unsigned char red4[12] = { 255,0,0, 255,0,0, 255,0,0, 255,0,0 };
    CHECK(pixelOutputSize(PIXOUT_YU12, 2, 2) == 6, "yu12 size");
    convertPixels(out, PIXOUT_YU12, red4, 2, 2, 3, 0);
    CHECK(near(out[0], 82) && near(out[3], 82) && near(out[4], 90) && near(out[5], 240), "red yu12 = %u .. %u %u", out[0], out[4], out[5]);
    CHECK(pixelOutputSize(PIXOUT_YU12, 3, 3) == 9 + 2 * 4, "odd yu12 size");

    /* RGBA -> RGB24 drops alpha; RGB -> BGR24 swaps */
    unsigned char rgba[4] = { 1,2,3,4 };
    convertPixels(out, PIXOUT_RGB24, rgba, 1, 1, 4, 0);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3, "rgba->rgb24");
    convertPixels(out, PIXOUT_BGR24, rgba, 1, 1, 4, 0);
    CHECK(out[0] == 3 && out[1] == 2 && out[2] == 1, "rgba->bgr24");
    convertPixels(out, PIXOUT_RGB24, rgba, 1, 1, 4, 1); /* BGRA source -> RGB */
    CHECK(out[0] == 3 && out[2] == 1, "bgra->rgb24");

    /* grey passthrough and grey -> RGB */
    unsigned char g[2] = { 7, 9 };
    convertPixels(out, PIXOUT_GREY, g, 2, 1, 1, 0);
    CHECK(out[0] == 7 && out[1] == 9, "grey passthrough");
    convertPixels(out, PIXOUT_RGB24, g, 2, 1, 1, 0);
    CHECK(out[3] == 9 && out[4] == 9 && out[5] == 9, "grey->rgb");
    convertPixels(out, PIXOUT_GREY, wb, 2, 1, 3, 0);
    CHECK(out[0] == 255 && out[1] == 0, "rgb->grey %u %u", out[0], out[1]);

    CHECK(!convertPixels(NULL, PIXOUT_GREY, g, 1, 1, 1, 0), "NULL dst accepted");

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("pixel_convert: all checks passed\n");
    return 0;
}
