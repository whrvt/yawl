AC_INIT([yawl], [0.6.8], [william@horvath.blog], [], [https://github.com/whrvt/yawl])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_SRCDIR([src/yawl.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([dist]) # default `make install`s to ./dist/bin/ (modify with --prefix=)

AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL

AM_INIT_AUTOMAKE([foreign subdir-objects parallel-tests])
AM_SILENT_RULES([yes])

top_srcdir_actual="$am_aux_dir/.." # autotools!

# Set up build directories
deps_builddir="$top_srcdir_actual/deps/build"
deps_prefix="$top_srcdir_actual/deps/prefix"
build_prefix="$top_srcdir_actual/deps/yawl"
mkdir -p "$deps_builddir" "$deps_prefix"/{lib,include} "$build_prefix/"{lib,include} "$PWD/dist/bin"

export cch="$am_aux_dir/muslconfig.cache"
AC_SUBST([cch])

# Make some wrapper shims for the dependencies to compile happily
export PATH="${build_prefix}:${build_prefix}/zig:$am_aux_dir:${PATH}"
AC_SUBST([PATH])

rm -f "$build_prefix"/{musl-gcc,zigcc*,zigcpp*} # clear out existing ccache wrappers

AC_PATH_PROG([CCACHE], [ccache], [])

AC_PROG_CC([musl-gcc])
if test -z "$CC"; then
    AC_MSG_ERROR([musl-gcc not found])
fi

AC_PATH_PROG([CC_REAL], [musl-gcc])
install -Dm755 /dev/stdin "$build_prefix"/musl-gcc <<EOF
#!/usr/bin/env sh
$CCACHE $CC_REAL "\$@"
EOF

if test -z $ZIG; then
    if ! test -x "${build_prefix}/zig/zig"; then
        AC_MSG_NOTICE([Downloading zig])
        sh $am_aux_dir/download-deps.sh zig "$build_prefix" "${build_prefix}/zig" || AC_MSG_ERROR([Failed to install zig])
    fi
fi

AC_SUBST([ZIG_REALPATH],[$build_prefix/zig/zig])

install -Dm755 /dev/stdin "$build_prefix"/zigcc-tobewrapped <<EOF
#!/usr/bin/env sh
zig cc -target x86_64-linux-musl -Qunused-arguments "\$@"
EOF
AC_PATH_PROG([ZIGCC_REAL], [zigcc-tobewrapped], [ path = "${build_prefix}" ])
install -Dm755 /dev/stdin "$build_prefix"/zigcc <<EOF
#!/usr/bin/env sh
$CCACHE $ZIGCC_REAL "\$@"
EOF

export ZIGCC="zigcc"
AC_SUBST([ZIGCC])

install -Dm755 /dev/stdin "$build_prefix"/zigcpp-tobewrapped <<EOF
#!/usr/bin/env sh
zig c++ -target x86_64-linux-musl -Qunused-arguments "\$@"
EOF
AC_PATH_PROG([ZIGCPP_REAL], [zigcpp-tobewrapped], [ path = "${build_prefix}" ])
install -Dm755 /dev/stdin "$build_prefix"/zigcpp <<EOF
#!/usr/bin/env sh
$CCACHE $ZIGCPP_REAL "\$@"
EOF

export ZIGCPP="zigcpp"
AC_SUBST([ZIGCPP])
AC_PROG_CXX([zigcpp])

rm -f "$build_prefix"/pkg-config # don't recurse infinitely :)

PKG_PROG_PKG_CONFIG

install -Dm755 /dev/stdin "$build_prefix"/pkg-config <<EOF
#!/usr/bin/env sh
$PKG_CONFIG --static "\$@"
EOF

export PKG_CONFIG="$build_prefix"/pkg-config
AC_SUBST([PKG_CONFIG])
export PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
AC_SUBST([PKG_CONFIG_PATH])
export PKG_CONFIG_LIBDIR="$deps_prefix/lib:$PKG_CONFIG_LIBDIR"
AC_SUBST([PKG_CONFIG_LIBDIR])

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

AC_DEFUN([AC_PROG_CMAKE], [
    AC_CHECK_PROG([CMAKE], [cmake], [cmake])
    if test -z "$CMAKE"; then
        AC_MSG_ERROR([cmake not found - required for building dependencies])
    fi
])
AC_PROG_CMAKE
AC_DEFUN([AC_PROG_MESON], [
    AC_CHECK_PROG([MESON], [meson], [meson])
    if test -z "$MESON"; then
        AC_MSG_ERROR([meson not found - required for building dependencies])
    fi
])
AC_PROG_MESON
AC_DEFUN([AC_PROG_NINJA], [
    AC_CHECK_PROG([NINJA], [ninja], [ninja])
    if test -z "$NINJA"; then
        AC_MSG_ERROR([ninja not found - required for building dependencies])
    fi
])
AC_PROG_NINJA

AC_PROG_RANLIB

AC_DEFUN([AX_COUNT_CPUS], [
    AC_CHECK_PROG([GETCONF], [getconf], [getconf])
    AS_IF([test -n "$GETCONF"],
        [n_cpus=$($GETCONF _NPROCESSORS_ONLN 2>/dev/null)],
        [n_cpus=1])
    AS_IF([test -z "$n_cpus" -o "$n_cpus" -le "0"], [n_cpus=1])
    AC_SUBST([CPU_COUNT], [$n_cpus])
])
AX_COUNT_CPUS

export MAKEFLAGS="-j$CPU_COUNT"
AC_SUBST([MAKEFLAGS])

musl_compat="$deps_builddir/musl_temp"
musl_compat2="$deps_builddir/musl_temp2"
mkdir -p "$musl_compat" "$musl_compat2"/{linux,asm,asm-generic}

AS_IF([test -n "$LIMITS_H"], [],
    [LIMITS_H="/usr/include/linux/limits.h"]
)
AC_SUBST([LIMITS_H])
AC_CHECK_FILE([$LIMITS_H], [],
    [AC_MSG_ERROR([Could not find the linux/limits.h header (set LIMITS_H to specify one manually)])
])
cp $LIMITS_H "$musl_compat2"/linux/

AS_IF([test -n "$TYPES_H"], [],
    [TYPES_H="/usr/include/linux/types.h"]
)
AC_SUBST([TYPES_H])
AC_CHECK_FILE([$TYPES_H], [],
    [AC_MSG_ERROR([Could not find the linux/types.h header (set TYPES_H to specify one manually)])
])
cp $TYPES_H "$musl_compat2"/linux/

AS_IF([test -n "$MUSL_INCDIR"], [],
    [MUSL_INCDIR="/usr/lib/musl/include/sys"]
)
AC_SUBST([MUSL_INCDIR])
AC_CHECK_FILE([$MUSL_INCDIR], [],
    [AC_MSG_ERROR([Could not find the musl header files (set MUSL_INCDIR to specify a musl/include folder manually)])
])
AS_IF([test -L "$musl_compat/linux"],
    [unlink "$musl_compat/linux"]
)
$LN_S "$MUSL_INCDIR" "$musl_compat/linux"

export CC="$CC"
export CXX="$CXX"
export LD="$CC"
shared_includes="-D_GNU_SOURCE -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -I$deps_prefix/include"
shared_flags="-fwrapv -fno-strict-aliasing -ffat-lto-objects -fno-exceptions \
    -ffunction-sections -fdata-sections -pthread -fcf-protection=full -fstack-clash-protection -fstack-protector-all \
    -static-libgcc -static -fPIC -fPIE -static-libasan -L$deps_prefix/lib -Wl,--gc-sections -Wl,-z,noexecstack \
    -Wno-date-time -Wl,-z,defs -Wl,-z,relro,-z,now -Wl,--as-needed"
export CPPFLAGS="$shared_includes"
export CFLAGS="-I$musl_compat -I$musl_compat2 $shared_includes $shared_flags -Oz -flto=auto -march=x86-64 -mtune=generic"
export CXXFLAGS="$shared_includes $shared_flags -Os -flto=thin -march=x86_64 -mtune=x86_64 -static-libstdc++"
export LDFLAGS="$shared_flags -flto"
export STRIP="$STRIP"
export AR="$AR"

AC_SUBST([CC])
AC_SUBST([CXX])
AC_SUBST([LD])
AC_SUBST([CPPFLAGS])
AC_SUBST([CFLAGS])
AC_SUBST([CXXFLAGS])
AC_SUBST([LDFLAGS])
AC_SUBST([STRIP])
AC_SUBST([AR])

# Check for required headers
AC_CHECK_HEADERS([stdlib.h string.h unistd.h sys/stat.h linux/mman.h linux/limits.h sys/vfs.h])

if test "x$ac_cv_header_linux_mman_h" != "xyes"
then
    AC_MSG_ERROR([Could not find the linux/mman.h header (MUSL_INCDIR=$MUSL_INCDIR is likely missing)])
fi

if test "x$ac_cv_header_linux_limits_h" != "xyes"
then
    AC_MSG_ERROR([Could not find the linux/limits.h header (LIMITS_H=$LIMITS_H is likely missing)])
fi

if test "x$ac_cv_header_sys_vfs_h" != "xyes"
then
    AC_MSG_ERROR([Could not find the sys/vfs.h header])
fi

export CMAKE NINJA MESON

# Download and build mimalloc (to replace the slow musl malloc)
if ! test -f "$build_prefix/lib/mimalloc.o"; then
    AC_MSG_NOTICE([Building mimalloc])
    sh $am_aux_dir/download-deps.sh mimalloc "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build mimalloc])
    cp $deps_prefix/lib/*malloc* $build_prefix/lib/
    cp -r $deps_prefix/include/*malloc* $build_prefix/include/
fi

export CPPFLAGS="$CPPFLAGS -DHAVE_FREE_POSIX=1 -Drpl_free=free"
export CFLAGS="$CFLAGS -DHAVE_FREE_POSIX=1 -Drpl_free=free"
export CXXFLAGS="$CXXFLAGS -DHAVE_FREE_POSIX=1 -Drpl_free=free"
export LDFLAGS="$LDFLAGS"
export LD="$LD"
AC_SUBST([CPPFLAGS])
AC_SUBST([CFLAGS])
AC_SUBST([CXXFLAGS])
AC_SUBST([LDFLAGS])
AC_SUBST([LD])

# Download and build libunistring (needed for idn and psl)
if ! test -f "$build_prefix/lib/libunistring.a"; then
    AC_MSG_NOTICE([Building libunistring])
    sh $am_aux_dir/download-deps.sh libunistring "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libunistring])
    cp $deps_prefix/lib/libunistring.a $build_prefix/lib/
fi

# Download and build libidn2 (needed for curl/openssl)
if ! test -f "$build_prefix/lib/libidn2.a"; then
    AC_MSG_NOTICE([Building libidn2])
    sh $am_aux_dir/download-deps.sh libidn2 "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libidn2])
    cp $deps_prefix/lib/libidn2.a $build_prefix/lib/
fi

# Download and build libpsl (needed for curl/openssl)
if ! test -f "$build_prefix/lib/libpsl.a"; then
    AC_MSG_NOTICE([Building libpsl])
    sh $am_aux_dir/download-deps.sh libpsl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libpsl])
    cp $deps_prefix/lib/libpsl.a $build_prefix/lib/
fi

# Download and build libz
if ! test -f "$build_prefix/lib/libz.a"; then
    AC_MSG_NOTICE([Building libz])
    sh $am_aux_dir/download-deps.sh libz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libz])
    cp $deps_prefix/lib/libz.a $build_prefix/lib/
fi

# Download and build xz
if ! test -f "$build_prefix/lib/liblzma.a"; then
    AC_MSG_NOTICE([Building xz])
    sh $am_aux_dir/download-deps.sh xz "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build xz])
    cp $deps_prefix/lib/liblzma.a $build_prefix/lib/
fi

# Download and build zstd
if ! test -f "$build_prefix/lib/libzstd.a"; then
    AC_MSG_NOTICE([Building zstd])
    sh $am_aux_dir/download-deps.sh zstd "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build zstd])
    cp $deps_prefix/lib/libzstd.a $build_prefix/lib/
fi

# Download and build openssl
if ! test -f "$build_prefix/lib/libssl.a" || ! test -f "$build_prefix/lib/libcrypto.a"; then
    AC_MSG_NOTICE([Building openssl])
    sh $am_aux_dir/download-deps.sh openssl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build openssl])
    cp $deps_prefix/lib/libssl.a $build_prefix/lib/
    cp $deps_prefix/lib/libcrypto.a $build_prefix/lib/
    cp -r $deps_prefix/include/openssl $build_prefix/include/
fi

# Download and build libarchive
if ! test -f "$build_prefix/lib/libarchive.a"; then
    AC_MSG_NOTICE([Building libarchive])
    sh $am_aux_dir/download-deps.sh libarchive "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libarchive])
    cp $deps_prefix/lib/libarchive.a $build_prefix/lib/
    cp $deps_prefix/include/archive* $build_prefix/include/
fi

# Download and build curl
if ! test -f "$build_prefix/lib/libcurl.a"; then
    AC_MSG_NOTICE([Building curl])
    sh $am_aux_dir/download-deps.sh curl "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build curl])
    cp $deps_prefix/lib/libcurl.a $build_prefix/lib/
    cp -r $deps_prefix/include/curl $build_prefix/include/
fi

# Download and build libnotify and json-glib
if ! test -f "$build_prefix/lib/libnotify.a" || ! test -f "$build_prefix/lib/libjson-glib-1.0.a" || ! test -f "$build_prefix/lib/libgdk_pixbuf-2.0.a" || ! test -f "$build_prefix/lib/libglib-2.0.a" || ! test -f "$build_prefix/lib/libpcre2-8.a"; then
    # Download and build libffi (required for gdk-pixbuf)
    if ! test -f "$deps_prefix/lib/libffi.a"; then
        AC_MSG_NOTICE([Building libffi])
        sh $am_aux_dir/download-deps.sh libffi "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libffi])
    fi
    # Download and build gdk-pixbuf (only required for libnotify)
    if ! test -f "$deps_prefix/lib/libgdk_pixbuf-2.0.a"; then
        AC_MSG_NOTICE([Building gdk-pixbuf])
        sh $am_aux_dir/download-deps.sh gdk-pixbuf "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build gdk-pixbuf])
    fi
    # Download and build json-glib
    if ! test -f "$deps_prefix/lib/libjson-glib-1.0.a"; then
        AC_MSG_NOTICE([Building json-glib])
        sh $am_aux_dir/download-deps.sh json-glib "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build json-glib])
    fi
    if ! test -f "$deps_prefix/lib/libnotify.a"; then
        AC_MSG_NOTICE([Building libnotify])
        sh $am_aux_dir/download-deps.sh libnotify "$deps_builddir" "$deps_prefix" || AC_MSG_ERROR([Failed to build libnotify])
    fi
    cp $deps_prefix/lib/libnotify.a $build_prefix/lib/
    cp $deps_prefix/lib/libglib-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libgobject-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libgmodule-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libgio-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libgirepository-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libgdk_pixbuf-2.0.a $build_prefix/lib/
    cp $deps_prefix/lib/libffi.a $build_prefix/lib/
    cp $deps_prefix/lib/libpcre2-8.a $build_prefix/lib/
    cp -r $deps_prefix/include/libnotify $build_prefix/include/
    cp -r $deps_prefix/include/glib-2.0 $build_prefix/include/
    cp -r $deps_prefix/include/gdk-pixbuf-2.0 $build_prefix/include/
    cp $deps_prefix/lib/libjson-glib-1.0.a $build_prefix/lib/
    cp -r $deps_prefix/include/json-glib-1.0/ $build_prefix/include/
fi

# Download and build libcap (for nsenter functionality)
if ! test -f "$build_prefix/lib/libcap.a"; then
    AC_MSG_NOTICE([Building libcap])
    { sh $am_aux_dir/download-deps.sh libcap "$deps_builddir" "$deps_prefix" &&
    cp $deps_prefix/lib/libcap.a $build_prefix/lib/ &&
    cp -r $deps_prefix/include/linux $build_prefix/include/ &&
    cp $deps_prefix/include/capability.h $build_prefix/include; } || AC_MSG_ERROR([Failed to build libcap])
fi

# Download and build fmtlib headers (for better string formatting)
if ! test -f "$build_prefix/include/fmt/format.h"; then
    AC_MSG_NOTICE([Building libfmt])
    { sh $am_aux_dir/download-deps.sh fmt "$deps_builddir" "$deps_prefix" &&
    cp $deps_prefix/lib/libfmt.a $build_prefix/lib/ &&
    cp -r $deps_prefix/include/fmt $build_prefix/include/; } || AC_MSG_ERROR([Failed to build libfmt])
fi

# Now download the CA certificate bundle to embed and use with curl
if ! test -f "$top_srcdir_actual/assets/external/cacert.pem"; then
    AC_MSG_NOTICE([Downloading CA certificate bundle for curl])
    sh $am_aux_dir/download-deps.sh cacert "$top_srcdir_actual/assets/external/" "$top_srcdir_actual/assets/external/" ||
        AC_MSG_ERROR([Failed to download the curl embedded certificate bundle, download it from https://curl.se/ca/cacert.pem and place it in assets/])
fi

# Get the bwrap-userns-restrict file to embed for AppArmor integration too
if ! test -f "$top_srcdir_actual/assets/external/bwrap-userns-restrict"; then
    AC_MSG_NOTICE([Downloading bwrap-userns-restrict to fix AppArmor])
    sh $am_aux_dir/download-deps.sh bwrap "$top_srcdir_actual/assets/external/" "$top_srcdir_actual/assets/external/" ||
        AC_MSG_ERROR([Failed to download the bwrap-userns-restrict file, find another source for it (taking it out of an Ubuntu/Debian package should work) and place it in assets/external/])
fi

CURL_CPPFLAGS="-DCURL_STATICLIB -DWITH_GZFILEOP"
LIBARCHIVE_CPPFLAGS="-DLIBARCHIVE_STATIC"

NON_GLIB_CPPFLAGS="$LIBARCHIVE_CPPFLAGS $CURL_CPPFLAGS -I$build_prefix/include"

COMMON_DEP_LIBS="-lcap -lidn2 -lpsl -lunistring -lz -llzma -lzstd"
CURL_LIBS="-lcurl"
LIBARCHIVE_LIBS="-larchive"
OPENSSL_LIBS="-lssl -lcrypto"
FMT_LIBS="-lfmt"

NON_GLIB_LIBS="$build_prefix/lib/mimalloc.o -L$build_prefix/lib $FMT_LIBS $CURL_LIBS $LIBARCHIVE_LIBS $OPENSSL_LIBS $COMMON_DEP_LIBS"

GLIB_CPPFLAGS="-I$build_prefix/include/glib-2.0 -DPCRE2_STATIC"
GDK_PIXBUF_CPPFLAGS="-I$build_prefix/include/gdk-pixbuf-2.0"
JSON_GLIB_CPPFLAGS="-I$build_prefix/include/json-glib-1.0"

ALL_GLIB_CPPFLAGS="$GDK_PIXBUF_CPPFLAGS $JSON_GLIB_CPPFLAGS $GLIB_CPPFLAGS"

GLIB_LIBS="-lgmodule-2.0 -lgio-2.0 -lgirepository-2.0 -lgobject-2.0 -lglib-2.0 -lpcre2-8 -lffi -lm"
GDK_PIXBUF_LIBS="-lgdk_pixbuf-2.0"
LIBNOTIFY_LIBS="-lnotify"
JSON_GLIB_LIBS="-ljson-glib-1.0"

ALL_GLIB_LIBS="$JSON_GLIB_LIBS $LIBNOTIFY_LIBS $GDK_PIXBUF_LIBS $GLIB_LIBS"

AC_LANG_PUSH([C++])
export CXX="$ZIGCPP"
export CC="$ZIGCPP"
export LD="$ZIGCPP"

# Shared flags/libraries for all targets
CPPFLAGS="-D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -D_GNU_SOURCE $NON_GLIB_CPPFLAGS $ALL_GLIB_CPPFLAGS"
CXXFLAGS="-std=gnu++26 -march=x86_64 -mtune=x86_64 -pthread -fwrapv -fno-strict-aliasing -fno-rtti -fno-exceptions -static-libstdc++ \
    -static-libgcc -static -fPIC -fPIE -static-pie -static-libsan -fcf-protection=full -fstack-protector -fstack-clash-protection \
    -fstack-protector-all -Wall -Werror -Wextra -Wpedantic -Wno-unused-command-line-argument -Wno-gnu-include-next -Wno-gnu-zero-variadic-macro-arguments \
    -Wno-nullability-extension -Wno-c99-extensions -Wno-c23-extensions -Wno-zero-length-array -Wl,-z,noexecstack \
    -Wl,-z,defs -Wl,-z,relro,-z,now -Wl,--as-needed -pthread"
CFLAGS="$CXXFLAGS"
LDFLAGS="$CXXFLAGS -xnone"

AC_SUBST([CC])
AC_SUBST([CXX])
AC_SUBST([LD])
AC_SUBST([CPPFLAGS])
AC_SUBST([CXXFLAGS])
AC_SUBST([LDFLAGS])

AC_SUBST([NON_GLIB_LIBS])
AC_SUBST([ALL_GLIB_LIBS])

# Don't need these anymore, we manually specify libs/flags
PKG_CONFIG=""
AC_SUBST([PKG_CONFIG])
PKG_CONFIG_PATH=""
AC_SUBST([PKG_CONFIG_PATH])
PKG_CONFIG_LIBDIR=""
AC_SUBST([PKG_CONFIG_LIBDIR])

AC_CHECK_FUNCS(renameat renameat2)

# Generate Makefile
AC_CONFIG_FILES([Makefile])

AC_OUTPUT
