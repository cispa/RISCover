#pragma once

#include <zlib.h>
#include <stddef.h>
#include <arpa/inet.h>

typedef struct {
    int      socket;
    z_stream comp_deflate_stream;
    uint8_t* comp_deflate_buffer;
    uint32_t comp_deflate_buffer_size;
    z_stream comp_inflate_stream;
    uint8_t* comp_inflate_buffer;
    uint32_t comp_inflate_buffer_size;
} connection_context;

void connect_with_retry(connection_context* connection, struct sockaddr_in* server_address);
void init_comp(connection_context* connection, uint32_t size_out, uint32_t size_in);

void recv_all(connection_context* connection, uint8_t* buffer, uint32_t n);
uint32_t recv_msg(connection_context* connection, uint8_t* buffer, uint32_t max);
uint32_t recv_msg_compressed(connection_context* connection, uint8_t* buffer, uint32_t max);
void recv_msg_n(connection_context* connection, uint8_t* buffer, uint32_t n);

void send_msg(connection_context* connection, uint8_t* buffer, uint32_t n);
void send_msg_compressed(connection_context* connection, uint8_t* buffer, uint32_t n);
void send_string(connection_context* connection, char* str);
