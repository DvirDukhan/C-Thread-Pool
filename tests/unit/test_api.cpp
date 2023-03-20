#include "thpool.h"
#include "test_utils.h"
#include "gtest/gtest.h"

class APITest : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(APITest, testAPI) {
    const auto [tasks, threads] = GetParam();
    int sleep_time = 2;
    threadpool thpool = thpool_init(threads);
    for (int i = 0; i < tasks; i++) {
        thpool_add_work(thpool, sleep_x_secs, &sleep_time);
    }
    // TODO: remove it when removing bsem
    sleep(1);
    ASSERT_EQ(thpool_num_threads_working(thpool), tasks);
    thpool_destroy(thpool);
}

INSTANTIATE_TEST_SUITE_P(MyTestInstantiation, APITest,
                         testing::Values(std::make_tuple(4, 10), std::make_tuple(2, 5)));