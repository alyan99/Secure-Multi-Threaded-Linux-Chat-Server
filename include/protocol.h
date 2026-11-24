#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H
#include "config.h"
#include <netinet/in.h>
#include <stddef.h>

typedef enum {
    CMD_REGISTER, CMD_LOGIN, CMD_LOGOUT, CMD_MESSAGE, CMD_PRIVATE,
    CMD_CREATE, CMD_JOIN, CMD_LEAVE, CMD_ROOMS, CMD_USERS,
    CMD_HELP, CMD_QUIT, CMD_INVALID
} CommandType;

typedef struct {
    CommandType type;
    char argument[MAX_USERNAME];
    char text[BUFFER_SIZE];
} Command;

int parse_command(const char *line, Command *command);
int valid_wire_byte(unsigned char byte);
int parse_endpoint(int argc, char **argv, struct sockaddr_in *address);
#endif
