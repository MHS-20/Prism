cmake_minimum_required(VERSION 3.10)
project(prism LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

file(GLOB PRISM_SOURCES src/*.cpp)

add_executable(prism ${PRISM_SOURCES})
target_link_libraries(prism PRIVATE Threads::Threads)
