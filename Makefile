OBJECTS = build/main.o build/game.o build/board.o build/masks.o build/magics.o
CC = g++
FLAGS = -g -O0

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
