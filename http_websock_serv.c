#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "http.h"
#include "websocket.h"
#include "poll.h"
#include <poll.h>

#define PORT "8080"
#define BACKLOG_AMOUNT 10

void* get_in_addr(struct sockaddr* sa) {
    if(sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

unsigned short get_port(struct addrinfo *info) {
    if(info->ai_family == AF_INET) {
        return htons(((struct sockaddr_in*)info->ai_addr)->sin_port);
    }
    return htons(((struct sockaddr_in6*)info->ai_addr)->sin6_port);
}

int main(void) {
    // call getaddrinfo
    int listenfd, connfd, rv, yes = 1;
    struct addrinfo hints, *res, *p;

    char myip[INET_ADDRSTRLEN]; // bind to an IPv4 address

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if((rv = getaddrinfo(NULL, PORT, &hints, &res)) == -1) {
        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

    for(p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(listenfd == -1) {
            continue;
        }

        if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            close(listenfd);
            continue;
        }
        if(bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(listenfd);
            continue;
        }
        break;
    }

    if(p == NULL) {
        fprintf(stderr, "Could not assign a socket\n");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    if(listen(listenfd, BACKLOG_AMOUNT) == -1) {
        perror("listen");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }
    printf("Server is listening on %s port %d", inet_ntop(p->ai_family, get_in_addr(p->ai_addr), myip, INET_ADDRSTRLEN), get_port(p));

    struct sockaddr_storage clientaddr;
    socklen_t addrlen;
    char request[1024];
    char remoteIP[INET6_ADDRSTRLEN]; // ipv6 size can accomodate ipv4

    // establish poll mechanism
    int fd_count = 0;
    int fd_size = 5; // reallocate as necesary
    struct pollfd *pfds = malloc(sizeof *pfds * fd_size);
    if(pfds == NULL) {
        perror("could not allocate pfds");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    //add listener to set
    pfds[0].fd = listenfd;
    pfds[0].events = POLLIN; // ready to read incoming data

    fd_count = 1;

    //use fd sets for the websockets
    fd_set websocketfds;
    FD_ZERO(&websocketfds);

    // serve
    for( ; ; ) {
        int poll_count = poll(pfds, fd_count, -1);
        if(poll_count == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        for(int i = 0; i < fd_count; i++) {
            if(pfds[i].revents & POLLIN) {
                if(pfds[i].fd == listenfd) { // new connection ready for accept
                    addrlen = sizeof(clientaddr);
                    connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &addrlen);
                    if(connfd == -1) {
                        perror("accept");
                    } else {
                        add_to_pfds(&pfds, connfd, &fd_count, &fd_size);
                        printf("Server: new connection from %s on socket %d\n",
                                inet_ntop(clientaddr.ss_family,
                                    get_in_addr((struct sockaddr*)&clientaddr),
                                    remoteIP,
                                    INET6_ADDRSTRLEN),
                                connfd
                                );        
                    }
                } else { // be prepared to handle http or websocket 
                    // check if this socket is a websocket
                    if(FD_ISSET(pfds[i].fd, &websocketfds)) {
                        // handle websocket communication
                        size_t bytes_received;

                        bytes_received = recv(pfds[i].fd, request, 2, 0);
                        if (bytes_received <= 0) {
                            if (bytes_received == 0) {
                                printf("Client closed the connection\n");
                            } else {
                                perror("websockserv/recv2");
                            }
                            close(pfds[i].fd);
                            FD_CLR(pfds[i].fd, &websocketfds);
                            del_from_pfds(pfds, i, &fd_count);
                        } else {
                            // handle a websocket frame
                            unsigned char fin = request[0] & 0x80;
                            unsigned char opcode = request[0] & 0x0F;
                            unsigned char mask = request[1] & 0x80;
                            unsigned char payload_len = request[1] & 0x7F;

                            if (opcode == 0x8) { // Connection close frame
                                printf("Received close frame\n");
                                close(pfds[i].fd);
                                FD_CLR(pfds[i].fd, &websocketfds);
                                del_from_pfds(pfds, i, &fd_count);
                                continue;
                            }

                            // Read extended payload length if necessary
                            size_t total_payload_len = payload_len;
                            if (payload_len == 126) {
                                bytes_received = recv(pfds[i].fd, request + 2, 2, 0);
                                if (bytes_received <= 0) {
                                    perror("websockserv/recv3");
                                    close(pfds[i].fd);
                                    FD_CLR(pfds[i].fd, &websocketfds);
                                    del_from_pfds(pfds, i, &fd_count);
                                    break;
                                }
                                total_payload_len = ntohs(*(uint16_t *)(request + 2));
                            } else if (payload_len == 127) {
                                bytes_received = recv(pfds[i].fd, request + 2, 8, 0);
                                if (bytes_received <= 0) {
                                    perror("websockserv/recv4");
                                    close(pfds[i].fd);
                                    FD_CLR(pfds[i].fd, &websocketfds);
                                    del_from_pfds(pfds, i, &fd_count);
                                    break;
                                }
                                total_payload_len = ntohll(*(uint64_t *)(request + 2));
                            }

                            // Read the mask key if present
                            unsigned char mask_key[4];
                            if (mask) {
                                bytes_received = recv(pfds[i].fd, mask_key, 4, 0);
                                if (bytes_received <= 0) {
                                    perror("websockserv/recv5");
                                    close(pfds[i].fd);
                                    FD_CLR(pfds[i].fd, &websocketfds);
                                    del_from_pfds(pfds, i, &fd_count);
                                    break;
                                }
                            }

                            // Read the payload data
                            unsigned char *payload_data = (unsigned char *)malloc(total_payload_len + 1); // +1 for null terminator
                            size_t total_received = 0;
                            while (total_received < total_payload_len) {
                                bytes_received = recv(pfds[i].fd, payload_data + total_received, total_payload_len - total_received, 0);
                                if (bytes_received <= 0) {
                                    perror("websockserv/recv6");
                                    // free(payload_data);
                                    close(pfds[i].fd);
                                    FD_CLR(pfds[i].fd, &websocketfds);
                                    del_from_pfds(pfds, i, &fd_count);
                                    break;
                                }
                                total_received += bytes_received;
                            }
                            if(total_received < total_payload_len) { // recv returned 0  
                                free(payload_data);
                                continue;
                            }

                            // Null-terminate the payload data to treat it as a string
                            payload_data[total_payload_len] = '\0';

                            // Unmask the payload data if masked
                            if (mask) {
                                for (size_t j = 0; j < total_payload_len; ++j) {
                                    payload_data[j] ^= mask_key[j % 4];
                                }
                            }

                            // Print the received message
                            printf("Received message: %s\n", payload_data);

                            // Repackage the message into a WebSocket frame
                            size_t response_len = 2 + (total_payload_len < 126 ? 0 : (total_payload_len <= 65535 ? 2 : 8)) + total_payload_len;
                            unsigned char *response = (unsigned char *)malloc(response_len);

                            // Frame header
                            response[0] = 0x81; // FIN and opcode
                            if (total_payload_len < 126) {
                                response[1] = total_payload_len;
                                memcpy(response + 2, payload_data, total_payload_len);
                            } else if (total_payload_len <= 65535) {
                                response[1] = 126;
                                *(uint16_t *)(response + 2) = htons(total_payload_len);
                                memcpy(response + 4, payload_data, total_payload_len);
                            } else {
                                response[1] = 127;
                                *(uint64_t *)(response + 2) = htonll(total_payload_len);
                                memcpy(response + 10, payload_data, total_payload_len);
                            }

                            // Relay the message back to clients
                            for (int j = 0; j <= fd_count; j++) {
                                if (FD_ISSET(pfds[j].fd, &websocketfds) && j != i) { // don't echo message back to sender
                                    int sent = 0;
                                    while (sent < response_len) {
                                        sent += send(pfds[j].fd, response, response_len, 0);
                                    }
                                }
                            }

                            free(payload_data);
                            free(response);
                        }
                    } else {
                        // handle http requests
                        int nbytes;
                        if ((nbytes = recv(pfds[i].fd, request, sizeof(request), 0)) <= 0) {
                            if (nbytes == 0) {
                                printf("server: socket %d hung up\n", pfds[i].fd);
                            } else {
                                perror("recv");
                            }
                            close(pfds[i].fd);
                            del_from_pfds(pfds, i, &fd_count);
                        } else { // we got a good http request
                            request[nbytes] = '\0';
                            printf("Received HTTP request: %s\n", request);

                            // check for websocket upgrade
                            if(is_websocket_upgrade_request(request)) {
                                printf("^^^^^^^New websocket connection^^^^^^^^^^^\n");
                                char *key_start = strstr(request, "Sec-WebSocket-Key: ");
                                if (key_start) {
                                    key_start += 19;
                                    char *key_end = strstr(key_start, "\r\n");
                                    if (key_end) {
                                        *key_end = '\0';
                                        handle_websocket_handshake(pfds[i].fd, key_start);
                                        FD_SET(pfds[i].fd, &websocketfds);
                                    }
                                }
                            } else {
                                //handle a simple http request
                                char* html_content = read_html_file("index.html");
                                char* response = create_http_response(html_content);
                                if(send_http_response(pfds[i].fd, response) != 0) {
                                    perror("send http response");
                                }

                                free(html_content);
                                free(response);
                                close(pfds[i].fd);
                                del_from_pfds(pfds, i, &fd_count);

                            }
                        }
                    }
                }
            }
        }
    }


    // clean up with sighandler
    freeaddrinfo(res);
}
