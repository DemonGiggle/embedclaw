#include "test_runner.h"
#include "ec_io.h"
#include "ec_config.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

static int connect_client(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(EC_CONFIG_TELNET_PORT);

    for (int attempt = 0; attempt < 20; attempt++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return fd;
        }
        usleep(50000);
    }

    close(fd);
    return -1;
}

static int child_send_and_expect(const unsigned char *parts[],
                                 const size_t lengths[],
                                 size_t count,
                                 const char *expected_reply)
{
    int fd = connect_client();
    if (fd < 0) return 0;

    for (size_t i = 0; i < count; i++) {
        if (send(fd, parts[i], lengths[i], 0) != (ssize_t)lengths[i]) {
            close(fd);
            return 0;
        }
        usleep(30000);
    }

    char reply[64];
    ssize_t n = recv(fd, reply, sizeof(reply) - 1, 0);
    close(fd);
    if (n <= 0) return 0;
    reply[n] = '\0';
    return strstr(reply, expected_reply) != NULL;
}

static int fork_case(const unsigned char *parts[],
                     const size_t lengths[],
                     size_t count,
                     const char *expected_reply)
{
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        int ok = child_send_and_expect(parts, lengths, count, expected_reply);
        _exit(ok ? 0 : 1);
    }

    return pid;
}

static int wait_child_ok(pid_t pid)
{
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int test_telnet_line_fragmentation(void)
{
    const unsigned char *parts[] = {
        (const unsigned char *)"he",
        (const unsigned char *)"lp\n",
    };
    const size_t lengths[] = { 2, 3 };

    pid_t pid = fork_case(parts, lengths, 2, "ACK1\n");
    ASSERT(pid > 0, "client fork should succeed");

    ec_io_init(&ec_io_telnet_ops);

    char line[EC_CONFIG_IO_LINE_BUF];
    int n = ec_io_read_line(line, sizeof(line));
    int read_ok = (n == 4);
    int line_ok = (strcmp(line, "help") == 0);
    int write_ok = (ec_io_write("ACK1\n") == 0);
    int child_ok = wait_child_ok(pid);

    ASSERT(read_ok, "fragmented line should reassemble");
    ASSERT(line_ok, "line should equal help");
    ASSERT(write_ok, "reply should send");
    ASSERT(child_ok, "client should receive reply");
    return 1;
}

static int test_telnet_split_iac_sequence(void)
{
    static const unsigned char part0[] = { 0xFF };
    static const unsigned char part1[] = { 0xFB };
    static const unsigned char part2[] = { 0x03, 'o', 'k', '\n' };

    const unsigned char *parts[] = { part0, part1, part2 };
    const size_t lengths[] = { sizeof(part0), sizeof(part1), sizeof(part2) };

    pid_t pid = fork_case(parts, lengths, 3, "ACK2\n");
    ASSERT(pid > 0, "client fork should succeed");

    ec_io_init(&ec_io_telnet_ops);

    char line[EC_CONFIG_IO_LINE_BUF];
    int n = ec_io_read_line(line, sizeof(line));
    int read_ok = (n == 2);
    int line_ok = (strcmp(line, "ok") == 0);
    int write_ok = (ec_io_write("ACK2\n") == 0);
    int child_ok = wait_child_ok(pid);

    ASSERT(read_ok, "split IAC should be stripped before line return");
    ASSERT(line_ok, "line should equal ok");
    ASSERT(write_ok, "reply should send");
    ASSERT(child_ok, "client should receive reply");
    return 1;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    printf("=== EmbedClaw Telnet IO Tests ===\n\n");
    RUN_TEST(test_telnet_line_fragmentation);
    RUN_TEST(test_telnet_split_iac_sequence);
    PRINT_RESULTS();
    return (_tests_pass == _tests_run) ? 0 : 1;
}
