cmake_minimum_required(VERSION 3.10)


project(Thpool_UnitTest)

include(CTest)

enable_testing()

add_executable(test_api test_api.cpp test_utils.cpp)
add_executable(test_threadpool test_threadpool.cpp test_utils.cpp)

target_link_libraries(test_api PUBLIC gtest_main thpool)
target_link_libraries(test_threadpool PUBLIC gtest_main thpool)

include(GoogleTest)

gtest_discover_tests(test_api)
gtest_discover_tests(test_threadpool)
