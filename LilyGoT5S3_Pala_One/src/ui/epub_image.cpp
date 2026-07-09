#include "src/ui/epub_image.h"

#include "src/config.h"
#include "src/hal/display.h"          // u8g2
#include "src/hal/epd_backend.h"      // g_canvas, epd_draw_pixel
#include "src/hal/orientation.h"      // Orientation::isPortrait
#include "src/state.h"                // FS (#define FS SD)
#include "src/storage/epub_import.h"  // EPUB_KEEP_IMAGES
#include "src/ui/font.h"              // Font::useBody / bodyLayout

// "Is the page currently being rendered an image page?" Set by
// drawEpubImagePage(), cleared per page by epubResetPageImageFlag(). The
// reader reads it to drop the status bar on image pages.
static bool s_pageIsImage = false;

void epubResetPageImageFlag() { s_pageIsImage = false; }
bool epubLastPageWasImage()   { return s_pageIsImage; }

// Placeholder for a page we can't render as an image — keeps the text-only
// path intact. Shared by the disabled build and every decode-failure path.
static void drawPlaceholder() {
  Font::useBody();
  const LayoutMetrics& m = Font::bodyLayout();
  u8g2.setCursor(MARGIN_X, TOP_PAD + m.ascent);
  u8g2.print("[image]");
}

#include <JPEGDEC.h>
#include <esp_heap_caps.h>

// Ordered 8x8 Bayer matrix (values 0..63). Ordered dithering is stateless and
// block-safe — each pixel's threshold depends only on its (x,y), so it works
// with JPEGDEC's block callback without carrying error across blocks (which
// Floyd-Steinberg would require). The decoded greyscale is mapped to pure
// black/white so it survives the 1-bit commit path unchanged.
static const uint8_t BAYER8[64] = {
   0, 32,  8, 40,  2, 34, 10, 42,
  48, 16, 56, 24, 50, 18, 58, 26,
  12, 44,  4, 36, 14, 46,  6, 38,
  60, 28, 52, 20, 62, 30, 54, 22,
   3, 35, 11, 43,  1, 33,  9, 41,
  51, 19, 59, 27, 49, 17, 57, 25,
  15, 47,  7, 39, 13, 45,  5, 37,
  63, 31, 55, 23, 61, 29, 53, 21
};

static JPEGDEC g_jpeg;

// Write one pixel in screen space (SCREEN_W x SCREEN_H) into g_canvas, applying
// the same panel rotation as EpdGFXAdapter::drawPixel so images land upright.
static inline void setScreenPixel(int x, int y, uint8_t gray255) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  if (!g_canvas) return;
  int px, py;
  if (Orientation::isPortrait()) {
    px = y;
    py = (SCREEN_W - 1) - x;
  } else {
    px = x;   // native panel orientation (matches drawPixel): buttons on top
    py = y;
  }
  epd_draw_pixel(px, py, gray255, g_canvas);
}

// Placement params for jpegDrawCB, set by drawEpubImagePage before decode().
// The callback is a plain function pointer (no capture), so these ride along as
// file statics. We decode at offset (0,0) — callback coords are pure (scaled)
// image space — and do all centring/rotation here. s_imgRotate90 turns the
// image 90° into screen space; s_imgRotCW picks the direction (a wide image on
// the portrait panel turns CW, a tall image on the landscape panel turns CCW —
// opposite ways so each reads right-side-up when the device is turned the
// natural way). s_imgW/H are the scaled image dimensions before rotation.
static bool s_imgRotate90 = false;
static bool s_imgRotCW    = true;
static int  s_imgOffX = 0, s_imgOffY = 0;
static int  s_imgW = 0, s_imgH = 0;

// JPEGDEC draw callback. pDraw->{x,y} are (scaled) image-space coords (decode
// offset is 0); pPixels is 1 byte/pixel luma (EIGHT_BIT_GRAYSCALE).
static int jpegDrawCB(JPEGDRAW* pDraw) {
  const uint8_t* p = (const uint8_t*)pDraw->pPixels;
  for (int row = 0; row < pDraw->iHeight; row++) {
    int iy = pDraw->y + row;
    for (int col = 0; col < pDraw->iWidth; col++) {
      int ix = pDraw->x + col;
      uint8_t luma = p[row * pDraw->iWidth + col];
      // Map image-space -> screen-space. When rotating, image (ix,iy) in an
      // s_imgW×s_imgH box maps into the rotated s_imgH×s_imgW box, then offset
      // to centre on the panel. CW: (s_imgH-1-iy, ix). CCW: (iy, s_imgW-1-ix).
      int sx, sy;
      if (s_imgRotate90) {
        if (s_imgRotCW) {
          sx = s_imgOffX + (s_imgH - 1 - iy);
          sy = s_imgOffY + ix;
        } else {
          sx = s_imgOffX + iy;
          sy = s_imgOffY + (s_imgW - 1 - ix);
        }
      } else {
        sx = s_imgOffX + ix;
        sy = s_imgOffY + iy;
      }
      // Ordered-dither threshold keyed on the final screen coords so the
      // pattern stays aligned to the panel regardless of rotation.
      uint8_t thr = (uint8_t)(BAYER8[(sy & 7) * 8 + (sx & 7)] * 4 + 2);
      setScreenPixel(sx, sy, luma > thr ? 255 : 0);
    }
  }
  return 1;   // continue decoding
}

void drawEpubImagePage(const char* sdPath) {
  s_pageIsImage = true;

  File f = FS.open(sdPath, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    drawPlaceholder();
    return;
  }
  size_t sz = f.size();
  if (sz == 0) { f.close(); drawPlaceholder(); return; }

  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (uint8_t*)malloc(sz);   // fall back to internal RAM if no PSRAM
  if (!buf) { f.close(); drawPlaceholder(); return; }

  size_t got = f.read(buf, sz);
  f.close();
  if (got != sz) { free(buf); drawPlaceholder(); return; }

  bool ok = false;
  if (g_jpeg.openRAM(buf, (int)sz, jpegDrawCB)) {
    g_jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
    int w = g_jpeg.getWidth();
    int h = g_jpeg.getHeight();

    // Display each image in the orientation that fills the panel best: rotate
    // 90° when the image's long axis would otherwise land on the screen's short
    // axis. Aligning the long axes is what "most appropriate to the aspect
    // ratio" means here; near-square images (aspects already matching) stay
    // upright. The user turns the device to read a rotated image, as on any
    // e-reader. When rotating, the image is fit against the swapped screen box
    // (its width spans the screen height and vice-versa).
    const bool rotate90 = ((w > h) != (SCREEN_W > SCREEN_H));
    const int fitW = rotate90 ? SCREEN_H : SCREEN_W;
    const int fitH = rotate90 ? SCREEN_W : SCREEN_H;

    // Pick the smallest power-of-two downscale that fits.
    int opt = 0, div = 1;
    if (w > fitW * 4 || h > fitH * 4)      { opt = JPEG_SCALE_EIGHTH;  div = 8; }
    else if (w > fitW * 2 || h > fitH * 2) { opt = JPEG_SCALE_QUARTER; div = 4; }
    else if (w > fitW || h > fitH)         { opt = JPEG_SCALE_HALF;    div = 2; }

    int sw = w / div, sh = h / div;   // scaled image dims (before rotation)

    // Displayed dims are swapped when rotating; centre them on the panel.
    int dispW = rotate90 ? sh : sw;
    int dispH = rotate90 ? sw : sh;
    int offX = (SCREEN_W - dispW) / 2; if (offX < 0) offX = 0;
    int offY = (SCREEN_H - dispH) / 2; if (offY < 0) offY = 0;

    s_imgRotate90 = rotate90;
    s_imgRotCW    = (w > h);   // wide image turns CW, tall image turns CCW
    s_imgOffX = offX; s_imgOffY = offY;
    s_imgW = sw; s_imgH = sh;

    // Decode at (0,0) — the callback works in image space and applies the
    // rotation + centring itself.
    ok = (g_jpeg.decode(0, 0, opt) != 0);
    g_jpeg.close();
  }
  free(buf);
  if (!ok) drawPlaceholder();
}
