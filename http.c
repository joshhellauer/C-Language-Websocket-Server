#include "http.h"


// Function to create the HTTP response
char* create_http_response(char *html_content) {
    size_t content_length = strlen(html_content);
    size_t header_length = snprintf(NULL, 0, HTTP_HEADER_TEMPLATE, content_length);
    char *response = (char *)malloc(header_length + content_length + 1);

    if (response == NULL) {
        perror("Error allocating memory");
        return NULL;
    }

    snprintf(response, header_length + 1, HTTP_HEADER_TEMPLATE, content_length);
    strcat(response, html_content);

    return response;
}


/*
	Returns a buffer containing the text representation
	of a given html file
*/
char* read_html_file(char* filename) {
	FILE* file = fopen(filename, "r");

	if(file == NULL) {
		fprintf(stderr, "could not find html file: %s\n", filename);
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long file_length = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* ret = malloc(file_length + 1);
	if(ret == NULL) {
		perror("malloc failed");
		return NULL;
	}

	fread(ret, 1, file_length, file);
	ret[file_length + 1] = '\0';

	return ret;
}

// Function to send the HTTP response using the send() method
int send_http_response(int sockfd, char *response) {
    size_t length = strlen(response);
    ssize_t sent = 0;
    ssize_t total_sent = 0;

    while (total_sent < length) {
        sent = send(sockfd, response + total_sent, length - total_sent, 0);
        if (sent == -1) {
            perror("Error sending data");
            return -1;
        }
        total_sent += sent;
    }

    return 0;
}
