CC = gcc
CFLAGS = -Wall -Wextra -g -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lcurl -lmicrohttpd -lsqlite3 -lpthread

SRC = main.c monitor.c webui.c storage.c
OBJ = $(SRC:.c=.o)
EXEC = uptime_monitor

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

clean:
	rm -f $(OBJ) $(EXEC)

.PHONY: all clean