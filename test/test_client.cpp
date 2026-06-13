#include "prism.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static int tests = 0;
static int passed = 0;

#define CHECK(cond, msg) do {                                   \
    tests++;                                                    \
    if (!(cond)) {                                              \
        fprintf(stderr, "FAIL  %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } else {                                                    \
        passed++;                                               \
        fprintf(stderr, "ok    %s\n", msg);                     \
    }                                                           \
} while (0)

static int wait_for_server(const char *host, uint16_t port, int timeout_ms) {
    int iter = 0;
    while (iter * 50 < timeout_ms) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(fd);
            return 0;
        }
        close(fd);
        usleep(50 * 1000);
        iter++;
    }
    return -1;
}

static void test_basic_kv(PrismConn *c) {
    PrismReply *r;

    r = prism_get(c, "nonexist");
    if (!r) { CHECK(0, "get reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "get non-existent returns nil");
    prism_reply_free(r);

    r = prism_set(c, "hello", "world");
    if (!r) { CHECK(0, "set reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "set returns nil");
    prism_reply_free(r);

    r = prism_get(c, "hello");
    if (!r) { CHECK(0, "get reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_STR, "get existing returns str");
    size_t len;
    const char *val = prism_str(r, &len);
    CHECK(len == 5 && memcmp(val, "world", 5) == 0, "get returns correct value");
    prism_reply_free(r);

    r = prism_set(c, "hello", "newworld");
    prism_reply_free(r);
    r = prism_get(c, "hello");
    val = prism_str(r, &len);
    CHECK(len == 8 && memcmp(val, "newworld", 8) == 0, "set overwrites existing");
    prism_reply_free(r);
}

static void test_del(PrismConn *c) {
    PrismReply *r;

    r = prism_del(c, "nonexist");
    if (!r) { CHECK(0, "del reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "del non-existent returns 0");
    prism_reply_free(r);

    r = prism_del(c, "hello");
    if (!r) { CHECK(0, "del reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "del existing returns 1");
    prism_reply_free(r);

    r = prism_get(c, "hello");
    if (!r) { CHECK(0, "get reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "get after del returns nil");
    prism_reply_free(r);
}

static void test_ttl(PrismConn *c) {
    PrismReply *r;

    r = prism_set(c, "ttlkey", "boom");
    prism_reply_free(r);

    r = prism_pttl(c, "ttlkey");
    if (!r) { CHECK(0, "pttl reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == -1, "pttl without expiry returns -1");
    prism_reply_free(r);

    r = prism_pexpire(c, "ttlkey", 60000);
    if (!r) { CHECK(0, "pexpire reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "pexpire on existing returns 1");
    prism_reply_free(r);

    r = prism_pttl(c, "ttlkey");
    if (!r) { CHECK(0, "pttl reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) > 0 && prism_int(r) <= 60000,
          "pttl after pexpire returns remaining ms");
    prism_reply_free(r);

    r = prism_del(c, "ttlkey");
    prism_reply_free(r);
}

static void test_keys(PrismConn *c) {
    PrismReply *r;

    r = prism_set(c, "k1", "v1");
    prism_reply_free(r);
    r = prism_get(c, "k1");
    prism_reply_free(r);
    r = prism_set(c, "k2", "v2");
    prism_reply_free(r);
    r = prism_get(c, "k2");
    prism_reply_free(r);

    r = prism_keys(c);
    if (!r) { CHECK(0, "keys reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_ARR, "keys returns arr");
    int found = 0;
    for (size_t i = 0; i < prism_arr_len(r); i++) {
        const char *s = prism_str(prism_arr_at(r, i), NULL);
        if (s && (strcmp(s, "k1") == 0 || strcmp(s, "k2") == 0)) found++;
    }
    CHECK(found == 2, "keys contains k1 and k2");
    prism_reply_free(r);
}

static void test_zset(PrismConn *c) {
    PrismReply *r;

    r = prism_zadd(c, "z", 3.5, "c");
    if (!r) { CHECK(0, "zadd reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "zadd new member returns 1");
    prism_reply_free(r);

    r = prism_zadd(c, "z", 1.0, "a");
    if (!r) { CHECK(0, "zadd reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "zadd second member returns 1");
    prism_reply_free(r);

    r = prism_zadd(c, "z", 2.0, "b");
    prism_reply_free(r);

    r = prism_zadd(c, "z", 3.5, "c");
    if (!r) { CHECK(0, "zadd reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "zadd existing member returns 0");
    prism_reply_free(r);

    r = prism_zscore(c, "z", "b");
    if (!r) { CHECK(0, "zscore reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_DBL && prism_dbl(r) == 2.0, "zscore returns correct score");
    prism_reply_free(r);

    r = prism_zscore(c, "z", "nonexist");
    if (!r) { CHECK(0, "zscore reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "zscore non-existent returns nil");
    prism_reply_free(r);

    r = prism_zrem(c, "z", "b");
    if (!r) { CHECK(0, "zrem reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "zrem existing returns 1");
    prism_reply_free(r);

    r = prism_zscore(c, "z", "b");
    if (!r) { CHECK(0, "zscore reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "zscore after zrem returns nil");
    prism_reply_free(r);

    r = prism_zrem(c, "z", "b");
    if (!r) { CHECK(0, "zrem reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "zrem non-existent returns 0");
    prism_reply_free(r);

    r = prism_zquery(c, "z", 0, "", 0, 10);
    if (!r) { CHECK(0, "zquery reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_ARR, "zquery returns arr");
    CHECK(prism_arr_len(r) == 4, "zquery returns 2 members (4 values)");
    if (prism_arr_len(r) >= 4) {
        const char *n0 = prism_str(prism_arr_at(r, 0), NULL);
        double s0 = prism_dbl(prism_arr_at(r, 1));
        const char *n1 = prism_str(prism_arr_at(r, 2), NULL);
        double s1 = prism_dbl(prism_arr_at(r, 3));
        CHECK(n0 && strcmp(n0, "a") == 0 && s0 == 1.0, "zquery first is a/1.0");
        CHECK(n1 && strcmp(n1, "c") == 0 && s1 == 3.5, "zquery second is c/3.5");
    }
    prism_reply_free(r);
}

static void test_list(PrismConn *c) {
    PrismReply *r;

    r = prism_cmd(c, 2, "llen", "mylist");
    if (!r) { CHECK(0, "llen reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "llen empty list");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "lpush", "mylist", "a");
    if (!r) { CHECK(0, "lpush reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "lpush returns 1");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "lpush", "mylist", "b");
    if (!r) { CHECK(0, "lpush reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 2, "lpush returns 2");
    prism_reply_free(r);

    r = prism_cmd(c, 4, "lpush", "mylist", "c", "d");
    if (!r) { CHECK(0, "lpush multi"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 4, "lpush multi returns 4");
    prism_reply_free(r);

    // list should be: d c b a (head to tail)
    r = prism_cmd(c, 4, "lrange", "mylist", "0", "-1");
    if (!r) { CHECK(0, "lrange reply"); return; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 4, "lrange all returns 4");
    if (prism_arr_len(r) >= 4) {
        const char *s0 = prism_str(prism_arr_at(r, 0), NULL);
        const char *s1 = prism_str(prism_arr_at(r, 1), NULL);
        const char *s2 = prism_str(prism_arr_at(r, 2), NULL);
        const char *s3 = prism_str(prism_arr_at(r, 3), NULL);
        CHECK(s0 && strcmp(s0, "d") == 0, "lrange[0]=d");
        CHECK(s1 && strcmp(s1, "c") == 0, "lrange[1]=c");
        CHECK(s2 && strcmp(s2, "b") == 0, "lrange[2]=b");
        CHECK(s3 && strcmp(s3, "a") == 0, "lrange[3]=a");
    }
    prism_reply_free(r);

    // lrange partial
    r = prism_cmd(c, 4, "lrange", "mylist", "1", "2");
    if (!r) { CHECK(0, "lrange partial"); return; }
    CHECK(prism_arr_len(r) == 2, "lrange 1-2 returns 2");
    const char *s1 = prism_str(prism_arr_at(r, 0), NULL);
    const char *s2 = prism_str(prism_arr_at(r, 1), NULL);
    CHECK(s1 && strcmp(s1, "c") == 0, "lrange[1]=c");
    CHECK(s2 && strcmp(s2, "b") == 0, "lrange[2]=b");
    prism_reply_free(r);

    // lpop
    r = prism_cmd(c, 2, "lpop", "mylist");
    if (!r) { CHECK(0, "lpop reply"); return; }
    CHECK(prism_type(r) == PRISM_STR, "lpop returns str");
    const char *popped = prism_str(r, NULL);
    CHECK(popped && strcmp(popped, "d") == 0, "lpop returns d");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "llen", "mylist");
    CHECK(prism_int(r) == 3, "llen after pop is 3");
    prism_reply_free(r);

    // pop empty
    r = prism_cmd(c, 2, "lpop", "nonexist");
    CHECK(prism_type(r) == PRISM_NIL, "lpop non-existent returns nil");
    prism_reply_free(r);
}

static void test_hash(PrismConn *c) {
    PrismReply *r;

    r = prism_cmd(c, 4, "hset", "myhash", "name", "alice");
    if (!r) { CHECK(0, "hset reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "hset new returns 1");
    prism_reply_free(r);

    r = prism_cmd(c, 4, "hset", "myhash", "age", "30");
    if (!r) { CHECK(0, "hset reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "hset second returns 1");
    prism_reply_free(r);

    r = prism_cmd(c, 4, "hset", "myhash", "name", "bob");
    if (!r) { CHECK(0, "hset update"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "hset update returns 0");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "hget", "myhash", "name");
    if (!r) { CHECK(0, "hget reply"); return; }
    CHECK(prism_type(r) == PRISM_STR, "hget returns str");
    const char *name = prism_str(r, NULL);
    CHECK(name && strcmp(name, "bob") == 0, "hget name is bob");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "hget", "myhash", "nonexist");
    CHECK(prism_type(r) == PRISM_NIL, "hget non-existent returns nil");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "hdel", "myhash", "age");
    if (!r) { CHECK(0, "hdel reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 1, "hdel existing returns 1");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "hdel", "myhash", "age");
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "hdel non-existent returns 0");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "hgetall", "myhash");
    if (!r) { CHECK(0, "hgetall reply"); return; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 2, "hgetall returns 2 values");
    if (prism_arr_len(r) >= 2) {
        const char *f = prism_str(prism_arr_at(r, 0), NULL);
        const char *v = prism_str(prism_arr_at(r, 1), NULL);
        CHECK(f && strcmp(f, "name") == 0, "hgetall field is name");
        CHECK(v && strcmp(v, "bob") == 0, "hgetall val is bob");
    }
    prism_reply_free(r);
}

static void test_bitmap(PrismConn *c) {
    PrismReply *r;

    // getbit of non-existent
    r = prism_cmd(c, 3, "getbit", "mybits", "0");
    CHECK(prism_int(r) == 0, "getbit non-existent returns 0");
    prism_reply_free(r);

    // setbit
    r = prism_cmd(c, 4, "setbit", "mybits", "7", "1");
    if (!r) { CHECK(0, "setbit reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) == 0, "setbit new returns old 0");
    prism_reply_free(r);

    // getbit
    r = prism_cmd(c, 3, "getbit", "mybits", "7");
    CHECK(prism_int(r) == 1, "getbit returns 1");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "getbit", "mybits", "0");
    CHECK(prism_int(r) == 0, "getbit other bit returns 0");
    prism_reply_free(r);

    // setbit back to 0
    r = prism_cmd(c, 4, "setbit", "mybits", "7", "0");
    CHECK(prism_int(r) == 1, "setbit existing returns old 1");
    prism_reply_free(r);

    r = prism_cmd(c, 3, "getbit", "mybits", "7");
    CHECK(prism_int(r) == 0, "getbit after clear returns 0");
    prism_reply_free(r);

    // set multiple bits and check bitcount
    r = prism_cmd(c, 4, "setbit", "mybits", "0", "1");
    prism_reply_free(r);
    r = prism_cmd(c, 4, "setbit", "mybits", "1", "1");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "bitcount", "mybits");
    if (!r) { CHECK(0, "bitcount reply"); return; }
    CHECK(prism_int(r) == 2, "bitcount returns 2");
    prism_reply_free(r);

    // bitcount with range
    r = prism_cmd(c, 3, "bitcount", "mybits", "0");
    if (!r) { CHECK(0, "bitcount range"); return; }
    CHECK(prism_int(r) == 2, "bitcount byte 0 returns 2");
    prism_reply_free(r);
}

static void test_misc(PrismConn *c) {
    PrismReply *r;

    // EXISTS
    r = prism_cmd(c, 2, "exists", "nonexist");
    if (!r) { CHECK(0, "exists reply"); return; }
    CHECK(prism_int(r) == 0, "exists non-existent returns 0");
    prism_reply_free(r);

    r = prism_set(c, "exkey", "val");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "exists", "exkey");
    CHECK(prism_int(r) == 1, "exists existing returns 1");
    prism_reply_free(r);

    // TYPE
    r = prism_cmd(c, 2, "type", "nonexist");
    if (!r) { CHECK(0, "type reply"); return; }
    const char *t = prism_str(r, NULL);
    CHECK(t && strcmp(t, "none") == 0, "type non-existent is none");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "type", "exkey");
    t = prism_str(r, NULL);
    CHECK(t && strcmp(t, "string") == 0, "type string is string");
    prism_reply_free(r);

    // STRLEN
    r = prism_cmd(c, 2, "strlen", "nonexist");
    CHECK(prism_int(r) == 0, "strlen non-existent returns 0");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "strlen", "exkey");
    CHECK(prism_int(r) == 3, "strlen 'val' returns 3");
    prism_reply_free(r);

    // RENAME
    r = prism_cmd(c, 3, "rename", "exkey", "newkey");
    if (!r) { CHECK(0, "rename reply"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "rename returns nil");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "exists", "exkey");
    CHECK(prism_int(r) == 0, "old key gone after rename");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "exists", "newkey");
    CHECK(prism_int(r) == 1, "new key exists after rename");
    prism_reply_free(r);

    r = prism_get(c, "newkey");
    CHECK(prism_type(r) == PRISM_STR, "renamed key has value");
    prism_reply_free(r);

    // SCAN
    r = prism_cmd(c, 2, "scan", "0");
    if (!r) { CHECK(0, "scan reply"); return; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) >= 1, "scan returns array");
    int64_t next_cursor = prism_int(prism_arr_at(r, 0));
    CHECK(next_cursor == 0, "scan complete (next cursor 0)");
    prism_reply_free(r);

    // DEBUG
    r = prism_cmd(c, 1, "debug");
    if (!r) { CHECK(0, "debug reply"); return; }
    CHECK(prism_type(r) == PRISM_ARR, "debug returns array");
    prism_reply_free(r);

    // OBJECT
    r = prism_cmd(c, 2, "object", "newkey");
    if (!r) { CHECK(0, "object reply"); return; }
    CHECK(prism_type(r) == PRISM_ARR, "object returns array");
    prism_reply_free(r);

    r = prism_cmd(c, 2, "object", "nonexist");
    CHECK(prism_type(r) == PRISM_NIL, "object non-existent returns nil");
    prism_reply_free(r);
}

static void test_pubsub(PrismConn *c) {
    PrismConn *sub = prism_connect("127.0.0.1", 1234);
    if (!sub) { CHECK(0, "sub connect"); return; }

    PrismReply *r;
    const char *s;
    int64_t n;
    size_t len;

    r = prism_subscribe(sub, "news");
    if (!r) { CHECK(0, "subscribe reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 3, "subscribe ack is arr(3)");
    s = prism_str(prism_arr_at(r, 0), NULL);
    CHECK(s && strcmp(s, "subscribe") == 0, "subscribe ack type");
    s = prism_str(prism_arr_at(r, 1), NULL);
    CHECK(s && strcmp(s, "news") == 0, "subscribe ack channel");
    n = prism_int(prism_arr_at(r, 2));
    CHECK(n == 1, "subscribe ack count == 1");
    prism_reply_free(r);
    r = prism_subscribe(sub, "news");
    if (!r) { CHECK(0, "subscribe again reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 3, "subscribe again ack is arr(3)");
    CHECK(prism_int(prism_arr_at(r, 2)) == 1, "subscribe again count still 1");
    prism_reply_free(r);

    // subscribe to a second channel
    r = prism_subscribe(sub, "sports");
    prism_reply_free(r);

    // unsubscribe
    r = prism_unsubscribe(sub, "news");
    if (!r) { CHECK(0, "unsubscribe reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 3, "unsubscribe ack is arr(3)");
    s = prism_str(prism_arr_at(r, 0), NULL);
    CHECK(s && strcmp(s, "unsubscribe") == 0, "unsubscribe ack type");
    s = prism_str(prism_arr_at(r, 1), NULL);
    CHECK(s && strcmp(s, "news") == 0, "unsubscribe ack channel");
    n = prism_int(prism_arr_at(r, 2));
    CHECK(n == 1, "unsubscribe ack count == 1");
    prism_reply_free(r);

    // subscribe back to news
    r = prism_subscribe(sub, "news");
    prism_reply_free(r);

    // publish from another connection, then read push on subscriber
    r = prism_publish(c, "news", "hello subscribers");
    if (!r) { CHECK(0, "publish reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) >= 1, "publish returns subscriber count >= 1");
    prism_reply_free(r);

    // subscriber reads the push message
    r = prism_read_next(sub);
    if (!r) { CHECK(0, "read_next reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_ARR && prism_arr_len(r) == 3, "push message is arr(3)");
    s = prism_str(prism_arr_at(r, 0), NULL);
    CHECK(s && strcmp(s, "message") == 0, "push message type");
    s = prism_str(prism_arr_at(r, 1), NULL);
    CHECK(s && strcmp(s, "news") == 0, "push message channel");
    s = prism_str(prism_arr_at(r, 2), &len);
    CHECK(s && len == 17 && memcmp(s, "hello subscribers", 17) == 0, "push message body");
    prism_reply_free(r);

    // subscribing client rejects regular commands
    r = prism_get(sub, "shouldfail");
    if (!r) { CHECK(0, "get reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_ERR, "get in pub/sub mode returns error");
    prism_reply_free(r);

    // unsubscribe all
    r = prism_unsubscribe(sub, "sports");
    if (!r) { CHECK(0, "unsubscribe sports reply non-null"); goto cleanup; }
    prism_reply_free(r);
    r = prism_unsubscribe(sub, "news");
    if (!r) { CHECK(0, "unsubscribe news reply non-null"); goto cleanup; }
    prism_reply_free(r);

    // after all unsubscribes, regular commands work again
    r = prism_set(sub, "after_unsub", "ok");
    if (!r) { CHECK(0, "set after unsubscribe reply non-null"); goto cleanup; }
    CHECK(prism_type(r) == PRISM_NIL, "set works after unsubscribing all");
    prism_reply_free(r);

cleanup:
    prism_close(sub);
}

static void test_persistence(PrismConn *c) {
    PrismReply *r;

    r = prism_set(c, "pk", "pv");
    prism_reply_free(r);

    // SAVE
    r = prism_cmd(c, 1, "save");
    if (!r) { CHECK(0, "save reply non-null"); return; }
    CHECK(prism_type(r) == PRISM_NIL, "save returns nil");
    prism_reply_free(r);

    // check file
    FILE *f = fopen("prism.rdb", "r");
    CHECK(f != NULL, "prism.rdb exists after save");
    if (f) fclose(f);

    // data still readable
    r = prism_get(c, "pk");
    if (!r) { CHECK(0, "get after save"); return; }
    CHECK(prism_type(r) == PRISM_STR, "key alive after save");
    prism_reply_free(r);

    // BGSAVE
    r = prism_cmd(c, 1, "bgsave");
    if (!r) { CHECK(0, "bgsave reply"); return; }
    CHECK(prism_type(r) == PRISM_INT && prism_int(r) > 0, "bgsave returns pid");
    prism_reply_free(r);

    // server still works after bgsave fork
    r = prism_get(c, "pk");
    if (!r) { CHECK(0, "get after bgsave"); return; }
    CHECK(prism_type(r) == PRISM_STR, "key alive after bgsave");
    prism_reply_free(r);

    // AOF file exists after writes
    f = fopen("prism.aof", "r");
    CHECK(f != NULL, "prism.aof exists after writes");
    if (f) fclose(f);

    remove("prism.rdb");
    remove("prism.aof");
}

int main(int argc, char **argv) {
    const char *server_path = "./build/prism-server";
    if (argc > 1) {
        server_path = argv[1];
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child: start server
        execlp(server_path, server_path, (char *)NULL);
        // if exec fails, try cwd
        execl(server_path, server_path, (char *)NULL);
        fprintf(stderr, "failed to exec %s: %s\n", server_path, strerror(errno));
        _exit(1);
    }
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    // clean up persistence files from previous runs
    remove("prism.aof");
    remove("prism.rdb");

    fprintf(stderr, "--- prism test ---\n");
    fprintf(stderr, "server PID %d, waiting for startup...\n", (int)pid);

    if (wait_for_server("127.0.0.1", 1234, 3000) < 0) {
        fprintf(stderr, "server did not start in time\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 1;
    }
    fprintf(stderr, "server ready.\n\n");

    PrismConn *c = prism_connect("127.0.0.1", 1234);
    if (!c) {
        fprintf(stderr, "failed to connect\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 1;
    }

    test_basic_kv(c);
    test_del(c);
    test_ttl(c);
    test_keys(c);
    test_zset(c);
    test_list(c);
    test_hash(c);
    test_bitmap(c);
    test_misc(c);
    test_pubsub(c);
    test_persistence(c);

    prism_close(c);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    fprintf(stderr, "\n%d / %d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
