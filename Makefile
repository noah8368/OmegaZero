OBJECTS = build/main.o build/game.o build/board.o build/masks.o build/magics.o



build/OmegaZero : $(OBJECTS)
	gcc -o $@ $(OBJECTS) -lstdc++

build/main.o : src/main.cpp src/constants.h src/game.h build
	gcc -o $@ -c src/main.cpp -lstdc++
build/game.o : src/game.cpp src/board.h src/constants.h src/game.h src/move.h build
	gcc -o $@ -c src/game.cpp -lstdc++
build/board.o : src/board.cpp src/board.h src/constants.h src/move.h build
	gcc -o $@ -c src/board.cpp -lstdc++
build/masks.o : src/masks.cpp src/board.h src/constants.h build
	gcc -o $@ -c src/masks.cpp -lstdc++
build/magics.o : src/magics.cpp src/board.h src/constants.h build
	gcc -o $@ -c src/magics.cpp -lstdc++

build :
	mkdir $@

src/masks.cpp :
	python3 scripts/generate_masks.py
src/magics.cpp :
	python3 scripts/mine_magics.py

.PHONY: clean

clean:
	rm -rf build
