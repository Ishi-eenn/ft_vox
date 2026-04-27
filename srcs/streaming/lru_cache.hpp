#pragma once
#include <unordered_map>
#include <list>
#include <vector>
#include <functional>

// Fixed-capacity LRU cache.
// On overflow, evicts the least-recently-used entry.
// K must be hashable. Hash type defaults to std::hash<K>.
template<typename K, typename V, typename Hash = std::hash<K>>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // Insert or update. Returns list of evicted keys (usually 0 or 1).
    std::vector<K> put(const K& key, V value) {
        std::vector<K> evicted;
        auto it = map_.find(key);
        if (it != map_.end()) {
            // Update: move to front
            order_.erase(it->second.list_it);
            order_.push_front(key);
            it->second.list_it = order_.begin();
            it->second.value   = std::move(value);
            return evicted;
        }
        // New entry
        if (map_.size() >= capacity_) {
            K lru_key = order_.back();
            order_.pop_back();
            map_.erase(lru_key);
            evicted.push_back(lru_key);
        }
        order_.push_front(key);
        map_[key] = {std::move(value), order_.begin()};
        return evicted;
    }

    // Touch (mark recently used). Returns false if not found.
    bool touch(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        order_.erase(it->second.list_it);
        order_.push_front(key);
        it->second.list_it = order_.begin();
        return true;
    }

    // Get value (also touches). Returns nullptr if not found.
    V* get(const K& key) {
        if (!touch(key)) return nullptr;
        return &map_.at(key).value;
    }

    bool contains(const K& key) const { return map_.count(key) > 0; }
    size_t size()     const { return map_.size(); }
    size_t capacity() const { return capacity_; }

    // Evict least-recently-used entry and return its key+value.
    // Returns false if cache is empty.
    bool evictLRU(K& out_key, V& out_val) {
        if (order_.empty()) return false;
        out_key = order_.back();
        out_val = std::move(map_.at(out_key).value);
        map_.erase(out_key);
        order_.pop_back();
        return true;
    }

    // Iterate all entries (unordered). Fn signature: void(const K&, const V&)
    template<typename Fn>
    void forEach(Fn fn) const {
        for (const auto& kv : map_) fn(kv.first, kv.second.value);
    }

private:
    struct Entry {
        V value;
        typename std::list<K>::iterator list_it;
    };
    size_t                             capacity_;
    std::list<K>                       order_;   // front = MRU, back = LRU
    std::unordered_map<K, Entry, Hash> map_;
};
