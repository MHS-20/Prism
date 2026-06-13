#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

static int g_fails = 0;

static int connect_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const uint8_t *data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static void drain_response(int fd) {
    struct pollfd pfd = {fd, POLLIN, 0};
    uint8_t buf[4096];
    while (poll(&pfd, 1, 5) > 0) { // 5ms timeout per read
        if (read(fd, buf, sizeof(buf)) <= 0) break;
    }
}

// ---- fuzz payloads ----

static void fuzz_garbage(int fd) {
    size_t len = (size_t)(rand() % 256);
    uint8_t buf[256];
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(rand() & 0xFF);
    send_all(fd, buf, len);
}

static void fuzz_bad_msg_len(int fd) {
    uint32_t msglen = 0xFFFFFFFF;
    send_all(fd, (uint8_t *)&msglen, 4);
    uint8_t body[16];
    for (int i = 0; i < 16; i++) body[i] = (uint8_t)(rand() & 0xFF);
    send_all(fd, body, 16);
}

static void fuzz_bad_nargs(int fd) {
    uint32_t msglen = 12;
    send_all(fd, (uint8_t *)&msglen, 4);
    uint32_t nargs = 200001;
    send_all(fd, (uint8_t *)&nargs, 4);
    uint32_t arglen = 4;
    send_all(fd, (uint8_t *)&arglen, 4);
    send_all(fd, (uint8_t *)"test", 4);
}

static void fuzz_zero_msg(int fd) {
    uint32_t msglen = 0;
    send_all(fd, (uint8_t *)&msglen, 4);
}

static void fuzz_trailing_garbage(int fd) {
    uint32_t msglen = 4 + 4 + 3 + 5;
    send_all(fd, (uint8_t *)&msglen, 4);
    uint32_t nargs = 1;
    send_all(fd, (uint8_t *)&nargs, 4);
    uint32_t arglen = 3;
    send_all(fd, (uint8_t *)&arglen, 4);
    send_all(fd, (uint8_t *)"get", 3);
    send_all(fd, (uint8_t *)"extra", 5);
}

static void fuzz_partial_frame(int fd) {
    // send only msg_len header, no body
    uint32_t msglen = 100;
    send_all(fd, (uint8_t *)&msglen, 4);
    // don't send body - server should handle partial data gracefully
}

static void fuzz_valid_frame(int fd, int type) {
    uint8_t buf[256];
    uint8_t *p = buf + 4;
    const char *args[8];
    size_t alen[8];
    int nargs;

    switch (type % 8) {
    case 0: nargs = 1;  args[0]="keys";   alen[0]=4; break;
    case 1: nargs = 2;  args[0]="get";    alen[0]=3; args[1]="fk"; alen[1]=2; break;
    case 2: nargs = 3;  args[0]="set";    alen[0]=3; args[1]="fk"; alen[1]=2; args[2]="fv"; alen[2]=2; break;
    case 3: nargs = 2;  args[0]="exists"; alen[0]=6; args[1]="fk"; alen[1]=2; break;
    case 4: nargs = 2;  args[0]="type";   alen[0]=4; args[1]="fk"; alen[1]=2; break;
    case 5: nargs = 2;  args[0]="del";    alen[0]=3; args[1]="fk"; alen[1]=2; break;
    case 6: nargs = 4;  args[0]="zadd";   alen[0]=4; args[1]="fz"; alen[1]=2; args[2]="1.5"; alen[2]=3; args[3]="fa"; alen[3]=2; break;
    case 7: nargs = 2;  args[0]="strlen"; alen[0]=6; args[1]="fk"; alen[1]=2; break;
    }

    uint32_t net_nargs = (uint32_t)nargs;
    memcpy(p, &net_nargs, 4); p += 4;
    for (int i = 0; i < nargs; i++) {
        uint32_t net_len = (uint32_t)alen[i];
        memcpy(p, &net_len, 4); p += 4;
        memcpy(p, args[i], alen[i]); p += alen[i];
    }

    uint32_t body_len = (uint32_t)(p - buf - 4);
    memcpy(buf, &body_len, 4);
    send_all(fd, buf, body_len + 4);
}

int main(int argc, char **argv) {
    const char *server_path = "./build/prism-server";
    if (argc > 1) server_path = argv[1];

    int iterations = 2000;
    if (argc > 2) iterations = atoi(argv[2]);

    fprintf(stderr, "--- fuzz test ---\n");
    srand((unsigned)time(NULL));

    pid_t pid = fork();
    if (pid == 0) {
        // redirect server stderr to /dev/null to suppress verbose logs
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) dup2(nullfd, 2);
        execlp(server_path, server_path, (char *)NULL);
        execl(server_path, server_path, (char *)NULL);
        fprintf(stderr, "failed to exec %s\n", server_path);
        _exit(1);
    }
    if (pid < 0) { perror("fork"); return 1; }

    // wait for server
    int fd = -1;
    for (int attempt = 0; attempt < 200; attempt++) {
        fd = connect_server();
        if (fd >= 0) break;
        usleep(50000);
    }
    if (fd < 0) {
        fprintf(stderr, "server did not start\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 1;
    }
    close(fd);
    fprintf(stderr, "server ready, fuzzing %d iterations...\n", iterations);

    // suppress server output by redirecting stderr
    // actually just let it be, but keep iterations manageable

    for (int i = 0; i < iterations; i++) {
        fd = connect_server();
        if (fd < 0) {
            fprintf(stderr, "failed to connect at iteration %d\n", i);
            g_fails++;
            break;
        }

        int choice = rand() % 9;
        switch (choice) {
        case 0: fuzz_garbage(fd);         break;
        case 1: fuzz_bad_msg_len(fd);     break;
        case 2: fuzz_bad_nargs(fd);       break;
        case 3: fuzz_zero_msg(fd);        break;
        case 4: fuzz_trailing_garbage(fd);break;
        case 5: fuzz_partial_frame(fd);   break;
        default: fuzz_valid_frame(fd, rand() % 8); break;
        }

        drain_response(fd);
        close(fd);
    }

    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    int passed = iterations - g_fails;
    fprintf(stderr, "%d / %d fuzz iterations passed\n", passed, iterations);
    return g_fails > 0 ? 1 : 0;
}
