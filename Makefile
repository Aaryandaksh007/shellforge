CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -Iinclude

TARGET = shellforge

SRCS = src/main.c src/token.c src/lexer.c src/history.c src/parser.c src/expand.c src/executor.c src/jobs.c src/pagesim/pagesim.c

OBJ = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)
