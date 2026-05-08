CC = g++
UNAME_S := $(shell uname -s)

# Compiler flags common to all platforms and build types.
# Note: -lboost_program_options is a linker flag and belongs only in link steps;
# moved out of FLAGS so it isn't passed during compilation.
FLAGS = -march=native -pedantic -std=c++17 -Wall -Werror -Wextra -Wshadow

# -O3 instead of -Ofast: drops -ffast-math, which can silently break
# floating-point behavior. The remaining flags are safe on both platforms.
OPT_FLAGS = -O3 -fno-signed-zeros -fno-trapping-math -funroll-loops

DEBUG_FLAGS = -O0 -g -fsanitize=address -fno-omit-frame-pointer -DDEBUG

ifeq ($(UNAME_S), Darwin)
  # Homebrew on Apple Silicon installs to /opt/homebrew.
  BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
  FLAGS += -I$(BREW_PREFIX)/include
  LINK_FLAGS = -L$(BREW_PREFIX)/lib -lboost_program_options
else
  # Linux/GCC: restore the parallel STL and OpenMP flags if ever wired up.
  LINK_FLAGS = -lboost_program_options
endif

OBJECTS = build/play/board.o build/play/engine.o build/play/game.o build/play/magics.o \
          build/play/main.o build/play/masks.o build/play/transposition_table.o \
          build/play/piece_sq_tables.o build/play/uci.o

DEBUG_OBJECTS = build/debug/board.o build/debug/engine.o build/debug/game.o \
                build/debug/magics.o build/debug/test_harness.o \
                build/debug/masks.o build/debug/transposition_table.o \
                build/debug/piece_sq_tables.o

BENCH_OBJECTS = build/bench/board.o build/bench/engine.o build/bench/game.o \
                build/bench/magics.o build/bench/test_harness.o \
                build/bench/masks.o build/bench/transposition_table.o \
                build/bench/piece_sq_tables.o

all : build/play $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(OPT_FLAGS) $(LINK_FLAGS)
debug : build/debug $(DEBUG_OBJECTS)
	$(CC) -o build/test_harness $(DEBUG_OBJECTS) $(FLAGS) $(DEBUG_FLAGS) $(LINK_FLAGS)
bench : build/bench $(BENCH_OBJECTS)
	$(CC) -o build/bench_harness $(BENCH_OBJECTS) $(FLAGS) $(OPT_FLAGS) -DBENCHMARK $(LINK_FLAGS)
build/play/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS)
build/debug/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(DEBUG_FLAGS)
build/bench/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS) -DBENCHMARK

build :
	mkdir $@
build/play : build
	mkdir -p $@
build/debug : build
	mkdir -p $@
build/bench : build
	mkdir -p $@

src/masks.cc :
	python3 scripts/generate_masks.py
src/magics.cc :
	python3 scripts/mine_magics.py

.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@command -v $(CC) >/dev/null 2>&1 || \
	  { echo "ERROR: $(CC) not found. Install it:"; \
	    if [ "$(UNAME_S)" = "Darwin" ]; then echo "  xcode-select --install"; \
	    else echo "  sudo apt-get install g++"; fi; exit 1; }
	@command -v python3 >/dev/null 2>&1 || \
	  { echo "ERROR: python3 not found (needed to generate masks/magics). Install it:"; \
	    if [ "$(UNAME_S)" = "Darwin" ]; then echo "  brew install python3"; \
	    else echo "  sudo apt-get install python3"; fi; exit 1; }
	@$(CC) $(FLAGS) -x c++ /dev/null -c -o /dev/null >/dev/null 2>&1 \
	  && echo '#include <boost/program_options.hpp>' | \
	     $(CC) $(FLAGS) -x c++ - -fsyntax-only >/dev/null 2>&1 \
	  || { echo "ERROR: boost not found. Install it:"; \
	       if [ "$(UNAME_S)" = "Darwin" ]; then echo "  brew install boost"; \
	       else echo "  sudo apt-get install libboost-program-options-dev"; fi; exit 1; }
	@echo "All dependencies satisfied. Run 'make' to build."

.PHONY: clean
clean:
	rm -rf build
