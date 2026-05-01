#pragma once
#include "types.hpp"

class IWorld {
public:
    virtual ~IWorld() = default;
    virtual Chunk* getOrCreateChunk(ChunkPos pos) = 0;
    virtual void   setSeed(uint32_t seed)          = 0;
    virtual uint32_t getSeed() const               = 0;
    virtual std::vector<WorldPos> stepWater(ChunkPos min_chunk, ChunkPos max_chunk) = 0;

    // 非同期生成用: 空チャンクを確保してmapに挿入する（generate呼ばない）
    // 既に存在する場合は既存ポインタを返す。メインスレッドからのみ呼ぶこと。
    virtual Chunk* allocateChunk(ChunkPos pos) = 0;

    // 存在確認のみのルックアップ（生成しない）。メインスレッドからのみ呼ぶこと。
    virtual Chunk* findChunk(ChunkPos pos) const = 0;
};
