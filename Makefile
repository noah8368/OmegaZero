CC = g++
UNAME_S := $(shell uname -s)

# Compiler flags common to all platforms and build types.
# Note: -lboost_program_options is a linker flag and belongs only in link steps;
# moved out of FLAGS so it isn't passed during compilation.
FLAGS = -march=native -pedantic -std=c++17 -Wall -Werror -Wextra -Wshadow

# -O3 instead of -Ofast: drops -ffast-math, which can silently break
# floating-point behavior. The remaining flags are safe on both platforms.
OPT_FLAGS = -O3 -fno-signed-zeros -fno-trapping-math -funroll-loops

DEBUG_FLAGS = -O0 -g

ifeq ($(UNAME_S), Darwin)
  # Homebrew on Apple Silicon installs to /opt/homebrew.
  BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
  FLAGS += -I$(BREW_PREFIX)/include
  LINK_FLAGS = -L$(BREW_PREFIX)/lib -lboost_program_options
else
  # Linux/GCC: restore the parallel STL and OpenMP flags if ever wired up.
  LINK_FLAGS = -lboost_program_options
endif

DEBUG_OBJECTS = debug_build/board.o debug_build/engine.o debug_build/game.o \
                debug_build/magics.o debug_build/main.o debug_build/masks.o \
                debug_build/transposition_table.o debug_build/piece_sq_tables.o
OBJECTS = build/board.o build/engine.o build/game.o build/magics.o \
          build/main.o build/masks.o build/transposition_table.o \
          build/piece_sq_tables.o

all : build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(OPT_FLAGS) $(LINK_FLAGS)
build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS)

debug : debug_build $(DEBUG_OBJECTS)
	$(CC) -o debug_build/OmegaZero $(DEBUG_OBJECTS) $(FLAGS) $(DEBUG_FLAGS) $(LINK_FLAGS)
debug_build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(DEBUG_FLAGS)

build :
	mkdir $@
debug_build :
	mkdir $@

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
	@echo "All dependencies satisfied. Run 'make debug' or 'make all' to build."

.PHONY: purge
purge:
	rm -rf build debug_build

.PHONY: clean
clean:
	rm -f build/board.o build/engine.o build/game.o build/main.o \
	   build/transposition_table.o build/piece_sq_tables.o build/OmegaZero \
	   debug_build/board.o debug_build/engine.o debug_build/game.o \
	   debug_build/main.o debug_build/transposition_table.o \
	   debug_build/piece_sq_tables.o debug_build/OmegaZero
