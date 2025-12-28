AUTOMAKE_OPTIONS := foreign

bin_PROGRAMS := yawl

yawl_SOURCES := src/yawl.cpp src/util.cpp src/apparmor.cpp src/log.cpp src/result.cpp src/update.cpp src/nsenter.cpp src/yawlconfig.cpp
if USE_ASAN
yawl_CXXFLAGS := -march=$(COMPILER_MARCH) -Og -ggdb -gdwarf-4 -fsanitize=address,undefined,cfi -fvisibility=hidden -Wno-backend-plugin
else
yawl_CXXFLAGS := -march=$(COMPILER_MARCH) -static-libgcc -fno-rtti -fno-exceptions -static-libstdc++ -static -fPIC -fPIE -static-pie -Os -ffunction-sections -fdata-sections -Wl,--gc-sections,-s
endif
yawl_LDFLAGS := $(yawl_CXXFLAGS)
yawl_LDADD := $(NON_GLIB_LIBS) $(ALL_GLIB_LIBS)

EXTRA_DIST = README.md assets/external/bwrap-userns-restrict assets/external/cacert.pem

compile_commands.json: mostlyclean-compile
	@python --version &>/dev/null || { echo python is unavailable to generate a compile_commands.json, install python && exit 1; }
	$(MAKE) ZIG_VERBOSE_CC=1 $(AM_MAKEFLAGS) $(yawl_OBJECTS) $(yawl_DEPENDENCIES) $(EXTRA_yawl_DEPENDENCIES) 2>collect.log && \
	python $(abs_top_srcdir)/build-aux/zig-cc-db-parser.py collect.log && rm collect.log

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

