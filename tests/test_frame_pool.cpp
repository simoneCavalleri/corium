#include <gtest/gtest.h>
#include <corium/corium.hpp>

using namespace corium::async;

#if defined(_MSC_VER)
#define CORIUM_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define CORIUM_NOINLINE __attribute__((noinline))
#else
#define CORIUM_NOINLINE
#endif

namespace {

CORIUM_NOINLINE PooledTask<int, 4, 512> computePooledSum(int a, int b) {
    co_return a + b;
}

CORIUM_NOINLINE PooledTask<void, 4, 512> voidPooledOperation(int& output, int val) {
    output = val * 2;
    co_return;
}

CORIUM_NOINLINE PooledGenerator<int, 4, 512> generatePooledRange(int start, int count) {
    for (int i = 0; i < count; ++i) {
        co_yield start + i;
    }
}

} // namespace

TEST(FramePoolTest, StaticFramePoolAllocationAndDeallocation)
{
    using Pool = StaticFramePool<2, 256>;
    Pool::reset();
    EXPECT_EQ(Pool::activeCount(), 0u);

    void* p1 = Pool::allocate(128);
    EXPECT_NE(p1, nullptr);
    EXPECT_EQ(Pool::activeCount(), 1u);

    void* p2 = Pool::allocate(256);
    EXPECT_NE(p2, nullptr);
    EXPECT_EQ(Pool::activeCount(), 2u);

    // Exceed capacity
    void* p3 = Pool::allocate(64);
    EXPECT_EQ(p3, nullptr);

    // Exceed max size
    void* pTooBig = Pool::allocate(512);
    EXPECT_EQ(pTooBig, nullptr);

    Pool::deallocate(p1, 128);
    EXPECT_EQ(Pool::activeCount(), 1u);

    // Can allocate again after free
    void* p4 = Pool::allocate(64);
    EXPECT_NE(p4, nullptr);
    EXPECT_EQ(Pool::activeCount(), 2u);

    Pool::deallocate(p2, 256);
    Pool::deallocate(p4, 64);
    EXPECT_EQ(Pool::activeCount(), 0u);
}

TEST(FramePoolTest, PooledTaskExecution)
{
    using Pool = StaticFramePool<4, 512>;
    Pool::reset();

    {
        auto task = computePooledSum(20, 22);
        EXPECT_EQ(Pool::activeCount(), 1u);

        task.resume();
        EXPECT_TRUE(task.done());
        EXPECT_EQ(task.await_resume(), 42);
    }
    // Destruction should deallocate from pool
    EXPECT_EQ(Pool::activeCount(), 0u);
}

TEST(FramePoolTest, PooledVoidTaskExecution)
{
    using Pool = StaticFramePool<4, 512>;
    Pool::reset();

    int result = 0;
    {
        auto task = voidPooledOperation(result, 21);
        EXPECT_EQ(Pool::activeCount(), 1u);

        task.resume();
        EXPECT_TRUE(task.done());
        EXPECT_EQ(result, 42);
    }
    EXPECT_EQ(Pool::activeCount(), 0u);
}

TEST(FramePoolTest, PooledGeneratorExecution)
{
    using Pool = StaticFramePool<4, 512>;
    Pool::reset();

    {
        auto gen = generatePooledRange(10, 3);
        EXPECT_EQ(Pool::activeCount(), 1u);

        std::vector<int> vals;
        for (int v : gen) {
            vals.push_back(v);
        }
        ASSERT_EQ(vals.size(), 3u);
        EXPECT_EQ(vals[0], 10);
        EXPECT_EQ(vals[1], 11);
        EXPECT_EQ(vals[2], 12);
    }
    EXPECT_EQ(Pool::activeCount(), 0u);
}
