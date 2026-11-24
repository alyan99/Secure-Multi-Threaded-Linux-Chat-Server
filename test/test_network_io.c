/* Exercise the real server buffering functions with a nonblocking socketpair. */
#define main chat_server_main
#include "../server/server.c"
#undef main
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(set_nonblocking(pair[0]) == 0);
    assert(set_nonblocking(pair[1]) == 0);
    int small = 1024;
    assert(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
    Client client = {.fd = pair[0]};
    char expected[OUTPUT_BUFFER_SIZE + 1];
    memset(expected, 'x', OUTPUT_BUFFER_SIZE);
    expected[OUTPUT_BUFFER_SIZE] = 0;
    queue_message(&client, expected);
    assert(client.output_length == OUTPUT_BUFFER_SIZE);
    size_t previous;
    int blocked = 0;
    for (int i = 0; i < 20 && client.output_length; ++i) {
        previous = client.output_length;
        flush_output(&client);
        assert(!client.drop);
        if (previous == client.output_length) { blocked = 1; break; }
    }
    assert(blocked && client.output_length > 0);
    char received[OUTPUT_BUFFER_SIZE];
    size_t used = 0;
    double deadline = monotonic_now() + 5;
    while (used < sizeof(received)) {
        ssize_t count = recv(pair[1], received + used, sizeof(received) - used, 0);
        if (count > 0)
            used += (size_t)count;
        else
            assert(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        flush_output(&client);
        assert(!client.drop && monotonic_now() < deadline);
    }
    assert(!memcmp(expected, received, sizeof(received)));
    assert(client.output_length == 0);
    queue_message(&client, expected);
    queue_message(&client, "overflow");
    assert(client.drop); /* A slow recipient is removed without blocking peers. */
    close(pair[0]);
    close(pair[1]);
    puts("PASS partial sends, EAGAIN retention, output order, slow-buffer overflow");
    return 0;
}
