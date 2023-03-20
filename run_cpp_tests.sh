#!/bin/bash
mkdir -p build
cd build
cmake ..
make
cd unit_tests
CTEST_ARGS+=" --output-on-failure"
echo ${CTEST_ARGS}
GTEST_COLOR=1 ctest ${CTEST_ARGS}
