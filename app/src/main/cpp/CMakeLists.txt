cmake_minimum_required(VERSION 3.22.1)
project("linuxcyberdeck-native" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Android NDK toolchain
if(ANDROID)
    set(CMAKE_ANDROID_NDK ${CMAKE_ANDROID_NDK} CACHE PATH "Android NDK path")
    set(CMAKE_SYSTEM_NAME Android)
    set(CMAKE_SYSTEM_VERSION ${ANDROID_PLATFORM})
    set(CMAKE_ANDROID_ARCH_ABI ${ANDROID_ABI})
    set(CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION clang)
endif()

# Output directories
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/jniLibs/${ANDROID_ABI})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${ANDROID_ABI})

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Find required libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(dl-lib dl)

# Build JNI bridge
add_library(linuxcyberdeck_jni SHARED
    src/jni/jni_bridge.c
    src/supervisor/linux_session.c
    src/supervisor/storage_registry.c
)

target_link_libraries(linuxcyberdeck_jni
    PRIVATE
    ${log-lib}
    ${android-lib}
    ${dl-lib}
)
