cmake_minimum_required(VERSION 3.28)

project(leetvim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules here; skipping the scan also avoids clang-scan-deps, which can't see
# the Nix compiler wrapper's include paths.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(LEETVIM_BUILD_TESTS "Build the unit tests" ON)
option(LEETVIM_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

add_library(leetvim_options INTERFACE)
target_compile_options(
    leetvim_options
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnull-dereference
        -Wimplicit-fallthrough
        -Wno-missing-designated-field-initializers # fields have defaults
)
if(LEETVIM_SANITIZE)
    target_compile_options(
        leetvim_options
        INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(leetvim_options INTERFACE -fsanitize=address,undefined)
endif()

# Each directory under src/ is a static library; dependencies only point downward:
#   util <- content, leetcode, tui <- core <- app <- leetvim
function(leetvim_module name)
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/src/${name}/*.cpp"
    )
    add_library(leetvim_${name} STATIC ${sources})
    target_include_directories(
        leetvim_${name}
        PUBLIC "${PROJECT_SOURCE_DIR}/src"
    )
    target_link_libraries(leetvim_${name} PUBLIC leetvim_options ${ARGN})
endfunction()

leetvim_module(util)
leetvim_module(content leetvim_util)
leetvim_module(
    leetcode
    leetvim_util
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Threads::Threads
    SQLite3::SQLite3 # browser cookie stores
    OpenSSL::Crypto # Chromium cookie decryption
)
leetvim_module(tui leetvim_util PkgConfig::VTERM)
leetvim_module(core leetvim_util leetvim_leetcode)
leetvim_module(
    app
    leetvim_core
    leetvim_content
    leetvim_tui
    leetvim_leetcode
    CLI11::CLI11
)
target_compile_definitions(
    leetvim_app
    PRIVATE LEETVIM_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(leetvim_tui PUBLIC util) # forkpty
endif()

add_executable(leetvim src/main.cpp)
target_link_libraries(leetvim PRIVATE leetvim_app)
install(TARGETS leetvim)

if(LEETVIM_BUILD_TESTS)
    find_package(doctest REQUIRED)
    enable_testing()
    foreach(
        module
        util
        content
        leetcode
        tui
        core
        app
    )
        file(
            GLOB test_sources
            CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/tests/${module}/*.cpp"
        )
        if(test_sources)
            add_executable(test_${module} tests/test_main.cpp ${test_sources})
            target_link_libraries(
                test_${module}
                PRIVATE leetvim_${module} doctest::doctest
            )
            target_compile_definitions(
                test_${module}
                PRIVATE LEETVIM_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures"
            )
            add_test(NAME ${module} COMMAND test_${module})
        endif()
    endforeach()
endif()
