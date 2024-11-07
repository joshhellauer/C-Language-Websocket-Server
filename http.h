#ifndef HTTP_H
#define HTTP_H
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#define HTTP_HEADER_TEMPLATE "HTTP/1.1 200 OK\r\n" \
                             "Content-Type: text/html\r\n" \
                             "Content-Length: %ld\r\n" \
                             "Joshs-Header: iloveyou\r\n" \
                             "\r\n"

char* read_html_file(char* filename);
char* create_http_response(char* html_content);
int send_http_response(int fd, char* html_content);

#endif
