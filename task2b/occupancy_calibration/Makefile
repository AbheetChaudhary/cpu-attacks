CC = gcc
CFLAGS = -Wall -lm

# Targets
all: sender.cal receiver.cal

# Build sender.cal executable
sender.cal: sender.cal.o utils.o
	$(CC) $(CFLAGS) -o sender.cal sender.cal.o utils.o

# Build receiver.cal executable
receiver.cal: receiver.cal.o utils.o
	$(CC) $(CFLAGS) -o receiver.cal receiver.cal.o utils.o

# Compile utils.c to utils.o
utils.o: utils.c utils.h
	$(CC) $(CFLAGS) -c utils.c -o utils.o

# Compile sender.cal.c to sender.cal.o
sender.cal.o: sender.cal.c utils.h
	$(CC) $(CFLAGS) -c sender.cal.c -o sender.cal.o

# Compile receiver.cal.c to receiver.cal.o
receiver.cal.o: receiver.cal.c utils.h
	$(CC) $(CFLAGS) -c receiver.cal.c -o receiver.cal.o

occupancy: occupancy.c utils.h utils.o
	$(CC) $(CLFLAGS) occupancy.c utils.o -o occupancy

# Clean up build artifacts
clean:
	rm -f sender.cal receiver.cal *.o
