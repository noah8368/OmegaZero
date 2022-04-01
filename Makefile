CC = g++
FLAGS = -lboost_program_options -march=native -pedantic -std=c++17 -Wall \
        -Werror -Wextra -Wshadow
DEBUG_FLAGS = -O0 -g
OPT_FLAGS = -Ofast -D_GLIBCXX_PARALLEL -fno-signed-zeros -fno-trapping-math \
            -fopenmp -frename-registers -funroll-loops
DEBUG_OBJECTS = debug_build/board.o debug_build/engine.o debug_build/game.o \
				debug_build/magics.o debug_build/main.o debug_build/masks.o \
				debug_build/transposition_table.o debug_build/piece_sq_tables.o
OBJECTS = build/board.o build/engine.o build/game.o build/magics.o \
          build/main.o build/masks.o build/transposition_table.o \
		  build/piece_sq_tables.o

all : build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(OPT_FLAGS)
build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS)

debug : debug_build $(DEBUG_OBJECTS)
	$(CC) -o debug_build/OmegaZero $(DEBUG_OBJECTS) $(FLAGS) $(DEBUG_FLAGS)
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

.PHONY: purge
purge:
	rm -rf build debug_build

.PHONY: clean
clean:
	rm build/board.o build/engine.o build/game.o build/main.o \
	   build/transposition_table.o build/OmegaZero \
	   debug_build/board.o debug_build/engine.o debug_build/game.o \
	   debug_build/main.o debug_build/transposition_table.o \
	   debug_build/OmegaZero