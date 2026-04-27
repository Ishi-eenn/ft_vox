#include "texture_atlas.hpp"
#include <glad/gl.h>
#include <cstring>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Deterministic pixel hash — returns [0, 255].
// Different seeds produce independent noise layers.
static uint8_t hash2(int x, int y, int seed) {
    unsigned v = (unsigned)(x * 1619 + y * 31337 + seed * 6271);
    v = (v ^ 61u) ^ (v >> 16u);
    v *= 9u;
    v ^= v >> 4u;
    v *= 0x27d4eb2du;
    v ^= v >> 15u;
    return (uint8_t)(v & 0xFFu);
}

static inline int clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static inline void setPixel(uint8_t* buf, int atlas_w,
                             int ox, int oy, int px, int py,
                             int r, int g, int b) {
    int idx = ((oy + py) * atlas_w + (ox + px)) * 3;
    buf[idx + 0] = (uint8_t)clamp255(r);
    buf[idx + 1] = (uint8_t)clamp255(g);
    buf[idx + 2] = (uint8_t)clamp255(b);
}

// ─── Dirt ────────────────────────────────────────────────────────────────────
// Minecraft-style dirt: warm mid-brown base with scattered single-pixel
// dark and light specks — no smooth gradients, just discrete noise.
static void fillDirt(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    // Minecraft dirt palette (approximated)
    //  base     : (134, 96, 67)
    //  dark     : (101, 67, 42)   ~25% of pixels
    //  very dark: ( 74, 47, 28)   ~ 5% of pixels
    //  light    : (162,120, 87)   ~10% of pixels
    //  highlight: (192,148,108)   ~ 3% of pixels

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n = hash2(px, py, 42);  // one noise value drives the palette

            int r, g, b;
            if      (n < 12)  { r =  74; g =  47; b =  28; }  // very dark (~5%)
            else if (n < 75)  { r = 101; g =  67; b =  42; }  // dark      (~25%)
            else if (n > 247) { r = 192; g = 148; b = 108; }  // highlight (~3%)
            else if (n > 218) { r = 162; g = 120; b =  87; }  // light     (~10%)
            else              { r = 134; g =  96; b =  67; }  // base      (~57%)

            // Tiny ±4 dither so identical-class pixels aren't perfectly flat
            int d = hash2(px, py, 199);
            r += (d & 7) - 4;
            g += ((d >> 3) & 3) - 2;

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Grass (top) ─────────────────────────────────────────────────────────────
// Varied greens; some yellow-green blades, darker hollows.
static void fillGrass(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 7);
            int n2 = hash2(px, py, 19);
            int n3 = hash2(px, py, 31);

            int r = 58,  g = 140, b = 40;

            // Large variation (light / dark patches)
            r += (n1 - 128) * 18 / 128;
            g += (n1 - 128) * 28 / 128;
            b += (n1 - 128) *  8 / 128;

            // Yellow-green highlights (~10%)
            if (n2 > 230) { r += 30; g += 20; b -= 10; }

            // Dark spots (shade between blades, ~8%)
            if (n3 < 20) {
                r = r * 60 / 100;
                g = g * 60 / 100;
                b = b * 60 / 100;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Stone ───────────────────────────────────────────────────────────────────
// Mid-gray with soft variation, faint horizontal crack lines.
static void fillStone(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 61);
            int n2 = hash2(px, py, 113);

            int base = 118 + (n1 - 128) * 22 / 128;  // 96–140
            int r = base + (n2 - 128) * 5 / 128;
            int g = base + (n2 - 128) * 5 / 128;
            int b = base + 6 + (n2 - 128) * 7 / 128; // slight blue cast

            // Horizontal crack lines at fixed rows
            if ((py == 5 || py == 11) && hash2(px, py, 7) < 90) {
                r = r * 72 / 100;
                g = g * 72 / 100;
                b = b * 72 / 100;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Sand ────────────────────────────────────────────────────────────────────
// Warm yellow-beige with fine grain.
static void fillSand(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 23);
            int n2 = hash2(px, py, 71);

            int r = 196 + (n1 - 128) * 18 / 128;
            int g = 172 + (n1 - 128) * 14 / 128;
            int b = 112 + (n1 - 128) *  8 / 128;

            // Fine dark grains (~5%)
            if (n2 < 12) { r -= 30; g -= 25; b -= 15; }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Snow ────────────────────────────────────────────────────────────────────
// Near-white with subtle blue-cool shadow.
static void fillSnow(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 89);

            int shade = 230 + (n1 - 128) * 20 / 128;  // 220–250
            int r = shade;
            int g = shade;
            int b = shade + 8;  // cool blue tint

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Water ───────────────────────────────────────────────────────────────────
// Deep blue base with lighter ripple highlights and darker shadow patches.
static void fillWater(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 173);
            int n2 = hash2(px, py, 251);

            int r = 32  + (n1 - 128) * 12 / 128;
            int g = 96  + (n1 - 128) * 18 / 128;
            int b = 188 + (n1 - 128) *  8 / 128;

            // Ripple highlights (~8%)
            if (n2 > 232) { r += 18; g += 28; b += 14; }
            // Dark shadow patches (~5%)
            if (n2 < 12)  { r -= 12; g -= 18; b -= 20; }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─── Solid color (for Air / unused tiles) ────────────────────────────────────
static void fillSolid(uint8_t* buf, int atlas_w, int tile_col, int tile_row,
                       uint8_t r, uint8_t g, uint8_t b) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py)
        for (int px = 0; px < tw; ++px)
            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas::generate()
// ─────────────────────────────────────────────────────────────────────────────
bool TextureAtlas::generate() {
    const int atlas_w = ATLAS_COLS * ATLAS_TILE_SIZE;  // 128
    const int atlas_h = rows_      * ATLAS_TILE_SIZE;  // 64

    uint8_t pixels[128 * 64 * 3];
    std::memset(pixels, 0, sizeof(pixels));

    // col index matches (int)BlockType
    fillSolid(pixels, atlas_w, 0, 0, 255, 0, 255);  // Air (unused) — magenta
    fillGrass(pixels, atlas_w, 1, 0);
    fillDirt (pixels, atlas_w, 2, 0);
    fillStone(pixels, atlas_w, 3, 0);
    fillSand (pixels, atlas_w, 4, 0);
    fillSnow (pixels, atlas_w, 5, 0);
    fillWater(pixels, atlas_w, 6, 0);

    glGenTextures(1, &tex_id_);
    glBindTexture(GL_TEXTURE_2D, tex_id_);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 atlas_w, atlas_h,
                 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return (tex_id_ != 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void TextureAtlas::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, tex_id_);
}

void TextureAtlas::destroy() {
    if (tex_id_ != 0) { glDeleteTextures(1, &tex_id_); tex_id_ = 0; }
}

TextureAtlas::~TextureAtlas() { destroy(); }

AtlasUV TextureAtlas::getUV(BlockType type) const {
    int   tile  = static_cast<int>(type);
    float icols = 1.0f / static_cast<float>(cols_);
    float irows = 1.0f / static_cast<float>(rows_);
    AtlasUV uv;
    uv.u0 = (tile % cols_) * icols;
    uv.v0 = (tile / cols_) * irows;
    uv.u1 = uv.u0 + icols;
    uv.v1 = uv.v0 + irows;
    return uv;
}
