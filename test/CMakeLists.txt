cmake_minimum_required(VERSION 3.14)
project(STS3xTests CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
# doctest is a single header. SOURCE_SUBDIR points to a non-existent directory
# so that only the sources are fetched and doctest's own build is not configured.
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  do-not-configure
)
FetchContent_MakeAvailable(doctest)

set(LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# The library under test, compiled against the fake Arduino API in mocks/.
add_library(sts3x STATIC ${LIBRARY_DIR}/STS3x.cpp)
target_include_directories(sts3x PUBLIC ${LIBRARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_compile_options(sts3x PRIVATE -Wall -Wextra -Werror --coverage -O0)

add_executable(sts3x_tests test_sts3x.cpp mocks/mocks.cpp)
target_include_directories(sts3x_tests SYSTEM PRIVATE ${doctest_SOURCE_DIR}/doctest)
target_compile_options(sts3x_tests PRIVATE -Wall -Wextra)
target_link_libraries(sts3x_tests PRIVATE sts3x --coverage)

enable_testing()
add_test(NAME sts3x_tests COMMAND sts3x_tests)
