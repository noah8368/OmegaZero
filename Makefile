CC = g++
FLAGS = -D_GLIBCXX_PARALLEL -fno-signed-zeros -fno-trapping-math -fopenmp \
		-frename-registers -funroll-loops -lboost_program_options \
		-march=native -Ofast -pedantic -std=c++11 -Wall -Wconversion -Werror \
		-Wextra -Wshadow

OBJECTS = build/board.o build/engine.o build/game.o build/magics.o \
          build/main.o build/masks.o

all : build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS)
build/%.o: src/%.cc
	$(CC) -c -o $@ $< $(FLAGS)

build :
	mkdir $@
src/masks.cc :
	python3 scripts/generate_masks.py
src/magics.cc :
	python3 scripts/mine_magics.py

.PHONY: clean
clean:
	rm -rf build
