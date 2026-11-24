#include "chat.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    Chat chat;
    chat_init(&chat);
    assert(chat_find(&chat, "general") == 0);
    assert(chat_create(&chat, "study") == 1);
    assert(chat_find(&chat, "study") == 1);
    assert(chat_create(&chat, "study") < 0);
    assert(chat_create(&chat, "bad/name") < 0);
    for (int i = 2; i < MAX_ROOMS; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "room%d", i);
        assert(chat_create(&chat, name) == i);
    }
    assert(chat_create(&chat, "overflow") < 0);
    puts("PASS chat");
    return 0;
}
