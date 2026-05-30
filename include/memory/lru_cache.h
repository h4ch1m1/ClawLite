#pragma once

#include <cstddef>
#include <iterator>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace clawlite {

template <typename Key, typename Value>
class LruCache {
public:
    explicit LruCache(size_t capacity) : m_capacity(capacity) {}

    void put(const Key& key, const Value& value) {
        if (m_capacity == 0) return;
        auto it = m_index.find(key);
        if (it != m_index.end()) {
            it->second->second = value;
            m_items.splice(m_items.begin(), m_items, it->second);
            return;
        }

        m_items.emplace_front(key, value);
        m_index[key] = m_items.begin();
        if (m_index.size() > m_capacity) {
            auto last = std::prev(m_items.end());
            m_index.erase(last->first);
            m_items.pop_back();
        }
    }

    std::optional<Value> get(const Key& key) {
        auto it = m_index.find(key);
        if (it == m_index.end()) return std::nullopt;
        m_items.splice(m_items.begin(), m_items, it->second);
        return it->second->second;
    }

    bool contains(const Key& key) const {
        return m_index.find(key) != m_index.end();
    }

    size_t size() const { return m_index.size(); }
    size_t capacity() const { return m_capacity; }

private:
    size_t m_capacity;
    std::list<std::pair<Key, Value>> m_items;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> m_index;
};

} // namespace clawlite
