# Compiler and Flags
CC = gcc
CFLAGS = -Iinclude
SRC = src/funciones_arboles.c

# Targets for all programs
all: serial_com serial_dec forks_com forks_dec threads_com threads_dec

# Serial Compress
serial_com: serial/compresor_serial.c
	$(CC) $(CFLAGS) $(SRC) serial/compresor_serial.c -o serial/compresor_serial

# Serial Decompresor
serial_dec: serial/decompresor_serial.c
	$(CC) $(CFLAGS) $(SRC) serial/decompresor_serial.c -o serial/decompresor_serial

# Forks compresor
forks_com: fork/compresor_fork.c
	$(CC) $(CFLAGS) $(SRC) fork/compresor_fork.c -o fork/compresor_fork

# Forks Decompresor
forks_dec: fork/decompresor_fork.c
	$(CC) $(CFLAGS) $(SRC) fork/decompresor_fork.c -o fork/decompresor_fork

# Threads compresor
threads_com: thread/compresor_thread.c
	$(CC) $(CFLAGS) $(SRC) thread/compresor_thread.c -o thread/compresor_thread

# Threads Decompresor
threads_dec: thread/decompresor_thread.c
	$(CC) $(CFLAGS) $(SRC) thread/decompresor_thread.c -o thread/decompresor_thread

# Clean up compiled programs
clean:
	rm -f serial/compresor_serial serial/decompresor_serial \
		fork/compresor_fork fork/decompresor_fork \
		thread/compresor_thread thread/decompresor_thread