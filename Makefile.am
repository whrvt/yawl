AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = yawl

yawl_SOURCES = src/yawl.c src/strings.c
yawl_CFLAGS  = $(LIBUNISTRING_CFLAGS) $(LIBIDN2_CFLAGS) $(LIBPSL_CFLAGS) $(LIBZ_CFLAGS) \
				$(LIBLZMA_CFLAGS) $(LIBZSTD_CFLAGS) $(LIBARCHIVE_CFLAGS) $(OPENSSL_CFLAGS) $(CURL_CFLAGS) \
				-Wall -Werror -Wextra -Wpedantic
yawl_LDFLAGS = -static -static-libgcc
yawl_LDADD = $(LIBUNISTRING_LIBS) $(LIBIDN2_LIBS) $(LIBPSL_LIBS) $(LIBZ_LIBS) $(LIBLZMA_LIBS) \
			 $(LIBZSTD_LIBS) $(LIBARCHIVE_LIBS) $(OPENSSL_LIBS) $(CURL_LIBS) $(PTHREAD_EXTLIBS)
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
