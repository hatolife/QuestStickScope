#include "ipc/SpscRingBuffer.hpp"

#include <gtest/gtest.h>

namespace {

TEST(RingBufferTests, PreservesFifoOrder) {
	qss::SpscRingBuffer<int, 4> buffer;

	EXPECT_TRUE(buffer.TryPush(10));
	EXPECT_TRUE(buffer.TryPush(20));

	int value = 0;
	EXPECT_TRUE(buffer.TryPop(value));
	EXPECT_EQ(value, 10);
	EXPECT_TRUE(buffer.TryPop(value));
	EXPECT_EQ(value, 20);
	EXPECT_TRUE(buffer.Empty());
}

TEST(RingBufferTests, DropsNewestValueWhenFull) {
	qss::SpscRingBuffer<int, 4> buffer;

	EXPECT_TRUE(buffer.TryPush(1));
	EXPECT_TRUE(buffer.TryPush(2));
	EXPECT_TRUE(buffer.TryPush(3));
	EXPECT_FALSE(buffer.TryPush(4));
	EXPECT_EQ(buffer.DroppedCount(), 1U);

	int value = 0;
	EXPECT_TRUE(buffer.TryPop(value));
	EXPECT_EQ(value, 1);
	EXPECT_TRUE(buffer.TryPop(value));
	EXPECT_EQ(value, 2);
	EXPECT_TRUE(buffer.TryPop(value));
	EXPECT_EQ(value, 3);
	EXPECT_FALSE(buffer.TryPop(value));
}

} // namespace
