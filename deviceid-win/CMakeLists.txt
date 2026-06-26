cmake_minimum_required(VERSION 3.20)
project(DeviceInfoSDK LANGUAGES CXX C)

add_library(DeviceInfoSDK STATIC
    src/DeviceInfo.cpp
    src/DeviceInfoC.cpp
    src/internal/DeviceIdManager.cpp
    src/internal/NetworkCollector.cpp
    src/internal/SmbiosCollector.cpp
    src/internal/SystemCollectors.cpp
    src/internal/Utf8.cpp
)

target_include_directories(DeviceInfoSDK PUBLIC include PRIVATE src)
target_compile_features(DeviceInfoSDK PUBLIC cxx_std_17)
target_compile_definitions(DeviceInfoSDK PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    NTDDI_VERSION=0x06010000
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(DeviceInfoSDK PRIVATE /utf-8 /W4 /permissive- /sdl)
endif()

target_link_libraries(DeviceInfoSDK PUBLIC
    bcrypt
    iphlpapi
    advapi32
    user32
    shell32
    ole32
    ws2_32
)

add_executable(deviceinfo_cli main.cpp)
target_link_libraries(deviceinfo_cli PRIVATE DeviceInfoSDK)
target_include_directories(deviceinfo_cli PRIVATE src)
