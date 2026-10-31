cmake_minimum_required(VERSION 3.21)

project(TextEditor VERSION 2.5.0 LANGUAGES CXX)

option(TEXTEDITOR_SFTP "SFTP support (needs libssh)" ON)
option(TEXTEDITOR_TESTS "Build the unit tests" ON)
option(TEXTEDITOR_FUZZERS "Build the fuzz targets (libFuzzer with Clang, or a standalone driver)" OFF)
set(TEXTEDITOR_SANITIZE "" CACHE STRING
    "Sanitizers for every target, e.g. \"address;undefined\" or \"thread\"")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets PrintSupport Concurrent)
qt_standard_project_setup()

if(TEXTEDITOR_SFTP)
    # libssh's own CMake package, or pkg-config where only that is installed.
    # 0.11.5 / 0.12.2 fix every libssh security issue known in October 2026.
    find_package(libssh 0.11.5 CONFIG QUIET)
    if(TARGET ssh)
        set(TEXTEDITOR_LIBSSH ssh)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh>=0.11.5)
        set(TEXTEDITOR_LIBSSH PkgConfig::LIBSSH)
    endif()
endif()

# Fuzzing: with libFuzzer available, every target gets coverage
# instrumentation so the fuzzers can see into the libraries they test.
if(TEXTEDITOR_FUZZERS)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_cxx_source_compiles(
        "#include <cstdint>
         #include <cstddef>
         extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t *, size_t) { return 0; }"
        TEXTEDITOR_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if(TEXTEDITOR_HAVE_LIBFUZZER)
        add_compile_options(-fsanitize=fuzzer-no-link)
    endif()
endif()

# Compiler and linker hardening, applied to every target.
include(CheckPIESupported)
check_pie_supported()
function(texteditor_harden target)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /sdl /guard:cf)
        get_target_property(type ${target} TYPE)
        if(NOT type STREQUAL "STATIC_LIBRARY")
            target_link_options(${target} PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -fstack-protector-strong)
        if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # Control Flow Guard, as /guard:cf does for MSVC builds. lld
            # turns on ASLR (high-entropy) and DEP for MinGW by default.
            target_compile_options(${target} PRIVATE -mguard=cf)
            get_target_property(type ${target} TYPE)
            if(NOT type STREQUAL "STATIC_LIBRARY")
                target_link_options(${target} PRIVATE -mguard=cf
                    -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
            endif()
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # macOS turns on _FORTIFY_SOURCE by default; Linux toolchains vary.
            target_compile_options(${target} PRIVATE -fstack-clash-protection
                "$<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>")
            target_link_options(${target} PRIVATE
                -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
        endif()
    endif()
    if(TEXTEDITOR_SANITIZE)
        list(JOIN TEXTEDITOR_SANITIZE "," sanitizers)
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer
                                                 -fno-sanitize-recover=all)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
    endif()
endfunction()

# Everything that doesn't need widgets: shared by the app, the tests and
# the fuzzers.
add_library(texteditor_core STATIC
    src/filesystem.h
    src/latexhtml.cpp src/latexhtml.h
    src/localfilesystem.cpp src/localfilesystem.h
    src/location.cpp src/location.h
    src/texcompiler.cpp src/texcompiler.h
    src/textcodec.cpp src/textcodec.h
)
target_include_directories(texteditor_core PUBLIC src)
target_link_libraries(texteditor_core PUBLIC Qt6::Core Qt6::Concurrent)
if(TEXTEDITOR_SFTP)
    target_sources(texteditor_core PRIVATE
        src/sftp/connectionmanager.cpp src/sftp/connectionmanager.h
        src/sftp/sftpconnection.cpp src/sftp/sftpconnection.h
        src/sftp/sftpfilesystem.cpp src/sftp/sftpfilesystem.h
    )
    target_link_libraries(texteditor_core PUBLIC ${TEXTEDITOR_LIBSSH})
    if(WIN32)
        # shutdown() on the connection's socket, for cancelling.
        target_link_libraries(texteditor_core PUBLIC ws2_32)
    endif()
    target_compile_definitions(texteditor_core PUBLIC TEXTEDITOR_HAVE_SFTP)
endif()
texteditor_harden(texteditor_core)

# The window and its parts, in a library so the tests can drive them.
add_library(texteditor_ui STATIC
    src/classicstyle.cpp src/classicstyle.h
    src/codeeditor.cpp src/codeeditor.h
    src/document.cpp src/document.h
    src/findbar.cpp src/findbar.h
    src/foldersidebar.cpp src/foldersidebar.h
    src/highlighter.cpp src/highlighter.h
    src/mainwindow.cpp src/mainwindow.h
    src/messagebox.cpp src/messagebox.h
    src/remotefiledialog.cpp src/remotefiledialog.h
    src/preview.cpp src/preview.h
    src/remotefilemodel.cpp src/remotefilemodel.h
    src/settingsdialog.cpp src/settingsdialog.h
    src/spellchecker.cpp src/spellchecker.h
    src/theme.h
    resources/toolbar/toolbar.qrc
)
# The system's spell checker.
if(APPLE)
    enable_language(OBJCXX)
    target_sources(texteditor_ui PRIVATE src/spellchecker_mac.mm)
    target_link_libraries(texteditor_ui PUBLIC "-framework AppKit")
elseif(WIN32)
    target_sources(texteditor_ui PRIVATE src/spellchecker_win.cpp)
    target_link_libraries(texteditor_ui PUBLIC ole32)
else()
    target_sources(texteditor_ui PRIVATE src/spellchecker_none.cpp)
endif()
if(TEXTEDITOR_SFTP)
    target_sources(texteditor_ui PRIVATE
        src/sftp/authdialog.cpp src/sftp/authdialog.h
        src/sftp/connectdialog.cpp src/sftp/connectdialog.h
        src/sftp/hostkeydialog.cpp src/sftp/hostkeydialog.h
    )
endif()
target_link_libraries(texteditor_ui PUBLIC texteditor_core Qt6::Widgets Qt6::PrintSupport)
texteditor_harden(texteditor_ui)

qt_add_executable(TextEditor
    src/main.cpp
    resources.qrc
)
target_link_libraries(TextEditor PRIVATE texteditor_ui)
target_compile_definitions(TextEditor PRIVATE APP_VERSION="${PROJECT_VERSION}")
texteditor_harden(TextEditor)

set_target_properties(TextEditor PROPERTIES
    OUTPUT_NAME "TextEditor+"
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "TextEditor+"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.sjwrc.texteditor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE icon.icns
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
)

if(APPLE)
    # TextEditor+'s licence and those of the libraries macdeployqt bundles
    # with it. THIRD-PARTY-NOTICES-macOS.txt is made from the bundle by
    # tools/macos-third-party-notices.py (tools/make-macos-dmg.sh runs it).
    file(GLOB licence_files ${CMAKE_CURRENT_SOURCE_DIR}/licenses/*.txt)
    list(APPEND licence_files ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
    set_source_files_properties(${licence_files} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/licenses)
    target_sources(TextEditor PRIVATE ${licence_files})
    set(app_icon ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.icns)
    set_source_files_properties(${app_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(TextEditor PRIVATE ${app_icon})
endif()

if(WIN32)
    configure_file(resources/TextEditor.rc.in TextEditor.rc @ONLY)
    target_sources(TextEditor PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/TextEditor.rc)
endif()

if(TEXTEDITOR_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

if(TEXTEDITOR_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()

include(GNUInstallDirs)
install(TARGETS TextEditor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
