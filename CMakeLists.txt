cmake_minimum_required(VERSION 3.14)

project(tomlex VERSION 0.0.0)

option(tomlex_BUILD_TEST "Build toml tests" OFF)

if (tomlex_BUILD_TEST)
    add_subdirectory(tests)
endif ()
