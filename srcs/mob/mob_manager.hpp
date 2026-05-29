#pragma once
#include "mob/zombie.hpp"
#include <vector>
#include <functional>
#include <utility>

class World;

struct MobExplosion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 0.0f;
};

class MobManager {
public:
    // Update all mobs.
    // px/py/pz = player eye position (camera).
    // Returns total HP damage dealt to the player this frame.
    float update(float dt,
                 float px, float py, float pz,
                 float time_of_day,
                 const std::function<bool(int,int,int)>& isSolid,
                 const World& world);

    // Called when the player attacks (left-click + direction).
    // Deals damage to the closest mob within melee range in front of px/py/pz.
    // Returns the mob index that was hit, or -1.
    int playerMeleeAttack(float px, float py, float pz,
                          float front_x, float front_z);

    const std::vector<Zombie>& zombies() const { return zombies_; }
    std::vector<Zombie>&       zombiesMut() { return zombies_; }
    void setZombies(std::vector<Zombie> z) { zombies_ = std::move(z); }
    std::vector<MobExplosion> consumeExplosions();

private:
    void trySpawn(float px, float pz, const World& world, float time_of_day);
    float updateZombie(Zombie& z, float dt,
                       float px, float py, float pz,
                       const std::function<bool(int,int,int)>& isSolid);
    float updateCreeper(Zombie& z, float dt,
                        float px, float py, float pz,
                        const std::function<bool(int,int,int)>& isSolid);
    static bool overlapsAny(float x, float y, float z,
                             const std::function<bool(int,int,int)>& isSolid);
    static void applyMovement(Zombie& z, float dt, float move_x, float move_z,
                              const std::function<bool(int,int,int)>& isSolid);
    int findGroundY(float x, float z, const World& world) const;

    std::vector<Zombie> zombies_;
    std::vector<MobExplosion> explosions_;
    float spawn_timer_ = 0.0f;

    static constexpr int   MAX_ZOMBIES     = 24;
    static constexpr float SPAWN_INTERVAL  = 5.0f;
};
