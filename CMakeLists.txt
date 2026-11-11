cmake_minimum_required(VERSION 3.24)
project(PlayView VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static CRT: the resulting executable does not require the VC++ runtime to be
    # installed separately on a clean Windows machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    )
endif()

configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/PlayView.rc @ONLY)

add_executable(PlayView WIN32
    src/main.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/PlayView.rc
)

target_include_directories(PlayView PRIVATE resources)
target_compile_definitions(PlayView PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
# The custom application manifest is embedded from PlayView.rc.
# Disable the linker's automatic manifest embedding to avoid a duplicate
# RT_MANIFEST resource (CVTRES : fatal error CVT1100).
if(MSVC)
    target_link_options(PlayView PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(PlayView PRIVATE
    mf
    mfplat
    evr
    mfreadwrite
    mfuuid
    ole32
    uuid
    winmm
    comctl32
    shell32
    comdlg32
    windowscodecs
    user32
    gdi32
)

set_target_properties(PlayView PROPERTIES
    OUTPUT_NAME "PlayView"
    WIN32_EXECUTABLE TRUE
)
