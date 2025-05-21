AUTOMAKE_OPTIONS = foreign

bin_PROGRAMS = yawl

yawl_SOURCES = src/yawl.cpp src/util.cpp src/apparmor.cpp src/log.cpp src/result.cpp src/update.cpp src/nsenter.cpp
#yawl_CXXFLAGS = -O0 -flto=full -ffunction-sections -fdata-sections -fsanitize=address,undefined,cfi,safe-stack -fvisibility=hidden
yawl_CXXFLAGS = -Os -flto=full -ffunction-sections -fdata-sections -Wl,--gc-sections,-s
yawl_LDFLAGS = $(yawl_CXXFLAGS)
yawl_LDADD = $(NON_GLIB_LIBS) $(ALL_GLIB_LIBS)

EXTRA_DIST = README.md assets/external/bwrap-userns-restrict assets/external/cacert.pem

compile_commands.json: clean
	@bear --version || { echo bear is unavailable to generate a compile_commands.json, install bear && exit 1; }
	@(echo '{'; \
	  echo '"compilation": {'; \
	  echo '"compilers_to_recognize": ['; \
	  echo '{'; \
	  echo '"executable": "$(ZIG_REALPATH)"'; \
	  echo '}'; \
	  echo ']'; \
	  echo '}'; \
	  echo '}' ) > bear.cfg
	bear --config bear.cfg --append -- $(MAKE) $(AM_MAKEFLAGS) all-am
	@sed -i -e 's|.*-march=.*||' -e 's|.*-mtune=.*||' compile_commands.json # zig annoyingly uses underscores here

$(abs_top_srcdir)/.clangd: compile_commands.json
	@if [ -f $@ ]; then \
		if grep -q "CompilationDatabase:" $@; then \
			sed -i 's|CompilationDatabase:.*|CompilationDatabase: $(shell realpath --relative-to=$(abs_top_srcdir) $(CURDIR))|g' $@; \
		else \
			echo "CompileFlags:" >> $@; \
			echo "    CompilationDatabase: $(shell realpath --relative-to=$(abs_top_srcdir) $(CURDIR))" >> $@; \
		fi; \
	else \
		echo "CompileFlags:" > $@; \
		echo "    CompilationDatabase: $(shell realpath --relative-to=$(abs_top_srcdir) $(CURDIR))" >> $@; \
		echo "" >> $@; \
		echo "InlayHints:" >> $@; \
		echo "    Enabled: No" >> $@; \
	fi

compile-commands: $(abs_top_srcdir)/.clangd

clean-local: uninstall
	rm -rf $(yawl_OBJECTS) src/$(DEPDIR)

clean-deps:
	rm -rf deps
.PHONY: clean-deps

maintainer-clean-local: clean-deps

distclean-local: clean-deps
