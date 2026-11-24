#include "chat.h"
#include "security.h"
#include <string.h>
void chat_init(Chat *chat)
{
    memset(chat, 0, sizeof(*chat));
    memcpy(chat->names[0], "general", sizeof("general"));
}
int chat_find(const Chat *chat, const char *name)
{
    for (int i = 0; i < MAX_ROOMS; ++i)
        if (chat->names[i][0] && strcmp(chat->names[i], name) == 0)
            return i;
    return -1;
}
int chat_create(Chat *chat, const char *name)
{
    if (!validate_name(name, MAX_ROOM_NAME) || chat_find(chat, name) >= 0)
        return -1;
    for (int i = 1; i < MAX_ROOMS; ++i) {
        if (!chat->names[i][0]) {
            memcpy(chat->names[i], name, strlen(name) + 1);
            return i;
        }
    }
    return -1;
}
