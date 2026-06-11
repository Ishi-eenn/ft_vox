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
// fillColoredLeaves() — 色付き葉テクスチャの共通生成ロジック
//
// baseR/G/B: 基本色, varR/G/B: ノイズで変化させる振れ幅
// ─────────────────────────────────────────────────────────────────────────────
static void fillColoredLeaves(uint8_t* buf, int atlas_w, int tile_col, int tile_row,
                               int baseR, int baseG, int baseB,
                               int varR, int varG, int varB, int seed1, int seed2) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, seed1);
            int n2 = hash2(px, py, seed2);

            int r = clamp255(baseR + (n1 - 128) * varR / 128);
            int g = clamp255(baseG + (n1 - 128) * varG / 128);
            int b = clamp255(baseB + (n1 - 128) * varB / 128);

            if (n2 > 222) { r = clamp255(r + 18); g = clamp255(g + 12); }
            else if (n2 < 20) { r = clamp255(r - 14); g = clamp255(g - 10); }

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

// ─────────────────────────────────────────────────────────────────────────────
// fillCactus() — サボテンのテクスチャを生成する
//
// 濃い緑の縦縞模様で、サボテンのリブ（肋骨状の凸凹）を表現する。
// ─────────────────────────────────────────────────────────────────────────────
static void fillCactus(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n = hash2(px, py, 503);

            int r = 44  + (n - 128) * 8 / 128;
            int g = 110 + (n - 128) * 20 / 128;
            int b = 30  + (n - 128) * 6 / 128;

            // 縦リブ（3ピクセル間隔の明るい縞）
            if (px % 3 == 1) {
                r += 12;
                g += 22;
                b += 8;
            }
            // 横の節（4ピクセル間隔の暗い線）
            if (py % 4 == 0) {
                r -= 10;
                g -= 18;
                b -= 6;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillOre() — 石をベースに鉱石の斑点を混ぜる
// ─────────────────────────────────────────────────────────────────────────────
static void fillOre(uint8_t* buf, int atlas_w, int tile_col, int tile_row,
                    int ore_r, int ore_g, int ore_b, int seed) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 61);
            int n2 = hash2(px, py, 113);

            int base = 112 + (n1 - 128) * 22 / 128;
            int r = base + (n2 - 128) * 5 / 128;
            int g = base + (n2 - 128) * 5 / 128;
            int b = base + 6 + (n2 - 128) * 7 / 128;

            int vein = hash2(px, py, seed);
            bool ore = vein > 218
                    || ((px + py) % 5 == 0 && vein > 176)
                    || (std::abs(px - py) <= 1 && vein > 188);
            if (ore) {
                int sparkle = hash2(px, py, seed + 37);
                r = ore_r + (sparkle - 128) * 24 / 128;
                g = ore_g + (sparkle - 128) * 24 / 128;
                b = ore_b + (sparkle - 128) * 24 / 128;
                if (sparkle > 238) {
                    r += 36;
                    g += 36;
                    b += 36;
                }
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

static void fillShortGrassPlant(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int from_bottom = tw - 1 - py;
            int h1 = 5 + (hash2(px, 0, 911) % 8);
            int h2 = 4 + (hash2(px, 0, 977) % 6);
            bool blade = ((px % 4 == 1 || px % 7 == 2) && from_bottom < h1)
                      || ((px + py) % 9 == 0 && from_bottom < h2);
            if (!blade) {
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
                continue;
            }
            int n = hash2(px, py, 941);
            setPixel(buf, atlas_w, ox, oy, px, py,
                     50 + (n % 20), 126 + (n % 46), 32 + (n % 18));
        }
    }
}

static void fillFlowerPlant(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            bool stem = (px == 7 || px == 8) && py >= 5;
            bool leaf = (py >= 9 && py <= 12 && (px == 5 || px == 10));
            int dx = px - 7;
            int dy = py - 4;
            bool petal = dx * dx + dy * dy <= 9
                      || ((px - 5) * (px - 5) + (py - 5) * (py - 5) <= 4)
                      || ((px - 10) * (px - 10) + (py - 5) * (py - 5) <= 4);
            if (!stem && !leaf && !petal) {
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
                continue;
            }
            if (petal) {
                int n = hash2(px, py, 1013);
                setPixel(buf, atlas_w, ox, oy, px, py,
                         210 + (n % 35), 52 + (n % 34), 60 + (n % 48));
            } else {
                int n = hash2(px, py, 1021);
                setPixel(buf, atlas_w, ox, oy, px, py,
                         45 + (n % 18), 128 + (n % 40), 38 + (n % 20));
            }
        }
    }
}

static void fillMushroomPlant(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            bool stem = (px >= 6 && px <= 9 && py >= 7 && py <= 14);
            int dx = px - 7;
            int dy = py - 5;
            bool cap = (dx * dx * 2 + dy * dy * 3 <= 38) && py <= 8;
            if (!stem && !cap) {
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
                continue;
            }
            if (cap) {
                bool spot = ((px - 5) * (px - 5) + (py - 5) * (py - 5) <= 2)
                         || ((px - 10) * (px - 10) + (py - 4) * (py - 4) <= 2);
                int n = hash2(px, py, 1103);
                if (spot)
                    setPixel(buf, atlas_w, ox, oy, px, py, 226, 214, 184);
                else
                    setPixel(buf, atlas_w, ox, oy, px, py,
                             146 + (n % 36), 48 + (n % 24), 38 + (n % 18));
            } else {
                int n = hash2(px, py, 1117);
                setPixel(buf, atlas_w, ox, oy, px, py,
                         184 + (n % 32), 160 + (n % 28), 126 + (n % 22));
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillStick() — 棒（クラフト素材）テクスチャを生成
//
// 中央 2px に茶色の棒。周囲は透明。
// 3D 松明メッシュの棒部分にも貼られる（タイル全体を細い棒の側面として使う）。
// ─────────────────────────────────────────────────────────────────────────────
static void fillStick(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            bool stick = (px >= 7 && px <= 8);
            if (!stick) {
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
                continue;
            }
            int n = hash2(px, py, 1301);
            int r = 138 + ((n % 24) - 12);
            int g =  98 + ((n % 18) -  9);
            int b =  56 + ((n % 14) -  7);
            // 縦の木目（暗いライン）
            if (px == 7 && (py % 3 == 0)) { r -= 22; g -= 18; b -= 10; }
            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillTorch() — 松明アイコンテクスチャを生成
//
// 16x16: 下 12px が茶色の棒（細い 2px 幅）、上 4px が Minecraft 風の炎。
// ホットバーの 2D アイコン表示と、3D 設置メッシュの炎部分のサンプリングで使う。
// 炎は白熱(W) → 黄(Y/y) → オレンジ(O) のグラデーションで本家の見た目に寄せる。
// ─────────────────────────────────────────────────────────────────────────────
static void fillTorch(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    // 炎の 4×16 ピクセルマップ（py=0 は炎の先端、py=3 は基部 = 棒の真上）
    // '.' = 透明, 'W' = 白熱中心, 'Y' = 明るい黄, 'y' = 黄, 'O' = オレンジ, 'o' = 暗オレンジ
    static const char* kFlame[4] = {
        ".......YY.......",
        "......YWWY......",
        ".....yOWWOy.....",
        "....oOyWWyOo....",
    };

    auto putFlame = [&](int px, int py, char c) {
        int r = 0, g = 0, b = 0, a = 0;
        switch (c) {
            case 'W': r = 255; g = 252; b = 220; a = 255; break;
            case 'Y': r = 255; g = 232; b =  80; a = 255; break;
            case 'y': r = 250; g = 200; b =  50; a = 255; break;
            case 'O': r = 245; g = 148; b =  32; a = 255; break;
            case 'o': r = 198; g =  92; b =  18; a = 255; break;
            default:  a = 0; break;
        }
        setPixel(buf, atlas_w, ox, oy, px, py, r, g, b, a);
    };

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            if (py < 4) {
                // 炎エリア
                putFlame(px, py, kFlame[py][px]);
            } else if (px == 7 || px == 8) {
                // 棒エリア（中央 2px）
                int n = hash2(px, py, 1301);
                int r = 138 + ((n % 24) - 12);
                int g =  98 + ((n % 18) -  9);
                int b =  56 + ((n % 14) -  7);
                if (px == 7 && (py % 3 == 0)) { r -= 22; g -= 18; b -= 10; }
                setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
            } else {
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
            }
        }
    }
}

// 弓アイコン16x16ピクセルマップを焼き込む
static void fillBow(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    static const char* pix[16] = {
        "................",
        "...........bbbb.",
        "........bbbcddce",
        "......bbcdceeee.",
        ".....bfdeee..g..",
        "....bfhf....g...",
        "...bfhf....g....",
        "...bdf....g.....",
        "..bce....g......",
        "..bde...g.......",
        "..bce..g........",
        ".bce..g.........",
        ".bde.g..........",
        ".bdeg...........",
        ".bce............",
        "..e.............",
    };

    auto put = [&](int px, int py, char c) {
        switch (c) {
            case 'b': setPixel(buf, atlas_w, ox, oy, px, py,  73,  54,  21); break;
            case 'c': setPixel(buf, atlas_w, ox, oy, px, py, 137, 103,  39); break;
            case 'd': setPixel(buf, atlas_w, ox, oy, px, py, 104,  78,  30); break;
            case 'e': setPixel(buf, atlas_w, ox, oy, px, py,  40,  30,  11); break;
            case 'f': setPixel(buf, atlas_w, ox, oy, px, py, 107, 107, 107); break;
            case 'g': setPixel(buf, atlas_w, ox, oy, px, py,  68,  68,  68); break;
            case 'h': setPixel(buf, atlas_w, ox, oy, px, py, 150, 150, 150); break;
            default:  setPixel(buf, atlas_w, ox, oy, px, py,   0,   0,   0, 0); break;
        }
    };

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px)
            put(px, py, pix[py][px]);
    }
}

// ドラゴンエッグアイコン 16x16 (黒紫の卵 + ピンクの斑点)
static void fillDragonEgg(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;
    static const char* pix[16] = {
        "................",
        "......aaaa......",
        ".....abbbba.....",
        "....abbbcbba....",
        "...abbcbbbbba...",
        "...abbbbcbbba...",
        "..abbcbbbbbcba..",
        "..abbbbbcbbbba..",
        "..abbbcbbbbbba..",
        "..abbbbbbbcbba..",
        "..abbbcbbbbbba..",
        "...abbbbbcbba...",
        "...abbcbbbbba...",
        "....abbbbbba....",
        ".....abbbba.....",
        "......aaaa......",
    };
    auto put = [&](int px, int py, char c) {
        switch (c) {
            case 'a': setPixel(buf, atlas_w, ox, oy, px, py,  10,   4,  18); break; // 縁: 黒
            case 'b': setPixel(buf, atlas_w, ox, oy, px, py,  35,  18,  55); break; // 本体: 暗紫
            case 'c': setPixel(buf, atlas_w, ox, oy, px, py, 180,  90, 200); break; // 斑点: 紫光
            default:  setPixel(buf, atlas_w, ox, oy, px, py,   0,   0,   0, 0); break;
        }
    };
    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px)
            put(px, py, pix[py][px]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillCobblestone() — 石レンガのテクスチャを生成する（村の壁用）
//
// 丸石風: 暗灰色のベースに、約4×4ブロックのモルタル線入り。
// Minecraft の cobblestone に近い「分割された石」の見た目。
// ─────────────────────────────────────────────────────────────────────────────
static void fillCobblestone(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 431);
            int n2 = hash2(px, py, 557);

            // ベースグレー（Stone より少し暗め）
            int base = 98 + (n1 - 128) * 20 / 128;
            int r = base + (n2 - 128) * 4 / 128;
            int g = base + (n2 - 128) * 4 / 128;
            int b = base + 4 + (n2 - 128) * 5 / 128;

            // モルタル線（4px 間隔の縦横グリッド）
            bool mortar_h = (py % 4 == 0 || py % 4 == 1) && hash2(px, py, 13) < 180;
            bool mortar_v = (px % 4 == 0 || px % 4 == 1) && hash2(px, py, 37) < 180;
            // 市松パターン: 縦と横が交互にずれる
            bool offset = (py / 4) % 2 == 0;
            bool mortar_v2 = ((px + (offset ? 2 : 0)) % 4 == 0) && hash2(px, py, 53) < 180;

            if (mortar_h || mortar_v || mortar_v2) {
                r = r * 60 / 100;
                g = g * 60 / 100;
                b = b * 60 / 100;
            }
            // 石ブロック内部の丸み（角を少し暗く）
            if (!mortar_h && !mortar_v && !mortar_v2 && n2 < 22) {
                r += 14; g += 14; b += 16;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillPlanks() — 木の板材テクスチャを生成する（村の屋根・床用）
//
// 明るいタン/ベージュ色に横方向の板の継ぎ目（4px 間隔）。
// Minecraft の oak planks に近い見た目。
// ─────────────────────────────────────────────────────────────────────────────
static void fillPlanks(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 617);
            int n2 = hash2(px, py, 743);

            // 明るい木材色（Minecraft oak planks: 淡いタン〜オレンジベージュ）
            int r = 162 + (n1 - 128) * 14 / 128;
            int g = 130 + (n1 - 128) * 10 / 128;
            int b =  72 + (n1 - 128) *  8 / 128;

            // 横方向の板の継ぎ目（4px 間隔）
            if (py % 4 == 0 && n2 < 200) {
                r = r * 75 / 100;
                g = g * 75 / 100;
                b = b * 75 / 100;
            }
            // 板の縦木目（細い）
            if ((px % 2 == 0) && n1 > 220) {
                r += 8; g += 6; b += 3;
            }
            // 暗いノット（節）
            if (n2 < 8) { r -= 18; g -= 14; b -= 8; }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillFarmland() — 農耕地テクスチャを生成する
//
// 暗くしっとりした土：Dirt より暗く、横方向の耕作溝（2行おき）を表現。
// ─────────────────────────────────────────────────────────────────────────────
static void fillFarmland(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 883);
            int n2 = hash2(px, py, 919);

            // 暗い湿った土色
            int r = 100 + (n1 - 128) * 16 / 128;
            int g =  70 + (n1 - 128) * 12 / 128;
            int b =  40 + (n1 - 128) *  8 / 128;

            // 耕作溝（2行ごとに暗い影）
            if (py % 4 < 2 && n2 < 160) {
                r = r * 78 / 100;
                g = g * 78 / 100;
                b = b * 78 / 100;
            }
            // わずかに青みがかった湿り気
            b += 6;

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillGravelPath() — 砂利道テクスチャを生成する（村の道路用）
//
// 混在する灰色・ベージュ・茶色の小石で構成される踏み固めた砂利道。
// ─────────────────────────────────────────────────────────────────────────────
static void fillGravelPath(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 1013);
            int n2 = hash2(px, py, 1097);
            int n3 = hash2(px, py, 1153);

            // ベース: 暗めグレー-ベージュ
            int r = 136 + (n1 - 128) * 28 / 128;
            int g = 126 + (n1 - 128) * 24 / 128;
            int b = 116 + (n1 - 128) * 20 / 128;

            // 石ごとに色味を変える（3種のトーン）
            if (n2 < 50) {
                // 暗い小石
                r -= 24; g -= 22; b -= 20;
            } else if (n2 > 210) {
                // 明るい小石
                r += 18; g += 16; b += 14;
            } else if (n3 < 40) {
                // 茶みがかった小石
                r += 10; g += 2; b -= 8;
            }

            setPixel(buf, atlas_w, ox, oy, px, py, r, g, b);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// fillWheatPlant() — 小麦クロスモデル用テクスチャを生成する
//
// 黄緑〜黄色の穂：ShortGrass より高く、成熟した小麦の色合い。
// アルファで背景を透明にしてクロスモデル描画に対応する。
// ─────────────────────────────────────────────────────────────────────────────
static void fillWheatPlant(uint8_t* buf, int atlas_w, int tile_col, int tile_row) {
    const int tw = ATLAS_TILE_SIZE;
    const int ox = tile_col * tw;
    const int oy = tile_row * tw;

    for (int py = 0; py < tw; ++py) {
        for (int px = 0; px < tw; ++px) {
            int n1 = hash2(px, py, 1201);
            int n2 = hash2(px, py, 1279);

            // 茎の位置（2px幅の縦縞を数本）
            bool stem = (px % 4 < 2);
            // 上部（穂）の高さ（上から6px）は黄色が強め
            bool head = (py < 6);

            if (stem) {
                // 茎: 黄緑〜黄色
                int r = (head ? 200 : 140) + (n1 - 128) * 18 / 128;
                int g = (head ? 185 : 160) + (n1 - 128) * 14 / 128;
                int b =  30 + (n1 - 128) * 10 / 128;

                // 穂の先端: 明るい黄色
                if (head && n2 > 200) { r += 20; g += 10; }
                // 茎の根元: 少し暗い
                if (py > 12 && n2 < 50) { r -= 12; g -= 10; }

                setPixel(buf, atlas_w, ox, oy, px, py, r, g, b, 255);
            } else {
                // 背景: 完全透明
                setPixel(buf, atlas_w, ox, oy, px, py, 0, 0, 0, 0);
            }
        }
    }
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
    fillLeaves   (pixels, atlas_w, 0, 1);            // Leaves  (col=0, row=1) tile=8
    fillCactus   (pixels, atlas_w, 1, 1);            // Cactus  (col=1, row=1) tile=9
    fillGrassSide(pixels, atlas_w, 2, 1);            // GrassSide (col=2, row=1) tile=10
    fillOre      (pixels, atlas_w, 3, 1, 214, 174,  48, 701); // GoldOre    tile=11
    fillOre      (pixels, atlas_w, 4, 1,  68, 218, 224, 809); // DiamondOre tile=12
    fillShortGrassPlant(pixels, atlas_w, 5, 1);      // ShortGrass tile=13
    fillFlowerPlant    (pixels, atlas_w, 6, 1);      // Flower     tile=14
    fillMushroomPlant  (pixels, atlas_w, 7, 1);      // Mushroom   tile=15
    // row=2: 春バイオーム・秋バイオーム用の色付き葉
    fillColoredLeaves(pixels, atlas_w, 0, 2, 210, 140, 160, 20, 12, 14, 511, 719); // PinkLeaves   tile=16 (桜ピンク)
    fillColoredLeaves(pixels, atlas_w, 1, 2, 195, 105,  30, 22, 18, 10, 613, 821); // OrangeLeaves tile=17 (紅葉オレンジ)
    fillBow      (pixels, atlas_w, 2, 2);            // Bow        tile=18
    fillStick    (pixels, atlas_w, 3, 2);            // Stick      tile=19
    fillTorch    (pixels, atlas_w, 4, 2);            // Torch       tile=20
    fillDragonEgg(pixels, atlas_w, 5, 2);            // DragonEgg   tile=21
    fillCobblestone(pixels, atlas_w, 6, 2);          // Cobblestone tile=22
    fillPlanks     (pixels, atlas_w, 7, 2);          // Planks      tile=23
    // row=3: 村用農業ブロック
    fillFarmland   (pixels, atlas_w, 0, 3);          // Farmland    tile=24
    fillGravelPath (pixels, atlas_w, 1, 3);          // GravelPath  tile=25
    fillWheatPlant (pixels, atlas_w, 2, 3);          // Wheat       tile=26

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
