CC = g++
FLAGS = -lboost_program_options -lrt -march=native -pedantic -std=c++17 -Wall \
        -Wconversion -Werror -Wextra -Wshadow
DEBUG_FLAGS = -O0 -g
OPT_FLAGS = -Ofast -D_GLIBCXX_PARALLEL -fno-signed-zeros -fno-trapping-math \
            -fopenmp -frename-registers -funroll-loops 
OBJECTS = build/board.o build/engine.o build/game.o build/magics.o \
          build/main.o build/masks.o build/transp_table.o

all : build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(OPT_FLAGS)
build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(OPT_FLAGS)

debug : debug_build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS) $(DEBUG_FLAGS)
debug_build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS) $(DEBUG_FLAGS)

build :
	mkdir $@
src/masks.cc :
	python3 scripts/generate_masks.py
src/magics.cc :
	python3 scripts/mine_magics.py

.PHONY: clean
clean:
	rm -rf build

.PHONY: quick_clean
quick_clean:
	rm build/board.o build/engine.o build/game.o build/main.o \
	build/transp_table.o build/OmegaZero