all: server client 

client: server.o client.o
	gcc -o client client.o

server: server.o
	gcc -o server server.o -lpthread

server.o: server.c Process.h
	gcc -c server.c
	
client.o: client.c Process.h
	gcc -c client.c
	
clean:
	rm -f *.o client server any.txt
