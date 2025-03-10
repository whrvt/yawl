AC_INIT([yawl], [0.5.1], [william@horvath.blog])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/yawl.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([dist]) # default `make install`s to ./dist/bin/ (modify with --prefix=)

AC_PROG_CC([musl-clang])
if test -z "$CC"; then
    AC_MSG_ERROR([musl-clang not found])
fi
AC_PROG_CXX([musl-clang])
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL

AC_DEFUN([AC_PROG_STRIP], [
    AC_CHECK_PROG([STRIP], [strip], [strip])
    if test -z "$STRIP"; then
        AC_MSG_ERROR([strip not found])
    fi
])
AC_PROG_STRIP

AC_DEFUN([AC_PROG_AR], [
    AC_CHECK_PROG([AR], [ar], [ar])
    if test -z "$AR"; then
        AC_MSG_ERROR([ar not found])
    fi
])
AC_PROG_AR

# Check pkg-config
PKG_PROG_PKG_CONFIG

# Set up build directories
deps_builddir="$PWD/deps/build"
deps_prefix="$PWD/deps/prefix"
mkdir -p "$deps_builddir" "$deps_prefix"

musl_is_dumb="$deps_builddir/musl_temp"
mkdir -p "$musl_is_dumb"

AS_IF([test -n "$MUSL_INCDIR"], [],
    [MUSL_INCDIR="/usr/lib/musl/include/sys"]
)
AC_SUBST([MUSL_INCDIR])
AC_CHECK_FILE([$MUSL_INCDIR], [],
    [AC_MSG_ERROR([Could not find the musl header files (set MUSL_INCDIR to specify a musl/include folder manually)])
])
AS_IF([test -L "$musl_is_dumb/linux"],
    [unlink "$musl_is_dumb/linux"]
)
$LN_S "$MUSL_INCDIR" "$musl_is_dumb/linux"

CLANGVER="$($CC --version | head -n 1 | cut -f3 -d' ' | cut -f1 -d'.')"

AS_IF([test -n "$CLANG_INCDIR"], [],
    [CLANG_INCDIR="/usr/lib/clang/$CLANGVER/include"]
)
AC_SUBST([CLANG_INCDIR])
AC_CHECK_FILE([$CLANG_INCDIR], [],
    [AC_MSG_ERROR([Could not find the clang header files (set CLANG_INCDIR to specify a clang/<version>/include folder manually)])
])

# Always enforce static linking, and make small binaries
export CC="$CC --target=x86_64-linux-musl"
export CXX="$CXX --target=x86_64-linux-musl"
shared_includes="-I$musl_is_dumb -I$CLANG_INCDIR -I$deps_prefix/include"
shared_cflags="$shared_includes -Oz -ffunction-sections -fdata-sections -static -march=x86-64 -mtune=generic \
               -Wno-unused-command-line-argument"
export CPPFLAGS="$CPPFLAGS -D__STDC_NO_ATOMICS__ $shared_includes"
export CFLAGS="$CFLAGS $shared_cflags -std=gnu17 -Wno-gnu-include-next"
export CXXFLAGS="$CXXFLAGS $shared_cflags -std=gnu++17"
export LDFLAGS="$LDFLAGS -L$deps_prefix/lib -static -Wl,--gc-sections,-s -fuse-ld=musl-clang"
export STRIP="$STRIP"
export AR="$AR"

export PTHREAD_EXTLIBS="-Wl,--whole-archive -lpthread -Wl,--no-whole-archive"

AC_SUBST([CC])
AC_SUBST([CXX])
AC_SUBST([CPPFLAGS])
AC_SUBST([CFLAGS])
AC_SUBST([CXXFLAGS])
AC_SUBST([LDFLAGS])
AC_SUBST([STRIP])
AC_SUBST([AR])
AC_SUBST([PTHREAD_EXTLIBS])

# Get number of processors for parallel builds
AC_DEFUN([AX_COUNT_CPUS], [
    AC_CHECK_PROG([GETCONF], [getconf], [getconf])
    AS_IF([test -n "$GETCONF"],
        [n_cpus=$($GETCONF _NPROCESSORS_ONLN 2>/dev/null)],
        [n_cpus=1])
    AS_IF([test -z "$n_cpus" -o "$n_cpus" -le "0"], [n_cpus=1])
    AC_SUBST([CPU_COUNT], [$n_cpus])
])
AX_COUNT_CPUS

# Set make flags for parallel build
export MAKEFLAGS="-j$CPU_COUNT"
AC_SUBST([MAKEFLAGS])

# Add build-aux to PATH for helper scripts
export PATH="$PWD/build-aux:$PATH"

export PKG_CONFIG="pkg-config --static"
AC_SUBST([PKG_CONFIG])
export PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
AC_SUBST([PKG_CONFIG_PATH])
export PKG_CONFIG_LIBDIR="$deps_prefix/lib:$PKG_CONFIG_LIBDIR"
AC_SUBST([PKG_CONFIG_LIBDIR])

# Check for required headers
AC_CHECK_HEADERS([stdlib.h string.h unistd.h sys/stat.h linux/mman.h])

if test "x$ac_cv_header_linux_mman_h" != "xyes"
then
    AC_MSG_ERROR([Could not find the linux/mman.h header (MUSL_INCDIR=$MUSL_INCDIR is likely missing)])
fi

AS_IF([test -n "$CERT_BUNDLE"], [],
    [CERT_BUNDLE="/etc/ssl/certs/ca-bundle.crt"]
)
export CERT_BUNDLE
AC_SUBST([CERT_BUNDLE])

AC_CHECK_FILE([$CERT_BUNDLE], [],
    [AC_MSG_ERROR([Could not find an ssl certificate bundle (set CERT_BUNDLE to specify a ca-bundle.crt manually)])]
)

# Download and build libunistring (needed for idn and psl)
if ! test -f "$deps_prefix/lib/libunistring.a"; then
    AC_MSG_NOTICE([Building libunistring])
    sh build-aux/download-deps.sh libunistring "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libunistring])
fi

# Download and build libidn2 (needed for curl)
if ! test -f "$deps_prefix/lib/libidn2.a"; then
    AC_MSG_NOTICE([Building libidn2])
    sh build-aux/download-deps.sh libidn2 "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libidn2])
fi

# Download and build libpsl (needed for curl)
if ! test -f "$deps_prefix/lib/libpsl.a"; then
    AC_MSG_NOTICE([Building libpsl])
    sh build-aux/download-deps.sh libpsl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libpsl])
fi

# Download and build libz
if ! test -f "$deps_prefix/lib/libz.a"; then
    AC_MSG_NOTICE([Building libz])
    sh build-aux/download-deps.sh libz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libz])
fi

# Download and build xz
if ! test -f "$deps_prefix/lib/liblzma.a"; then
    AC_MSG_NOTICE([Building xz])
    sh build-aux/download-deps.sh xz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build xz])
fi

# Download and build zstd
if ! test -f "$deps_prefix/lib/libzstd.a"; then
    AC_MSG_NOTICE([Building zstd])
    sh build-aux/download-deps.sh zstd "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build zstd])
fi

# Download and build openssl
if ! test -f "$deps_prefix/lib/libssl.a" || ! test -f "$deps_prefix/lib/libcrypto.a"; then
    AC_MSG_NOTICE([Building openssl])
    sh build-aux/download-deps.sh openssl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build openssl])
fi

# Download and build libarchive
if ! test -f "$deps_prefix/lib/libarchive.a"; then
    AC_MSG_NOTICE([Building libarchive])
    sh build-aux/download-deps.sh libarchive "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libarchive])
fi

# Download and build curl
if ! test -f "$deps_prefix/lib/libcurl.a"; then
    AC_MSG_NOTICE([Building curl])
    sh build-aux/download-deps.sh curl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build curl])
fi

# The flags are taken from the generated .pc (pkg-config) files, slightly modified.
# Hardcoding them here to avoid needing to ship .pc files.
CURL_CFLAGS="-I$deps_prefix/include -DCURL_STATICLIB -DWITH_GZFILEOP"
CURL_LIBS="-L$deps_prefix/lib -lcurl -lidn2 -lunistring -lpsl -lssl -lcrypto -lzstd -lz -l:libunistring.a -l:libidn2.a -l:libpsl.a -l:libz.a -l:libssl.a -l:libcrypto.a -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -lunistring -lpthread -lzstd -l:libunistring.a -l:libidn2.a -l:libpsl.a -l:libz.a -l:libssl.a -l:libcrypto.a -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -lidn2 -lunistring -lpthread -lz -lzstd -pthread -lssl -lcrypto -ldl -pthread -lpsl"
LIBARCHIVE_CFLAGS="-I$deps_prefix/include -DLIBARCHIVE_STATIC"
LIBARCHIVE_LIBS="-L$deps_prefix/lib -larchive -llzma -lzstd -lz -lcrypto"
LIBIDN2_CFLAGS="-I$deps_prefix/include"
LIBIDN2_LIBS="-L$deps_prefix/lib -lidn2 -lunistring"
LIBLZMA_CFLAGS="-I$deps_prefix/include -DLZMA_API_STATIC"
LIBLZMA_LIBS="-L$deps_prefix/lib -llzma"
LIBPSL_CFLAGS="-I$deps_prefix/include"
LIBPSL_LIBS="-L$deps_prefix/lib -lpsl"
LIBUNISTRING_CFLAGS="-I$deps_prefix/include"
LIBUNISTRING_LIBS="-l:libunistring.a"
LIBZSTD_CFLAGS="-I$deps_prefix/include"
LIBZSTD_LIBS="-L$deps_prefix/lib -lzstd"
LIBZ_CFLAGS="-I$deps_prefix/include -DWITH_GZFILEOP"
LIBZ_LIBS="-L$deps_prefix/lib -lz"
OPENSSL_CFLAGS="-I$deps_prefix/include"
OPENSSL_LIBS="-L$deps_prefix/lib -lssl -lcrypto"

AC_SUBST([LIBUNISTRING_CFLAGS])
AC_SUBST([LIBUNISTRING_LIBS])
AC_SUBST([LIBPSL_CFLAGS])
AC_SUBST([LIBPSL_LIBS])
AC_SUBST([LIBIDN2_CFLAGS])
AC_SUBST([LIBIDN2_LIBS])
AC_SUBST([LIBZ_CFLAGS])
AC_SUBST([LIBZ_LIBS])
AC_SUBST([LIBLZMA_CFLAGS])
AC_SUBST([LIBLZMA_LIBS])
AC_SUBST([LIBZSTD_CFLAGS])
AC_SUBST([LIBZSTD_LIBS])
AC_SUBST([LIBARCHIVE_CFLAGS])
AC_SUBST([LIBARCHIVE_LIBS])
AC_SUBST([OPENSSL_CFLAGS])
AC_SUBST([OPENSSL_LIBS])
AC_SUBST([CURL_CFLAGS])
AC_SUBST([CURL_LIBS])

# Here for reference:

# LIBUNISTRING_CFLAGS="-I$deps_prefix/include"
# LIBUNISTRING_LIBS="-l:libunistring.a"
# LIBPSL_CFLAGS="$($PKG_CONFIG --cflags libpsl)"
# LIBPSL_LIBS="$($PKG_CONFIG --libs libpsl)"
# LIBIDN2_CFLAGS="$($PKG_CONFIG --cflags libidn2)"
# LIBIDN2_LIBS="$($PKG_CONFIG --libs libidn2)"
# LIBZ_CFLAGS="$($PKG_CONFIG --cflags zlib)"
# LIBZ_LIBS="$($PKG_CONFIG --libs zlib)"
# LIBLZMA_CFLAGS="$($PKG_CONFIG --cflags liblzma)"
# LIBLZMA_LIBS="$($PKG_CONFIG --libs liblzma)"
# LIBZSTD_CFLAGS="$($PKG_CONFIG --cflags libzstd)"
# LIBZSTD_LIBS="$($PKG_CONFIG --libs libzstd)"
# LIBARCHIVE_CFLAGS="$($PKG_CONFIG --cflags libarchive)"
# LIBARCHIVE_LIBS="$($PKG_CONFIG --libs libarchive)"
# OPENSSL_CFLAGS="$($PKG_CONFIG --cflags openssl) $($PKG_CONFIG --cflags libcrypto)"
# OPENSSL_LIBS="$($PKG_CONFIG --libs openssl) $($PKG_CONFIG --libs libcrypto)"
# CURL_CFLAGS="$($PKG_CONFIG --cflags libcurl)"
# CURL_LIBS="$($PKG_CONFIG --libs libcurl)"

# Generate Makefile
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
