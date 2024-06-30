# Define variables
CC = gcc
CFLAGS = -o
TARGET = marmalade-generator-run
BIN_DIR = /usr/local/bin

all: build install

build: $(TARGET).c
	$(CC) $(TARGET).c $(CFLAGS) $(TARGET)

clean:
	rm -f $(TARGET)

install: build
	sudo cp $(TARGET) $(BIN_DIR)
	sudo chown root:root $(BIN_DIR)/$(TARGET)
	sudo chmod 4755 $(BIN_DIR)/$(TARGET)

uninstall:
	sudo rm -f $(BIN_DIR)/$(TARGET)
