CC = g++
UNAME_S := $(shell uname -s)

FLAGS = -march=native -pedantic -std=c++17 -Wall -Werror -Wextra -Wshadow -MMD -MP
OPT_FLAGS = -O3 -fno-signed-zeros -fno-trapping-math -funroll-loops
DEBUG_FLAGS = -O0 -g -fsanitize=address -fno-omit-frame-pointer -DDEBUG

OBJECTS = build/play/board.o build/play/engine.o build/play/game.o build/play/magics.o \
          build/play/main.o build/play/masks.o build/play/nnue.o build/play/transposition_table.o \
          build/play/piece_sq_tables.o build/play/uci.o

DEBUG_OBJECTS = build/debug/board.o build/debug/engine.o build/debug/game.o \
                build/debug/magics.o build/debug/nnue.o build/debug/debug_harness.o \
                build/debug/masks.o build/debug/transposition_table.o \
                build/debug/piece_sq_tables.o

BENCH_OBJECTS = build/bench/board.o build/bench/engine.o build/bench/game.o \
                build/bench/magics.o build/bench/nnue.o build/bench/debug_harness.o \
                build/bench/masks.o build/bench/transposition_table.o \
                build/bench/piece_sq_tables.o

DATAGEN_OBJECTS = build/datagen/board.o build/datagen/engine.o build/datagen/game.o \
                  build/datagen/magics.o build/datagen/nnue.o build/datagen/datagen.o \
                  build/datagen/masks.o build/datagen/transposition_table.o \
                  build/datagen/piece_sq_tables.o

all : build/play $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(OPT_FLAGS)
debug : build/debug $(DEBUG_OBJECTS)
	$(CC) -o build/debug_harness $(DEBUG_OBJECTS) $(FLAGS) $(DEBUG_FLAGS)
bench : build/bench $(BENCH_OBJECTS)
	$(CC) -o build/bench_harness $(BENCH_OBJECTS) $(FLAGS) $(OPT_FLAGS) -DBENCHMARK
datagen : build/datagen $(DATAGEN_OBJECTS)
	$(CC) -o build/datagen_harness $(DATAGEN_OBJECTS) $(FLAGS) $(OPT_FLAGS) -lpthread
build/play/magics.o: src/magics.cc
	$(CC) -c -o $@ $< $(FLAGS) -O0
build/play/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS)
build/debug/magics.o: src/magics.cc
	$(CC) -c -o $@ $< $(FLAGS) -O0
build/debug/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(DEBUG_FLAGS)
build/bench/magics.o: src/magics.cc
	$(CC) -c -o $@ $< $(FLAGS) -O0
build/bench/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS) -DBENCHMARK
build/datagen/magics.o: src/magics.cc
	$(CC) -c -o $@ $< $(FLAGS) -O0
build/datagen/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS) -DNDEBUG

build :
	mkdir $@
build/play : build
	mkdir -p $@
build/debug : build
	mkdir -p $@
build/bench : build
	mkdir -p $@
build/datagen : build
	mkdir -p $@

src/masks.cc :
	python3 scripts/generate_masks.py
src/magics.cc :
	python3 scripts/mine_magics.py

-include build/play/*.d build/debug/*.d build/bench/*.d build/datagen/*.d

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
	@python3 -c "import chess" >/dev/null 2>&1 || \
	  { echo "WARNING: python-chess not found (needed for NNUE data generation)."; \
	    echo "  pip3 install python-chess"; }
	@python3 -c "import torch" >/dev/null 2>&1 || \
	  { echo "WARNING: PyTorch not found (needed for NNUE training)."; \
	    echo "  pip3 install torch"; }
	@python3 -c "import tqdm" >/dev/null 2>&1 || \
	  { echo "WARNING: tqdm not found (needed for NNUE training progress bars)."; \
	    echo "  pip3 install tqdm"; }
	@echo "All dependencies satisfied. Run 'make' to build."

.PHONY: clean
clean:
	rm -rf build
