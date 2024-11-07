#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "http.h"

#define PORT "8080"

// Function to get the address, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

unsigned short get_port(struct addrinfo *info) {
    if(info->ai_family == AF_INET) {
        return htons(((struct sockaddr_in*)info->ai_addr)->sin_port);
    }
    return htons(((struct sockaddr_in6*)info->ai_addr)->sin6_port);
}


pid_t start_websocket_server() {
    char* websocket_server = "./websockserv";
    char* args[] = {"./websockserv", NULL};
    pid_t pid = fork();
    if(pid == -1) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        if(execvp(websocket_server, args) == -1) {
            perror("exec failed");
        }
    } 
    return pid;
}


int main(void) {
    // immediately start the websocket server
//    pid_t websock_server = start_websocket_server();

    int listenfd, sockfd, rv, yes = 1;
    struct addrinfo hints, *res, *p, l;
    char myip[INET_ADDRSTRLEN];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "server: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(listenfd < 0) {
            continue;
        }

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if(bind(listenfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listenfd);
            continue;
        }

        break;
    }

    if(p == NULL) {
        fprintf(stderr, "could not assign a socket\n");
        exit(2);
    }
    memcpy(&l, p, sizeof(struct addrinfo));
    freeaddrinfo(res);

    if(listen(listenfd, 10) == -1) {
        perror("listen");
        exit(1);
    }
    printf("HTTP server is listening on %s port %d\n", inet_ntop(l.ai_family, get_in_addr(l.ai_addr), myip, INET_ADDRSTRLEN), get_port(&l));

    struct sockaddr_storage theiraddr;
    socklen_t addrlen;

    char buf[4096];
    char remoteIP[INET6_ADDRSTRLEN];

    fd_set readfds;
    fd_set master;
    fd_set websocks;
    FD_ZERO(&master);
    FD_ZERO(&readfds);
    FD_ZERO(&websocks);
    FD_SET(listenfd, &master);
    int fd_max = listenfd;

    for( ; ; ) {
        readfds = master;
        if(select(fd_max + 1, &readfds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for(int i = 0; i < fd_max + 1; i++) {
            if(FD_ISSET(i, &readfds)) {
                if(i == listenfd) {
                    addrlen = sizeof(theiraddr);
                    sockfd = accept(listenfd, (struct sockaddr*)&theiraddr, &addrlen);
                    if(sockfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(sockfd, &master);
                        if(sockfd > fd_max) {
                            fd_max = sockfd;
                        }
                        printf("server: new connection from %s on socket %d\n",
                               inet_ntop(theiraddr.ss_family, get_in_addr((struct sockaddr*)&theiraddr), remoteIP, INET6_ADDRSTRLEN), sockfd);
                    }
                } else {
                    int nbytes;
                    if ((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
                        if (nbytes == 0) {
                            printf("server: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i);
                        FD_CLR(i, &master);
                    } else {
                        buf[nbytes] = '\0';
                        printf("Received request: %s\n", buf);

                        /*
                            Insert request handling here
                            This implementation is designed to only serve
                            one page. The websocket server is where the
                            magic happens
                        */
                        char* html_content = read_html_file("index.html");
                        char* response = create_http_response(html_content);
                        if(send_http_response(i, response) != 0) {
                            perror("send http response");
                        }

                        free(html_content);
                        free(response);
                        close(i);
                        FD_CLR(i, &master);
                    }
                }
            }
        }
    }
    wait(NULL);
    return 0;
}

