#pragma once
#include <cstdint>

class NoiseGen {
public:
    NoiseGen();
    ~NoiseGen();

    void setSeed(uint32_t seed);

    // 2D FBm Perlin — base terrain height [-1, 1]
    float getHeight(float x, float z) const;

    // 2D Ridged — valley / erosion mask [0, ~1] (high = deep valley)
    float getValley(float x, float z) const;

    // 3D Perlin — cave carving [-1, 1]
    float getCave(float x, float y, float z) const;

    // 2D low-frequency Perlin — biome temperature [-1, 1]  (-1=cold, +1=hot)
    float getTemperature(float x, float z) const;

    // 2D low-frequency Perlin — biome humidity [-1, 1]  (-1=dry, +1=wet)
    float getHumidity(float x, float z) const;

private:
    void* height_noise_ = nullptr;
    void* valley_noise_ = nullptr;
    void* cave_noise_   = nullptr;
    void* temp_noise_   = nullptr;
    void* humid_noise_  = nullptr;
};
