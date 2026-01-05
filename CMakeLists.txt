cmake_minimum_required(VERSION 3.14)
project(nanotask VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(json)
endif()

add_library(nanotask INTERFACE)
add_library(nanotask::nanotask ALIAS nanotask)

target_include_directories(nanotask INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nanotask INTERFACE
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(NANOTASK_BUILD_EXAMPLES "Build example applications" ON)
    if(NANOTASK_BUILD_EXAMPLES)
        add_executable(nanotask_example examples/main.cpp)
        target_link_libraries(nanotask_example PRIVATE nanotask::nanotask)
    endif()
endif()

if(NANOTASK_BUILD_EXAMPLES)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS nanotask
    EXPORT nanotaskTargets
)

install(EXPORT nanotaskTargets
    FILE nanotaskTargets.cmake
    NAMESPACE nanotask::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanotask
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/NanoTaskConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/nanotaskConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanotask
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nanotaskConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nanotaskConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nanotaskConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nanotask
)
