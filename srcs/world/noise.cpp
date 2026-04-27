#include "world/noise.hpp"
#include "FastNoiseLite.h"
#include <cstdlib>

NoiseGen::NoiseGen() {
    height_noise_ = new FastNoiseLite();
    valley_noise_ = new FastNoiseLite();
    cave_noise_   = new FastNoiseLite();
}

NoiseGen::~NoiseGen() {
    delete (FastNoiseLite*)height_noise_;
    delete (FastNoiseLite*)valley_noise_;
    delete (FastNoiseLite*)cave_noise_;
}

void NoiseGen::setSeed(uint32_t seed) {
    auto* hn = (FastNoiseLite*)height_noise_;
    hn->SetSeed((int)seed);
    hn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    hn->SetFrequency(0.004f);
    hn->SetFractalType(FastNoiseLite::FractalType_FBm);
    hn->SetFractalOctaves(5);
    hn->SetFractalLacunarity(2.0f);
    hn->SetFractalGain(0.5f);

    auto* vn = (FastNoiseLite*)valley_noise_;
    vn->SetSeed((int)(seed ^ 0xCAFEBABEu));
    vn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    vn->SetFractalType(FastNoiseLite::FractalType_Ridged);
    vn->SetFrequency(0.007f);
    vn->SetFractalOctaves(3);
    vn->SetFractalLacunarity(2.0f);
    vn->SetFractalGain(0.5f);

    auto* cn = (FastNoiseLite*)cave_noise_;
    cn->SetSeed((int)(seed ^ 0xDEADBEEFu));
    cn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    cn->SetFrequency(0.05f);
}

float NoiseGen::getHeight(float x, float z) const {
    return ((FastNoiseLite*)height_noise_)->GetNoise(x, z);
}

float NoiseGen::getValley(float x, float z) const {
    return ((FastNoiseLite*)valley_noise_)->GetNoise(x, z);
}

float NoiseGen::getCave(float x, float y, float z) const {
    return ((FastNoiseLite*)cave_noise_)->GetNoise(x, y, z);
}
