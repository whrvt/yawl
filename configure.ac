AC_INIT([yawl], [0.0.2], [william@horvath.blog])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([yawl.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([dist]) # default `make install`s to ./dist/bin/ (modify with --prefix=)

# Checks for programs
AC_PROG_CC([clang])
AC_PROG_CXX([clang++])
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

# Always enforce static linking, and make small binaries
export CC="clang"
export CXX="clang++"
shared_cflags="-Oz -ffunction-sections -fdata-sections -static -march=x86-64 -mtune=generic"
export CFLAGS="$CFLAGS $shared_cflags -std=gnu23"
export CXXFLAGS="$CXXFLAGS $shared_cflags -std=gnu++23"
export LDFLAGS="$LDFLAGS -fuse-ld=lld -static -static-libgcc -Wl,--gc-sections,-s"
export STRIP="$STRIP"
export AR="$AR"

export PTHREAD_EXTLIBS="-Wl,--whole-archive -lpthread -Wl,--no-whole-archive"

AC_SUBST([CC])
AC_SUBST([CXX])
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

# Set up build directories
deps_builddir="$PWD/deps/build"
deps_prefix="$PWD/deps/prefix"
mkdir -p "$deps_builddir" "$deps_prefix"

# Add build-aux to PATH for helper scripts
export PATH="$PWD/build-aux:$PATH"

export PKG_CONFIG="pkg-config --static"
AC_SUBST([PKG_CONFIG])
export PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
AC_SUBST([PKG_CONFIG_PATH])
export PKG_CONFIG_LIBDIR="$deps_prefix/lib:$PKG_CONFIG_LIBDIR"
AC_SUBST([PKG_CONFIG_LIBDIR])

AS_IF([test -n "$CERT_BUNDLE"], [],
    [CERT_BUNDLE="/etc/ssl/certs/ca-bundle.crt"]
)
export CERT_BUNDLE
AC_SUBST([CERT_BUNDLE])

AC_CHECK_FILE([$CERT_BUNDLE], [],
    [AC_MSG_ERROR([Could not find an ssl certificate bundle (set CERT_BUNDLE to specify a ca-bundle.crt manually)])
])

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

# Download and build libarchive
if ! test -f "$deps_prefix/lib/libarchive.a"; then
    AC_MSG_NOTICE([Building libarchive])
    sh build-aux/download-deps.sh libarchive "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libarchive])
fi

# Download and build openssl
if ! test -f "$deps_prefix/lib/libssl.a" || ! test -f "$deps_prefix/lib/libcrypto.a"; then
    AC_MSG_NOTICE([Building openssl])
    sh build-aux/download-deps.sh openssl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build openssl])
fi

# Download and build curl
if ! test -f "$deps_prefix/lib/libcurl.a"; then
    AC_MSG_NOTICE([Building curl])
    sh build-aux/download-deps.sh curl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build curl])
fi

# Set up flags to use our built libraries

export LIBUNISTRING_CFLAGS="-I$deps_prefix/include"
export LIBUNISTRING_LIBS="$deps_prefix/lib/libunistring.a"
export LIBPSL_CFLAGS="-I$deps_prefix/include"
export LIBPSL_LIBS="$deps_prefix/lib/libpsl.a"
export LIBIDN2_CFLAGS="-I$deps_prefix/include"
export LIBIDN2_LIBS="$deps_prefix/lib/libidn2.a"
export LIBZ_CFLAGS="-I$deps_prefix/include"
export LIBZ_LIBS="$deps_prefix/lib/libz.a"
export LIBLZMA_CFLAGS="-I$deps_prefix/include"
export LIBLZMA_LIBS="$deps_prefix/lib/liblzma.a"
export LIBZSTD_CFLAGS="-I$deps_prefix/include"
export LIBZSTD_LIBS="$deps_prefix/lib/libzstd.a"
export LIBARCHIVE_CFLAGS="-I$deps_prefix/include"
export LIBARCHIVE_LIBS="$deps_prefix/lib/libarchive.a"
export OPENSSL_CFLAGS="-I$deps_prefix/include"
export OPENSSL_LIBS="$deps_prefix/lib/libssl.a $deps_prefix/lib/libcrypto.a"
export CURL_CFLAGS="-DCURL_STATICLIB -I$deps_prefix/include"
export CURL_LIBS="$deps_prefix/lib/libcurl.a"

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

# AC_MSG_NOTICE([Stripping libs])
# sh -c "find $deps_prefix/ -name '*.a' -execdir $STRIP '{''}' '+'"

# Check for required headers
AC_CHECK_HEADERS([stdlib.h string.h unistd.h sys/stat.h])

# Generate Makefile
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
