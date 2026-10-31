# Cross-compiling for 64-bit Windows with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw). Used by build-windows-portable.sh;
# its bin folder must be on PATH. TEXTEDITOR_WINDOWS_ROOTS lists the folders
# holding Windows libraries (Qt, OpenSSL, libssh), so that nothing from the
# host (such as Homebrew's OpenSSL) is picked up by mistake.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB x86_64-w64-mingw32-ranlib)

if(DEFINED ENV{TEXTEDITOR_WINDOWS_ROOTS})
    string(REPLACE ":" ";" roots "$ENV{TEXTEDITOR_WINDOWS_ROOTS}")
    set(CMAKE_FIND_ROOT_PATH ${roots})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
