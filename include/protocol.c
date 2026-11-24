#include "protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int valid_wire_byte(unsigned char byte)
{
    return byte >= 32 && byte <= 126;
}

int parse_command(const char *line, Command *command)
{
    static const struct { const char *name; CommandType type; int arguments; } commands[] = {
        {"/register", CMD_REGISTER, 2}, {"/login", CMD_LOGIN, 2},
        {"/logout", CMD_LOGOUT, 0}, {"/msg", CMD_PRIVATE, 2},
        {"/create", CMD_CREATE, 1}, {"/join", CMD_JOIN, 1},
        {"/leave", CMD_LEAVE, 0}, {"/rooms", CMD_ROOMS, 0},
        {"/users", CMD_USERS, 0}, {"/help", CMD_HELP, 0}, {"/quit", CMD_QUIT, 0}
    };
    memset(command, 0, sizeof(*command));
    command->type = CMD_INVALID;
    size_t length = strnlen(line, BUFFER_SIZE);
    if (length == 0 || length >= BUFFER_SIZE)
        return -1;
    for (size_t i = 0; i < length; ++i)
        if (!valid_wire_byte((unsigned char)line[i]))
            return -1;
    if (line[0] != '/') {
        command->type = CMD_MESSAGE;
        memcpy(command->text, line, length + 1);
        return 0;
    }
    const char *space = strchr(line, ' ');
    size_t name_length = space ? (size_t)(space - line) : length;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strlen(commands[i].name) != name_length ||
            memcmp(commands[i].name, line, name_length) != 0)
            continue;
        const char *arg = space ? space + 1 : "";
        if (commands[i].arguments == 0) {
            if (*arg || space)
                return -1;
        } else {
            const char *next = strchr(arg, ' ');
            size_t n = next ? (size_t)(next - arg) : strlen(arg);
            if (n == 0 || n >= sizeof(command->argument))
                return -1;
            memcpy(command->argument, arg, n);
            if (commands[i].arguments == 1) {
                if (next)
                    return -1;
            } else {
                if (!next || !next[1])
                    return -1;
                memcpy(command->text, next + 1, strlen(next + 1) + 1);
            }
        }
        command->type = commands[i].type;
        return 0;
    }
    return -1;
}

int parse_endpoint(int argc, char **argv, struct sockaddr_in *address)
{
    const char *ip = SERVER_IP;
    long port = SERVER_PORT;
    if (argc != 1 && argc != 3)
        return -1;
    if (argc == 3) {
        char *end;
        ip = argv[1];
        if (argv[2][0] < '0' || argv[2][0] > '9')
            return -1;
        errno = 0;
        port = strtol(argv[2], &end, 10);
        if (errno || *end || port < 1 || port > 65535)
            return -1;
    }
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons((unsigned short)port);
    return inet_pton(AF_INET, ip, &address->sin_addr) == 1 ? 0 : -1;
}
