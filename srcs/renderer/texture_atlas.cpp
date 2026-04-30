// ─────────────────────────────────────────────────────────────────────────────
// texture_atlas.cpp — ブロックテクスチャの生成とアトラス管理
//
// 【テクスチャアトラスとは？】
//   全ブロックの絵柄を1枚の大きなテクスチャにまとめたもの。
//   テクスチャを切り替える（バインドし直す）と GPU に大きな負荷がかかるため、
//   1枚のアトラスにまとめて「UV 座標」だけで使うブロックを切り替える。
//
// 【UV 座標とは？】
//   テクスチャ上の位置を (0.0〜1.0, 0.0〜1.0) で指定する座標系。
//   左上が (0,0)、右下が (1,1)。
//   アトラスでは各ブロックのタイルに対応する UV 範囲（u0,v0)〜(u1,v1）を計算する。
//
// 【手続き的テクスチャ生成とは？】
//   PNG ファイルを読み込む代わりに、コードでピクセルを計算して生成する。
//   決定論的ハッシュ関数でノイズを作り、Minecraft 風のドット絵を再現している。
//
// 【ATLAS レイアウト】
//   幅: ATLAS_COLS × ATLAS_TILE_SIZE = 8 × 16 = 128 px
//   高さ: rows × ATLAS_TILE_SIZE     = 4 × 16 =  64 px  (現在 rows=2 使用)
//   タイル col は BlockType の整数値に対応（col 0 = Air, col 1 = Grass, ...）
// ─────────────────────────────────────────────────────────────────────────────
#include "texture_atlas.hpp"
#include <glad/gl.h>
#include <cstring>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// 無名名前空間 — このファイル内だけで使うヘルパー関数群
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// hash2() — 決定論的なピクセルノイズ生成（出力: 0〜255）
//
// 同じ (x, y, seed) には常に同じ値を返す（再現性がある）。
// 異なる seed を使えば独立したノイズ層が得られる。
// ビット演算を組み合わせた「整数ハッシュ」で疑似ランダム性を作る。
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t hash2(int x, int y, int seed) {
    unsigned v = (unsigned)(x * 1619 + y * 31337 + seed * 6271);
    v = (v ^ 61u) ^ (v >> 16u);
    v *= 9u;
    v ^= v >> 4u;
    v *= 0x27d4eb2du;
    v ^= v >> 15u;
    return (uint8_t)(v & 0xFFu);
}

// 0〜255 の範囲にクランプ（色のオーバーフローを防ぐ）
static inline int clamp255(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// アトラスバッファの指定ピクセルに RGBA 色をセットする
static inline void setPixel(uint8_t* buf, int atlas_w,
                             int ox, int oy, int px, int py,
                             int r, int g, int b, int a = 255) {
    int idx = ((oy + py) * atlas_w + (ox + px)) * 4;
    buf[idx + 0] = (uint8_t)clamp255(r);
    buf[idx + 1] = (uint8_t)clamp255(g);
    buf[idx + 2] = (uint8_t)clamp255(b);
    buf[idx + 3] = (uint8_t)clamp255(a);
}

// ─────────────────────────────────────────────────────────────────────────────
// fillDirt() — 土のテクスチャを生成する
//
// Minecraft 風の土: 暖かい中間ブラウンをベースに、
// 単一ピクセルの暗・明スペックルを散らす。なめらかなグラデーションは使わない。
// ─────────────────────────────────────────────────────────────────────────────
static void fillDirt(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    // 土のパレット（Minecraft 近似）
    //  ベース:   (134, 96, 67)  約57% のピクセル
    //  暗め:     (101, 67, 42)  約25%
    //  とても暗: ( 74, 47, 28)  約5%
    //  明るめ:   (162,120, 87)  約10%
    //  ハイライト:(192,148,108) 約3%

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n = hash2(px, py, 42);  // 1つのノイズ値でパレットを選ぶ

            int r, g, b;
            if      (n < 12)  { r =  74; g =  47; b =  28; }  // とても暗い
            else if (n < 75)  { r = 101; g =  67; b =  42; }  // 暗め
            else if (n > 247) { r = 192; g = 148; b = 108; }  // ハイライト
            else if (n > 218) { r = 162; g = 120; b =  87; }  // 明るめ
            else              { r = 134; g =  96; b =  67; }  // ベース

            // 同じクラスのピクセルが完全に同色にならないよう ±4 のディザを加える
            int d = hash2(px, py, 199);
            r += (d & 7) - 4;
            g += ((d >> 3) & 3) - 2;

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillGrass() — 草のテクスチャ（上面）を生成する
//
// 様々な緑。黄緑の茎、暗い影の部分を混在させる。
// ─────────────────────────────────────────────────────────────────────────────
static void fillGrass(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 7);   // 大きな明暗バリエーション
            int n2 = hash2(px, py, 19);  // 黄緑ハイライト
            int n3 = hash2(px, py, 31);  // 暗い陰影スポット

            int r = 58,  g = 140, b = 40;  // ベースの緑

            // 大きな明暗変化（光の当たり方）
            r += (n1 - 128) * 18 / 128;
            g += (n1 - 128) * 28 / 128;
            b += (n1 - 128) *  8 / 128;

            // 黄緑のハイライト（約10%のピクセル）
            if (n2 > 230) { r += 30; g += 20; b -= 10; }

            // 草の間の暗い影（約8%のピクセル）
            if (n3 < 20) {
                r = r * 60 / 100;
                g = g * 60 / 100;
                b = b * 60 / 100;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillStone() — 石のテクスチャを生成する
//
// 中間グレーに微妙な変化をつけ、横方向の薄いひび割れ線を加える。
// ─────────────────────────────────────────────────────────────────────────────
static void fillStone(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 61);
            int n2 = hash2(px, py, 113);

            int base = 118 + (n1 - 128) * 22 / 128;  // 96〜140 のグレー範囲
            int r = base + (n2 - 128) * 5 / 128;
            int g = base + (n2 - 128) * 5 / 128;
            int b = base + 6 + (n2 - 128) * 7 / 128;  // 僅かに青み（石っぽさ）

            // 固定行にひび割れ線（ランダムな割合で）
            if ((py == 5 || py == 11) && hash2(px, py, 7) < 90) {
                r = r * 72 / 100;
                g = g * 72 / 100;
                b = b * 72 / 100;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillSand() — 砂のテクスチャを生成する（砂漠バイオーム用）
//
// 暖かい黄ベージュ色に細かい粒感を加える。
// ─────────────────────────────────────────────────────────────────────────────
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

            // 暗い砂粒（約5%）
            if (n2 < 12) { r -= 30; g -= 25; b -= 15; }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillSnow() — 雪のテクスチャを生成する（ツンドラバイオーム用）
//
// ほぼ白色に、僅かに青みがかった冷たい影を加える。
// ─────────────────────────────────────────────────────────────────────────────
static void fillSnow(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 89);

            int shade = 230 + (n1 - 128) * 20 / 128;  // 220〜250 の白に近いグレー
            int r = shade;
            int g = shade;
            int b = shade + 8;  // 青みがかった冷たい雰囲気

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillWater() — 水のテクスチャを生成する
//
// 深い青をベースに、明るい波紋のハイライトと暗い影を加える。
// アルファ値を168（不透明度 66%）にして半透明にする。
// ─────────────────────────────────────────────────────────────────────────────
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
            int a = 168;  // 半透明（α = 168/255 ≈ 66%）

            // 波紋の光反射（約8%のピクセル）
            if (n2 > 232) { r += 18; g += 28; b += 14; }
            // 暗い波影（約5%のピクセル）
            if (n2 < 12)  { r -= 12; g -= 18; b -= 20; }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b, a);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillWood() — 木材（木の幹）のテクスチャを生成する
//
// 縦方向の木目縞と、暗いノットホール（節）を表現する。
// ─────────────────────────────────────────────────────────────────────────────
static void fillWood(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 301);
            int n2 = hash2(px, py, 337);

            int r = 112 + (n1 - 128) * 16 / 128;
            int g = 82  + (n1 - 128) * 12 / 128;
            int b = 48  + (n1 - 128) * 10 / 128;

            // 縦木目（4ピクセル間隔の明るい縞）
            if ((px % 4 == 1 || px % 4 == 2) && n2 > 84) {
                r += 18;
                g += 12;
                b += 6;
            }
            // 暗いノットホール（節）
            if (n2 < 18) {
                r -= 22;
                g -= 16;
                b -= 10;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillLeaves() — 葉のテクスチャを生成する
//
// 濃い緑に、明るいハイライトと暗い影を混在させて葉の密集感を出す。
// ─────────────────────────────────────────────────────────────────────────────
static void fillLeaves(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 401);
            int n2 = hash2(px, py, 449);

            int r = 42 + (n1 - 128) * 12 / 128;
            int g = 118 + (n1 - 128) * 26 / 128;
            int b = 34 + (n1 - 128) * 10 / 128;

            if (n2 > 222) {
                // 明るい葉（日光が当たっている部分）
                r += 18;
                g += 24;
                b += 10;
            } else if (n2 < 20) {
                // 暗い葉（影になっている部分）
                r -= 14;
                g -= 20;
                b -= 10;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillGrassSide() — 草ブロックの側面テクスチャを生成する
//
// 上部4テクセル: 草の緑色
// 残り: 土の色
// これにより「上が草、中身が土」の草ブロックの見た目を実現する。
// ─────────────────────────────────────────────────────────────────────────────
static void fillGrassSide(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    const int GREEN_ROWS = 4;  // 上部の草部分の行数

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int r, g, b;
            if (py >= tw - GREEN_ROWS) {
                // 上部（grass strip）: 草の緑
                int n1 = hash2(px, py, 7);
                r = 58  + (n1 - 128) * 18 / 128;
                g = 140 + (n1 - 128) * 28 / 128;
                b = 40  + (n1 - 128) *  8 / 128;
            } else {
                // 下部: 土の色
                int n = hash2(px, py, 42);
                if      (n < 12)  { r =  74; g =  47; b =  28; }
                else if (n < 75)  { r = 101; g =  67; b =  42; }
                else if (n > 247) { r = 192; g = 148; b = 108; }
                else if (n > 218) { r = 162; g = 120; b =  87; }
                else              { r = 134; g =  96; b =  67; }
                int d = hash2(px, py, 199);
                r += (d & 7) - 4;
                g += ((d >> 3) & 3) - 2;
            }
            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// 単色で塗りつぶす（Air タイルなど未使用スロット用）
static void fillSolid(uint8_t* buf, int atlas_w, int tile_col, int tile_row,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py)
        for (int px = 0; px < tw; ++px)
            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b, a);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TextureAtlas::generate() — アトラステクスチャを生成して GPU に転送する
//
// 1. CPU 上でピクセルデータを計算する
// 2. OpenGL テクスチャとして GPU に転送する
// 3. フィルタリングを GL_NEAREST（ニアレスト）に設定する
//    → ドット絵がぼけずにシャープに見える（Minecraft スタイル）
// ─────────────────────────────────────────────────────────────────────────────
bool TextureAtlas::generate() {
    const int atlas_w = ATLAS_COLS * ATLAS_TILE_SIZE;  // 128 px
    const int atlas_h = rows_      * ATLAS_TILE_SIZE;  // 64 px（2行 × 16px）

    uint8_t pixels[128 * 64 * 4];
    std::memset(pixels, 0, sizeof(pixels));

    // col インデックスは (int)BlockType に対応する
    fillSolid(pixels, atlas_w, 0, 0, 255, 0, 255);  // Air（未使用: マゼンタ）
    fillGrass(pixels, atlas_w, 1, 0);                // Grass 上面
    fillDirt (pixels, atlas_w, 2, 0);                // Dirt
    fillStone(pixels, atlas_w, 3, 0);                // Stone
    fillSand (pixels, atlas_w, 4, 0);                // Sand
    fillSnow (pixels, atlas_w, 5, 0);                // Snow
    fillWater(pixels, atlas_w, 6, 0);                // Water
    fillWood (pixels, atlas_w, 7, 0);                // Wood (trunk)
    fillLeaves   (pixels, atlas_w, 0, 1);            // Leaves (row 1)
    fillGrassSide(pixels, atlas_w, 1, 1);            // GrassSide (col=1, row=1)

    // GPU にテクスチャを作成して転送する
    glGenTextures(1, &tex_id_);
    glBindTexture(GL_TEXTURE_2D, tex_id_);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 atlas_w, atlas_h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // GL_NEAREST: 拡大/縮小時に補間しない（ドット絵がぼけない）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // テクスチャのはみ出し部分をクランプ（タイル境界でのにじみを防ぐ）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return (tex_id_ != 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// bind() — 指定テクスチャスロットにアトラスをバインドする
//
// シェーダーの sampler2D に対応するスロット番号（通常は 0）を指定する。
// ─────────────────────────────────────────────────────────────────────────────
void TextureAtlas::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, tex_id_);
}

void TextureAtlas::destroy() {
    if (tex_id_ != 0) { glDeleteTextures(1, &tex_id_); tex_id_ = 0; }
}

TextureAtlas::~TextureAtlas() { destroy(); }

// ─────────────────────────────────────────────────────────────────────────────
// getUV() — ブロックタイプに対応する UV 座標範囲を返す
//
// アトラス上のタイル位置を計算して (u0, v0)〜(u1, v1) の範囲を返す。
// メッシュ生成時に各面の頂点 UV として使われる。
// ─────────────────────────────────────────────────────────────────────────────
AtlasUV TextureAtlas::getUV(BlockType type) const {
    int   tile  = static_cast<int>(type);
    float icols = 1.0f / static_cast<float>(cols_);  // 1タイルの幅（UV 単位）
    float irows = 1.0f / static_cast<float>(rows_);  // 1タイルの高さ（UV 単位）
    AtlasUV uv;
    uv.u0 = (tile % cols_) * icols;   // タイルの左端 U
    uv.v0 = (tile / cols_) * irows;   // タイルの上端 V
    uv.u1 = uv.u0 + icols;            // タイルの右端 U
    uv.v1 = uv.v0 + irows;            // タイルの下端 V
    return uv;
}
