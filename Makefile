all: httpserv websockserv http_websock_serv

poll.o: poll.c
	gcc -c poll.c

http.o: http.c
	gcc -c http.c

websocket.o: websocket.c
	gcc -c websocket.c

httpserv.o: httpserv.c 
	gcc -c httpserv.c

websockserv.o: websockserv.c
	gcc -c websockserv.c

http_websock_serv.o: http_websock_serv.c
	gcc -c http_websock_serv.c

httpserv: httpserv.o http.o
	gcc -o httpserv httpserv.o http.o

websockserv: websockserv.o websocket.o
	gcc -o websockserv websockserv.o websocket.o -lcrypto -lssl

http_websock_serv: http_websock_serv.o http.o websocket.o poll.o
	gcc -o http_websock_serv http_websock_serv.o http.o websocket.o poll.o -lcrypto -lssl

clean:
	rm *.o websockserv httpserv http_websock_serv