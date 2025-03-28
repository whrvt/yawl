AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = yawl

yawl_SOURCES = src/yawl.c src/util.c src/util.h src/apparmor.c src/apparmor.h src/bwrap_data.h src/log.c src/log.h \
	src/result.c src/result.h src/update.c src/update.h src/macros.h src/nsenter.c src/nsenter.h config.h
yawl_CXXFLAGS = -Os -flto=thin -ffat-lto-objects -ffunction-sections -fdata-sections -Wl,--gc-sections,-s
yawl_CFLAGS = $(yawl_CXXFLAGS)
yawl_LDFLAGS = $(yawl_CXXFLAGS)
yawl_LDADD = $(NON_GLIB_LIBS) $(ALL_GLIB_LIBS)

EXTRA_DIST = README.md assets/external/bwrap-userns-restrict assets/external/cacert.pem

clean-local: uninstall
	rm -rf $(yawl_OBJECTS) src/$(DEPDIR)

clean-deps:
	rm -rf deps
.PHONY: clean-deps

maintainer-clean-local: clean-deps

distclean-local: clean-deps
