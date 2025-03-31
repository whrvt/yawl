#!/usr/bin/env bash
# Script to download and build dependencies with minimal configurations

set -e

ZIG_VERSION="0.15.0-dev.155+acfdad858"
MIMALLOC_VERSION="3.0.3"
LIBUNISTRING_VERSION="1.3"
LIBIDN2_VERSION="2.3.7"
PSL_VERSION="0.21.5"
LIBZ_VERSION="2.2.4"
XZ_VERSION="5.6.4"
ZSTD_VERSION="1.5.7"
OPENSSL_VERSION="3.2.1"
CURL_VERSION="8.12.1"
LIBFFI_VERSION="3.4.7"
GDK_PIXBUF_VERSION="2.42.12"
LIBNOTIFY_VERSION="0.8.4"
JSON_GLIB_VERSION="1.10.6"
LIBARCHIVE_VERSION="3.7.7"
LIBCAP_VERSION="2.27" # Newer versions have useless Go stuff

# Parse arguments
LIB="$1"
BUILDDIR="$2"
PREFIX="$3"

if [ -z "$LIB" ] || [ -z "$BUILDDIR" ] || [ -z "$PREFIX" ]; then
    echo "Usage: $0 <library> <build-dir> <prefix>"
    exit 1
fi

mkdir -p "$BUILDDIR" "$PREFIX"
cd "$BUILDDIR"

download_file() {
    local url="$1"
    local output="$2"

    if command -v wget >/dev/null 2>&1; then
        wget -q "$url" -O "$output"
    elif command -v curl >/dev/null 2>&1; then
        curl -sSL "$url" -o "$output"
    else
        echo "Error: Neither wget nor curl found"
        return 1
    fi
}

JOBS=$(nproc)

case "$LIB" in
    zig)
        [ -d "zig" ] && rm -rf zig
        echo "Downloading zig-$ZIG_VERSION..."
        download_file "https://ziglang.org/builds/zig-linux-x86_64-$ZIG_VERSION.tar.xz" "zig.tar.xz"
        tar -xf zig.tar.xz
        rm zig.tar.xz
        mv zig-linux-x86_64-$ZIG_VERSION "zig/"
        ;;

    mimalloc)
        if [ ! -d "mimalloc-$MIMALLOC_VERSION" ]; then
            echo "Downloading mimalloc-$MIMALLOC_VERSION..."
            download_file "https://github.com/microsoft/mimalloc/archive/refs/tags/v$MIMALLOC_VERSION.tar.gz" "mimalloc.tar.gz"
            tar -xf mimalloc.tar.gz
            rm mimalloc.tar.gz
        fi

        cd "mimalloc-$MIMALLOC_VERSION"

        # Multiple definition issue with musl/zig headers
        sed -i 's|#include <linux/prctl.h>|#define PR_GET_THP_DISABLE 42\n#define PR_SET_VMA 0x53564d41\n#define PR_SET_VMA_ANON_NAME 0|g' src/prim/unix/prim.c

        mkdir -p out/release
        cd out/release
        env CXX="$CXX" LD="$CXX" CFLAGS="$CXXFLAGS" \
            CXXFLAGS="$CXXFLAGS -xc++" LDFLAGS="$CXXFLAGS -xnone" cmake \
            -DCMAKE_C_COMPILER="$ZIGCC" \
            -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
            -DCMAKE_CXX_COMPILER="$CXX" \
            -DCMAKE_CXX_FLAGS="$CXXFLAGS -xc++" \
            -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DMI_OPT_ARCH=OFF \
            -DMI_USE_CXX=ON \
            -DMI_NO_OPT_ARCH=ON \
            -DMI_BUILD_SHARED=OFF \
            -DMI_BUILD_TESTS=OFF \
            -DMI_INSTALL_TOPLEVEL=ON \
            -DMI_LIBC_MUSL=ON \
            -DMI_BUILD_STATIC=OFF \
            -DMI_BUILD_OBJECT=ON ../.. && \
        make -j"$JOBS" && \
        make install || exit 1
        ;;

    libunistring)
        if [ ! -d "libunistring-$LIBUNISTRING_VERSION" ]; then
            echo "Downloading libunistring-$LIBUNISTRING_VERSION..."
            download_file "https://ftp.gnu.org/gnu/libunistring/libunistring-$LIBUNISTRING_VERSION.tar.xz" "libunistring.tar.xz"
            tar -xf libunistring.tar.xz
            rm libunistring.tar.xz
        fi

        cd "libunistring-$LIBUNISTRING_VERSION"

        cp $cch . &&
        ./configure -C \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    libidn2)
        if [ ! -d "libidn2-$LIBIDN2_VERSION" ]; then
            echo "Downloading libidn2-$LIBIDN2_VERSION..."
            download_file "https://ftp.gnu.org/gnu/libidn/libidn2-$LIBIDN2_VERSION.tar.gz" "libidn2.tar.gz"
            tar -xzf libidn2.tar.gz
            rm libidn2.tar.gz
        fi

        cd "libidn2-$LIBIDN2_VERSION"

        cp $cch . &&
        ./configure -C  \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            --with-included-unistring=no \
            --disable-nls \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="-lunistring $LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    libpsl)
        if [ ! -d "libpsl-$PSL_VERSION" ]; then
            echo "Downloading libpsl-$PSL_VERSION..."
            download_file "https://github.com/rockdaboot/libpsl/releases/download/$PSL_VERSION/libpsl-$PSL_VERSION.tar.gz" "libpsl.tar.gz"
            tar -xzf libpsl.tar.gz
            rm libpsl.tar.gz
        fi

        cd "libpsl-$PSL_VERSION"

        cp $cch . &&
        ./configure -C  \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="-lunistring -lidn2 $LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    libz)
        if [ ! -d "libz-$LIBZ_VERSION" ]; then
            echo "Downloading libz-$LIBZ_VERSION..."
            download_file "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$LIBZ_VERSION.tar.gz" "libz.tar.gz"
            tar -xzf libz.tar.gz
            rm libz.tar.gz
        fi

        cd "zlib-ng-$LIBZ_VERSION"
        sed -i 's|noltoflag="-fno-lto"||g' configure
        CC="$CC" \
        CXX="$CXX" \
        CPPFLAGS="$CPPFLAGS" \
        CFLAGS="$CFLAGS -Wl,-z,undefs" \
        CXXFLAGS="$CXXFLAGS" \
        LDFLAGS="$LDFLAGS -Wl,-z,undefs" \
        ./configure \
            --prefix="$PREFIX" \
            --static \
            --zlib-compat

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    xz)
        if [ ! -d "xz-$XZ_VERSION" ]; then
            echo "Downloading xz-$XZ_VERSION..."
            download_file "https://github.com/tukaani-project/xz/releases/download/v$XZ_VERSION/xz-$XZ_VERSION.tar.gz" "xz.tar.gz"
            tar -xzf xz.tar.gz
            rm xz.tar.gz
        fi

        cd "xz-$XZ_VERSION"

        # Configure xz with minimal features
        # We only need the liblzma library for libarchive
        cp $cch . &&
        ./configure -C \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            --disable-dependency-tracking \
            --disable-xz \
            --disable-xzdec \
            --disable-lzmadec \
            --disable-lzmainfo \
            --disable-scripts \
            --disable-doc \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    zstd)
        if [ ! -d "zstd-$ZSTD_VERSION" ]; then
            echo "Downloading zstd-$ZSTD_VERSION..."
            download_file "https://github.com/facebook/zstd/releases/download/v$ZSTD_VERSION/zstd-$ZSTD_VERSION.tar.gz" "zstd.tar.gz"
            tar -xzf zstd.tar.gz
            rm zstd.tar.gz
        fi

        cd "zstd-$ZSTD_VERSION/lib"

        FLAGS_ZST=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -DZSTD_MULTITHREAD -DZSTD_HAVE_WEAK_SYMBOLS=0 -DZSTD_TRACE=0"
            "CFLAGS=$CFLAGS" "CXXFLAGS=$CFLAGS" "LDFLAGS=$LDFLAGS"
        )

        "${FLAGS_ZST[@]}" make -j"$JOBS" libzstd.a && "${FLAGS_ZST[@]}" make -j"$JOBS" libzstd.pc && \
        install -Dm644 "libzstd.pc" "$PREFIX/lib/pkgconfig/libzstd.pc" && \
        cd .. && \
        install -Dm644 "lib/libzstd.a" "$PREFIX/lib/libzstd.a" && \
        install -Dm644 "lib/zstd.h" "$PREFIX/include/zstd.h" && \
        install -Dm644 "lib/zdict.h" "$PREFIX/include/zdict.h" && \
        install -Dm644 "lib/zstd_errors.h" "$PREFIX/include/zstd_errors.h"
        ;;

    openssl)
        if [ ! -d "openssl-$OPENSSL_VERSION" ]; then
            echo "Downloading openssl-$OPENSSL_VERSION..."
            download_file "https://www.openssl.org/source/openssl-$OPENSSL_VERSION.tar.gz" "openssl.tar.gz"
            tar -xzf openssl.tar.gz
            rm openssl.tar.gz
        fi

        rm -rf "${PREFIX:?}/lib64"
        ln -sf "$PREFIX/lib" "$PREFIX/lib64"

        cd "openssl-$OPENSSL_VERSION"

        # OpenSSL has a unique build system
        # Configure with minimal options, only what's needed for HTTPS
        # Disable unnecessary engines, ciphers, and protocols
        CC="$CC" \
        CXX="$CXX" \
        CPPFLAGS="$CPPFLAGS -U_GNU_SOURCE" \
        CFLAGS="$CFLAGS -U_GNU_SOURCE" \
        CXXFLAGS="$CXXFLAGS -U_GNU_SOURCE" \
        LDFLAGS="$LDFLAGS" \
        ./config \
            --prefix="$PREFIX" \
            --openssldir="$PREFIX/ssl" \
            threads \
            no-shared \
            no-legacy \
            no-afalgeng \
            no-quic \
            no-weak-ssl-ciphers \
            no-ssl3 \
            no-dtls \
            no-dtls1 \
            no-dtls1_2 \
            no-srp \
            no-psk \
            no-ocsp \
            no-ts \
            no-cms \
            no-rfc3779 \
            no-seed \
            no-idea \
            no-md4 \
            no-mdc2 \
            no-whirlpool \
            no-bf \
            no-cast \
            no-camellia \
            no-scrypt \
            no-rc2 \
            no-rc4 \
            no-rc5 \
            no-ssl3-method \
            no-tests \
            no-apps \
            no-docs \
            enable-ec_nistp_64_gcc_128

        make -j"$JOBS" && \
        make install_sw || exit 1
        ;;

    libarchive)
        if [ ! -d "libarchive-$LIBARCHIVE_VERSION" ]; then
            echo "Downloading libarchive-$LIBARCHIVE_VERSION..."
            download_file "https://github.com/libarchive/libarchive/releases/download/v$LIBARCHIVE_VERSION/libarchive-$LIBARCHIVE_VERSION.tar.gz" "libarchive.tar.gz"
            tar -xzf libarchive.tar.gz
            rm libarchive.tar.gz
        fi

        cd "libarchive-$LIBARCHIVE_VERSION"

        # Configure libarchive with minimal features
        # We need tar and xz support for extracting the Steam Runtime
        cp $cch . &&
        ./configure -C \
            --prefix="$PREFIX" \
            --enable-bsdtar=static \
            --disable-shared \
            --enable-static \
            --disable-dependency-tracking \
            --without-bz2lib \
            --without-libb2 \
            --without-iconv \
            --without-lz4 \
            --with-zstd \
            --disable-acl \
            --disable-xattr \
            --without-xml2 \
            --without-expat \
            --with-openssl \
            --with-lzma="$PREFIX" \
            --with-zlib="$PREFIX" \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    curl)
        if [ ! -d "curl-$CURL_VERSION" ]; then
            echo "Downloading curl-$CURL_VERSION..."
            download_file "https://curl.se/download/curl-$CURL_VERSION.tar.gz" "curl.tar.gz"
            tar -xzf curl.tar.gz
            rm curl.tar.gz
        fi

        cd "curl-$CURL_VERSION"

        # Configure curl with minimal features
        # We only need HTTP and HTTPS support for downloading the Steam Runtime
        cp $cch . &&
        ./configure -C \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            --disable-ldap \
            --disable-sspi \
            --without-librtmp \
            --disable-dependency-tracking \
            --with-zstd \
            --without-brotli \
            --with-libidn2 \
            --without-libssh2 \
            --without-nghttp2 \
            --without-nghttp3 \
            --without-ngtcp2 \
            --with-ca-embed="$CERT_BUNDLE" \
            --without-ca-path \
            --without-ca-bundle \
            --without-ca-fallback \
            --disable-manual \
            --disable-libcurl-option \
            --enable-verbose \
            --disable-ftp \
            --disable-file \
            --disable-ldap \
            --disable-ldaps \
            --disable-rtsp \
            --disable-dict \
            --disable-telnet \
            --disable-tftp \
            --disable-pop3 \
            --disable-imap \
            --disable-smb \
            --disable-smtp \
            --disable-gopher \
            --disable-mqtt \
            --enable-http \
            --with-openssl \
            CC="$CC" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LIBS="-lunistring -lidn2 -lpsl -lz -lssl -lcrypto" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    libffi)
        # Required for gdk/glib stuff
        if [ ! -d "libffi-$LIBFFI_VERSION" ]; then
            echo "Downloading libffi-$LIBFFI_VERSION..."
            download_file "https://github.com/libffi/libffi/releases/download/v$LIBFFI_VERSION/libffi-$LIBFFI_VERSION.tar.gz" "libffi.tar.gz"
            tar -xzf libffi.tar.gz
            rm libffi.tar.gz
        fi

        cd "libffi-$LIBFFI_VERSION"

        cp $cch . &&
        ./configure -C \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            --enable-portable-binary \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS" && \
        make install || exit 1
        ;;

    gdk-pixbuf)
        if [ ! -d "gdk-pixbuf-$GDK_PIXBUF_VERSION" ]; then
            echo "Downloading gdk-pixbuf-$GDK_PIXBUF_VERSION (required for libnotify)..."
            download_file "https://gitlab.gnome.org/GNOME/gdk-pixbuf/-/archive/$GDK_PIXBUF_VERSION/gdk-pixbuf-$GDK_PIXBUF_VERSION.tar.gz" "gdk-pixbuf.tar.gz"
            tar -xzf gdk-pixbuf.tar.gz
            rm gdk-pixbuf.tar.gz
        fi

        cd "gdk-pixbuf-$GDK_PIXBUF_VERSION"

        # meson is absolutely UNREAL
        sed -i 's|.*thumbnailer.*||g' meson.build
        rm -f "$PREFIX/lib/pkgconfig/{gthread*.pc,gobject*.pc,glib*.pc,gmodule-no-export*.pc,gmodule-export*.pc,gmodule*.pc,girepository*.pc,gio-unix*.pc,gio*.pc,gdk-pixbuf*.pc}"
        find "${PREFIX:?}"/ '(' -iregex ".*deps/prefix.*glib.*" -o -iregex ".*deps/prefix.*pcre.*" ')' -exec rm -rf '{''}' '+'
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS -lm"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            -Ddebug=false \
                            -Dpng=disabled \
                            -Dtiff=disabled \
                            -Djpeg=disabled \
                            -Dgif=disabled \
                            -Dothers=disabled \
                            -Dbuiltin_loaders=none \
                            -Dgtk_doc=false \
                            -Ddocs=false \
                            -Dintrospection=disabled \
                            -Dman=false \
                            -Drelocatable=false \
                            -Dnative_windows_loaders=false \
                            -Dtests=false \
                            -Dinstalled_tests=false \
                            -Dgio_sniffing=false \
                            -Dprefer_static=true \
                            -Db_lto=true \
                            -Db_lto_threads="$JOBS" \
                            -Db_staticpic=true \
                            -Db_sanitize=none \
                            -Ddefault_library=static \
                            -Ddefault_both_libraries=static \
                            -Dpcre2:b_sanitize=none \
                            -Dpcre2:test=false \
                            -Dpcre2:prefer_static=true \
                            -Dpcre2:b_lto=true \
                            -Dpcre2:b_lto_threads="$JOBS" \
                            -Dpcre2:b_staticpic=true \
                            -Dpcre2:default_library=static \
                            -Dpcre2:default_both_libraries=static \
                            -Dgvdb:b_sanitize=none \
                            -Dgvdb:prefer_static=true \
                            -Dgvdb:b_lto=true \
                            -Dgvdb:b_lto_threads="$JOBS" \
                            -Dgvdb:b_staticpic=true \
                            -Dgvdb:default_library=static \
                            -Dgvdb:default_both_libraries=static \
                            -Dglib:b_sanitize=none \
                            -Dglib:prefer_static=true \
                            -Dglib:b_lto=true \
                            -Dglib:b_lto_threads="$JOBS" \
                            -Dglib:b_staticpic=true \
                            -Dglib:default_library=static \
                            -Dglib:default_both_libraries=static \
                            -Dglib:man=false \
                            -Dglib:man-pages=disabled \
                            -Dglib:dtrace=disabled \
                            -Dglib:systemtap=disabled \
                            -Dglib:sysprof=disabled \
                            -Dglib:documentation=false \
                            -Dglib:gtk_doc=false \
                            -Dglib:tests=false \
                            -Dglib:installed_tests=false \
                            -Dglib:nls=disabled \
                            -Dglib:bsymbolic_functions=false \
                            -Dglib:oss_fuzz=disabled \
                            -Dglib:glib_debug=disabled \
                            -Dglib:glib_assert=false \
                            -Dglib:glib_checks=false \
                            -Dglib:libelf=disabled \
                            -Dglib:introspection=disabled build .
        sed -i 's|.*atomic_dep = .*|atomic_dep = []|g' subprojects/glib/meson.build # this is the reason the build was failing without musl installed...?
        "${FLAGS_MESON[@]}" meson compile -C build
        "${FLAGS_MESON[@]}" meson install -C build || true # WTF?

        # PISS OFF
        find "$PWD"/ -iregex ".*meson-private.*\.pc" -execdir cp '{''}' "$PREFIX/lib/pkgconfig" ';'
        find "$PWD/build/subprojects/glib"/{gio,gobject} -type f -executable -execdir cp '{''}' "$PREFIX/lib" ';'
        find "$PWD/build/subprojects/glib/glib"/ -type f -iregex ".*\.h" -execdir cp '{''}' "$PREFIX/include/glib-2.0/" ';'
        find "$PWD/build/gdk-pixbuf"/ -type f -iregex ".*\.h" -execdir cp '{''}' "$PREFIX/include/gdk-pixbuf-2.0/gdk-pixbuf" ';'
        ;;

    libnotify)
        if [ ! -d "libnotify-$LIBNOTIFY_VERSION" ]; then
            echo "Downloading libnotify-$LIBNOTIFY_VERSION..."
            download_file "https://github.com/GNOME/libnotify/archive/refs/tags/$LIBNOTIFY_VERSION.tar.gz" "libnotify.tar.gz"
            tar -xzf libnotify.tar.gz
            rm libnotify.tar.gz
        fi

        cd "libnotify-$LIBNOTIFY_VERSION"

        sed -i 's|.*subdir.*tools.*||g' meson.build
        sed -i 's|libnotify_lib = shared|libnotify_lib = static|g' "$PWD/libnotify/meson.build"
        sed -i 's|.*LT_CURRENT.*||g' "$PWD/libnotify/meson.build"
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            -Dtests=false \
                            -Dintrospection=disabled \
                            -Dman=false \
                            -Dgtk_doc=false \
                            -Ddocbook_docs=disabled \
                            -Dprefer_static=true \
                            -Db_lto=true \
                            -Db_lto_threads="$JOBS" \
                            -Db_sanitize=none \
                            -Ddefault_library=static \
                            -Ddefault_both_libraries=static \
                            -Db_staticpic=true build . 
        "${FLAGS_MESON[@]}" meson compile -C build && \
        "${FLAGS_MESON[@]}" meson install -C build || exit 1
        ;;

    json-glib)
        if [ ! -d "json-glib-$JSON_GLIB_VERSION" ]; then
            echo "Downloading json-glib-$JSON_GLIB_VERSION..."
            download_file "https://gitlab.gnome.org/GNOME/json-glib/-/archive/$JSON_GLIB_VERSION/json-glib-$JSON_GLIB_VERSION.tar.gz" "json-glib.tar.gz"
            tar -xzf json-glib.tar.gz
            rm json-glib.tar.gz
        fi

        cd "json-glib-$JSON_GLIB_VERSION"
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            -Dintrospection=disabled \
                            -Ddocumentation=disabled \
                            -Dgtk_doc=disabled \
                            -Dman=false \
                            -Dtests=false \
                            -Dconformance=false \
                            -Dnls=disabled \
                            -Dprefer_static=true \
                            -Dinstalled_tests=false \
                            -Db_sanitize=none \
                            -Db_lto=true \
                            -Db_lto_threads="$JOBS" \
                            -Ddefault_library=static \
                            -Ddefault_both_libraries=static \
                            -Db_staticpic=true build . 
        "${FLAGS_MESON[@]}" meson compile -C build && \
        "${FLAGS_MESON[@]}" meson install -C build || exit 1
        ;;

    libcap)
        # Required for nsenter
        if [ ! -d "libcap-$LIBCAP_VERSION" ]; then
            echo "Downloading libcap-$LIBCAP_VERSION..."
            download_file "https://web.git.kernel.org/pub/scm/libs/libcap/libcap.git/snapshot/libcap-$LIBCAP_VERSION.tar.gz" "libcap.tar.gz"
            tar -xzf libcap.tar.gz
            rm libcap.tar.gz
        fi

        cd "libcap-$LIBCAP_VERSION"

        sed -i 's|-fPIC||g' Make.Rules
        sed -i 's|^CC :=|CC ?=|g' Make.Rules
        sed -i 's|CFLAGS := -O2 -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64|CFLAGS := $(CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64|g' Make.Rules
        sed -i 's|-Wl,-x -shared|-static|g' Make.Rules
    cat >Makefile <<'EOF'
topdir=$(shell pwd)
include Make.Rules
all install clean: %: %-here
	$(MAKE) -C libcap libcap.a
all-here:
install-here:
clean-here:
	$(LOCALCLEAN)
distclean: clean
	$(DISTCLEAN)
EOF
        make prefix="$PREFIX" \
            CC="$ZIGCC" \
            LD="$ZIGCC" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CXXFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS" -j"$JOBS" && \
        cp libcap/libcap.a "$PREFIX/lib" && \
        cp -r libcap/include/uapi/linux "$PREFIX/include" && \
        cp libcap/include/sys/capability.h "$PREFIX/include"
        ;;

    cacert)
        download_file "https://curl.se/ca/cacert.pem" "cacert.pem" || exit 1
        ;;

    bwrap)
        download_file "https://gitlab.com/apparmor/apparmor/-/raw/8ed0bddcc9299b475356aacc4ee4fb523715649b/profiles/apparmor/profiles/extras/bwrap-userns-restrict" "bwrap-userns-restrict" || exit 1
        ;;

    *)
        echo "Unknown library: $LIB"
        exit 1
        ;;
esac

echo "$LIB built and installed successfully to $PREFIX"
exit 0
