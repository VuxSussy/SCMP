# ====================
#  LIBRARY DEFINITIONS
# ====================

TARGET= scmp
TEST_TARGET= run_tests
PERF_TEST_TARGET= run_perf_tests

###############################################################################
##  SETTINGS                                                                 ##
###############################################################################

CLANG := $(shell which clang)
CCACHE := $(shell which ccache)
OS= $(shell uname)

#prefer clang over g++
ifdef SCMP_COMPILER
CXX=$(SCMP_COMPILER)
endif

ifndef PREFIX
PREFIX=/usr/local/bin/
endif

DIR=$(shell pwd)
BUILD_DIR = $(DIR)/build
BUILD_TEST_DIR = $(DIR)/build_tests
BUILD_PERF_TEST_DIR = $(DIR)/build_perf_tests

# Override optimizations via: make OPT_LEVEL=n
OPT_LEVEL = 3

# Make-local Compiler Flags
CXX_FLAGS = -std=c++14
CXX_FLAGS += -O$(OPT_LEVEL)
CXX_FLAGS += -march=native
CXX_FLAGS += -ffast-math
CXX_FLAGS += -fno-omit-frame-pointer
CXX_FLAGS += -D__extern_always_inline=inline

TEST_CCACHE_CLANG=ccache clang++
TEST_CLANG=clang++

ALL_WARNINGS=-Werror -Weverything -Wno-variadic-macros -Wno-format-nonliteral -Wno-global-constructors -Wno-exit-time-destructors -Wno-padded -Wno-reserved-id-macro -Wno-gnu-zero-variadic-macro-arguments -Wno-c++98-compat
# Only turn on extra warnings for clang since g++ does not support -Weverything
ifneq ($(OS),Darwin)
ifeq ($(CXX),$(TEST_CLANG))
CXX_FLAGS += $(ALL_WARNINGS)
else
ifeq ($(CXX),$(TEST_CCACHE_CLANG))
CXX_FLAGS += $(ALL_WARNINGS)
endif
endif
endif

#perf tests should not have many warnings or error out on warning
PERF_TEST_CXX_FLAGS = -std=c++14
PERF_TEST_CXX_FLAGS += -O$(OPT_LEVEL)
PERF_TEST_CXX_FLAGS += -march=native
PERF_TEST_CXX_FLAGS += -ffast-math
PERF_TEST_CXX_FLAGS += -fno-omit-frame-pointer
PERF_TEST_CXX_FLAGS += -ggdb -g2

ifeq ($(OS),Darwin)
MACOSX_DEPLOYMENT_TARGET ?= 14.2
CXX_FLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET) -D_OS_OSX -D_XOPEN_SOURCE_EXTENDED
PERF_TEST_CXX_FLAGS += -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
else
CXX_FLAGS += -D_LINUX
endif

# Linker flags
LD_FLAGS = $(LDFLAGS) -fno-omit-frame-pointer
PERF_TEST_LD_FLAGS = -fno-omit-frame-pointer

ifeq ($(OS),Darwin)
LD_FLAGS += -D_XOPEN_SOURCE_EXTENDED
endif

# DEBUG Settings
ifdef DEBUG
OPT_LEVEL=0
CXX_FLAGS += -ggdb -g2 -DSCMP_LOG_DEBUG
LD_FLAGS += -DSCMP_LOG_DEBUG
endif

# Clang sanitize options
ifdef SANITIZE
#to get symbols from clang sanitize, run "export ASAN_SYMBOLIZER_PATH=<path_to_llvm_symbolizer>" on Arch Linux symbolizer is usually at"/usr/bin/llvm-symbolizer"
CXX_FLAGS += -fsanitize=$(SANITIZE)
LD_FLAGS += -fsanitize=$(SANITIZE)
endif

# Include Paths
INCLUDE_PATH = -I/usr/local/include -I$(DIR)/include -I$(DIR)/src
TEST_INCLUDE_PATH = -I/usr/include
PERF_TEST_INCLUDE_PATH = -I/usr/include

# Lib Paths
LIB_PATH = -L/usr/local/lib

# Libs
LIBS = -lfftw3 -lm
TEST_LIBS = -lgtest -lpthread
PERF_TEST_LIBS = -lbenchmark -lpthread

ifeq ($(OS),Darwin)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
FFTW_PREFIX ?= $(BREW_PREFIX)/opt/fftw
NCURSES_PREFIX ?= $(BREW_PREFIX)/opt/ncurses
INCLUDE_PATH := -I$(FFTW_PREFIX)/include -I$(NCURSES_PREFIX)/include $(INCLUDE_PATH)
LIB_PATH := -L$(FFTW_PREFIX)/lib -L$(NCURSES_PREFIX)/lib $(LIB_PATH)
CXX_FLAGS += -DNCURSESW
LIBS += -lncursesw -framework CoreAudio -framework Foundation -framework AVFoundation -framework CoreGraphics -framework ImageIO -framework JavaScriptCore
PLUGIN_SUFFIX = dylib
PLUGIN_FLAGS = -dynamiclib
SCMP_LINK_FLAGS = -Wl,-sectcreate,__TEXT,__info_plist,$(DIR)/src/Info.plist
SCMP_LINK_INPUT = src/Info.plist
else
LIBS += -ldl
PLUGIN_SUFFIX = so
PLUGIN_FLAGS = -shared -fPIC
#if this box has an older version of ncurses
ifneq ("$(wildcard /usr/include/ncursesw/ncurses.h)","")
	LIBS += -lncursesw
	CXX_FLAGS += -DNCURSESW
	LD_FLAGS += -DNCURSESW
else
	LIBS += -lncurses
endif
endif

#use jemalloc if available
ifneq ("$(wildcard /usr/lib/libjemalloc.so)","")
LIBS += -ljemalloc
endif

#clang tidy checks
CLANG_TIDY_CHECKS=clang-analyzer-*,modernize-*,readability-*,misc-*,cppcoreguidelines-*,google-*,-google-readability-namespace-comments

###############################################################################
##  OBJECTS                                                                  ##
###############################################################################

SOURCES= $(wildcard src/*.cpp) $(wildcard src/*/*.cpp) $(wildcard src/*/*/*.cpp)

HEADERS= $(wildcard include/*.h) $(wildcard include/*/*.h) $(wildcard src/*.h) $(wildcard src/*/*.h) $(wildcard src/*/*/*.h)

OBJECTS= $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.cpp=.o)))
ifeq ($(OS),Darwin)
MAC_SOURCES = $(wildcard src/*/*.mm)
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(MAC_SOURCES:.mm=.o)))
endif

TEST_SOURCES= $(wildcard tests/*.cpp) $(wildcard tests/*/*.cpp) $(wildcard tests/*/*/*.cpp)

TEST_OBJECTS= $(addprefix $(BUILD_TEST_DIR)/,$(notdir $(TEST_SOURCES:.cpp=.o))) $(filter-out $(BUILD_DIR)/$(TARGET).o,$(OBJECTS))

PERF_TEST_SOURCES= $(wildcard perf_tests/*.cpp) $(wildcard perf_tests/*/*.cpp) $(wildcard perf_tests/*/*/*.cpp)

PERF_TEST_OBJECTS= $(addprefix $(BUILD_PERF_TEST_DIR)/,$(notdir $(PERF_TEST_SOURCES:.cpp=.o))) $(filter-out $(BUILD_DIR)/$(TARGET).o,$(OBJECTS))

VPATH= $(dir $(wildcard src/*/ src/*/*/)) $(dir $(wildcard tests/*/ tests/*/*/)) $(dir $(wildcard perf_tests/*/ perf_tests/*/*/))

###############################################################################
##  MAIN TARGETS                                                             ##
###############################################################################

all: build

.PHONY: example-plugin
example-plugin: $(BUILD_DIR)/plugins/pulse-grid.$(PLUGIN_SUFFIX)

$(BUILD_DIR)/plugins/pulse-grid.$(PLUGIN_SUFFIX): examples/plugins/pulse-grid.c include/scmp/plugin.h
	mkdir -p $(BUILD_DIR)/plugins
	$(CC) $(PLUGIN_FLAGS) -I$(DIR)/include $< -o $@

test: tests

tests: prepare_tests build_tests

perf_tests: prepare_perf_tests build_perf_tests

.PHONY: prepare
prepare:
	mkdir -p $(BUILD_DIR)
	rm -f $(BUILD_DIR)/$(TARGET)

.PHONY: prepare_tests
prepare_tests:
	mkdir -p $(BUILD_TEST_DIR)
	rm -f $(BUILD_TEST_DIR)/$(TEST_TARGET)

.PHONY: prepare_perf_tests
prepare_perf_tests:
	mkdir -p $(BUILD_PERF_TEST_DIR)
	rm -f $(BUILD_PERF_TEST_DIR)/$(PERF_TEST_TARGET)

.PHONY: build
build: $(OBJECTS) $(TARGET)

.PHONY: build build_tests run_tests
build_tests: $(TEST_OBJECTS) $(TEST_TARGET)

.PHONY: build build_perf_tests run_perf_tests
build_perf_tests: $(PERF_TEST_OBJECTS) $(PERF_TEST_TARGET)

.PHONY:clean
clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf $(BUILD_TEST_DIR)
	@rm -rf $(BUILD_PERF_TEST_DIR)

uninstall:
	@rm -f $(BUILD_DIR)/$(TARGET)
	@rm -f "$(PREFIX)/$(TARGET)" "$(PREFIX)/scmp-spotify"

install: $(TARGET)
	cp "$(BUILD_DIR)/$(TARGET)" "$(BUILD_DIR)/scmp-spotify" "$(PREFIX)"

###############################################################################
##  BUILD TARGETS                                                            ##
###############################################################################

$(BUILD_DIR)/%.o: %.cpp $(HEADERS) Makefile
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) $(LD_FLAGS) $(INCLUDE_PATH) -c $< -o $@

$(BUILD_DIR)/%.o: %.mm $(HEADERS) Makefile
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXX_FLAGS) $(INCLUDE_PATH) -fobjc-arc -c $< -o $@

WALLPAPER_SCENES = $(wildcard vendor/claude-desktop-themes/scenes/*.js)
WALLPAPER_SOURCES = vendor/claude-desktop-themes/scene-kit.js $(WALLPAPER_SCENES) src/Writer/WallpaperAdapter.js
WALLPAPER_LICENSE = vendor/claude-desktop-themes/LICENSE
$(BUILD_DIR)/Wallpapers.inc: $(WALLPAPER_SOURCES) $(WALLPAPER_LICENSE) Makefile
	mkdir -p $(BUILD_DIR)
	{ printf '%s\n' 'static const char wallpaper_source[] = R"SCMP_WALLPAPER('; \
	  printf '%s\n' '/*'; cat $(WALLPAPER_LICENSE); printf '%s\n' '*/'; \
	  cat $(firstword $(WALLPAPER_SOURCES)); \
	  for scene in $(WALLPAPER_SCENES); do printf '%s\n' '{'; cat "$$scene"; printf '\n}\n'; done; \
	  cat $(lastword $(WALLPAPER_SOURCES)); printf '\n%s\n' ')SCMP_WALLPAPER";'; } > $@.tmp
	mv $@.tmp $@

$(BUILD_DIR)/MacWallpaper.o: $(BUILD_DIR)/Wallpapers.inc
$(BUILD_DIR)/MacWallpaper.o: INCLUDE_PATH += -I$(BUILD_DIR)

.PHONY: $(TARGET)
$(TARGET): $(BUILD_DIR)/$(TARGET) $(BUILD_DIR)/scmp-spotify

$(BUILD_DIR)/scmp-spotify: tools/scmp-spotify
	mkdir -p "$(BUILD_DIR)"
	install -m 755 "$<" "$@"

$(BUILD_DIR)/$(TARGET): $(OBJECTS) $(SCMP_LINK_INPUT)
	$(CXX) $(CXX_FLAGS) $(LDFLAGS) $(SCMP_LINK_FLAGS) $(LIB_PATH) -o $@ $(OBJECTS) $(LIBS)

$(BUILD_TEST_DIR)/%.o: %.cpp $(HEADERS) Makefile
	mkdir -p $(BUILD_TEST_DIR)
	$(CXX) $(CXX_FLAGS) $(LD_FLAGS) $(INCLUDE_PATH) $(TEST_INCLUDE_PATH) -c $< -o $@

$(TEST_TARGET): $(OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(CXX_FLAGS) $(LDFLAGS) $(INCLUDE_PATH) $(TEST_INCLUDE_PATH) $(LIB_PATH) -o $(BUILD_TEST_DIR)/$(TEST_TARGET) $(TEST_OBJECTS) $(LIBS) $(TEST_LIBS)
	$(BUILD_TEST_DIR)/$(TEST_TARGET)

$(BUILD_PERF_TEST_DIR)/%.o: %.cpp $(HEADERS) Makefile
	mkdir -p $(BUILD_PERF_TEST_DIR)
	$(CXX) $(PERF_TEST_CXX_FLAGS) $(PERF_TEST_LD_FLAGS) $(INCLUDE_PATH) $(PERF_TEST_INCLUDE_PATH) -c $< -o $@

$(PERF_TEST_TARGET): $(OBJECTS) $(PERF_TEST_OBJECTS)
	$(CXX) $(PERF_TEST_CXX_FLAGS) $(PERF_TEST_LD_FLAGS) $(INCLUDE_PATH) $(PERF_TEST_INCLUDE_PATH) $(LIB_PATH) -o $(BUILD_PERF_TEST_DIR)/$(PERF_TEST_TARGET) $(PERF_TEST_OBJECTS) $(LIBS) $(PERF_TEST_LIBS)
	$(BUILD_PERF_TEST_DIR)/$(PERF_TEST_TARGET)

clang_tidy: $(HEADERS) $(SOURCES) $(TEST_SOURCES) $(PERF_TEST_SOURCES)
	clang-tidy -checks=${CLANG_TIDY_CHECKS} $? -- -x c++ -std=c++14 -I$(INCLUDE_PATH)

clang_format: $(HEADERS) $(SOURCES) $(TEST_SOURCES) $(PERF_TEST_SOURCES)
	clang-format -i $?
