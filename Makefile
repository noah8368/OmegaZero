OBJECTS = build/main.o build/game.o build/board.o build/masks.o build/magics.o
CC = g++
FLAGS = -g -Og -lgmp -lgmpxx

all : build $(OBJECTS)
	$(CC) -o build/OmegaZero $(OBJECTS) $(FLAGS)
build/%.o: src/%.cc
	$(CC) $(FLAGS) -c -o $@ $<

build :
	mkdir $@
src/masks.cpp :
	python3 scripts/generate_masks.py
src/magics.cpp :
	python3 scripts/mine_magics.py

.PHONY: clean
clean:
	rm -rf build
