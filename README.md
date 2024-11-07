# C Language Websocket Server
http_websock_serv.c:
A simple http/websocket combo server program demonstrated with a simple chat application

## Installation

*1 clone the repository with `git clone https://github.com/joshhellauer/C-Language-Websocket-Server.git`
*2 navigate into cloned repository
*3 compile with `make`

*4 change the address in the WebSocket object in index.html to your desired address. (localhost by default)

*5 run the server with `./http_websock_serv`
*6 navigate to http://<server ip>:8080 on client machines

You can also run the httpserver or websocket server by themselves with `./httpserv` and `./websockserv` respectively. When run together, they serve in the same way as `./http_websock_serv`. The first line of the main method in httpserv.c can be uncommented to run the websocket server automatically when the http server is starting up.