CC = gcc
CFLAGS = -Wall -Iinclude
BIN = bin
SRC = src

all: $(BIN)/main $(BIN)/cable

$(BIN):
	mkdir -p $(BIN)

$(BIN)/main: $(SRC)/main.c $(SRC)/link_layer.c $(SRC)/application.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/cable: cable.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BIN) a.out
