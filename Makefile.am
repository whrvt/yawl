AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = yawl

yawl_SOURCES = src/yawl.c src/util.c src/util.h src/apparmor.c src/apparmor.h src/bwrap_data.h src/log.c src/log.h \
	src/result.c src/result.h src/update.c src/update.h config.h
# Additional release build flags
yawl_CFLAGS = -Os -flto=thin -ffat-lto-objects -ffunction-sections -fdata-sections -Wl,--gc-sections,-s
yawl_LDFLAGS = $(yawl_CFLAGS)
yawl_LDADD = $(NON_GLIB_LIBS) $(ALL_GLIB_LIBS)

EXTRA_DIST = README.md assets/external/bwrap-userns-restrict assets/external/cacert.pem

clean-local: uninstall
	rm -rf $(yawl_OBJECTS)

clean-deps:
	rm -rf deps
.PHONY: clean-deps

maintainer-clean-local: clean-deps

distclean-local: clean-deps
