cmake_policy(SET CMP0077 NEW)
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)
option(THPOOL_BUILD_TESTS "Build tests" ON)

# ----------------------------------------------------------------------------------------------
project(thpool)

set(CMAKE_CXX_STANDARD 20)
if(DEFINED ENV{DEBUG})
    message("Setting build type to " $ENV{DEBUG})
  set(CMAKE_BUILD_TYPE $ENV{DEBUG})
else()
  set(CMAKE_BUILD_TYPE Release)
endif()

# Only do these if this is the main project, and not if it is included through add_subdirectory
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
include_directories(src)
add_library(thpool
    src/thpool.c
)

if(THPOOL_BUILD_TESTS)

	include(FetchContent)
	enable_testing()

	FetchContent_Declare(
		googletest
		URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
	)

	# For Windows: Prevent overriding the parent project's compiler/linker settings
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googletest)

	add_subdirectory(tests/unit unit_tests)

endif()