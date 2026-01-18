cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(StreamTicks VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
    set(PLATFORM_NAME "windows")
elseif(APPLE)
    set(PLATFORM_NAME "macos")
elseif(UNIX)
    set(PLATFORM_NAME "linux")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM|aarch64|AARCH64")
    set(ARCH_NAME "arm64")
elseif(CMAKE_CXX_COMPILER_TARGET MATCHES "aarch64")
    set(ARCH_NAME "arm64")
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64")
    set(ARCH_NAME "arm64")
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "x64|Win64")
    #set(ARCH_NAME "x86_64")
    set(ARCH_NAME "x64")
else()
    #set(ARCH_NAME "x86_64")
    set(ARCH_NAME "x64")
endif()

message(STATUS "Building for: ${PLATFORM_NAME}-${ARCH_NAME}")

# =========================================================================

option(BUILD_STATIC "Build fully static binary" ON)
option(USE_MUSL "Use musl libc for static linking" OFF)
option(SIMDJSON_HEADER_ONLY "Use simdjson header-only mode" OFF)

# =========================================================================

set(Boost_NO_BOOST_CMAKE ON)
set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/boost_1_87_0")
include_directories(${BOOST_ROOT})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(WIN32)
    set(ZSTD_WIN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/build-win-${ARCH_NAME}/lib")
    add_library(zstd_static STATIC IMPORTED)
    set_target_properties(zstd_static PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${ZSTD_WIN_LIB}/Debug/zstd_static.lib"
        IMPORTED_LOCATION_RELEASE "${ZSTD_WIN_LIB}/Release/zstd_static.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib"
    )
    set(ZSTD_TARGET zstd_static)
    message(STATUS "Using local zstd build (Windows)")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib/libzstd.a")
    message(STATUS "Using local zstd build")
    add_library(zstd_static STATIC IMPORTED)
    set_target_properties(zstd_static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib/libzstd.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/zstd/lib"
    )
    set(ZSTD_TARGET zstd_static)
else()
    find_package(zstd QUIET)
    if(NOT zstd_FOUND)
        find_library(ZSTD_LIBRARY NAMES libzstd.a zstd)
        find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
        if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
            add_library(zstd_found UNKNOWN IMPORTED)
            set_target_properties(zstd_found PROPERTIES
                IMPORTED_LOCATION "${ZSTD_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
            )
            set(ZSTD_TARGET zstd_found)
            message(STATUS "Found Zstd: ${ZSTD_LIBRARY}")
        else()
            message(FATAL_ERROR "Zstd not found. Build it first in external/zstd/")
        endif()
    else()
        set(ZSTD_TARGET zstd::zstd)
    endif()
endif()

if(SIMDJSON_HEADER_ONLY)
    message(STATUS "Using simdjson header-only mode")
    add_library(simdjson_header INTERFACE)
    target_include_directories(simdjson_header INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/singleheader"
    )
    target_compile_definitions(simdjson_header INTERFACE SIMDJSON_HEADER_ONLY)
    set(SIMDJSON_TARGET simdjson_header)
elseif(WIN32)
    set(SIMDJSON_WIN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/build-win-${ARCH_NAME}")
    add_library(simdjson_static STATIC IMPORTED)
    set_target_properties(simdjson_static PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${SIMDJSON_WIN_LIB}/Debug/simdjson.lib"
        IMPORTED_LOCATION_RELEASE "${SIMDJSON_WIN_LIB}/Release/simdjson.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/include"
    )
    set(SIMDJSON_TARGET simdjson_static)
    message(STATUS "Using local simdjson build (Windows ${ARCH_NAME})")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/build-arm/libsimdjson.a")
    message(STATUS "Using local simdjson build (ARM)")
    add_library(simdjson_static STATIC IMPORTED)
    set_target_properties(simdjson_static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/build-arm/libsimdjson.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/include"
    )
    set(SIMDJSON_TARGET simdjson_static)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/build-x86/libsimdjson.a")
    message(STATUS "Using local simdjson build (x86_64)")
    add_library(simdjson_static STATIC IMPORTED)
    set_target_properties(simdjson_static PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/build-x86/libsimdjson.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/include"
    )
    set(SIMDJSON_TARGET simdjson_static)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/simdjson/CMakeLists.txt")
    message(STATUS "Building simdjson from source")
    set(SIMDJSON_JUST_LIBRARY ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/simdjson EXCLUDE_FROM_ALL)
    set(SIMDJSON_TARGET simdjson)
else()
    message(FATAL_ERROR "simdjson not found. Clone it to external/simdjson/")
endif()

# =========================================================================

add_executable(stream_ticks
    src/stream_ticks.cpp
)


target_include_directories(stream_ticks PRIVATE
    ${Boost_INCLUDE_DIRS}
)


target_include_directories(stream_ticks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(stream_ticks PRIVATE
    #Boost::system
    Threads::Threads
    ${ZSTD_TARGET}
    ${SIMDJSON_TARGET}
)

# =========================================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT APPLE)
    target_compile_options(stream_ticks PRIVATE
        -O3
        -flto
        #-fno-exceptions
        -fno-rtti
    )
    
    if(ARCH_NAME STREQUAL "arm64")
        target_compile_options(stream_ticks PRIVATE
            -march=armv8-a
            -mtune=generic
        )
    else()
        target_compile_options(stream_ticks PRIVATE
            -march=native
            -mtune=native
        )
    endif()
    
    if(BUILD_STATIC)
        target_link_options(stream_ticks PRIVATE
            -static
            -fuse-ld=lld
            -flto
        )
        
        if(USE_MUSL)
            if(ARCH_NAME STREQUAL "arm64")
                set(MUSL_TARGET "aarch64-linux-musl")
            else()
                set(MUSL_TARGET "x86_64-linux-musl")
            endif()
            
            target_compile_options(stream_ticks PRIVATE
                --target=${MUSL_TARGET}
            )
            target_link_options(stream_ticks PRIVATE
                --target=${MUSL_TARGET}
            )
        endif()
    endif()
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(stream_ticks PRIVATE
        -O3
        -Wall
        -Wextra
    )
    
    if(ARCH_NAME STREQUAL "arm64")
        target_compile_options(stream_ticks PRIVATE
            -march=armv8-a
            -mtune=generic
        )
    else()
        target_compile_options(stream_ticks PRIVATE
            -march=native
            -mtune=native
        )
    endif()
    
    if(BUILD_STATIC)
        target_link_options(stream_ticks PRIVATE
            -static-libgcc
            -static-libstdc++
        )
    endif()

elseif(APPLE)
    target_compile_options(stream_ticks PRIVATE
        -O3
        -flto
    )
    
    target_link_options(stream_ticks PRIVATE
        -flto
        -dead_strip
    )
    
elseif(MSVC)
    target_compile_options(stream_ticks PRIVATE
        /O2
        /W4
    )
    set_target_properties(stream_ticks PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()


if(WIN32)
    target_link_libraries(stream_ticks PRIVATE ws2_32 wsock32)
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
    if(CMAKE_STRIP)
        add_custom_command(TARGET stream_ticks POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:stream_ticks>
            COMMENT "Stripping binary..."
        )
    endif()
endif()

# =========================================================================

install(TARGETS stream_ticks
    RUNTIME DESTINATION bin
)

# =========================================================================

set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "stream_ticks")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "stream_ticks-${PROJECT_VERSION}-${PLATFORM_NAME}-${ARCH_NAME}")
include(CPack)

# =========================================================================

message(STATUS "=================================")
message(STATUS "Stream Ticks Configuration")
message(STATUS "=================================")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${PLATFORM_NAME}")
message(STATUS "Architecture: ${ARCH_NAME}")
message(STATUS "Static Build: ${BUILD_STATIC}")
message(STATUS "Use Musl: ${USE_MUSL}")
message(STATUS "Simdjson Header Only: ${SIMDJSON_HEADER_ONLY}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "=================================")