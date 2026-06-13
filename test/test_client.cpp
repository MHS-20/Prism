#include "prism.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
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
    test_pubsub(c);

    prism_close(c);

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    fprintf(stderr, "\n%d / %d tests passed\n", passed, tests);
    return passed == tests ? 0 : 1;
}
