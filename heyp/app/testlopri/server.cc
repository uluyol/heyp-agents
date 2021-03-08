#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "heyp/encoding/binary.h"

#define DEFAULT_BACKLOG 128

namespace heyp {
namespace {

constexpr bool kDebug = false;

uv_loop_t *loop;

void AllocBuf(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

void OnCloseConn(uv_handle_t *handle) { free(handle); }

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
  char bufdata[8];
} write_req_t;

void OnRpcAck(uv_write_t *req, int status) {
  if (status) {
    absl::FPrintF(stderr, "Write error %s\n", uv_strerror(status));
  }
  write_req_t *wr = (write_req_t *)req;
  free(wr);
}

typedef struct {
  uv_tcp_t client;
  uint32_t bytes_left;
  char header[12];
  int num_header_read;
} client_stream_t;

void OnRpcRead(uv_stream_t *client_stream, ssize_t nread, const uv_buf_t *buf) {
  client_stream_t *client = (client_stream_t *)client_stream;
  if (nread < 0) {
    if (nread != UV_EOF) absl::FPrintF(stderr, "Read error %s\n", uv_err_name(nread));
    uv_close((uv_handle_t *)client, OnCloseConn);
  }
  char *b = buf->base;
  char *e = buf->base + nread;
  while (b < e) {
    if (client->num_header_read < 12) {
      client->header[client->num_header_read++] = *b;
      ++b;
      if (client->num_header_read >= 12) {
        client->bytes_left = ReadU32LE(client->header);
        if (kDebug) {
          absl::FPrintF(stderr, "got rpc_id=%d size=%d header=%s\n",
                        ReadU64LE(client->header + 4), client->bytes_left,
                        ToHex(client->header, 12));
        }
      }
      continue;
    }
    if (client->bytes_left > e - b) {
      client->bytes_left -= e - b;
      b = e;
      if (kDebug) {
        absl::FPrintF(stderr, "still have %d bytes to read\n", client->bytes_left);
      }
      continue;
    }
    b += client->bytes_left;

    write_req_t *req = (write_req_t *)malloc(sizeof(write_req_t));
    memcpy(req->bufdata, client->header + 4, 8);
    if (kDebug) {
      absl::FPrintF(stderr, "ack rpc id=%d header=%s\n", ReadU64LE(req->bufdata),
                    ToHex(req->bufdata, 8));
    }
    req->buf = uv_buf_init(req->bufdata, 8);
    uv_write((uv_write_t *)req, (uv_stream_t *)&client->client, &req->buf, 1, OnRpcAck);

    client->num_header_read = 0;
  }
  free(buf->base);
}

void OnNewConnection(uv_stream_t *server, int status) {
  if (status < 0) {
    absl::FPrintF(stderr, "New connection error %s\n", uv_strerror(status));
    return;
  }

  absl::FPrintF(stderr, "Got new connection...\n");

  client_stream_t *client = (client_stream_t *)malloc(sizeof(client_stream_t));
  client->num_header_read = 0;
  uv_tcp_init(loop, &client->client);
  if (uv_accept(server, (uv_stream_t *)&client->client) == 0) {
    uv_read_start((uv_stream_t *)&client->client, AllocBuf, OnRpcRead);
  } else {
    uv_close((uv_handle_t *)client, OnCloseConn);
  }
}

}  // namespace
}  // namespace heyp

int main(int argc, char **argv) {
  heyp::loop = uv_default_loop();

  if (argc != 2) {
    absl::FPrintF(stderr, "usage: %s port\n", argv[0]);
    return 2;
  }

  int port;
  if (!absl::SimpleAtoi(argv[1], &port)) {
    absl::FPrintF(stderr, "failed to parse port\n");
  }

  struct sockaddr_in addr;
  uv_tcp_t server;
  uv_tcp_init(heyp::loop, &server);
  uv_ip4_addr("0.0.0.0", port, &addr);

  uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);

  absl::FPrintF(stderr, "Listening on port %d\n", port);

  int r = uv_listen((uv_stream_t *)&server, DEFAULT_BACKLOG, heyp::OnNewConnection);
  if (r) {
    absl::FPrintF(stderr, "Listen error %s\n", uv_strerror(r));
    return 1;
  }
  return uv_run(heyp::loop, UV_RUN_DEFAULT);
}
