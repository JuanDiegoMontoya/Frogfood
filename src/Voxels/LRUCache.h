#pragma once
#include <list>
#include <unordered_map>

template<typename K, typename V>
class LRUCache
{
public:
  LRUCache(size_t cacheSize = 100) : cacheSize_(cacheSize) { }

  V& set(const K& key, V value)
  {
    if (auto it = cache_.find(key); it == cache_.end())
    {
      items_.push_front(key);
      if (cache_.size() > cacheSize_)
      {
        cache_.erase(items_.back());
        items_.pop_back();
      }
    }
    else
    {
      // Move node to front via pointer swap. Crucially, do not invalidate iterators in the cache.
      items_.splice(items_.begin(), items_, it->second.second);
    }

    return cache_.insert_or_assign(key, std::make_pair(std::move(value), items_.begin())).first->second.first;
  }

  // Returned pointer is not stable.
  // TODO: return shared_ptr?
  V* get(const K& key)
  {
    auto it = cache_.find(key);
    if (it == cache_.end())
    {
      return nullptr;
    }
    items_.splice(items_.begin(), items_, it->second.second);
    return &it->second.first;
  }

  // Invalidates the cache.
  void clear()
  {
    items_.clear();
    cache_.clear();
  }

private:
  // Stores keys in most- to least-recently used order.
  std::list<K> items_;
  std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>> cache_;
  size_t cacheSize_;
};
