all: server 
server: server.o
	gcc -pthread -o server server.o -Wall
server.o: server.c
	gcc -pthread -c server.c -c -o server.o
clean:
	rm -f *.o


