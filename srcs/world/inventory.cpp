// ─────────────────────────────────────────────────────────────────────────────
// inventory.cpp — Inventory 操作の実装
// ─────────────────────────────────────────────────────────────────────────────
#include "inventory.hpp"

static int stackLimit(BlockType type) {
    return type == BlockType::Bow ? 1 : STACK_MAX;
}

void inventoryAdd(Inventory& inv, BlockType type, int n) {
    if (n <= 0) return;
    if (type == BlockType::Air || type == BlockType::Water ||
        type == BlockType::ShortGrass || type == BlockType::Flower ||
        type == BlockType::Mushroom)
        return;

    const int limit = stackLimit(type);

    // 既存スタックに積む
    for (int i = 0; i < HOTBAR_SIZE && n > 0; ++i) {
        if (inv.slots[i].type == type && inv.slots[i].count < limit) {
            int add = limit - inv.slots[i].count;
            if (add > n) add = n;
            inv.slots[i].count += add;
            n -= add;
        }
    }
    // 空きスロットを埋める
    for (int i = 0; i < HOTBAR_SIZE && n > 0; ++i) {
        if (inv.slots[i].type == BlockType::Air) {
            int add = n > limit ? limit : n;
            inv.slots[i] = {type, add};
            n -= add;
        }
    }
    // インベントリ満杯: 残りは捨てる
}

bool inventoryConsumeSelected(Inventory& inv) {
    ItemStack& s = inv.slots[inv.selected];
    if (s.type == BlockType::Air || s.count <= 0) return false;
    --s.count;
    if (s.count == 0) s = {};
    return true;
}

int inventoryCount(const Inventory& inv, BlockType type) {
    if (type == BlockType::Air) return 0;
    int sum = 0;
    for (int i = 0; i < HOTBAR_SIZE; ++i)
        if (inv.slots[i].type == type)
            sum += inv.slots[i].count;
    return sum;
}

bool inventoryConsume(Inventory& inv, BlockType type, int n) {
    if (n <= 0) return true;
    if (inventoryCount(inv, type) < n) return false;
    for (int i = 0; i < HOTBAR_SIZE && n > 0; ++i) {
        if (inv.slots[i].type != type) continue;
        int take = inv.slots[i].count < n ? inv.slots[i].count : n;
        inv.slots[i].count -= take;
        n -= take;
        if (inv.slots[i].count == 0) inv.slots[i] = {};
    }
    return true;
}
