all: server 
server: server.o
        gcc -o server server.o -Wall
server.o: serverTP.c
        gcc -c -pthread serverTP.c -c -o server.o
clean:
        rm -f *.o