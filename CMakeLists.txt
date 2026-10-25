cmake_minimum_required(VERSION 3.21)

project(TextEditor VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets PrintSupport)
qt_standard_project_setup()

qt_add_executable(TextEditor
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/codeeditor.cpp src/codeeditor.h
    src/findbar.cpp src/findbar.h
    src/highlighter.cpp src/highlighter.h
    src/theme.h
    resources.qrc
)

target_link_libraries(TextEditor PRIVATE Qt6::Widgets Qt6::PrintSupport)

set_target_properties(TextEditor PROPERTIES
    OUTPUT_NAME "TextEditor+"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TextEditor+"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.sjwrc.texteditor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE icon.icns
)

# Compiler and linker hardening.
include(CheckPIESupported)
check_pie_supported()
set_target_properties(TextEditor PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(TextEditor PRIVATE /W4 /sdl /guard:cf)
    target_link_options(TextEditor PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
else()
    target_compile_options(TextEditor PRIVATE -Wall -Wextra -fstack-protector-strong)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(TextEditor PRIVATE
            -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    endif()
endif()

if(APPLE)
    set(app_icon ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns)
    set_source_files_properties(${app_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(TextEditor PRIVATE ${app_icon})
endif()

include(GNUInstallDirs)
install(TARGETS TextEditor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
