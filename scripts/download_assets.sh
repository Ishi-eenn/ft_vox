#!/usr/bin/env bash
# download_assets.sh
#
# 使い方:
#   bash scripts/download_assets.sh
#
# URL の拡張子は自動判定（ogg / mp3 / wav / flac すべてOK）。
# "" のままにすれば無音 WAV で自動補完。

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# =============================================================================
# ここに URL を書く（不要なら "" のままでOK）
# =============================================================================

# ── BGM ──────────────────────────────────────────────────────────────────────
URL_MUSIC_PLAINS="https://opengameart.org/sites/default/files/old%20city%20theme_3.ogg"
URL_MUSIC_DESERT=""
URL_MUSIC_TUNDRA=""
URL_MUSIC_ROCKY=""
URL_MUSIC_SWAMP="https://opengameart.org/sites/default/files/old%20city%20theme_3.ogg"
URL_MUSIC_MOUNTAIN=""
URL_MUSIC_CANYON=""
URL_MUSIC_SPRING=""
URL_MUSIC_AUTUMN=""

# ── アンビエント ──────────────────────────────────────────────────────────────
URL_AMBIENT_PLAINS=""
URL_AMBIENT_DESERT=""
URL_AMBIENT_TUNDRA=""
URL_AMBIENT_ROCKY=""
URL_AMBIENT_SWAMP=""
URL_AMBIENT_MOUNTAIN=""
URL_AMBIENT_CANYON=""
URL_AMBIENT_SPRING=""
URL_AMBIENT_AUTUMN=""

# ── SE（効果音） ──────────────────────────────────────────────────────────────
URL_SE_FOOTSTEP_GRASS=""
URL_SE_FOOTSTEP_STONE=""
URL_SE_FOOTSTEP_SAND=""
URL_SE_FOOTSTEP_SNOW=""
URL_SE_FOOTSTEP_WOOD=""
URL_SE_ATTACK=""
URL_SE_HURT=""
URL_SE_SWIM=""
URL_SE_BLOCK_BREAK=""
URL_SE_BLOCK_PLACE=""
URL_SE_MOB_GROAN=""
URL_SE_MOB_HURT=""
URL_SE_MOB_EXPLODE=""

# =============================================================================
# 以下は変更不要
# =============================================================================

mkdir -p \
  "$REPO_ROOT/assets/music" \
  "$REPO_ROOT/assets/sounds/ambient" \
  "$REPO_ROOT/assets/sounds"

# URL の末尾から拡張子を取り出す（クエリ文字列を除去してから）
url_ext() {
  local url="$1"
  # クエリ文字列・フラグメントを除去してから拡張子を取得
  local path="${url%%\?*}"
  path="${path%%\#*}"
  echo "${path##*.}"
}

# stem: 拡張子なしのファイルパス（例: assets/music/plains_bgm）
# URL の拡張子でファイルを保存。stem.* がすでに存在すればスキップ。
fetch() {
  local url="$1" stem="$2"
  [[ -z "$url" ]] && return 0

  # 本物のファイルがすでにある（マーカーなし）ならスキップ
  if [[ ! -f "${stem}.placeholder" ]]; then
    for f in "${stem}".ogg "${stem}".mp3 "${stem}".wav "${stem}".flac; do
      [[ -f "$f" ]] && return 0
    done
  fi

  local ext
  ext="$(url_ext "$url")"
  local dst="${stem}.${ext}"
  echo "  downloading $(basename "$dst") ..."
  if command -v curl &>/dev/null; then
    curl -fsSL -o "$dst" "$url"
  else
    wget -q -O "$dst" "$url"
  fi

  # ダウンロード成功 → プレースホルダーを削除
  rm -f "${stem}.placeholder" "${stem}.wav"
}

# BGM
fetch "$URL_MUSIC_PLAINS" "$REPO_ROOT/assets/music/plains_bgm"
fetch "$URL_MUSIC_DESERT" "$REPO_ROOT/assets/music/desert_bgm"
fetch "$URL_MUSIC_TUNDRA" "$REPO_ROOT/assets/music/tundra_bgm"
fetch "$URL_MUSIC_ROCKY" "$REPO_ROOT/assets/music/rocky_bgm"
fetch "$URL_MUSIC_SWAMP" "$REPO_ROOT/assets/music/swamp_bgm"
fetch "$URL_MUSIC_MOUNTAIN" "$REPO_ROOT/assets/music/mountain_bgm"
fetch "$URL_MUSIC_CANYON" "$REPO_ROOT/assets/music/canyon_bgm"
fetch "$URL_MUSIC_SPRING" "$REPO_ROOT/assets/music/spring_bgm"
fetch "$URL_MUSIC_AUTUMN" "$REPO_ROOT/assets/music/autumn_bgm"

# Ambient
fetch "$URL_AMBIENT_PLAINS" "$REPO_ROOT/assets/sounds/ambient/plains"
fetch "$URL_AMBIENT_DESERT" "$REPO_ROOT/assets/sounds/ambient/desert"
fetch "$URL_AMBIENT_TUNDRA" "$REPO_ROOT/assets/sounds/ambient/tundra"
fetch "$URL_AMBIENT_ROCKY" "$REPO_ROOT/assets/sounds/ambient/rocky"
fetch "$URL_AMBIENT_SWAMP" "$REPO_ROOT/assets/sounds/ambient/swamp"
fetch "$URL_AMBIENT_MOUNTAIN" "$REPO_ROOT/assets/sounds/ambient/mountain"
fetch "$URL_AMBIENT_CANYON" "$REPO_ROOT/assets/sounds/ambient/canyon"
fetch "$URL_AMBIENT_SPRING" "$REPO_ROOT/assets/sounds/ambient/spring"
fetch "$URL_AMBIENT_AUTUMN" "$REPO_ROOT/assets/sounds/ambient/autumn"

# SE
fetch "$URL_SE_FOOTSTEP_GRASS" "$REPO_ROOT/assets/sounds/footstep_grass"
fetch "$URL_SE_FOOTSTEP_STONE" "$REPO_ROOT/assets/sounds/footstep_stone"
fetch "$URL_SE_FOOTSTEP_SAND" "$REPO_ROOT/assets/sounds/footstep_sand"
fetch "$URL_SE_FOOTSTEP_SNOW" "$REPO_ROOT/assets/sounds/footstep_snow"
fetch "$URL_SE_FOOTSTEP_WOOD" "$REPO_ROOT/assets/sounds/footstep_wood"
fetch "$URL_SE_ATTACK" "$REPO_ROOT/assets/sounds/attack"
fetch "$URL_SE_HURT" "$REPO_ROOT/assets/sounds/hurt"
fetch "$URL_SE_SWIM" "$REPO_ROOT/assets/sounds/swim"
fetch "$URL_SE_BLOCK_BREAK" "$REPO_ROOT/assets/sounds/block_break"
fetch "$URL_SE_BLOCK_PLACE" "$REPO_ROOT/assets/sounds/block_place"
fetch "$URL_SE_MOB_GROAN" "$REPO_ROOT/assets/sounds/mob_groan"
fetch "$URL_SE_MOB_HURT" "$REPO_ROOT/assets/sounds/mob_hurt"
fetch "$URL_SE_MOB_EXPLODE" "$REPO_ROOT/assets/sounds/mob_explode"

# 未ダウンロードのファイルは無音 WAV で補完
echo ""
echo "Generating placeholders for missing files..."
ASSETS="$REPO_ROOT/assets" bash "$REPO_ROOT/scripts/gen_placeholder_assets.sh"

echo ""
echo "Done."
