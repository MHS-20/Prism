cmake_minimum_required(VERSION 3.10)
project(prism LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -- server --
find_package(Threads REQUIRED)

file(GLOB PRISM_SERVER_SOURCES server/*.cpp)

add_executable(prism-server ${PRISM_SERVER_SOURCES})
target_link_libraries(prism-server PRIVATE Threads::Threads)

# -- client library --
file(GLOB PRISM_CLIENT_SOURCES client/*.cpp)

add_library(prism-client STATIC ${PRISM_CLIENT_SOURCES})
target_include_directories(prism-client PUBLIC client)

# -- tests --
enable_testing()

file(GLOB PRISM_TEST_SOURCES test/*.cpp)

add_executable(prism-test ${PRISM_TEST_SOURCES})
target_include_directories(prism-test PRIVATE client)
target_link_libraries(prism-test PRIVATE prism-client)

add_test(NAME client-tests
    COMMAND prism-test "$<TARGET_FILE:prism-server>"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/..)
