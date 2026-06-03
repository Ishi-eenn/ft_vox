#!/usr/bin/env bash
# gen_placeholder_assets.sh
#
# 開発用: 無音の WAV プレースホルダーを生成する。
# 生成したファイルには stem.placeholder マーカーを置く。
# マーカーがあるファイルは download_assets.sh が URL 設定時に上書きする。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="${ASSETS:-$REPO_ROOT/assets}"

python3 - "$ASSETS" <<'PYEOF'
import struct, os, sys

assets = sys.argv[1]

def make_wav(stem, duration_s, sample_rate=44100, channels=1):
    os.makedirs(os.path.dirname(stem), exist_ok=True)

    # 本物のファイルがあればスキップ（マーカーなし＝本物）
    for ext in ('.ogg', '.mp3', '.wav', '.flac'):
        candidate = stem + ext
        if os.path.exists(candidate) and not os.path.exists(stem + '.placeholder'):
            return

    wav_path = stem + '.wav'
    num_samples = int(sample_rate * duration_s * channels)
    pcm = bytes(num_samples * 2)
    with open(wav_path, 'wb') as f:
        data_size = len(pcm)
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + data_size))
        f.write(b'WAVE')
        f.write(b'fmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, channels, sample_rate,
                            sample_rate * channels * 2, channels * 2, 16))
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(pcm)

    # マーカーを置く（download_assets.sh が上書き対象と判断するため）
    open(stem + '.placeholder', 'w').close()
    print(f"  placeholder  {os.path.relpath(wav_path)}")

biomes = ["plains","desert","tundra","rocky","swamp","mountain","canyon","spring","autumn"]
se_files = [
    "footstep_grass","footstep_stone","footstep_sand","footstep_snow","footstep_wood",
    "attack","hurt","swim","block_break","block_place",
    "mob_groan","mob_hurt","mob_explode",
]

print("\n[BGM]")
for b in biomes:
    make_wav(f"{assets}/music/{b}_bgm", 1.0)

print("\n[Ambient]")
for b in biomes:
    make_wav(f"{assets}/sounds/ambient/{b}", 1.0)

print("\n[SE]")
for s in se_files:
    make_wav(f"{assets}/sounds/{s}", 0.3)

print("\nDone.")
PYEOF
