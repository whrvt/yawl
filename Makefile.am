AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = yawl

yawl_SOURCES = src/yawl.c src/util.c src/util.h src/apparmor.c src/apparmor.h src/bwrap_data.h src/log.c src/log.h \
				src/result.c src/result.h src/update.c src/update.h
yawl_CFLAGS  = $(LIBARCHIVE_CFLAGS) $(OPENSSL_CFLAGS) $(CURL_CFLAGS) $(LIBNOTIFY_CFLAGS) $(JSON_GLIB_CFLAGS) \
				-Wall -Werror -Wextra -Wpedantic -std=gnu17 -fwrapv -fno-strict-aliasing
yawl_LDFLAGS = -static -static-libgcc
yawl_LDADD = $(LIBARCHIVE_LIBS) $(OPENSSL_LIBS) $(CURL_LIBS) $(LIBNOTIFY_LIBS) $(JSON_GLIB_LIBS) $(PTHREAD_EXTLIBS)
EXTRA_DIST = README.md

uninstall-hook:
	rm -f "$(DESTDIR)$(bindir)/$(bin_PROGRAMS)"

clean-local:
	rm -rf $(yawl_OBJECTS)

clean-deps:
	rm -rf deps
.PHONY: clean-deps

maintainer-clean-local: clean-deps

distclean-local: clean-deps
