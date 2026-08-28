//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_FRAMEWORK_INCLUDE_COMMON_CACHE_LRU_CACHE_H
#define QIFENG_FRAMEWORK_INCLUDE_COMMON_CACHE_LRU_CACHE_H

#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace common {
    namespace cache {

        // 提供两种锁能力（互斥锁、读写锁）
        struct ExclusivePolicy {
            template <typename Mutex>
            using ReadGuard = std::lock_guard<Mutex>;

            template <typename Mutex>
            using WriteGuard = std::lock_guard<Mutex>;
        };

        struct SharedPolicy {
            template <typename Mutex>
            using ReadGuard = std::shared_lock<Mutex>;

            template <typename Mutex>
            using WriteGuard = std::unique_lock<Mutex>;
        };

        // 默认采用读写锁
        template <typename Key, typename Value, typename Mutex = std::shared_mutex, typename LockPolicy = SharedPolicy>
        class LRUCache {
            // 从 Policy 萃取两种 Guard 的别名
            template <typename M>
            using ReadGuard = typename LockPolicy::template ReadGuard<M>;

            template <typename M>
            using WriteGuard = typename LockPolicy::template WriteGuard<M>;

        public:
            explicit LRUCache(size_t maxSize = 1024) : mMaxSize(maxSize) {
                mMap.reserve(mMaxSize);
            }

            LRUCache(const LRUCache&) = delete;
            LRUCache& operator=(const LRUCache&) = delete;
            LRUCache(LRUCache&&) = delete;
            LRUCache& operator=(LRUCache&&) = delete;
            ~LRUCache() = default;

            void SetEnabled(bool enabled) {
                mEnabled = enabled;
            }
            bool IsEnabled() const {
                return mEnabled;
            }

            void SetMaxSize(size_t maxSize) {
                WriteGuard<Mutex> lock(mMutex);
                mMaxSize = maxSize;
                EvictIfNeeded();
            }

            void Put(const Key& key, const Value& value) {
                if (!mEnabled) {
                    return;
                }
                WriteGuard<Mutex> lock(mMutex);
                auto it = mMap.find(key);
                if (it != mMap.end()) {
                    it->second->second = value;
                    mList.splice(mList.begin(), mList, it->second);
                } else {
                    mList.emplace_front(key, value);
                    mMap[key] = mList.begin();
                    EvictIfNeeded();
                }
            }

            void Put(Key&& key, Value&& value) {
                if (!mEnabled) {
                    return;
                }
                WriteGuard<Mutex> lock(mMutex);
                auto it = mMap.find(key);
                if (it != mMap.end()) {
                    it->second->second = std::move(value);
                    mList.splice(mList.begin(), mList, it->second);
                } else {
                    mList.emplace_front(std::move(key), std::move(value));
                    mMap[mList.front().first] = mList.begin();
                    EvictIfNeeded();
                }
            }

            void Remove(const Key& key) {
                if (!mEnabled) {
                    return;
                }
                WriteGuard<Mutex> lock(mMutex);
                auto it = mMap.find(key);
                if (it != mMap.end()) {
                    mList.erase(it->second);
                    mMap.erase(it);
                }
            }

            void Clear() {
                WriteGuard<Mutex> lock(mMutex);
                mList.clear();
                mMap.clear();
            }

            size_t Size() const {
                if (!mEnabled) {
                    return 0;
                }
                ReadGuard<Mutex> lock(mMutex);
                return mMap.size();
            }

            size_t MaxSize() const {
                return mMaxSize;
            }

            // Get 会修改链表顺序（LRU 更新），必须用写锁
            bool Get(const Key& key, Value& outValue) {
                if (!mEnabled) {
                    return false;
                }
                WriteGuard<Mutex> lock(mMutex);
                auto it = mMap.find(key);
                if (it == mMap.end()) {
                    return false;
                }
                outValue = it->second->second;
                mList.splice(mList.begin(), mList, it->second);
                return true;
            }

            bool Contains(const Key& key) const {
                if (!mEnabled) {
                    return false;
                }
                ReadGuard<Mutex> lock(mMutex);
                return mMap.find(key) != mMap.end();
            }

        private:
            using KeyValuePair = std::pair<Key, Value>;
            using ListIterator = typename std::list<KeyValuePair>::iterator;

            void EvictIfNeeded() {  // 调用方已持有写锁
                while (mMap.size() > mMaxSize) {
                    mMap.erase(std::prev(mList.end())->first);
                    mList.pop_back();
                }
            }

            mutable Mutex mMutex;
            std::list<KeyValuePair> mList;
            std::unordered_map<Key, ListIterator> mMap;
            size_t mMaxSize;
            bool mEnabled = true;
        };
    }  // namespace cache
}  // namespace common

#endif  // QIFENG_FRAMEWORK_INCLUDE_COMMON_CACHE_LRU_CACHE_H
