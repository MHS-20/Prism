#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRISM_NIL = 0,
    PRISM_ERR = 1,
    PRISM_STR = 2,
    PRISM_INT = 3,
    PRISM_DBL = 4,
    PRISM_ARR = 5,
} PrismType;

typedef struct PrismConn PrismConn;
typedef struct PrismReply PrismReply;

// Connection
PrismConn *prism_connect(const char *host, uint16_t port);
void prism_close(PrismConn *conn);

// Request helpers
PrismReply *prism_cmd(PrismConn *conn, int nargs, ...);
PrismReply *prism_cmdv(PrismConn *conn, const char **args, int nargs);

// Reply inspection
PrismType prism_type(PrismReply *reply);
const char *prism_err_msg(PrismReply *reply, uint32_t *code);
const char *prism_str(PrismReply *reply, size_t *len);
int64_t prism_int(PrismReply *reply);
double prism_dbl(PrismReply *reply);
size_t prism_arr_len(PrismReply *reply);
PrismReply *prism_arr_at(PrismReply *reply, size_t idx);
void prism_reply_free(PrismReply *reply);

// Convenience commands (return parsed reply)
PrismReply *prism_get(PrismConn *conn, const char *key);
PrismReply *prism_set(PrismConn *conn, const char *key, const char *val);
PrismReply *prism_del(PrismConn *conn, const char *key);
PrismReply *prism_pexpire(PrismConn *conn, const char *key, int64_t ttl_ms);
PrismReply *prism_pttl(PrismConn *conn, const char *key);
PrismReply *prism_keys(PrismConn *conn);
PrismReply *prism_zadd(PrismConn *conn, const char *zset, double score, const char *name);
PrismReply *prism_zrem(PrismConn *conn, const char *zset, const char *name);
PrismReply *prism_zscore(PrismConn *conn, const char *zset, const char *name);
PrismReply *prism_zquery(PrismConn *conn, const char *zset, double score, const char *name, int64_t offset, int64_t limit);

#ifdef __cplusplus
}
#endif
