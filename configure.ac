AC_INIT([yawl], [0.6.1], [william@horvath.blog], [], [https://github.com/whrvt/yawl])
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

AC_DEFUN([AC_PROG_PERL], [
    AC_CHECK_PROG([PERL], [perl], [perl])
    if test -z "$PERL"; then
        AC_MSG_ERROR([perl not found])
    fi
])
AC_PROG_PERL

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
build_prefix="$PWD/deps/yawl"
mkdir -p "$deps_builddir" "$deps_prefix" "$deps_prefix/yawl/"{lib,include} "$build_prefix"

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
export CC="$CC --target=x86_64-linux-musl -Qunused-arguments"
export CXX="$CXX --target=x86_64-linux-musl -Qunused-arguments"
shared_includes="-I$musl_is_dumb -I$CLANG_INCDIR -I$deps_prefix/include"
shared_cflags="$shared_includes -Oz -flto=auto -ffunction-sections -fdata-sections -static -march=x86-64 -mtune=generic \
               -Wno-unused-command-line-argument -Qunused-arguments"
export CPPFLAGS="-D__STDC_NO_ATOMICS__ $shared_includes"
export CFLAGS="$shared_cflags -Wno-gnu-include-next"
export CXXFLAGS="$shared_cflags"
export LDFLAGS="-L$deps_prefix/lib -static -flto=auto -Wl,-O2,--gc-sections,-s -fuse-ld=musl-clang"
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
if ! test -f "$build_prefix/lib/libunistring.a"; then
    AC_MSG_NOTICE([Building libunistring])
    sh build-aux/download-deps.sh libunistring "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libunistring])
    cp $deps_prefix/{lib,yawl/lib}/libunistring.a
fi

# Download and build libidn2 (needed for curl/openssl)
if ! test -f "$build_prefix/lib/libidn2.a"; then
    AC_MSG_NOTICE([Building libidn2])
    sh build-aux/download-deps.sh libidn2 "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libidn2])
    cp $deps_prefix/{lib,yawl/lib}/libidn2.a
fi

# Download and build libpsl (needed for curl/openssl)
if ! test -f "$build_prefix/lib/libpsl.a"; then
    AC_MSG_NOTICE([Building libpsl])
    sh build-aux/download-deps.sh libpsl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libpsl])
    cp $deps_prefix/{lib,yawl/lib}/libpsl.a
fi

# Download and build libz
if ! test -f "$build_prefix/lib/libz.a"; then
    AC_MSG_NOTICE([Building libz])
    sh build-aux/download-deps.sh libz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libz])
    cp $deps_prefix/{lib,yawl/lib}/libz.a
fi

# Download and build xz
if ! test -f "$build_prefix/lib/liblzma.a"; then
    AC_MSG_NOTICE([Building xz])
    sh build-aux/download-deps.sh xz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build xz])
    cp $deps_prefix/{lib,yawl/lib}/liblzma.a
fi

# Download and build zstd
if ! test -f "$build_prefix/lib/libzstd.a"; then
    AC_MSG_NOTICE([Building zstd])
    sh build-aux/download-deps.sh zstd "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build zstd])
    cp $deps_prefix/{lib,yawl/lib}/libzstd.a
fi

# Download and build openssl
if ! test -f "$build_prefix/lib/libssl.a" || ! test -f "$build_prefix/lib/libcrypto.a"; then
    AC_MSG_NOTICE([Building openssl])
    sh build-aux/download-deps.sh openssl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build openssl])
    cp $deps_prefix/{lib,yawl/lib}/libssl.a
    cp $deps_prefix/{lib,yawl/lib}/libcrypto.a
    cp -r $deps_prefix/{include,yawl/include}/openssl
fi

# Download and build libarchive
if ! test -f "$build_prefix/lib/libarchive.a"; then
    AC_MSG_NOTICE([Building libarchive])
    sh build-aux/download-deps.sh libarchive "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libarchive])
    cp $deps_prefix/{lib,yawl/lib}/libarchive.a
    cp $deps_prefix/{include/archive*,yawl/include}
fi

# Download and build curl
if ! test -f "$build_prefix/lib/libcurl.a"; then
    AC_MSG_NOTICE([Building curl])
    sh build-aux/download-deps.sh curl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build curl])
    cp $deps_prefix/{lib,yawl/lib}/libcurl.a
    cp -r $deps_prefix/{include,yawl/include}/curl
fi

# Download and build libnotify and json-glib
if ! test -f "$build_prefix/lib/libnotify.a" || ! test -f "$build_prefix/lib/libjson-glib-1.0.a" || ! test -f "$build_prefix/lib/libgdk_pixbuf-2.0.a" || ! test -f "$build_prefix/lib/libglib-2.0.a"; then
    # Download and build gdk-pixbuf (only required for libnotify)
    if ! test -f "$deps_prefix/lib/libgdk_pixbuf-2.0.a"; then
        AC_MSG_NOTICE([Building gdk-pixbuf])
        sh build-aux/download-deps.sh gdk-pixbuf "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build gdk-pixbuf])
    fi
    # Download and build json-glib
    if ! test -f "$deps_prefix/lib/libjson-glib-1.0.a"; then
        AC_MSG_NOTICE([Building json-glib])
        sh build-aux/download-deps.sh json-glib "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build json-glib])
    fi
    if ! test -f "$deps_prefix/lib/libnotify.a"; then
        AC_MSG_NOTICE([Building libnotify])
        sh build-aux/download-deps.sh libnotify "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libnotify])
    fi
    cp $deps_prefix/{lib,yawl/lib}/libnotify.a
    cp $deps_prefix/{lib,yawl/lib}/libglib-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libgobject-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libgmodule-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libgio-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libgirepository-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libgdk_pixbuf-2.0.a
    cp $deps_prefix/{lib,yawl/lib}/libffi.a
    cp -r $deps_prefix/{include,yawl/include}/libnotify
    cp -r $deps_prefix/{include,yawl/include}/glib-2.0
    cp -r $deps_prefix/{include,yawl/include}/gdk-pixbuf-2.0
    cp $deps_prefix/{lib,yawl/lib}/libjson-glib-1.0.a
    cp -r $deps_prefix/{include,yawl/include}/json-glib-1.0/
fi

# Now reset everything to a prefix with only the necessary deps to build yawl
if test -f "$deps_prefix/yawl/lib/libnotify.a"; then
    rm -rf "$build_prefix"
    cp -r $deps_prefix/yawl "$build_prefix"
fi

CURL_CPPFLAGS="-DCURL_STATICLIB -DWITH_GZFILEOP"
LIBARCHIVE_CPPFLAGS="-DLIBARCHIVE_STATIC"

NON_GLIB_CPPFLAGS="$LIBARCHIVE_CPPFLAGS $CURL_CPPFLAGS -I$build_prefix/include"

COMMON_DEP_LIBS="-l:libidn2.a -l:libunistring.a -l:libpsl.a -l:libzstd.a -l:libz.a -l:liblzma.a"
CURL_LIBS="-l:libcurl.a -ldl"
LIBARCHIVE_LIBS="-l:libarchive.a"
OPENSSL_LIBS="-l:libssl.a -l:libcrypto.a"

NON_GLIB_LIBS="-L$build_prefix/lib $CURL_LIBS $LIBARCHIVE_LIBS $OPENSSL_LIBS $COMMON_DEP_LIBS"

GLIB_CPPFLAGS="-I$build_prefix/include/glib-2.0"
GDK_PIXBUF_CPPFLAGS="-I$build_prefix/include/gdk-pixbuf-2.0"
JSON_GLIB_CPPFLAGS="-I$build_prefix/include/json-glib-1.0"

ALL_GLIB_CPPFLAGS="$GDK_PIXBUF_CPPFLAGS $JSON_GLIB_CPPFLAGS $GLIB_CPPFLAGS"

GLIB_LIBS="-l:libgmodule-2.0.a -l:libgio-2.0.a -l:libgirepository-2.0.a -l:libgobject-2.0.a -l:libglib-2.0.a -l:libffi.a -lm"
GDK_PIXBUF_LIBS="-l:libgdk_pixbuf-2.0.a"
LIBNOTIFY_LIBS="-l:libnotify.a"
JSON_GLIB_LIBS="-l:libjson-glib-1.0.a"

ALL_GLIB_LIBS="$JSON_GLIB_LIBS $LIBNOTIFY_LIBS $GDK_PIXBUF_LIBS $GLIB_LIBS"

export CPPFLAGS="-D_GNU_SOURCE $NON_GLIB_CPPFLAGS $ALL_GLIB_CPPFLAGS"
export CFLAGS="-O3 -std=gnu17 -fwrapv -fno-strict-aliasing -flto=auto -ffunction-sections -fdata-sections -static -march=x86-64 -mtune=generic \
    -Wall -Werror -Wextra -Wpedantic -Wno-unused-command-line-argument -Wno-gnu-include-next"
export LDFLAGS="-static -static-libgcc -flto=auto -Wl,-O2,--gc-sections,-s"

AC_SUBST([NON_GLIB_LIBS])
AC_SUBST([ALL_GLIB_LIBS])

AC_SUBST([CPPFLAGS])
AC_SUBST([CFLAGS])
AC_SUBST([LDFLAGS])

export PKG_CONFIG=""
AC_SUBST([PKG_CONFIG])
export PKG_CONFIG_PATH=""
AC_SUBST([PKG_CONFIG_PATH])
export PKG_CONFIG_LIBDIR=""
AC_SUBST([PKG_CONFIG_LIBDIR])

AC_CHECK_HEADERS([curl/ca_cert_embed.h], [], [AC_MSG_ERROR([Missing curl embedded certificate header, need to rebuild curl])])
AC_CHECK_FUNCS(renameat renameat2)

# Generate Makefile
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
