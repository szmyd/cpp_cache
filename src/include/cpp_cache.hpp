#pragma once

#include <functional>
#include <list>
#include <shared_mutex>
#include <unordered_map>

namespace cpp_cache
{

// Standard entry of size 1
template<typename ValueType>
struct size_calc {
   size_t operator()(ValueType const&) const
   { return 1; }
};

template<typename K, typename V>
struct eviction_strategy {
   using dirty_type = bool;
   using key_type = K;
   using entry_type = std::tuple<key_type, V, dirty_type>;
   using eviction_type = std::list<entry_type>;
   using size_type = std::size_t;
   using erase_map = std::function<void(key_type const&)>;

   // Functor for calculating the size of a value type
   size_calc<V> calc_size;

   explicit eviction_strategy(size_type const _max_size) :
         max_size(_max_size)
   { }

protected:
   size_type current_size {0};
   size_type max_size;

   virtual void clear_() = 0;

   virtual void _erase(key_type const& key) = 0;
   virtual typename eviction_type::iterator push_(entry_type&&) = 0;

   virtual void fit_(erase_map&& c) = 0;

   virtual void remove_(typename eviction_type::iterator& it) = 0;

   virtual void touch_(typename eviction_type::iterator const& entry) = 0;
};

/**
 * An abstract interface for a generic key-value cache. The cache
 * takes a basic size, either in number of element, and caches elements
 * using a specified eviction algorithm.
 *
 * This class IS thread safe
 */
template<class K, class V, class _Evict, class _Hash = std::hash<K>>
struct cache : public _Evict {
   using dirty_type = typename _Evict::dirty_type;
   using key_type = K;
   using mapped_type = V;
   using hash_type = _Hash;
   using size_type = typename _Evict::size_type;
   using value_type = mapped_type;

   // Backing data structure that implements key to value associated lookup.
   using iterator = typename _Evict::eviction_type::iterator;
   using index_type = typename std::unordered_map<key_type, iterator, hash_type>;

public:
   /**
    * Constructs the cache object but does not init
    * @param[in] modName     Name of this module
    * @param[in] _max_size   "Size" of the cache (term is implied by size_calc)
    *
    * @return none
    */
   explicit cache(size_type const _max_size = 0, bool const strong_assoc = false) :
         _Evict(_max_size),
         cache_map(),
         cache_lock(),
         _strong(strong_assoc) { }

   ~cache() = default;
   cache(cache const&) = delete;
   cache& operator=(cache const&) = delete;

   /**
    * Adds a key-value pair to the cache.
    * The cache will take ownership of value pointers added to
    * the cache. That is, the pointer cannot be deleted by
    * whoever passes it in. The cache will become responsible for that.
    * If the key already exists, it will be overwritten.
    * If the cache is full, the evicted entry will be returned.
    * Entries that are evicted are released.
    *
    * @param[in] key   Key to use for indexing
    * @param[in] value Associated value
    *
    * @return void
    */
   void add(const key_type& key, const value_type value, const dirty_type dirty = false) {
       std::unique_lock<std::shared_mutex> l(cache_lock);

       // Remove old value before adding current
       if (!_strong && !remove(key, dirty)) {
           return;
       }

       add_(key, value, dirty);
   }

   void merge(cache<key_type, mapped_type, hash_type> const& other) {
       std::unique_lock<std::shared_mutex> l(cache_lock);
       std::shared_lock<std::shared_mutex> sl(other.cache_lock);

       for (auto const& element : other.eviction_list) {
           // Remove old value before adding current
           if (!_strong && !remove(std::get<0>(element), std::get<2>(element))) {
               continue;
           }
           add_(std::get<0>(element), std::get<1>(element), std::get<2>(element));
       }
   }


   /**
    * Adds a key-value pair to the cache.
    * The cache will take ownership of value pointers added to
    * the cache. That is, the pointer cannot be deleted by
    * whoever passes it in. The cache will become responsible for that.
    * If the key already exists, it will be overwritten.
    * If the cache is full, the evicted entry will be returned.
    * Entries that are evicted are released.
    *
    * @param[in] key   Key to use for indexing
    * @param[in] value Associated value
    *
    * @return void
    */
   void merge(const key_type& key, const value_type value, const dirty_type dirty = false) {
       std::unique_lock<std::shared_mutex> l(cache_lock);

       if (_strong) {
           add_(key, value, dirty);
           return;
       }

       // Find existing value
       auto mapIt = cache_map.find(key);
       if (mapIt == cache_map.end()) {
           add_(key, value, dirty);
           return;
       }

       auto& old_value = std::get<1>(*mapIt->second);

       // Merge the two values together and determine a new "size"
       auto const old_size = calc_size(old_value);
       old_value->merge(*value);
       // Move the entry's position to the front
       // of the eviction list.
       touch_(mapIt->second);
       auto new_size = calc_size(old_value);

       if (0 < _Evict::max_size && _Evict::max_size < new_size) {
           old_value->trim(_Evict::max_size);
           new_size = _Evict::max_size;
       }

       if (old_size < new_size) {
           _Evict::current_size += (new_size - old_size);
       } else if (old_size > new_size) {
           _Evict::current_size -= (old_size - new_size);
       }

       _Evict::fit_([this](key_type const& k){ cache_map.erase(k); });
   }

   /**
    * Resize the cache to a new max_size
    */
   void resize(size_t const _new_max) {
       std::unique_lock<std::shared_mutex> l(cache_lock);
       _Evict::max_size = _new_max;
       _Evict::fit_([this](key_type const& k){ cache_map.erase(k); });
   }

   /**
    * Removes all keys and values from the cache line
    *
    * @return none
    */
   void clear() {
       std::unique_lock<std::shared_mutex> l(cache_lock);
       // Clear all of the containers
       _Evict::clear_();
       cache_map.clear();
       _Evict::current_size = 0;
   }

   /**
    * Returns the value for the assoicated key. When a value
    * is returned, the cache RETAINS ownership of pointer and
    * it cannot be freed by the caller.
    * When a value is return, the entry's access is updated.
    *
    * @param[in]  key      Key to use for indexing
    * @para[in]   do_touch  Whether to track this access
    *
    * @return optional value if element found
    */
   int get(const key_type &key,
           value_type& ret,
           bool const do_touch = true) {
       std::unique_lock<std::shared_mutex> l(cache_lock);
       typename _Evict::eviction_type::iterator elemIt;

       // Touch the entry in the list if we want
       // to track this access
       if (do_touch == true) {
           bool had {false};;
           std::tie(elemIt, had) = touch(key);
           if (!had)
              return -1;
       } else {
           auto mapIt = cache_map.find(key);
           if (mapIt == cache_map.end())
               return -1;
           else
               elemIt = mapIt->second;
       }

       // Set the return value
       ret = std::get<1>(*elemIt);
       return 0;
   }

   /**
    * Returns the values that have a match with a given predicate
    *
    * When a value is return, the entry's access is updated.
    *
    * @param[in]  key         Key to use for indexing
    * @param[out] values_out  List of pointers to values
    * @para[in]   do_touch    Whether to track this access
    *
    * @return void
    */
   template<typename FuncType>
   void get_all(FuncType func) {
       std::shared_lock<std::shared_mutex> sl(cache_lock);
       // Remove all elements who match the predicate
       for (auto cur = cache_map.begin(); cache_map.end() != cur; ++cur) {
           func(cur->first, std::get<1>(*cur->second));
       }
   }

   /**
    * Removes a key and value from the cache. Thread safe.
    *
    * @param[in] key   Key to use for indexing
    *
    * @return none
    */
   void remove(const key_type &key) {
       std::unique_lock<std::shared_mutex> l(cache_lock);
       remove(key, false);
   }

   /**
    * Checks if a key exists in the cache
    *
    * @param[in]  key  Key to use for indexing
    *
    * @return true if key exists, false otherwise
    */
   bool exists(const K &key) const {
       std::shared_lock<std::shared_mutex> sl(cache_lock);
       return (cache_map.find(key) != cache_map.end());
   }

   /**
    * Returns the current number size the cache
    *
    * @return cache size
    */
   size_type size() const {
       std::shared_lock<std::shared_mutex> sl(cache_lock);
       return _Evict::current_size;
   }

   bool empty() const { return 0 == size(); }

protected:
   // K->V association lookup
   index_type cache_map;

   // Synchronization members
   mutable std::shared_mutex   cache_lock;

   // Is the key<->value association strong?
   bool _strong {false};

   void add_(const key_type& key, const value_type value, const dirty_type dirty) {
       if (_strong) {
           // Touch any exiting entry from cache, if returns
           // non-NULL then we already have this value, return
           if (!touch(key).second)
               return;
       }

      // Check if anything needs to be evicted
      _Evict::current_size += _Evict::calc_size(value);

      _Evict::fit_([this](key_type const& k){ cache_map.erase(k); });

       // Add the entry to the front of the eviction list and into the map.
       cache_map[key] = _Evict::push_(std::make_tuple(key, value, dirty));
   }

   /**
     * Internal function to remove a key and value
     * from the cache.
     *
     * @param[in] key   Key to use for indexing
     *
     * @return none
     */
   bool remove(key_type const& key, dirty_type const dirty) {
       // Locate the key in the map
       auto mapIt = cache_map.find(key);
       if (mapIt != cache_map.end()) {
           auto cacheEntry = mapIt->second;

           // Only remove if the new element is not dirty or the existing is
           if (dirty && !std::get<2>(*cacheEntry)) {
               return false;
           }
           _Evict::current_size -= _Evict::calc_size(std::get<1>(*cacheEntry));

           // Remove from the cache_map
           cache_map.erase(mapIt);
           // Remove from the eviction_list
           _Evict::remove_(cacheEntry);
       }
       return true;
   }

   void _erase(key_type const& key) override {
      cache_map.erase(key);
   }

   /**
    * Touches a key for the purpose of representing an access.
    *
    * @param[in]  key  Key to use for indexing
    *
    * @return ERR_OK if a value is returned, ERR_NOT_FOUND otherwise.
    */
    auto touch(const key_type &key) {
       auto mapIt = cache_map.find(key);
       if (mapIt == cache_map.end())
          return std::make_pair(typename _Evict::eviction_type::iterator(), false);

       auto existingEntry = mapIt->second;

       _Evict::touch_(existingEntry);
       return std::make_pair(existingEntry, true);
   }
};

template<typename K, typename V>
struct lru_eviction : eviction_strategy<K, V> {
   using strategy = eviction_strategy<K, V>;
   using entry_type = typename strategy::entry_type;
   using eviction_type = typename strategy::eviction_type;

   explicit lru_eviction(typename strategy::size_type const _max_size) :
      strategy(_max_size),
      eviction_list()
   { }

protected:
   void clear_() override {
      eviction_list.clear();
   }

   void fit_(typename strategy::erase_map&& c) override {
      while (0 < strategy::max_size && strategy::current_size > strategy::max_size) {
         entry_type evicted = eviction_list.back();
         eviction_list.pop_back();

         auto entryToEvict = std::get<1>(evicted);

         // Remove the cache iterator entry from the map
         c(std::get<0>(evicted));

         strategy::current_size -= strategy::calc_size(entryToEvict);
      }

   }

   typename eviction_type::iterator push_(entry_type&& e) override {
      eviction_list.emplace_front(std::move(e));
      return eviction_list.begin();
   }

   void remove_(typename eviction_type::iterator& it) override {
      eviction_list.erase(it);
   }

   void touch_(typename eviction_type::iterator const& entry) override {
      // Move the entry's position to the front
      // of the eviction list.
      eviction_list.splice(eviction_list.begin(),
                           eviction_list,
                           entry);
   }

private:
   eviction_type eviction_list;
};

template<class K, class V, class _Hash = std::hash<K>>
using lru_cache = cache<K, V, lru_eviction<K, V>, _Hash>;

template<typename K, typename V>
struct mru_eviction : eviction_strategy<K, V> {
   using strategy = eviction_strategy<K, V>;
   using entry_type = typename strategy::entry_type;
   using eviction_type = typename strategy::eviction_type;

   explicit mru_eviction(typename strategy::size_type const _max_size) :
         strategy(_max_size),
         eviction_list()
   { }

protected:
   void clear_() override {
      eviction_list.clear();
   }

   void fit_(typename strategy::erase_map&& c) override {
      while (0 < strategy::max_size && strategy::current_size > strategy::max_size) {
         entry_type evicted = eviction_list.back();
         eviction_list.pop_back();

         auto entryToEvict = std::get<1>(evicted);

         // Remove the cache iterator entry from the map
         c(std::get<0>(evicted));

         strategy::current_size -= strategy::calc_size(entryToEvict);
      }

   }

   typename eviction_type::iterator push_(entry_type&& e) override {
      eviction_list.emplace_back(std::move(e));
      return --eviction_list.end();
   }

   void remove_(typename eviction_type::iterator& it) override {
      eviction_list.erase(it);
   }

   void touch_(typename eviction_type::iterator const& entry) override {
      // Move the entry's position to the back
      // of the eviction list.
      eviction_list.splice(eviction_list.end(),
                           eviction_list,
                           entry);
   }

private:
   eviction_type eviction_list;
};

template<class K, class V, class _Hash = std::hash<K>>
using mru_cache = cache<K, V, mru_eviction<K, V>, _Hash>;
}  // namespace cpp_cache
