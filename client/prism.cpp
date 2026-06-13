#include "prism.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// ---- protocol constants ----

static const size_t k_max_msg = 32 << 20;
static const size_t k_max_args = 200 * 1000;

// ---- wire helpers ----

static void buf_put_u8(uint8_t **p, uint8_t v) {
    *(*p)++ = v;
}
static void buf_put_u32(uint8_t **p, uint32_t v) {
    memcpy(*p, &v, 4); *p += 4;
}
static void buf_put_i64(uint8_t **p, int64_t v) {
    memcpy(*p, &v, 8); *p += 8;
}
static void buf_put_dbl(uint8_t **p, double v) {
    memcpy(*p, &v, 8); *p += 8;
}
static void buf_put_data(uint8_t **p, const void *d, size_t n) {
    memcpy(*p, d, n); *p += n;
}

static bool buf_get_u8(const uint8_t **p, const uint8_t *end, uint8_t *v) {
    if (*p + 1 > end) return false;
    *v = *(*p)++;
    return true;
}
static bool buf_get_u32(const uint8_t **p, const uint8_t *end, uint32_t *v) {
    if (*p + 4 > end) return false;
    memcpy(v, *p, 4); *p += 4;
    return true;
}
static bool buf_get_i64(const uint8_t **p, const uint8_t *end, int64_t *v) {
    if (*p + 8 > end) return false;
    memcpy(v, *p, 8); *p += 8;
    return true;
}
static bool buf_get_dbl(const uint8_t **p, const uint8_t *end, double *v) {
    if (*p + 8 > end) return false;
    memcpy(v, *p, 8); *p += 8;
    return true;
}

// ---- connection ----

struct PrismConn {
    int fd;
};

PrismConn *prism_connect(const char *host, uint16_t port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rv = getaddrinfo(host, port_str, &hints, &res);
    if (rv != 0) return NULL;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return NULL;

    PrismConn *conn = (PrismConn *)malloc(sizeof(PrismConn));
    conn->fd = fd;
    return conn;
}

void prism_close(PrismConn *conn) {
    if (conn) {
        close(conn->fd);
        free(conn);
    }
}

// ---- reply ----

struct PrismReply {
    PrismType tag;
    union {
        struct { uint32_t code; char *msg; size_t msg_len; } err;
        struct { char *data; size_t len; } str;
        int64_t i;
        double d;
        struct { PrismReply *items; size_t n; } arr;
    } u;
};

static PrismReply *reply_new_nil(void) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_NIL;
    return r;
}

static PrismReply *reply_new_err(uint32_t code, const char *msg, size_t len) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_ERR;
    r->u.err.code = code;
    r->u.err.msg = (char *)malloc(len + 1);
    memcpy(r->u.err.msg, msg, len);
    r->u.err.msg[len] = 0;
    r->u.err.msg_len = len;
    return r;
}

static PrismReply *reply_new_str(const char *data, size_t len) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_STR;
    r->u.str.data = (char *)malloc(len + 1);
    memcpy(r->u.str.data, data, len);
    r->u.str.data[len] = 0;
    r->u.str.len = len;
    return r;
}

static PrismReply *reply_new_int(int64_t v) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_INT;
    r->u.i = v;
    return r;
}

static PrismReply *reply_new_dbl(double v) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_DBL;
    r->u.d = v;
    return r;
}

static PrismReply *reply_new_arr(size_t n) {
    PrismReply *r = (PrismReply *)calloc(1, sizeof(PrismReply));
    r->tag = PRISM_ARR;
    if (n > 0) {
        r->u.arr.items = (PrismReply *)calloc(n, sizeof(PrismReply));
    }
    r->u.arr.n = n;
    return r;
}

// forward declaration
static bool parse_value(const uint8_t **p, const uint8_t *end, PrismReply *r);

static bool parse_reply_body(const uint8_t *data, size_t size, PrismReply *r) {
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    return parse_value(&p, end, r);
}

static bool parse_value(const uint8_t **p, const uint8_t *end, PrismReply *r) {
    uint8_t tag = 0;
    if (!buf_get_u8(p, end, &tag)) return false;

    switch (tag) {
    case 0: // nil
        r->tag = PRISM_NIL;
        return true;
    case 1: { // err
        uint32_t code = 0;
        uint32_t len = 0;
        if (!buf_get_u32(p, end, &code)) return false;
        if (!buf_get_u32(p, end, &len)) return false;
        if (*p + len > end) return false;
        r->tag = PRISM_ERR;
        r->u.err.code = code;
        r->u.err.msg = (char *)malloc(len + 1);
        memcpy(r->u.err.msg, *p, len);
        r->u.err.msg[len] = 0;
        r->u.err.msg_len = len;
        *p += len;
        return true;
    }
    case 2: { // str
        uint32_t len = 0;
        if (!buf_get_u32(p, end, &len)) return false;
        if (*p + len > end) return false;
        r->tag = PRISM_STR;
        r->u.str.data = (char *)malloc(len + 1);
        memcpy(r->u.str.data, *p, len);
        r->u.str.data[len] = 0;
        r->u.str.len = len;
        *p += len;
        return true;
    }
    case 3: { // int
        int64_t v = 0;
        if (!buf_get_i64(p, end, &v)) return false;
        r->tag = PRISM_INT;
        r->u.i = v;
        return true;
    }
    case 4: { // dbl
        double v = 0;
        if (!buf_get_dbl(p, end, &v)) return false;
        r->tag = PRISM_DBL;
        r->u.d = v;
        return true;
    }
    case 5: { // arr
        uint32_t n = 0;
        if (!buf_get_u32(p, end, &n)) return false;
        r->tag = PRISM_ARR;
        r->u.arr.n = n;
        r->u.arr.items = (PrismReply *)calloc(n, sizeof(PrismReply));
        for (uint32_t i = 0; i < n; i++) {
            if (!parse_value(p, end, &r->u.arr.items[i])) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

// ---- send / recv ----

static bool send_all(int fd, const uint8_t *data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

static bool recv_all(int fd, uint8_t *data, size_t len) {
    while (len > 0) {
        ssize_t n = read(fd, data, len);
        if (n <= 0) return false;
        data += n;
        len -= (size_t)n;
    }
    return true;
}

PrismReply *prism_cmdv(PrismConn *conn, const char **args, int nargs) {
    if (nargs < 0 || (size_t)nargs > k_max_args) return NULL;

    // build request body: nargs(u32) + for each arg: len(u32) + data
    size_t body_len = 4;
    for (int i = 0; i < nargs; i++) {
        body_len += 4 + strlen(args[i]);
    }
    if (body_len > k_max_msg) return NULL;

    uint8_t *body = (uint8_t *)malloc(body_len);
    uint8_t *p = body;
    buf_put_u32(&p, (uint32_t)nargs);
    for (int i = 0; i < nargs; i++) {
        size_t slen = strlen(args[i]);
        buf_put_u32(&p, (uint32_t)slen);
        buf_put_data(&p, args[i], slen);
    }

    // frame: msg_len(u32) + body
    uint32_t net_len = (uint32_t)body_len;
    uint8_t header[4];
    memcpy(header, &net_len, 4);

    if (!send_all(conn->fd, header, 4) || !send_all(conn->fd, body, body_len)) {
        free(body);
        return NULL;
    }
    free(body);

    // read response frame
    uint8_t rbuf[4];
    if (!recv_all(conn->fd, rbuf, 4)) return NULL;
    uint32_t resp_len = 0;
    memcpy(&resp_len, rbuf, 4);
    if (resp_len > k_max_msg) return NULL;

    uint8_t *resp = (uint8_t *)malloc(resp_len);
    if (!recv_all(conn->fd, resp, resp_len)) {
        free(resp);
        return NULL;
    }

    PrismReply *reply = (PrismReply *)calloc(1, sizeof(PrismReply));
    int ok = parse_reply_body(resp, resp_len, reply) ? 1 : 0;
    free(resp);
    if (!ok) {
        prism_reply_free(reply);
        return NULL;
    }
    return reply;
}

PrismReply *prism_cmd(PrismConn *conn, int nargs, ...) {
    va_list ap;
    va_start(ap, nargs);
    const char **args = (const char **)malloc(sizeof(char *) * (size_t)nargs);
    for (int i = 0; i < nargs; i++) {
        args[i] = va_arg(ap, const char *);
    }
    va_end(ap);
    PrismReply *r = prism_cmdv(conn, args, nargs);
    free(args);
    return r;
}

// ---- reply accessors ----

PrismType prism_type(PrismReply *reply) {
    return reply->tag;
}

const char *prism_err_msg(PrismReply *reply, uint32_t *code) {
    if (reply->tag != PRISM_ERR) return NULL;
    if (code) *code = reply->u.err.code;
    return reply->u.err.msg;
}

const char *prism_str(PrismReply *reply, size_t *len) {
    if (reply->tag != PRISM_STR) return NULL;
    if (len) *len = reply->u.str.len;
    return reply->u.str.data;
}

int64_t prism_int(PrismReply *reply) {
    if (reply->tag != PRISM_INT) return 0;
    return reply->u.i;
}

double prism_dbl(PrismReply *reply) {
    if (reply->tag != PRISM_DBL) return 0.0;
    return reply->u.d;
}

size_t prism_arr_len(PrismReply *reply) {
    if (reply->tag != PRISM_ARR) return 0;
    return reply->u.arr.n;
}

PrismReply *prism_arr_at(PrismReply *reply, size_t idx) {
    if (reply->tag != PRISM_ARR || idx >= reply->u.arr.n) return NULL;
    return &reply->u.arr.items[idx];
}

void prism_reply_free(PrismReply *reply) {
    if (!reply) return;
    if (reply->tag == PRISM_ERR) {
        free(reply->u.err.msg);
    } else if (reply->tag == PRISM_STR) {
        free(reply->u.str.data);
    } else if (reply->tag == PRISM_ARR) {
        for (size_t i = 0; i < reply->u.arr.n; i++) {
            prism_reply_free(&reply->u.arr.items[i]);
        }
        free(reply->u.arr.items);
    }
    free(reply);
}

// ---- convenience commands ----

PrismReply *prism_get(PrismConn *conn, const char *key) {
    return prism_cmd(conn, 2, "get", key);
}
PrismReply *prism_set(PrismConn *conn, const char *key, const char *val) {
    return prism_cmd(conn, 3, "set", key, val);
}
PrismReply *prism_del(PrismConn *conn, const char *key) {
    return prism_cmd(conn, 2, "del", key);
}
PrismReply *prism_pexpire(PrismConn *conn, const char *key, int64_t ttl_ms) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)ttl_ms);
    return prism_cmd(conn, 3, "pexpire", key, buf);
}
PrismReply *prism_pttl(PrismConn *conn, const char *key) {
    return prism_cmd(conn, 2, "pttl", key);
}
PrismReply *prism_keys(PrismConn *conn) {
    return prism_cmd(conn, 1, "keys");
}
PrismReply *prism_zadd(PrismConn *conn, const char *zset, double score, const char *name) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", score);
    return prism_cmd(conn, 4, "zadd", zset, buf, name);
}
PrismReply *prism_zrem(PrismConn *conn, const char *zset, const char *name) {
    return prism_cmd(conn, 3, "zrem", zset, name);
}
PrismReply *prism_zscore(PrismConn *conn, const char *zset, const char *name) {
    return prism_cmd(conn, 3, "zscore", zset, name);
}
PrismReply *prism_zquery(PrismConn *conn, const char *zset, double score, const char *name, int64_t offset, int64_t limit) {
    char sbuf[64], obuf[32], lbuf[32];
    snprintf(sbuf, sizeof(sbuf), "%.17g", score);
    snprintf(obuf, sizeof(obuf), "%lld", (long long)offset);
    snprintf(lbuf, sizeof(lbuf), "%lld", (long long)limit);
    return prism_cmd(conn, 6, "zquery", zset, sbuf, name, obuf, lbuf);
}
