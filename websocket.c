/**
 * WebSocket functionality
 */
#include "websocket.h"

uint64_t ntohll(uint64_t value) {
    return (((uint64_t)ntohl((uint32_t)value)) << 32) | ntohl((uint32_t)(value >> 32));
}

uint64_t htonll(uint64_t value) {
    static const int num = 42;
    // Check the endianness of the system
    if (*(const char*)&num == num) {
        // Little endian: convert
        return ((uint64_t)htonl((uint32_t)value) << 32) | htonl((uint32_t)(value >> 32));
    } else {
        // Big endian: no conversion needed
        return value;
    }
}

void sha1_base64(const char* input, char* output) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input, strlen(input), hash);

    // Base64 encoding
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_write(b64, hash, SHA_DIGEST_LENGTH);
    BIO_flush(b64);
    BUF_MEM *buffer_ptr;
    BIO_get_mem_ptr(b64, &buffer_ptr);
    BIO_set_close(b64, BIO_NOCLOSE);
    memcpy(output, buffer_ptr->data, buffer_ptr->length - 1);
    output[buffer_ptr->length - 1] = '\0';
    BIO_free_all(b64);
}

void handle_websocket_handshake(int client_fd, const char* sec_websocket_key) {
    char accept_key[256];
    char concat_key[256];
    snprintf(concat_key, sizeof(concat_key), "%s%s", sec_websocket_key, WS_GUID);
    sha1_base64(concat_key, accept_key);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);

    send(client_fd, response, strlen(response), 0);
}

// Function to check for WebSocket headers in an HTTP request
int is_websocket_upgrade_request( char *request) {
    int ret = 0;
    // Look for the "Connection: Upgrade" header
    if (strstr(request, "Connection: Upgrade") != NULL ||
        strstr(request, "connection: upgrade") != NULL) {
        ret = 1; // a WebSocket upgrade request
    }

    // Look for the "Upgrade: websocket" header
    if (strstr(request, "Upgrade: websocket") != NULL &&
        strstr(request, "upgrade: websocket") != NULL) {
        ret =  1; // a WebSocket upgrade request
    }

    // Look for the "Sec-WebSocket-Key" header
    if (strstr(request, "Sec-WebSocket-Key:") != NULL) {
        ret = 1; // a WebSocket upgrade request
    }

    // Optionally, check for "Sec-WebSocket-Version: 13"
    if (strstr(request, "Sec-WebSocket-Version: 13") != NULL) {
        ret = 1; // a valid WebSocket request
    }

    // All required headers found, so this is likely a WebSocket handshake request
    return ret;
}

