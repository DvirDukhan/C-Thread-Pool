#!/bin/bash
export CTEST_ARGS="-T memcheck --overwrite MemoryCheckCommandOptions=\"--leak-check=full --error-exitcode=255\""
export DEBUG=Debug
./run_cpp_tests.sh
