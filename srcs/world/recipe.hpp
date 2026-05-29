// ─────────────────────────────────────────────────────────────────────────────
// recipe.hpp — クラフトレシピシステム
//
// ・固定レシピ配列 kRecipes を提供する。
// ・canCraft() で在庫検査、applyCraft() で素材消費＋出力付与を行う。
// ・現状はクライアント完結（マルチプレイでの素材検証はサーバ側で持っていない）。
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "types.hpp"
#include <vector>

struct Ingredient {
    BlockType type;
    int       count;
};

struct Recipe {
    const char*             name;     // UI 表示名 (大文字 A-Z, スペース可)
    std::vector<Ingredient> inputs;
    ItemStack               output;
};

// クラフトメニューに表示する全レシピ。表示順 = 配列順。
const std::vector<Recipe>& getRecipes();

// inv が素材と出力先の空きを満たし、レシピを実行できるか判定。
bool canCraft(const Inventory& inv, const Recipe& r);

// 素材を消費して出力をインベントリに追加する。canCraft が true のときに呼ぶ。
// 在庫が足りない場合は何もせず false を返す。
bool applyCraft(Inventory& inv, const Recipe& r);
