// ─────────────────────────────────────────────────────────────────────────────
// recipe.cpp — クラフトレシピデータと検査/適用ロジック
// ─────────────────────────────────────────────────────────────────────────────
#include "recipe.hpp"
#include "inventory.hpp"

const std::vector<Recipe>& getRecipes() {
    static const std::vector<Recipe> kRecipes = {
        {
            "STICK",
            { {BlockType::Wood,  1} },
            { BlockType::Stick, 4 },
        },
        {
            "TORCH",
            { {BlockType::Stick, 1}, {BlockType::Wood, 1} },
            { BlockType::Torch, 4 },
        },
        {
            "BOW",
            { {BlockType::Stick, 3}, {BlockType::Wood, 2} },
            { BlockType::Bow, 1 },
        },
    };
    return kRecipes;
}

static int stackLimit(BlockType type) {
    return type == BlockType::Bow ? 1 : STACK_MAX;
}

static bool canFitOutput(const Inventory& inv, const ItemStack& out) {
    if (out.type == BlockType::Air || out.count <= 0) return false;

    int remaining = out.count;
    const int limit = stackLimit(out.type);
    for (int i = 0; i < HOTBAR_SIZE && remaining > 0; ++i) {
        const ItemStack& s = inv.slots[i];
        if (s.type == out.type && s.count < limit) {
            int room = limit - s.count;
            remaining -= room;
        }
    }
    for (int i = 0; i < HOTBAR_SIZE && remaining > 0; ++i) {
        if (inv.slots[i].type == BlockType::Air)
            remaining -= limit;
    }
    return remaining <= 0;
}

static bool hasIngredients(const Inventory& inv, const Recipe& r) {
    for (const Ingredient& in : r.inputs)
        if (inventoryCount(inv, in.type) < in.count)
            return false;
    return true;
}

bool canCraft(const Inventory& inv, const Recipe& r) {
    if (!hasIngredients(inv, r)) return false;
    Inventory next = inv;
    for (const Ingredient& in : r.inputs)
        inventoryConsume(next, in.type, in.count);
    return canFitOutput(next, r.output);
}

bool applyCraft(Inventory& inv, const Recipe& r) {
    if (!hasIngredients(inv, r)) return false;
    Inventory next = inv;
    for (const Ingredient& in : r.inputs)
        inventoryConsume(next, in.type, in.count);
    if (!canFitOutput(next, r.output)) return false;
    inventoryAdd(next, r.output.type, r.output.count);
    inv = next;
    return true;
}
