#pragma once
#include "types.hpp"
#include <cstdint>

struct AtlasUV {
    float u0, v0, u1, v1;
};

class TextureAtlas {
public:
    ~TextureAtlas();
    // Generate a procedural atlas (no PNG needed)
    bool generate();
    void bind(uint32_t slot = 0) const;
    void destroy();

    // Returns UV rect for given block type (all faces same tile for simplicity)
    AtlasUV getUV(BlockType type) const;

    uint32_t glId() const { return tex_id_; }

private:
    uint32_t tex_id_ = 0;
    int      cols_   = ATLAS_COLS;    // from constants
    int      rows_   = 4;             // enough for all block types
};
