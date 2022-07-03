# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

# == Compiler & uname ==
# Example: uname_S=Linux uname_M=x86_64 uname_R=2.6.17-44-kvm
uname_S		::= $(shell uname -s 2>/dev/null || echo None)
uname_M		::= $(shell uname -m 2>/dev/null || echo None)
uname_R		::= $(shell uname -r 2>/dev/null || echo None)
CXXVERSION	 != $(CXX) --version 2>&1
CXXKIND		 != case '$(CXXVERSION)' in *"Free Software Foundation"*) echo gcc;; *clang*) echo clang;; *) echo UNKNOWN;; esac
HAVE_GCC	::= $(if $(findstring $(CXXKIND), gcc),1,)
HAVE_CLANG	::= $(if $(findstring $(CXXKIND), clang),1,)
ifeq ($(HAVE_GCC)$(HAVE_CLANG),)		# do we HAVE_ *any* recognized compiler?
$(error Compiler '$(CXX)' not recognized, version identifier: $(CXXKIND))
endif
ifneq ($(and $(HAVE_GCC),$(filter 4.% 5.% 6.% 7.%, $(CXXVERSION))),)
GCC_OLDVERSION	 != echo ' $(CXXVERSION)' | egrep -o ' [34567]\.[0-9]+\.[0-9]+\b'
ifneq ($(GCC_OLDVERSION),)
$(error Compiler 'CXX=$(CXX)' version $(firstword $(GCC_OLDVERSION)) is too old, gcc/g++ >= 8.3.0 is required)
endif
endif
CCACHE		 ?= $(if $(CCACHE_DIR), ccache)

# == C/CXX/LD Flags ==
COMMONFLAGS	::= -Wall -Wdeprecated -Werror=format-security -Wredundant-decls -Wpointer-arith -Wmissing-declarations -Werror=return-type
#COMMONFLAGS	 += -Wdate-time -Wconversion -Wshadow
CONLYFLAGS	::= -Werror=incompatible-pointer-types -Werror-implicit-function-declaration -Wmissing-prototypes -Wnested-externs -Wno-pointer-sign
CXXONLYFLAGS	::= -Woverloaded-virtual -Wsign-promo -fvisibility-inlines-hidden
#CXXONLYFLAGS	 += -Wnon-virtual-dtor -Wempty-body -Wignored-qualifiers -Wunreachable-code -Wtype-limits
OPTIMIZE	::= -funroll-loops -ftree-vectorize
SANITIZECC	::= -fno-strict-overflow -fno-strict-aliasing # sane C / C++
LDOPTIMIZE	::= -O1 -Wl,--hash-style=both -Wl,--compress-debug-sections=zlib
LDMODEFLAGS	  =
__DEV__		::= 1

ifeq ($(MODE),quick)
MODEFLAGS	::= -O0
__DEV__		::=
else ifeq ($(MODE),production)
MODEFLAGS	::= -O3 -DNDEBUG
__DEV__		::=
else ifeq ($(MODE),devel)
MODEFLAGS	::= -g -O2
else ifeq ($(MODE),debug)
MODEFLAGS	::= -gdwarf-4 -O0 -fno-omit-frame-pointer -fno-inline -fstack-protector-all -fverbose-asm
else ifeq ($(MODE),ubsan)
MODEFLAGS	::= -O1 -fno-omit-frame-pointer -fstack-protector-all -fno-inline -g -fsanitize=undefined
LDMODEFLAGS	 += -lubsan
else ifeq ($(MODE),asan)
MODEFLAGS	::= -O1 -fno-omit-frame-pointer -fstack-protector-all -fno-inline -g -fsanitize=address -fno-common
LDMODEFLAGS	 += -lasan
else ifeq ($(MODE),tsan)
MODEFLAGS	::= -O1 -fno-omit-frame-pointer -fstack-protector-all -fno-inline -g -fsanitize=thread
LDMODEFLAGS	 += -ltsan
else ifeq ($(MODE),lsan)
MODEFLAGS	::= -O1 -fno-omit-frame-pointer -fstack-protector-all -fno-inline -g -fsanitize=leak
LDMODEFLAGS	 += -llsan
endif
MODEFLAGS        += $(if $(__DEV__),-DASE_ENABLE_DEBUG -DG_ENABLE_DEBUG)

# Beware, -ffast-math disables errno from math functions, math traps and signaling NaNs.
# It adds: -fno-math-errno -fno-rounding-math -fno-signaling-nans -fno-trapping-math
# -ffinite-math-only -fcx-limited-range -fno-signed-zeros
# -fexcess-precision=fast -fassociative-math -freciprocal-math
MODEFLAGS	 += -ffast-math
# Enforce proper handling of NaN and ±Inf
MODEFLAGS	 += -fno-finite-math-only $(if $(HAVE_GCC), -fno-cx-limited-range)

# Enable compiler specific options
ifdef HAVE_CLANG  # clang++
  COMMONFLAGS	 += -Wno-tautological-compare -Wno-constant-logical-operand
  #COMMONFLAGS	 += -Wno-unused-command-line-argument
endif
ifdef HAVE_GCC    # g++
  COMMONFLAGS	 += -fno-delete-null-pointer-checks
  OPTIMIZE	 += -fdevirtualize-speculatively -ftracer -ftree-loop-distribution -ftree-loop-ivcanon -ftree-loop-im
  ifdef CCACHE
    COMMONFLAGS	 += -fdiagnostics-color=auto
  endif
endif

# AMD64 / X86_64 optimizations
ifeq ($(uname_M),x86_64)
  OPTIMIZE	+= -minline-all-stringops
  ifeq ($(INSN),sse)		# SSE
    # 2006 era: Use just core2 features, but tune for newer CPUs
    OPTIMIZE	+= -march=core2 -mtune=sandybridge
  else ifeq ($(INSN),fma)	# FMA AVX
    # 2015 era: Use haswell (Intel) and bdver4 (AMD Excavator Family 15h) instructions (bdver4 lacks HLE)
    OPTIMIZE	+= -march=haswell -mno-hle
  else 				# NATIVE
    INSN         = native
    # Fastest, best for build machine CPU, not recommended for release builds
    OPTIMIZE	+= -march=native
  endif
endif

pkgcflags	::= $(strip $(COMMONFLAGS) $(CONLYFLAGS) $(MODEFLAGS) $(OPTIMIZE) $(SANITIZECC)) $(CFLAGS)
pkgcxxflags	::= $(strip $(COMMONFLAGS) $(CXXONLYFLAGS) $(MODEFLAGS) $(OPTIMIZE) $(SANITIZECC)) $(CXXFLAGS)
pkgldflags	::= $(strip $(LDOPTIMIZE) $(LDMODEFLAGS)) -Wl,-export-dynamic -Wl,--as-needed -Wl,--no-undefined -Wl,-Bsymbolic-functions $(LDFLAGS)

# == implicit rules ==
compiledefs     = $(DEFS) $(EXTRA_DEFS) $($<.DEFS) $($@.DEFS) $(INCLUDES) $(EXTRA_INCLUDES) $($<.INCLUDES) $($@.INCLUDES)
compilecflags   = $(pkgcflags) $(EXTRA_FLAGS) $(EXTRA_CFLAGS) $($<.FLAGS) $($@.FLAGS) -MQ '$@' -MMD -MF '$@'.d
compilecxxflags = $(pkgcxxflags) $(EXTRA_FLAGS) $(EXTRA_CXXFLAGS) $($<.FLAGS) $($@.FLAGS) -MQ '$@' -MMD -MF '$@'.d
$>/%.o: %.c
	$(QECHO) CC $@
	$(Q) $(CCACHE) $(CC) $(CSTD) -fPIC $(compiledefs) $(compilecflags) -o $@ -c $<
$>/%.o: $>/%.c
	$(QECHO) CC $@
	$(Q) $(CCACHE) $(CC) $(CSTD) -fPIC $(compiledefs) $(compilecflags) -o $@ -c $<
$>/%.o: %.cc
	$(QECHO) CXX $@
	$(Q) $(CCACHE) $(CXX) $(CXXSTD) -fPIC $(compiledefs) $(compilecxxflags) -o $@ -c $<
$>/%.o: $>/%.cc
	$(QECHO) CXX $@
	$(Q) $(CCACHE) $(CXX) $(CXXSTD) -fPIC $(compiledefs) $(compilecxxflags) -o $@ -c $<

# == BUILDDIR_O ==
# $(call BUILDDIR_O, sourcefiles...) - generate object file names from sources
BUILDDIR_O = $(addprefix $>/, $(sort $(foreach X, .c .C .cc .CC .y .l, $(subst $X,.o,$(filter %$X,$1)))))

# == LINKER ==
# $(call LINKER, EXECUTABLE, OBJECTS, DEPS, LIBS, RELPATHS)
define LINKER
$1: $2	$3
	$$(QECHO) LD $$@
	$$(call LINKER.pre-hook,$@)
	$$(call LINKER.solink-hook,$@)
	$$Q $$(CXX) $$(CXXSTD) -fPIC -o $$@ $$(pkgldflags) $$($$@.LDFLAGS) $2 $4 $(LDLIBS) \
		$$(if $$(findstring --version-script, $$(pkgldflags) $$($$@.LDFLAGS) $2 $4 $(LDLIBS)), $$(useld_fast+vs), $$(useld_fast)) \
		$(foreach P, $5, -Wl$(,)-rpath='$$$$ORIGIN/$P' -L'$$(@D)/$P') -Wl$,--print-map >$$@.map
	$$(call LINKER.xdbg-hook,$@)
	$$(call LINKER.post-hook,$@)
endef

# == BUILD_SHARED_LIB ==
# $(call BUILD_SHARED_LIB_SOLINKS, libfoo.so.1.2.3.4); yields: libfoo.so.1 libfoo.so
BUILD_SHARED_LIB_SOLINKS  = $(shell X="$1" ; \
			      [[ $$X =~ (.*\.so\.[0-9]+)(\.[0-9]+)+$$ ]] && echo "$${BASH_REMATCH[1]}" ; \
			      [[ $$X =~ (.*\.so)(\.[0-9]+)+$$ ]] && echo "$${BASH_REMATCH[1]}" )
# $(call BUILD_SHARED_LIB_SONAME, libfoo.so.1.2.3); yields: libfoo.so.1
BUILD_SHARED_LIB_SONAME = $(shell X="$(notdir $1)" ; while [[ $${X} =~ \.[0-9]+(\.[0-9]+)$$ ]] ; do X="$${X%$${BASH_REMATCH[1]}}"; done ; echo "$$X")
# BUILD_SHARED_LIB implementation
define BUILD_SHARED_LIB.impl
ALL_TARGETS += $1
# always build shared libraries with SONAME
$1: SONAME_LDFLAGS ::= -shared -Wl,-soname,$(call BUILD_SHARED_LIB_SONAME, $1)
# create .so link aliases before linking the .so
$1: LINKER.solink-hook ::= \
	$$Q cd . \
	  $$(foreach L, $(call BUILD_SHARED_LIB_SOLINKS, $1), \
	    && rm -f $$L && ln -s $(notdir $1) $$L)
# split debug info, using the --add-gnu-debuglink samedir logic
$1: LINKER.xdbg-hook ::= \
	$$Q $(if $(filter xdbg, $6), \
		cd $(dir $1) \
		&& objcopy --only-keep-debug $(notdir $1) $(notdir $1).debug \
		&& objcopy --strip-debug --add-gnu-debuglink=$(notdir $1).debug $(notdir $1) \
		&& mkdir -p .debug/ && mv $(notdir $1).debug .debug/ \
		|| { echo "$$0: objcopy failed for:" $1 >&2; exit 2; } \
)
# apply generic linker rule
$(call LINKER, $1, $2, $3, $4 $$(SONAME_LDFLAGS), $5)
# force re-linking if a link alias is missing
$$(foreach L, $(call BUILD_SHARED_LIB_SOLINKS, $1), \
  $$(eval $$L: $1 ;) \
  $$(if $$(wildcard $$L),, \
    $$(eval .PHONY: $1)) )
endef
# $(call BUILD_SHARED_LIB, sharedlibrary, objects, deps, libs, rpath)
BUILD_SHARED_LIB = $(eval $(call BUILD_SHARED_LIB.impl, $1, $2, $3, $4, $5))
# $(call BUILD_SHARED_LIB, sharedlibrary, objects, deps, libs, rpath)
BUILD_SHARED_LIB_XDBG = $(eval $(call BUILD_SHARED_LIB.impl, $1, $2, $3, $4, $5, xdbg))

# == BUILD_STATIC_LIB ==
# BUILD_STATIC_LIB implementation
define BUILD_STATIC_LIB.impl
ALL_TARGETS += $1
$1: $2	$3
	$$(QECHO) AR $$@
	$$Q $$(AR) rcs $$@ $2
endef
# $(call BUILD_STATIC_LIB, staticlibrary, objects, deps)
BUILD_STATIC_LIB = $(eval $(call BUILD_STATIC_LIB.impl, $1, $2, $3))

# == BUILD_PROGRAM ==
# $(call BUILD_PROGRAM, executable, objects, deps, libs, rpath)
BUILD_PROGRAM = $(eval $(call LINKER, $1, $2, $3, $4, $5))	$(eval ALL_TARGETS += $1)

# == BUILD_TEST ==
# $(call BUILD_TEST, executable, objects, deps, libs, rpath)
BUILD_TEST = $(eval $(call LINKER, $1, $2, $3, $4, $5))	$(eval ALL_TESTS += $1)

# == INSTALL_*_RULE ==
define INSTALL_FILE_RULE.impl
.PHONY: install--$(strip $1) uninstall--$(strip $1)
install--$(strip $1): $3 # mkdir $2, avoid EBUSY by deleting first, add link aliases L, install target T
	$$(QECHO) INSTALL '$(strip $2)/.'
	$$(call INSTALL_RULE.pre-hook,$@)
	$$Q $$(INSTALL) -d '$(strip $2)'
	$$Q cd '$(strip $2)' \
	  $$(foreach T, $(notdir $3), \
	    && rm -f $$T \
	    $$(foreach L, $$(call BUILD_SHARED_LIB_SOLINKS, $$T), \
	      && rm -f $$L && ln -s $$T $$L) )
	$$Q $4 $$^ '$(strip $2)'
	$$Q $$(foreach D, $$(if $$(filter xdbg, $5), \
				$$(wildcard $$(addsuffix .debug, $$^))), \
		$4 $$D '$(strip $2)')
	$$Q $$(foreach D, $$(if $$(filter xdbg, $5), \
				$$(wildcard $$(join $$(dir $$^), $$(patsubst %, .debug/%.debug, $$(notdir $$^))))), \
		$4 $$D -D '$(strip $2)'/.debug/$$(notdir $$D))
	$$(call INSTALL_RULE.post-hook,$@)
install: install--$(strip $1)
uninstall--$(strip $1): # delete target T and possible link aliases L
	$$(QECHO) REMOVE '$(strip $2)/.'
	$$Q if cd '$(strip $2)' 2>/dev/null ; then \
	      rm -f	$$(foreach T, $(notdir $3), $$T $$T.debug .debug/$$T.debug \
			   $$(foreach L, $$(call BUILD_SHARED_LIB_SOLINKS, $$T), $$L) ) ; \
	    fi
	$$Q rmdir -p '$(strip $2)' >/dev/null 2>&1 ; :
uninstall: uninstall--$(strip $1)
endef
# $(call INSTALL_RULE, rulename, directory, files)
INSTALL_DATA_RULE     = $(eval $(call INSTALL_FILE_RULE.impl,$(strip $1),$2, $3, $(INSTALL_DATA)))
INSTALL_BIN_RULE      = $(eval $(call INSTALL_FILE_RULE.impl,$(strip $1),$2, $3, $(INSTALL)))
INSTALL_BIN_RULE_XDBG = $(eval $(call INSTALL_FILE_RULE.impl,$(strip $1),$2, $3, $(INSTALL), xdbg))

define INSTALL_DIR_RULE.impl
.PHONY: install--$(strip $1) uninstall--$(strip $1)
install--$(strip $1): $3 $4 # mkdir $2, avoid EBUSY by first deleting target files, then install target T
	$$(QECHO) INSTALL '$(strip $2)/$(notdir $(abspath $(word 1,$3)))/.'
	$$(call INSTALL_RULE.pre-hook,$@)
	$$Q $$(INSTALL) -d '$(strip $2)'
	$$Q cd '$(strip $2)' && \
	      rm -f -r	$$(foreach T, $(notdir $(abspath $3)), $$T)
	$$Q $(CP) -dR $3 '$(strip $2)'
	$$(call INSTALL_RULE.post-hook,$@)
install: install--$(strip $1)
uninstall--$(strip $1): # delete target T
	$$(QECHO) REMOVE '$(strip $2)/$(notdir $(abspath $(word 1,$3)))/.'
	$$Q if cd '$(strip $2)' 2>/dev/null ; then \
	      rm -f -r	$$(foreach T, $(notdir $(abspath $3)), $$T) ; \
	    fi
	$$Q rmdir -p '$(strip $2)' >/dev/null 2>&1 ; :
uninstall: uninstall--$(strip $1)
endef
# $(call INSTALL_DIR_RULE, rulename, installdirectory, DIRNAMES, dependencies) - install directories via recursive copy
INSTALL_DIR_RULE = $(eval $(call INSTALL_DIR_RULE.impl,$(strip $1),$2, $3, $4))

# $(call INSTALL_SYMLINK, TARGET, LINKNAME) - install symbolic link to target file
INSTALL_SYMLINK = rm -f $2 && mkdir -p "$$(dirname $2)" && ln -s $1 $2
