#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define DEFAULT_BACKLOG 128

int32_t read_le_int32(char *b) {
  return (b[3] << 24) | (b[2] << 16) | (b[1] << 8) | b[0];
}

int64_t read_le_int64(char *b) {
  int64_t lo = read_le_int32(b);
  int64_t hi = read_le_int32(b + 4);
  return (hi << 32) | lo;
}

void write_le_int32(int32_t v, char *b) {
  b[0] = v & 0xff;
  b[1] = (v >> 8) & 0xff;
  b[2] = (v >> 16) & 0xff;
  b[3] = (v >> 24) & 0xff;
}

void write_le_int64(int64_t v, char *b) {
  write_le_int32(v, b);
  write_le_int32(v >> 32, b);
}

uv_loop_t *loop;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

void on_close(uv_handle_t *handle) { free(handle); }

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
  char bufdata[8];
} write_req_t;

void free_write_req(uv_write_t *req) {
  write_req_t *wr = (write_req_t *)req;
  free(wr);
}

void rpc_ack_cb(uv_write_t *req, int status) {
  if (status) {
    fprintf(stderr, "Write error %s\n", uv_strerror(status));
  }
  free_write_req(req);
}

typedef struct {
  uv_tcp_t client;
  int bytes_left;
  char header[16];
  int num_header_read;
} client_stream_t;

void rpc_read(uv_stream_t *client_stream, ssize_t nread, const uv_buf_t *buf) {
  client_stream_t *client = (client_stream_t *)client_stream;
  if (nread < 0) {
    if (nread != UV_EOF) fprintf(stderr, "Read error %s\n", uv_err_name(nread));
    uv_close((uv_handle_t *)client, on_close);
  }
  while (nread > 0) {
    char *b = buf->base;
    if (client->num_header_read < 16) {
      --nread;
      client->header[client->num_header_read++] = *b;
      ++b;
      if (client->num_header_read == 16) {
        client->bytes_left = read_le_int32(client->header);
      }
    }
    if (client->bytes_left > nread) {
      client->bytes_left -= nread;
      nread = 0;
      continue;
    }
    nread -= client->bytes_left;

    write_req_t *req = (write_req_t *)malloc(sizeof(write_req_t));
    memcpy(req->bufdata, client->header + 4, 8);
    req->buf = uv_buf_init(req->bufdata, 8);
    uv_write((uv_write_t *)req, client, &req->buf, 1, rpc_ack_cb);

    client->num_header_read = 0;
  }
  free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    fprintf(stderr, "New connection error %s\n", uv_strerror(status));
    return;
  }

  fprintf(stderr, "Got new connection...\n");

  client_stream_t *client = (client_stream_t *)malloc(sizeof(client_stream_t));
  client->num_header_read = 0;
  uv_tcp_init(loop, &client->client);
  if (uv_accept(server, (uv_stream_t *)&client->client) == 0) {
    uv_read_start((uv_stream_t *)&client->client, alloc_buffer, rpc_read);
  } else {
    uv_close((uv_handle_t *)client, on_close);
  }
}

int main(int argc, char **argv) {
  loop = uv_default_loop();

  if (argc != 2) {
    fprintf(stderr, "usage: %s port\n", argv[0]);
    return 2;
  }

  int port = atoi(argv[1]);

  struct sockaddr_in addr;
  uv_tcp_t server;
  uv_tcp_init(loop, &server);
  uv_ip4_addr("0.0.0.0", port, &addr);

  uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);

  fprintf(stderr, "Listening on port %d\n", port);

  int r = uv_listen((uv_stream_t *)&server, DEFAULT_BACKLOG, on_new_connection);
  if (r) {
    fprintf(stderr, "Listen error %s\n", uv_strerror(r));
    return 1;
  }
  return uv_run(loop, UV_RUN_DEFAULT);
}
