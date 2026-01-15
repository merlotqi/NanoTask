cmake_minimum_required(VERSION 3.14)
project(VeloTask VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

add_library(VeloTask INTERFACE)
add_library(VeloTask::VeloTask ALIAS VeloTask)

target_include_directories(VeloTask INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(VeloTask INTERFACE
    Threads::Threads
    nlohmann_json::nlohmann_json
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    option(VELOTASK_BUILD_EXAMPLES "Build example applications" ON)
    if(VELOTASK_BUILD_EXAMPLES)
        add_executable(velotask_example examples/main.cpp)
        target_link_libraries(velotask_example PRIVATE VeloTask::VeloTask)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS VeloTask
    EXPORT VeloTaskTargets
)

install(EXPORT VeloTaskTargets
    FILE VeloTaskTargets.cmake
    NAMESPACE VeloTask::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VeloTask
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/VeloTaskConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/VeloTaskConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VeloTask
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VeloTaskConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VeloTaskConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VeloTaskConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VeloTask
)
