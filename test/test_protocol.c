#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    Command command;
    assert(parse_command("/login alice strongPass1", &command) == 0);
    assert(command.type == CMD_LOGIN && strcmp(command.argument, "alice") == 0);
    assert(strcmp(command.text, "strongPass1") == 0);
    assert(parse_command("/msg bob two words", &command) == 0);
    assert(command.type == CMD_PRIVATE && strcmp(command.text, "two words") == 0);
    assert(parse_command("hello", &command) == 0 && command.type == CMD_MESSAGE);
    assert(parse_command("/users", &command) == 0 && command.type == CMD_USERS);
    const char *invalid[] = {"", "/missing", "/login", "/login a", "/join a b",
                             "/users ", "/msg  text", "bad\033text"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(parse_command(invalid[i], &command) < 0);
    char huge[BUFFER_SIZE + 1];
    memset(huge, 'x', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = 0;
    assert(parse_command(huge, &command) < 0);
    assert(!valid_wire_byte(0) && !valid_wire_byte(27) && valid_wire_byte('a'));
    puts("PASS protocol");
    return 0;
}
