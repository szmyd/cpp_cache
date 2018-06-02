//
// Created by brian.szmyd on 6/2/18.
//

#include "cpp_cache.hpp"

#include <GUnit.h>

GTEST("Basic Insertion") {
   int const test_value {15}, test_key {0}, max_size {1024};
   cpp_cache::lru_cache<int, int> cache(max_size);

   SHOULD("Return value previously added") {
      cache.add(test_key, test_value);
      int value {0};
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
   }

   SHOULD("Update value on re-insertion") {
      cache.add(test_key, test_value);
      int value {0};
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
      cache.add(test_key, test_value + 1);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value + 1, value);
   }

   SHOULD("Allow adding many entries") {
      int value{0};
      for (auto i = test_key; max_size > i; ++i) {
         cache.add(i, test_value + i);
         EXPECT_EQ(0, cache.get(i, value));
         EXPECT_EQ(test_value + i, value);
         if (test_key != i) {
            EXPECT_EQ(0, cache.get(i - 1, value));
            EXPECT_EQ(test_value + i - 1, value);
         }
      }
   }

   SHOULD("Enforce the max_size limits") {
      int value{0};
      for (auto i = test_key; max_size > i; ++i) {
         cache.add(i, test_value + i);
         EXPECT_EQ(0, cache.get(i, value));
         EXPECT_EQ(test_value + i, value);
      }
      EXPECT_EQ(0, cache.get(test_key, value, false)); // do not update eviction line
      EXPECT_EQ(test_value, value);
      cache.add(test_key - 1, test_value - 1);
      EXPECT_EQ(-1, cache.get(test_key, value));
   }
}

GTEST("Basic Removal") {
   int const test_value {15}, test_key {5};
   cpp_cache::lru_cache<int, int> cache;

   SHOULD("Return not fail removal on miss") {
      int value {0};
      EXPECT_EQ(-1, cache.get(test_key, value));
      cache.remove(test_key);
      EXPECT_EQ(-1, cache.get(test_key, value));
      EXPECT_EQ(0, value);
   }

   SHOULD("Properly remove on hit") {
      int value{0};
      cache.add(test_key, test_value);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
      value = 0;
      cache.remove(test_key);
      EXPECT_EQ(-1, cache.get(test_key, value));
      EXPECT_EQ(0, value);
   }
}

GTEST("LRU Semantics") {
   int const test_value {15}, test_key {0}, max_size {1024};
   cpp_cache::lru_cache<int, int> cache(max_size);

   SHOULD("Evict the Least Recently Accessed") {
      int value{0};
      for (auto i = test_key; max_size > i; ++i) {
         cache.add(i, test_value + i);
         EXPECT_EQ(0, cache.get(i, value));
         EXPECT_EQ(test_value + i, value);
      }
      EXPECT_EQ(0, cache.get(test_key, value)); // Update eviction line
      EXPECT_EQ(test_value, value);
      cache.add(test_key - 1, test_value - 1);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
   }
}

namespace cpp_cache {
template<>
struct size_calc<std::string> {
   size_t operator()(std::string const& s) const
   { return s.length(); }
};
}

GTEST("Customization") {
   int test_key {0}, max_size {19};
   std::string const test_value_1("0123456789");
   //std::string const test_value_2("0123456789");
   //std::string const test_value_3("0123456789");
   cpp_cache::lru_cache<int, std::string> cache(max_size);

   SHOULD("Use customized size_calc template") {
      std::string value;
      cache.add(test_key, test_value_1);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value_1, value);
      EXPECT_EQ(test_value_1.length(), cache.size());
   }
}

GTEST("Dirty Entries") {
   int const test_value {15}, test_key {0};
   cpp_cache::lru_cache<int, int> cache;

   SHOULD("Dirty additions leave original value") {
      cache.add(test_key, test_value, false);
      int value {0};
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
      cache.add(test_key, test_value + 1, true);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value, value);
      cache.add(test_key, test_value - 1, false);
      EXPECT_EQ(0, cache.get(test_key, value));
      EXPECT_EQ(test_value - 1, value);
   }
}

