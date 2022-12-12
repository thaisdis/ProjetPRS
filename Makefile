all: server 
server: server.o network.o
	gcc -pthread -o server network.o server.o -Wall
server.o: server.c network.h
	gcc -pthread -c server.c -c -o server.o
network.o: network.c network.h
	gcc -c network.c -c -o network.o
clean:
	rm -f *.o


