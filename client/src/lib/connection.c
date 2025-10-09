#include "connection.h"

#include "util.h"
#include "log.h"
#include "zlib.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

void connect_with_retry(connection_context* connection, struct sockaddr_in* server_address) {
    int connected = 0;
    for (int i=0; i<30; i++) {
        if (connect(connection->socket, (struct sockaddr*)server_address, sizeof(*server_address)) == -1) {
            log_warning("Connection failed. Retrying in 1s...");
            sleep(1);
        } else {
            connected = 1;
            break;
        }
    }
    if (!connected) {
        log_perror("connect");
        exit(EXIT_FAILURE);
    }
}

void init_comp(connection_context* connection, uint32_t size_send, uint32_t size_recv) {
    connection->comp_deflate_buffer_size = size_send;
    connection->comp_deflate_buffer = malloc(size_send);
    if (!connection->comp_deflate_buffer) {
        log_perror("malloc");
        exit(EXIT_FAILURE);
    }
    connection->comp_deflate_stream.zalloc = Z_NULL;
    connection->comp_deflate_stream.zfree = Z_NULL;
    connection->comp_deflate_stream.opaque = Z_NULL;
    // windowBits=15+16 for gzip wrapper.
    if (deflateInit2(&connection->comp_deflate_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        log_error("deflateInit2 failed");
        exit(EXIT_FAILURE);
    }

    connection->comp_inflate_buffer_size = size_recv;
    connection->comp_inflate_buffer = malloc(size_recv);
    if (!connection->comp_inflate_buffer) {
        log_perror("malloc");
        exit(EXIT_FAILURE);
    }
    connection->comp_inflate_stream.zalloc = Z_NULL;
    connection->comp_inflate_stream.zfree = Z_NULL;
    connection->comp_inflate_stream.opaque = Z_NULL;
    // windowBits=15+16 for gzip wrapper.
    if (inflateInit2(&connection->comp_inflate_stream, 15 + 16) != Z_OK) {
        log_error("inflateInit2 failed");
        exit(EXIT_FAILURE);
    }
}

void recv_all(connection_context* connection, uint8_t* buffer, uint32_t n) {
    uint64_t received = 0;
    while (received < n) {
        int res = recv(connection->socket, &buffer[received], n-received, 0);
        if (res == -1) {
            log_error("Error in recv: %s", strerror(errno));
            exit(EXIT_FAILURE);
        } else if (res == 0) {
            log_error("Connection closed by the peer");
            exit(EXIT_FAILURE);
        }

        received += res;
    }
}

uint32_t recv_msg(connection_context* connection, uint8_t* buffer, uint32_t max) {
    uint32_t n;
    recv_all(connection, (uint8_t*)&n, sizeof(n));
    if (n > max) {
        log_error("recv_msg: n (%u) > max (%u)", n, max);
        exit(EXIT_FAILURE);
    }
    recv_all(connection, buffer, n);
    return n;
}

uint32_t recv_msg_compressed(connection_context* connection, uint8_t* out_buffer, uint32_t max_out) {
    assert(connection->comp_inflate_buffer != NULL);

    // Receive compressed message
    uint8_t* compressed = connection->comp_inflate_buffer;
    uint32_t comp_size = recv_msg(connection, compressed, connection->comp_inflate_buffer_size);

    // Inflate data
    connection->comp_inflate_stream.next_in = compressed;
    connection->comp_inflate_stream.avail_in = comp_size;
    connection->comp_inflate_stream.next_out = out_buffer;
    connection->comp_inflate_stream.avail_out = max_out;

    int ret = inflate(&connection->comp_inflate_stream, Z_SYNC_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        log_error("inflate failed with code %d", ret);
        exit(EXIT_FAILURE);
    }

    uint32_t decompressed_size = max_out - connection->comp_inflate_stream.avail_out;
    return decompressed_size;
}

void recv_msg_n(connection_context* connection, uint8_t* buffer, uint32_t n) {
    uint32_t received = recv_msg(connection, buffer, n);
    if (received != n) {
        log_error("Received not enough data. Expected %u got %u.", n, received);
        exit(EXIT_FAILURE);
    }
}

void send_msg(connection_context* connection, uint8_t* buffer, uint32_t n) {
    if (send(connection->socket, &n, sizeof(n), 0) == -1) {
        log_perror("send size");
        exit(EXIT_FAILURE);
    }

    if (send(connection->socket, buffer, n, 0) == -1) {
        log_perror("send payload");
        exit(EXIT_FAILURE);
    }
}

void send_msg_compressed(connection_context* connection, uint8_t* buffer, uint32_t n) {
    assert(connection->comp_deflate_buffer != NULL);

    // Deflate data
    connection->comp_deflate_stream.next_in = buffer;
    connection->comp_deflate_stream.avail_in = n;
    connection->comp_deflate_stream.next_out = connection->comp_deflate_buffer;
    connection->comp_deflate_stream.avail_out = connection->comp_deflate_buffer_size;
    int ret = deflate(&connection->comp_deflate_stream, Z_SYNC_FLUSH);
    if (ret != Z_OK) {
        log_error("deflate failed with code %d", ret);
        exit(EXIT_FAILURE);
    }

    uint32_t comp_size = connection->comp_deflate_buffer_size - connection->comp_deflate_stream.avail_out;
    send_msg(connection, connection->comp_deflate_buffer, comp_size);
}

void send_string(connection_context* connection, char* str) {
    send_msg(connection, (uint8_t*)str, strlen(str));
}
