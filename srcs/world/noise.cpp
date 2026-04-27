#include "world/noise.hpp"
#include "FastNoiseLite.h"
#include <cstdlib>

NoiseGen::NoiseGen() {
    height_noise_ = new FastNoiseLite();
    valley_noise_ = new FastNoiseLite();
    cave_noise_   = new FastNoiseLite();
    temp_noise_   = new FastNoiseLite();
    humid_noise_  = new FastNoiseLite();
}

NoiseGen::~NoiseGen() {
    delete (FastNoiseLite*)height_noise_;
    delete (FastNoiseLite*)valley_noise_;
    delete (FastNoiseLite*)cave_noise_;
    delete (FastNoiseLite*)temp_noise_;
    delete (FastNoiseLite*)humid_noise_;
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

    // Temperature: very low frequency → large biome regions (~1250 blocks wide)
    auto* tn = (FastNoiseLite*)temp_noise_;
    tn->SetSeed((int)(seed ^ 0xABCD1234u));
    tn->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    tn->SetFrequency(0.0008f);
    tn->SetFractalType(FastNoiseLite::FractalType_FBm);
    tn->SetFractalOctaves(2);

    // Humidity: slightly different frequency to break grid alignment
    auto* hun = (FastNoiseLite*)humid_noise_;
    hun->SetSeed((int)(seed ^ 0x5678EFABu));
    hun->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    hun->SetFrequency(0.0011f);
    hun->SetFractalType(FastNoiseLite::FractalType_FBm);
    hun->SetFractalOctaves(2);
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

float NoiseGen::getTemperature(float x, float z) const {
    return ((FastNoiseLite*)temp_noise_)->GetNoise(x, z);
}

float NoiseGen::getHumidity(float x, float z) const {
    return ((FastNoiseLite*)humid_noise_)->GetNoise(x, z);
}
