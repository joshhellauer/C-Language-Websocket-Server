
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include "websocket.h"

#define PORT "8000"
#define BUFFER_SIZE 1024

typedef struct Node {
    int fd;
    struct Node* next;
} Node;

static Node* root;

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

void print_client_list() {
    Node* curr = root;
    int i = 0;
    while(curr != NULL) {
        printf("Client No. %d: socket %d\n", i, curr->fd);
        i++;
        curr = curr->next;
    }
}

void insert_Node(Node* new) {
    if(root == NULL) {
        root = new;
        root->next = NULL;
        return;
    }
    Node* curr = root;
    while(curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = new;
}

void add_to_list(int fd) {
    Node* new = malloc(sizeof(Node));
    if(new == NULL) {
        perror("websockserv/malloc");
        return;
    }
    new->fd = fd;
    new->next = NULL;
    insert_Node(new);
}

void remove_from_list(int oldfd) {
    struct Node* curr;
    struct Node* prev = NULL;

    for (curr = root; curr != NULL && curr->fd != oldfd; prev = curr, curr = curr->next);

    if (curr != NULL) {
        if (prev != NULL) {
            prev->next = curr->next;
        } else {
            root = curr->next;
        }
        free(curr);
    }
    print_client_list();
}

int is_active(int fd) {
    Node* curr = root;
    while(curr != NULL && curr->fd != fd) {
        curr = curr->next;
    }
    if(curr != NULL) {
        return 1;
    }
    return 0;
}

void sigpipe_handler(int signum) {
    printf("SIGPIPE caught\n");
}

int main(void) {
    int sockfd, listenfd, rv, yes = 1;
    struct addrinfo hints, *res, *p, l;
    char myip[INET_ADDRSTRLEN];

    signal(SIGPIPE, sigpipe_handler);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "server: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd < 0) {
            continue;
        }

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) < 0) {
            close(listenfd);
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "could not assign a socket\n");
        exit(2);
    }
    memcpy(&l, p, sizeof(struct addrinfo));
    freeaddrinfo(res);

    if (listen(listenfd, 10) == -1) {
        perror("websockserv/listen");
        exit(1);
    }
    printf("WebSocket server is listening on %s port %d\n", inet_ntop(l.ai_family, get_in_addr(l.ai_addr), myip, INET_ADDRSTRLEN), get_port(&l));

    struct sockaddr_storage theiraddr;
    socklen_t addrlen;

    char buf[BUFFER_SIZE];
    char remoteIP[INET6_ADDRSTRLEN];

    fd_set readfds;
    fd_set master;
    FD_ZERO(&master);
    FD_ZERO(&readfds);
    FD_SET(listenfd, &master);
    int fd_max = listenfd + 1;

    for (;;) {
        readfds = master;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) == -1) {
            perror("websockserv/select");
            exit(4);
        }

        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &readfds)) {
                if (i == listenfd) {
                    addrlen = sizeof(theiraddr);
                    sockfd = accept(listenfd, (struct sockaddr*)&theiraddr, &addrlen);
                    if (sockfd == -1) {
                        perror("websockserv/accept");
                    } else {
                        FD_SET(sockfd, &master);
                        if (sockfd > fd_max) {
                            fd_max = sockfd;
                        }
                        printf("server: new connection from %s on socket %d\n",
                               inet_ntop(theiraddr.ss_family, get_in_addr((struct sockaddr*)&theiraddr), remoteIP, INET6_ADDRSTRLEN), sockfd);
                    }
                } else if (!is_active(i)) {
                    int nbytes;
                    if ((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
                        if (nbytes == 0) {
                            printf("server: socket %d hung up\n", i);
                        } else {
                            perror("websockserv/recv1");
                        }
                        close(i);
                        FD_CLR(i, &master);
                    } else {
                        // Handle new connection (WebSocket handshake)
                        char *key_start = strstr(buf, "Sec-WebSocket-Key: ");
                        if (key_start) {
                            key_start += 19;
                            char *key_end = strstr(key_start, "\r\n");
                            if (key_end) {
                                *key_end = '\0';
                                handle_websocket_handshake(i, key_start);
                                add_to_list(i);
                                FD_SET(i, &master);
                                if (i > fd_max) {
                                    fd_max = i;
                                }
                            }
                        }
                    }
                } else {
                    // Handle WebSocket data frame
                    /*
                    * 0                   1                   2                   3
                    * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
                    *+-+-+-+-+-------+-+-------------+-------------------------------+
                    *|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
                    *|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
                    *|N|V|V|V|       |S|             |   (if payload len==126/127)   |
                    *| |1|2|3|       |K|             |                               |
                    *+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
                    *|     Extended payload length continued, if payload len == 127  |
                    *+ - - - - - - - - - - - - - - - +-------------------------------+
                    *|                               |Masking-key, if MASK set to 1  |
                    *+-------------------------------+-------------------------------+
                    *| Masking-key (continued)       |          Payload Data         |
                    *+-------------------------------- - - - - - - - - - - - - - - - +
                    *:                     Payload Data continued ...                :
                    *+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
                    *|                     Payload Data continued ...                |
                    *+---------------------------------------------------------------+
                    */
                    size_t bytes_received;

                    bytes_received = recv(i, buf, 2, 0);
                    if (bytes_received <= 0) {
                        if (bytes_received == 0) {
                            printf("Client closed the connection\n");
                        } else {
                            perror("websockserv/recv2");
                        }
                        remove_from_list(i);
                        close(i);
                        FD_CLR(i, &master);
                    } else {
                        unsigned char fin = buf[0] & 0x80;
                        unsigned char opcode = buf[0] & 0x0F;
                        unsigned char mask = buf[1] & 0x80;
                        unsigned char payload_len = buf[1] & 0x7F;

                        if (opcode == 0x8) { // Connection close frame
                            printf("Received close frame\n");
                            close(i);
                            remove_from_list(i);
                            FD_CLR(i, &master);
                        }

                        // Read extended payload length if necessary
                        size_t total_payload_len = payload_len;
                        if (payload_len == 126) {
                            bytes_received = recv(i, buf + 2, 2, 0);
                            if (bytes_received <= 0) {
                                perror("websockserv/recv3");
                                close(i);
                                remove_from_list(i); // i is inactive
                                FD_CLR(i, &master);
                                break;
                            }
                            total_payload_len = ntohs(*(uint16_t *)(buf + 2));
                        } else if (payload_len == 127) {
                            bytes_received = recv(i, buf + 2, 8, 0);
                            if (bytes_received <= 0) {
                                perror("websockserv/recv4");
                                close(i);
                                remove_from_list(i);
                                FD_CLR(i, &master);
                                break;
                            }
                            total_payload_len = ntohll(*(uint64_t *)(buf + 2));
                        }

                        // Read the mask key if present
                        unsigned char mask_key[4];
                        if (mask) {
                            bytes_received = recv(i, mask_key, 4, 0);
                            if (bytes_received <= 0) {
                                perror("websockserv/recv5");
                                close(i);
                                remove_from_list(i);
                                FD_CLR(i, &master);
                                break;
                            }
                        }

                        // Read the payload data
                        unsigned char *payload_data = (unsigned char *)malloc(total_payload_len + 1); // +1 for null terminator
                        size_t total_received = 0;
                        while (total_received < total_payload_len) {
                            bytes_received = recv(i, payload_data + total_received, total_payload_len - total_received, 0);
                            if (bytes_received <= 0) {
                                perror("websockserv/recv6");
                                // free(payload_data);
                                close(i);
                                remove_from_list(i);
                                FD_CLR(i, &master);
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
                        for (int j = 0; j <= fd_max; j++) {
                            if (FD_ISSET(j, &master) && j != i && is_active(j)) { // don't echo message back to sender
                                int sent = 0;
                                while (sent < response_len) {
                                    sent += send(j, response, response_len, 0);
                                }
                            }
                        }

                        free(payload_data);
                        free(response);
                    }
                }
            }
        }
    }
    return 0;
}
