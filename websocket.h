#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>



#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


uint64_t ntohll(uint64_t value);
uint64_t htonll(uint64_t value);
void sha1_base64(const char* input, char* output);

void handle_websocket_handshake(int client_fd, const char* sec_websocket_key);

int is_websocket_upgrade_request(char* request);


#endif
