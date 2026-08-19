CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -Iinclude

TARGET = shellforge

SRC = src/main.c \
      src/token.c \
      src/lexer.c \
      src/history.c \
      src/parser.c \
      src/expand.c

OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)'
