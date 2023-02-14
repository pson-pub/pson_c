cmake_minimum_required(VERSION 3.0)

project(pson_c )

option(ENABLE_TESTING "enable build testing code " OFF)

add_definitions(-O2)

include_directories(src)
add_library(pson_c src/cpson.c src/cpson.h)

add_executable(demo example.c)
target_link_libraries(demo pson_c)


if(ENABLE_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()