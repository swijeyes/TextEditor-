#!/bin/sh
# Builds the Windows x64 portable version of TextEditor+ on a Mac, by
# cross-compiling with llvm-mingw, and writes
#   ../release/TextEditor+-<version>-windows-x64.zip
#
# Usage: tools/build-windows-portable.sh [WORK_DIR]
#
# Needs: cmake, perl, curl, 7zz (brew install sevenzip) and zip. The host
# Qt tools (moc, rcc, uic) must be the same version as QT_VERSION below:
# Homebrew's qtbase is used if it is, otherwise Qt's own macOS qtbase is
# downloaded into the work folder.
#
# Everything downloaded is checked against the SHA-256 values below before
# it is used. The values were recorded on October 26, 2026 (Qt's and
# OpenSSL's against their published checksums, llvm-mingw's against GitHub's
# release digest, libssh's after checking its GPG signature with the libssh
# release key 88A2 28D8 9B07 C2C7 7D0C 7809 03D5 DF8C FDD3 E8E7).
#
# The official Windows build is meant to come from GitHub Actions
# (.github/workflows/windows.yml), which SignPath can sign. A build from
# this script can't be signed through SignPath, as with 1.1's.
set -eu

QT_VERSION=6.12.0
QT_PACKAGE=6.12.0-0-202609280346qtbase-Windows-Windows_11_24H2-Clang-Windows-Windows_11_24H2-X86_64.7z
QT_SHA256=391aa3976b33577f4f953958d58c5112aad1c2c760039c49dc178cb0bd1a7db6
# Host tools when Homebrew's Qt is another version (SHA-1 checked against
# Qt's published value on October 31, 2026).
QT_HOST_PACKAGE=6.12.0-0-202609280346qtbase-MacOS-MacOS_26-Clang-MacOS-MacOS_26-X86_64-ARM64.7z
QT_HOST_SHA256=1e0df66ed17ae97997971ff481983c5c0b7497a52ef52f699725701401fd80ee
# The llvm-mingw release Qt 6.12.0's Windows build was made with.
LLVM_MINGW=20260602
LLVM_MINGW_SHA256=d3310f9b86b368900850af8bc95da20648f9fc6b0be3bb64dbf8fb18d7c0894f
OPENSSL=4.0.3
OPENSSL_SHA256=325b5c806167c13b40b1ffeadfe0248197c00eccc4cf123ec1e28d2d2fd216d9
LIBSSH=0.12.2
LIBSSH_SHA256=49560f677d96e3706a904ac2de1116e25f3680937d51e5c92198fcba4a1c1e9f

native=$(cd "$(dirname "$0")/.." && pwd)
work=${1:-$native/build-windows}
mkdir -p "$work/downloads"
work=$(cd "$work" && pwd)
dl=$work/downloads
deps=$work/deps
jobs=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
version=$(sed -n 's/^project(TextEditor VERSION \([0-9.]*\).*/\1/p' "$native/CMakeLists.txt")

fetch() { # url file sha256
    if [ ! -f "$dl/$2" ]; then
        echo "Downloading $2"
        curl -fL --retry 3 -o "$dl/$2.part" "$1"
        mv "$dl/$2.part" "$dl/$2"
    fi
    echo "$3  $dl/$2" | shasum -a 256 -c - >/dev/null || {
        echo "Checksum mismatch for $2: refusing to use it." >&2
        rm -f "$dl/$2"
        exit 1
    }
}

fetch "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_llvm_mingw/qt.qt6.6120.win64_llvm_mingw/$QT_PACKAGE" \
      "$QT_PACKAGE" "$QT_SHA256"
fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW/llvm-mingw-$LLVM_MINGW-ucrt-macos-universal.tar.xz" \
      "llvm-mingw-$LLVM_MINGW-ucrt-macos-universal.tar.xz" "$LLVM_MINGW_SHA256"
fetch "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL/openssl-$OPENSSL.tar.gz" \
      "openssl-$OPENSSL.tar.gz" "$OPENSSL_SHA256"
fetch "https://www.libssh.org/files/0.12/libssh-$LIBSSH.tar.xz" "libssh-$LIBSSH.tar.xz" "$LIBSSH_SHA256"

host_qt=$(brew --prefix qtbase 2>/dev/null || echo /nonexistent)
host_qt_version=$("$host_qt/bin/qmake" -query QT_VERSION 2>/dev/null || echo none)
if [ "$host_qt_version" != "$QT_VERSION" ]; then
    echo "Homebrew's Qt is $host_qt_version, not $QT_VERSION: using Qt's macOS qtbase for the host tools."
    fetch "https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6120/qt6_6120/qt.qt6.6120.clang_64/$QT_HOST_PACKAGE" \
          "$QT_HOST_PACKAGE" "$QT_HOST_SHA256"
    if [ ! -d "$work/host-qt" ]; then
        mkdir "$work/host-qt" && (cd "$work/host-qt" && 7zz x -y "$dl/$QT_HOST_PACKAGE" >/dev/null)
    fi
    host_qt=$(dirname "$(dirname "$(find "$work/host-qt" -maxdepth 4 -path '*/libexec/moc' | head -n 1)")")
    [ -x "$host_qt/libexec/moc" ] || { echo "Can't find Qt in $work/host-qt" >&2; exit 1; }
fi

# --- Unpack -------------------------------------------------------------------
if [ ! -d "$work/qt" ]; then
    mkdir "$work/qt" && (cd "$work/qt" && 7zz x -y "$dl/$QT_PACKAGE" >/dev/null)
fi
# The archive unpacks either straight into qt/ or into a
# <version>/llvm-mingw*_64 folder, depending on how Qt packaged it.
qt=$(dirname "$(dirname "$(find "$work/qt" -maxdepth 4 -path '*/bin/Qt6Core.dll' | head -n 1)")")
[ -f "$qt/bin/Qt6Core.dll" ] || { echo "Can't find Qt in $work/qt" >&2; exit 1; }

if [ ! -d "$work/llvm-mingw" ]; then
    tar -xf "$dl/llvm-mingw-$LLVM_MINGW-ucrt-macos-universal.tar.xz" -C "$work"
    mv "$work/llvm-mingw-$LLVM_MINGW-ucrt-macos-universal" "$work/llvm-mingw"
fi
PATH="$work/llvm-mingw/bin:$PATH"
export PATH
export TEXTEDITOR_WINDOWS_ROOTS="$deps:$qt:$work/llvm-mingw/x86_64-w64-mingw32"
toolchain=$native/tools/llvm-mingw-toolchain.cmake

# --- OpenSSL (libcrypto, for libssh) --------------------------------------------
if [ ! -f "$deps/lib/libcrypto.dll.a" ]; then
    rm -rf "$work/openssl-$OPENSSL"
    tar -xzf "$dl/openssl-$OPENSSL.tar.gz" -C "$work"
    (
        cd "$work/openssl-$OPENSSL"
        ./Configure mingw64 --cross-compile-prefix=x86_64-w64-mingw32- \
            CC=clang CXX=clang++ AR=ar RANLIB=ranlib RC=windres \
            shared no-apps no-docs no-tests no-legacy no-module \
            --prefix="$deps" --libdir=lib
        make -j"$jobs" build_libs
        make install_dev install_runtime_libs
    )
fi

# --- libssh ------------------------------------------------------------------------
if [ ! -f "$deps/lib/cmake/libssh/libssh-config.cmake" ]; then
    rm -rf "$work/libssh-$LIBSSH" "$work/libssh-build"
    tar -xJf "$dl/libssh-$LIBSSH.tar.xz" -C "$work"
    cmake -S "$work/libssh-$LIBSSH" -B "$work/libssh-build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$deps" -DOPENSSL_ROOT_DIR="$deps" \
        -DBUILD_SHARED_LIBS=ON -DWITH_SERVER=OFF -DWITH_ZLIB=OFF -DWITH_GSSAPI=OFF \
        -DWITH_EXAMPLES=OFF -DUNIT_TESTING=OFF -DCLIENT_TESTING=OFF -DWITH_PCAP=OFF
    cmake --build "$work/libssh-build" -j"$jobs"
    cmake --install "$work/libssh-build"
fi

# --- TextEditor+ ----------------------------------------------------------------------
cmake -S "$native" -B "$work/app" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt;$deps" \
    -DQT_HOST_PATH="$host_qt" -DQT_HOST_PATH_CMAKE_DIR="$host_qt/lib/cmake" \
    -DTEXTEDITOR_TESTS=OFF
cmake --build "$work/app" -j"$jobs"

# --- The portable folder ------------------------------------------------------------
name="TextEditor+-$version-windows-x64"
out=$work/$name
rm -rf "$out" && mkdir -p "$out/platforms" "$out/styles"
cp "$work/app/TextEditor+.exe" "$out/"
for module in Core Gui Widgets PrintSupport Concurrent; do
    cp "$qt/bin/Qt6$module.dll" "$out/"
done
cp "$qt/plugins/platforms/qwindows.dll" "$out/platforms/"
cp "$qt/plugins/styles/qmodernwindowsstyle.dll" "$out/styles/"
# The C++ runtime, and the MinGW threads library that libssh uses.
for runtime in libc++.dll libunwind.dll libwinpthread-1.dll; do
    cp "$work/llvm-mingw/x86_64-w64-mingw32/bin/$runtime" "$out/"
done
cp "$deps/bin/libssh.dll" "$out/"
cp "$deps"/bin/libcrypto-*.dll "$out/"
cp -R "$native/packaging/windows/README.txt" "$native/packaging/windows/licenses" \
      "$native/packaging/windows/settings" "$out/"
cp "$native/../user-manual/TextEditor+-User-Manual.pdf" "$out/TextEditor+ User Manual.pdf"

# Every DLL that the program and its DLLs import must be in the folder or
# be part of Windows.
echo "Checking DLL dependencies"
missing=0
for binary in "$out"/*.exe "$out"/*.dll "$out"/*/*.dll; do
    for dll in $(x86_64-w64-mingw32-objdump -p "$binary" | sed -n 's/.*DLL Name: //p'); do
        lower=$(echo "$dll" | tr 'A-Z' 'a-z')
        case "$lower" in
            api-ms-win-*|kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|\
            oleaut32.dll|comdlg32.dll|winspool.drv|ws2_32.dll|crypt32.dll|bcrypt.dll|\
            imm32.dll|winmm.dll|version.dll|netapi32.dll|userenv.dll|uxtheme.dll|dwmapi.dll|\
            shcore.dll|setupapi.dll|d3d11.dll|d3d12.dll|dxgi.dll|dcomp.dll|wtsapi32.dll|\
            authz.dll|mpr.dll|ntdll.dll|shlwapi.dll|comctl32.dll|iphlpapi.dll|secur32.dll|\
            d3d9.dll|opengl32.dll|dwrite.dll|d2d1.dll|propsys.dll|runtimeobject.dll|\
            ucrtbase.dll|ncrypt.dll|cfgmgr32.dll|hid.dll|oleacc.dll|wininet.dll|\
            windowscodecs.dll|msimg32.dll|usp10.dll|rpcrt4.dll|dnsapi.dll|normaliz.dll|mscms.dll) ;;
            *) [ -e "$out/$dll" ] || { echo "  MISSING: $dll (needed by ${binary#$out/})"; missing=1; } ;;
        esac
    done
done
[ "$missing" = 0 ] || { echo "Some DLLs are missing." >&2; exit 1; }

# ASLR, DEP and Control Flow Guard on the program.
echo "Security flags of TextEditor+.exe:"
x86_64-w64-mingw32-objdump -p "$out/TextEditor+.exe" | sed -n '/DllCharacteristics/,/^[A-Z]/p' | sed 's/^/  /' || true

release=$native/../release
mkdir -p "$release"
rm -f "$release/$name.zip"
(cd "$work" && zip -qr -X "$release/$name.zip" "$name")
echo
echo "Built $release/$name.zip"
shasum -a 256 "$release/$name.zip"
