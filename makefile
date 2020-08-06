LIBRARIES=-lssl -lcrypto -lpthread -lz -lclamav
CC=gcc
OBJ = obj
SRC = src

proxy: $(OBJ)/proxy.o $(OBJ)/request.o $(OBJ)/get.o $(OBJ)/util.o
	$(CC) -g -o proxy $(OBJ)/proxy.o $(OBJ)/request.o $(OBJ)/get.o $(OBJ)/util.o $(LIBRARIES)

$(OBJ)/util.o: $(SRC)/util.c $(SRC)/proxy.h
	$(CC) -c -g -o $(OBJ)/util.o $(SRC)/util.c

$(OBJ)/proxy.o: $(SRC)/proxy.c $(SRC)/proxy.h
	$(CC) -c -g -o $(OBJ)/proxy.o $(SRC)/proxy.c

$(OBJ)/get.o: $(SRC)/get.c $(SRC)/proxy.h
	$(CC) -c -g -o $(OBJ)/get.o $(SRC)/get.c

$(OBJ)/request.o: $(SRC)/request.c $(SRC)/proxy.h
	$(CC) -c -g -o $(OBJ)/request.o $(SRC)/request.c

clean:
	rm $(OBJ)/*.o