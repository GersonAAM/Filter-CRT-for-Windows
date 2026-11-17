cmake_minimum_required(VERSION 3.15)
project(RetroCRT VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Static CRT: the exe runs on a clean Windows 11 without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(retrocrt_core OBJECT
    src/main.cpp
    src/App.cpp
    src/App.h
    src/Common.cpp
    src/Common.h
    src/Config.cpp
    src/Config.h
    src/Json.cpp
    src/Json.h
    src/Osd.cpp
    src/Osd.h
    src/Renderer.cpp
    src/Renderer.h
    src/SettingsPanel.cpp
    src/SettingsPanel.h
    src/resource.h
)

set(RETROCRT_DEFINES UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_definitions(retrocrt_core PRIVATE ${RETROCRT_DEFINES})
if(MSVC)
    target_compile_options(retrocrt_core PRIVATE /W4 /utf-8 /permissive- /EHsc)
else()
    target_compile_options(retrocrt_core PRIVATE -Wall -Wextra -municode)
endif()

# The .rc references shaders/crt.hlsl and the manifests relative to the project root.
set_source_files_properties(src/RetroCRT.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/crt.hlsl;${CMAKE_CURRENT_SOURCE_DIR}/src/RetroCRT.manifest;${CMAKE_CURRENT_SOURCE_DIR}/src/RetroCRT.uiaccess.manifest")

# RetroCRT: portable. RetroCRT_UIAccess: same code with uiAccess="true" in its manifest; it only
# starts once signed and placed in Program Files (install-uiaccess.ps1).
foreach(target RetroCRT RetroCRT_UIAccess)
    add_executable(${target} WIN32 $<TARGET_OBJECTS:retrocrt_core> src/RetroCRT.rc)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE ${RETROCRT_DEFINES})
    # d3dcompiler_47.dll is loaded at runtime (it ships with Windows), so it is not linked.
    target_link_libraries(${target} PRIVATE d3d11 dxgi user32 gdi32 shell32 comctl32 ole32)
    if(MSVC)
        target_link_options(${target} PRIVATE /MANIFEST:NO) # embedded through RetroCRT.rc
    else()
        target_link_options(${target} PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()
target_compile_definitions(RetroCRT_UIAccess PRIVATE RETROCRT_UIACCESS)
