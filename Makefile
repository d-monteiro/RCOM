CC = gcc
CFLAGS =
BIN = bin

all: $(BIN)/write $(BIN)/read $(BIN)/cable

$(BIN):
	mkdir -p $(BIN)

$(BIN)/write: write_noncanonical.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/read: read_noncanonical.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/cable: cable.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BIN) a.out
