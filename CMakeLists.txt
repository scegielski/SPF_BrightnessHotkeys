cmake_minimum_required(VERSION 3.16)
project(SPF_BrightnessHotkeys LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_library(SPF_BrightnessHotkeys SHARED BrightnessPlugin.cpp)
target_include_directories(SPF_BrightnessHotkeys PRIVATE SPF_API)
enable_testing()
add_executable(BrightnessTests Tests.cpp)
target_include_directories(BrightnessTests PRIVATE SPF_API)
add_test(NAME BrightnessRegression COMMAND BrightnessTests)
add_custom_command(TARGET SPF_BrightnessHotkeys POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/SPF_BrightnessHotkeys"
 COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:SPF_BrightnessHotkeys>" "${CMAKE_BINARY_DIR}/package/SPF_BrightnessHotkeys/")
