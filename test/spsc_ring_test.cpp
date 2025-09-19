#include "common/spsc_ring.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

// Basic functionality tests
TEST(SpscRingTest, EmptyRing) {
  mini_mart::common::SpscRing<int, 16> ring;
  EXPECT_EQ(ring.size(), 0);
  EXPECT_TRUE(ring.empty());
  EXPECT_FALSE(ring.full());
}

TEST(SpscRingTest, Capacity) {
  mini_mart::common::SpscRing<int, 16> ring;
  EXPECT_EQ(ring.get_capacity(), 16);

  mini_mart::common::SpscRing<int, 8> small_ring;
  EXPECT_EQ(small_ring.get_capacity(), 8);
}

TEST(SpscRingTest, SinglePushPop) {
  mini_mart::common::SpscRing<int, 16> ring;

  // Test successful push
  bool success = ring.try_push(42);
  EXPECT_TRUE(success);
  EXPECT_EQ(ring.size(), 1);
  EXPECT_FALSE(ring.empty());
  EXPECT_FALSE(ring.full());

  // Test successful pop
  int value;
  success = ring.try_pop(value);
  EXPECT_TRUE(success);
  EXPECT_EQ(value, 42);
  EXPECT_EQ(ring.size(), 0);
  EXPECT_TRUE(ring.empty());
  EXPECT_FALSE(ring.full());
}

TEST(SpscRingTest, PopFromEmpty) {
  mini_mart::common::SpscRing<int, 16> ring;

  int value = 999; // Initialize with sentinel value
  bool success = ring.try_pop(value);
  EXPECT_FALSE(success);
  EXPECT_EQ(value, 999); // Value should be unchanged
  EXPECT_TRUE(ring.empty());
}

TEST(SpscRingTest, FillToCapacity) {
  mini_mart::common::SpscRing<int, 4> ring;

  for (int i = 0; i < 4; i++) {
    bool success = ring.try_push(i);
    EXPECT_TRUE(success);
    EXPECT_EQ(ring.size(), i + 1);
  }

  EXPECT_TRUE(ring.full());
  EXPECT_FALSE(ring.empty());
  EXPECT_EQ(ring.size(), 4);
}

TEST(SpscRingTest, PushWhenFull) {
  mini_mart::common::SpscRing<int, 4> ring;

  for (int i = 0; i < 4; i++) {
    EXPECT_TRUE(ring.try_push(i));
  }

  bool success = ring.try_push(999);
  EXPECT_FALSE(success);
  EXPECT_TRUE(ring.full());
  EXPECT_EQ(ring.size(), 4);
}

TEST(SpscRingTest, WrapAround) {
  mini_mart::common::SpscRing<int, 4> ring;

  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < 4; i++) {
      EXPECT_TRUE(ring.try_push(cycle * 10 + i));
    }
    EXPECT_TRUE(ring.full());

    for (int i = 0; i < 4; i++) {
      int value;
      EXPECT_TRUE(ring.try_pop(value));
      EXPECT_EQ(value, cycle * 10 + i);
    }
    EXPECT_TRUE(ring.empty());
  }
}

TEST(SpscRingTest, MixedOperations) {
  mini_mart::common::SpscRing<int, 8> ring;

  // Mix push and pop operations
  EXPECT_TRUE(ring.try_push(1));
  EXPECT_TRUE(ring.try_push(2));
  EXPECT_EQ(ring.size(), 2);

  int value;
  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, 1);
  EXPECT_EQ(ring.size(), 1);

  EXPECT_TRUE(ring.try_push(3));
  EXPECT_TRUE(ring.try_push(4));
  EXPECT_EQ(ring.size(), 3);

  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, 2);
  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, 3);
  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, 4);

  EXPECT_TRUE(ring.empty());
}

// Test with different data types
TEST(SpscRingTest, StringType) {
  mini_mart::common::SpscRing<std::string, 4> ring;

  EXPECT_TRUE(ring.try_push("hello"));
  EXPECT_TRUE(ring.try_push("world"));

  std::string value;
  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, "hello");
  EXPECT_TRUE(ring.try_pop(value));
  EXPECT_EQ(value, "world");
}

// Test with move-only types
TEST(SpscRingTest, MoveOnlyType) {
  mini_mart::common::SpscRing<std::unique_ptr<int>, 4> ring;

  auto ptr1 = std::make_unique<int>(42);
  auto ptr2 = std::make_unique<int>(84);

  EXPECT_TRUE(ring.try_push(std::move(ptr1)));
  EXPECT_TRUE(ring.try_push(std::move(ptr2)));

  std::unique_ptr<int> result;
  EXPECT_TRUE(ring.try_pop(result));
  EXPECT_EQ(*result, 42);
  EXPECT_TRUE(ring.try_pop(result));
  EXPECT_EQ(*result, 84);
}

TEST(SpscRingTest, ProducerConsumer) {
  mini_mart::common::SpscRing<int, 1024> ring;
  const int num_items = 10000;

  std::atomic<bool> producer_done{false};
  std::vector<int> consumed_items;

  consumed_items.reserve(num_items);

  std::thread producer([&ring, &producer_done, num_items]() {
    for (int i = 0; i < num_items; i++) {
      while (!ring.try_push(i * i)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true);
  });

  std::thread consumer([&ring, &producer_done, &consumed_items]() {
    int value;
    while (!producer_done.load() || !ring.empty()) {
      if (ring.try_pop(value)) {
        consumed_items.push_back(value);
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(consumed_items.size(), num_items);
  for (int i = 0; i < num_items; i++) {
    EXPECT_EQ(consumed_items[i], i * i);
  }
  EXPECT_TRUE(ring.empty());
}
