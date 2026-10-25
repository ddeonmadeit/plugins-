cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(DDPlugins VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(DD_BUILD_HARNESS "Build the offline QA harness for every plugin" ON)

# JUCE is pinned here for every plugin in the repo. To reuse a local checkout:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

include(cmake/DDPlugin.cmake)

# Shared modules (see CLAUDE.md for what lives where)
juce_add_module(modules/dd_dsp)
juce_add_module(modules/dd_core)
juce_add_module(modules/dd_gui)

# Every folder in plugins/ with a CMakeLists.txt is a plugin.
file(GLOB DD_PLUGIN_DIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
foreach(dir ${DD_PLUGIN_DIRS})
    if(EXISTS "${dir}/CMakeLists.txt")
        add_subdirectory("${dir}")
    endif()
endforeach()
