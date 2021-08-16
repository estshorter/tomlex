include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/master.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable the testing features.
enable_testing()

add_executable(tests test.cpp ../include/tomlex/tomlex.hpp ../include/tomlex/resolvers.hpp)
target_include_directories(tests PRIVATE ../include ../include/toml11)
target_precompile_headers(tests PRIVATE pch.h)
target_link_libraries(tests gtest_main)

target_compile_options(tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /source-charset:utf-8 /Zc:__cplusplus /Zc:preprocessor>
)
target_compile_features(tests PRIVATE cxx_std_17)

# Enable the GoogleTest integration.
include(GoogleTest)

message("${CMAKE_CURRENT_LIST_DIR}/good.toml")
message("${CMAKE_CURRENT_LIST_DIR}/bad.toml")

set_target_properties(tests PROPERTIES 
VS_DEBUGGER_COMMAND_ARGUMENTS "\"${CMAKE_CURRENT_LIST_DIR}/good.toml\" \"${CMAKE_CURRENT_LIST_DIR}/bad.toml\"")

# Add the test case use the gtest feature.
gtest_discover_tests(tests 
                     EXTRA_ARGS "${CMAKE_CURRENT_LIST_DIR}/good.toml" "${CMAKE_CURRENT_LIST_DIR}/bad.toml")
