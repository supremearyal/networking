TARGET = geturl
SRC = geturl.c
CC = gcc
CFLAGS = -Wextra -Wall

debug: CFLAGS += -g
debug: $(TARGET)

release: $(TARGET)

$(TARGET) : $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
