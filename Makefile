CC=gcc
TARGET1 = trace
CFLAGS= -lpthread -pthread -g -Wall -Wextra -pedantic -lm
FILES = networking.c set_uid.c
all: $(TARGET1).c $(FILES)
	$(CC) $(CFLAGS) -o $(TARGET1) $(TARGET1).c $(FILES)
	
	# set 's' bit to run traceroute at root level
	sudo chown root:root $(TARGET1) 
	sudo chmod 4755 $(TARGET1)
clean: $(TARGET1) 
	rm $(TARGET1)
