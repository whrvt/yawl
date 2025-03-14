#!/usr/bin/env bash
# Script to download and build dependencies with minimal configurations

set -e

LIBUNISTRING_VERSION="1.3"
LIBIDN2_VERSION="2.3.7"
PSL_VERSION="0.21.5"
LIBZ_VERSION="2.2.4"
XZ_VERSION="5.6.4"
ZSTD_VERSION="1.5.7"
OPENSSL_VERSION="3.2.1"
CURL_VERSION="8.12.1"
GDK_PIXBUF_VERSION="2.42.12"
LIBNOTIFY_VERSION="0.8.4"
JSON_GLIB_VERSION="1.10.6"
LIBARCHIVE_VERSION="3.7.7"

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
    libunistring)
        if [ ! -d "libunistring-$LIBUNISTRING_VERSION" ]; then
            echo "Downloading libunistring-$LIBUNISTRING_VERSION..."
            download_file "https://ftp.gnu.org/gnu/libunistring/libunistring-$LIBUNISTRING_VERSION.tar.xz" "libunistring.tar.xz"
            tar -xf libunistring.tar.xz
            rm libunistring.tar.xz
        fi

        cd "libunistring-$LIBUNISTRING_VERSION"

        ./configure \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS $PTHREAD_EXTLIBS"

        make -j"$JOBS"
        make install
        ;;

    libidn2)
        if [ ! -d "libidn2-$LIBIDN2_VERSION" ]; then
            echo "Downloading libidn2-$LIBIDN2_VERSION..."
            download_file "https://ftp.gnu.org/gnu/libidn/libidn2-$LIBIDN2_VERSION.tar.gz" "libidn2.tar.gz"
            tar -xzf libidn2.tar.gz
            rm libidn2.tar.gz
        fi

        cd "libidn2-$LIBIDN2_VERSION"

        ./configure \
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
            LDFLAGS="$LDFLAGS -l:libunistring.a $PTHREAD_EXTLIBS"

        make -j"$JOBS"
        make install
        ;;

    libpsl)
        if [ ! -d "libpsl-$PSL_VERSION" ]; then
            echo "Downloading libpsl-$PSL_VERSION..."
            download_file "https://github.com/rockdaboot/libpsl/releases/download/$PSL_VERSION/libpsl-$PSL_VERSION.tar.gz" "libpsl.tar.gz"
            tar -xzf libpsl.tar.gz
            rm libpsl.tar.gz
        fi

        cd "libpsl-$PSL_VERSION"

        ./configure \
            --prefix="$PREFIX" \
            --disable-shared \
            --enable-static \
            CC="$CC" \
            CXX="$CXX" \
            CPPFLAGS="$CPPFLAGS" \
            CFLAGS="$CFLAGS" \
            CXXFLAGS="$CXXFLAGS" \
            LDFLAGS="$LDFLAGS -l:libunistring.a -l:libidn2.a $PTHREAD_EXTLIBS"

        make -j"$JOBS"
        make install
        ;;

    libz)
        if [ ! -d "libz-$LIBZ_VERSION" ]; then
            echo "Downloading libz-$LIBZ_VERSION..."
            download_file "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$LIBZ_VERSION.tar.gz" "libz.tar.gz"
            tar -xzf libz.tar.gz
            rm libz.tar.gz
        fi

        cd "zlib-ng-$LIBZ_VERSION"

        CC="$CC" \
        CXX="$CXX" \
        CPPFLAGS="$CPPFLAGS" \
        CFLAGS="$CFLAGS" \
        CXXFLAGS="$CXXFLAGS" \
        LDFLAGS="$LDFLAGS $PTHREAD_EXTLIBS" \
        ./configure \
            --prefix="$PREFIX" \
            --static \
            --zlib-compat

        make -j"$JOBS"
        make install
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
        ./configure \
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
            LDFLAGS="$LDFLAGS $PTHREAD_EXTLIBS"

        make -j"$JOBS"
        make install
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
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -DZSTD_MULTITHREAD"
            "CFLAGS=-pthread $CFLAGS -Wl,-pthread $LDFLAGS -Wl,--whole-archive -lpthread -Wl,--no-whole-archive"
            "CXXFLAGS=$CFLAGS" "LDFLAGS=-pthread $LDFLAGS $PTHREAD_EXTLIBS"
            "AR=$AR l \"$LDFLAGS $PTHREAD_EXTLIBS\""
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
        CPPFLAGS="$CPPFLAGS" \
        CFLAGS="$CFLAGS" \
        CXXFLAGS="$CXXFLAGS" \
        LDFLAGS="$LDFLAGS $PTHREAD_EXTLIBS" \
        ./config \
            --prefix="$PREFIX" \
            --openssldir="$PREFIX/ssl" \
            no-shared \
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

        make -j"$JOBS"

        # OpenSSL's build system has an install_sw target to install only the
        # libraries and not the documentation or other components
        make install_sw
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
        ./configure \
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
            LDFLAGS="$LDFLAGS $PTHREAD_EXTLIBS"

        make -j"$JOBS"
        make install
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
        ./configure \
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
            LIBS="-l:libunistring.a -l:libidn2.a -l:libpsl.a -l:libz.a -l:libssl.a -l:libcrypto.a $PTHREAD_EXTLIBS" \
            LDFLAGS="$LDFLAGS"

        make -j"$JOBS"
        make install

        # Copy the auto-generated ca certificate data to a header we can use for yawl 
        # (contains the `const unsigned char curl_ca_embed[]` blob of data)
        echo "#pragma once" > "$PREFIX/include/curl/ca_cert_embed.h"
        cat "src/tool_ca_embed.c" >> "$PREFIX/include/curl/ca_cert_embed.h"
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
        install -Dm755 /dev/stdin "$PWD"/pkgconfigstatic <<EOF
#!/usr/bin/env bash
pkg-config --static "\$@"
EOF
        rm -f "$PREFIX/lib/pkgconfig/{gthread*.pc,gobject*.pc,glib*.pc,gmodule-no-export*.pc,gmodule-export*.pc,gmodule*.pc,girepository*.pc,gio-unix*.pc,gio*.pc,gdk-pixbuf*.pc}"
        find "${PREFIX:?}"/ '(' -iregex ".*deps/prefix.*glib.*" -o -iregex ".*deps/prefix.*ffi.*" -o -iregex ".*deps/prefix.*pcre.*" ')' -exec rm -rf '{''}' '+'
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS $PTHREAD_EXTLIBS" "PKG_CONFIG=$PWD/pkgconfigstatic"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            --default-library=static \
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
                            -Dglib:libmount=disabled \
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
        install -Dm755 /dev/stdin "$PWD"/pkgconfigstatic <<EOF
#!/usr/bin/env bash
pkg-config --static "\$@"
EOF
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS $PTHREAD_EXTLIBS" "PKG_CONFIG=$PWD/pkgconfigstatic"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            --default-library=static \
                            -Dtests=false \
                            -Dintrospection=disabled \
                            -Dman=false \
                            -Dgtk_doc=false \
                            -Ddocbook_docs=disabled build . 
        "${FLAGS_MESON[@]}" meson compile -C build
        "${FLAGS_MESON[@]}" meson install -C build
        ;;

    json-glib)
        if [ ! -d "json-glib-$JSON_GLIB_VERSION" ]; then
            echo "Downloading json-glib-$JSON_GLIB_VERSION..."
            download_file "https://gitlab.gnome.org/GNOME/json-glib/-/archive/$JSON_GLIB_VERSION/json-glib-$JSON_GLIB_VERSION.tar.gz" "json-glib.tar.gz"
            tar -xzf json-glib.tar.gz
            rm json-glib.tar.gz
        fi

        cd "json-glib-$JSON_GLIB_VERSION"

        install -Dm755 /dev/stdin "$PWD"/pkgconfigstatic <<EOF
#!/usr/bin/env bash
pkg-config --static "\$@"
EOF
        FLAGS_MESON=("env"
            "CC=$CC" "CXX=$CXX" "CPPFLAGS=$CPPFLAGS -I$PREFIX/include/json-glib-1.0 -I$PREFIX/include/gdk-pixbuf-2.0 -I$PREFIX/include/glib-2.0"
            "CFLAGS=$CFLAGS -fno-exceptions" "CXXFLAGS=$CXXFLAGS" "LDFLAGS=$LDFLAGS $PTHREAD_EXTLIBS" "PKG_CONFIG=$PWD/pkgconfigstatic"
        )
        "${FLAGS_MESON[@]}" meson setup --prefix="$PREFIX" \
                            --bindir "$PREFIX/lib" --includedir "$PREFIX/include" \
                            --buildtype=minsize \
                            --default-library=static \
                            -Dintrospection=disabled \
                            -Ddocumentation=disabled \
                            -Dgtk_doc=disabled \
                            -Dman=false \
                            -Dtests=false \
                            -Dconformance=false \
                            -Dnls=disabled \
                            -Dinstalled_tests=false build . 
        "${FLAGS_MESON[@]}" meson compile -C build
        "${FLAGS_MESON[@]}" meson install -C build
        ;;

    *)
        echo "Unknown library: $LIB"
        exit 1
        ;;
esac

echo "$LIB built and installed successfully to $PREFIX"
exit 0
