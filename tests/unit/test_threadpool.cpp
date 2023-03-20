#include "thpool.h"
#include "gtest/gtest.h"

std::atomic<int> counter = 0;

class TestThreadpool : public ::testing::TestWithParam<std::tuple<int, int>> {
public:
    void SetUp() override { counter = 0; }
};

void increment(void *arg) { counter++; }

TEST_P(TestThreadpool, testMassAddition) {
    const auto [tasks, threads] = GetParam();
    threadpool thpool = thpool_init(threads);
    for (int i = 0; i < tasks; i++) {
        thpool_add_work(thpool, increment, NULL);
    }
    thpool_wait(thpool);
    ASSERT_EQ(counter, tasks);
    thpool_destroy(thpool);
}

INSTANTIATE_TEST_SUITE_P(MyTestInstantiation, TestThreadpool,
                         testing::Values(std::make_tuple(10, 4), std::make_tuple(1000, 100),
                                         std::make_tuple(100000, 100)));