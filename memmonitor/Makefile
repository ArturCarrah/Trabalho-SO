CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

all: monitor stress

monitor: monitor.c
	$(CC) $(CFLAGS) -o monitor monitor.c

stress: stress.c
	$(CC) $(CFLAGS) -o stress stress.c

clean:
	rm -f monitor stress
