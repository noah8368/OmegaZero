CC = g++
FLAGS = -g -o
EXE = OmegaZero
OBJECTS = main.o board.o game.o

$(EXE): $(OBJECTS)
	$(CC) $(FLAGS) $@ $(OBJECTS)
	rm -f *.o

main.o : constants.h game.h
board.o : constants.h
game.o : constants.h board.h

.PHONY: clean

clean:
	rm $(EXE)
