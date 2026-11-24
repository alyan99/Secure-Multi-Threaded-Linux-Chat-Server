#ifndef CHAT_ROOMS_H
#define CHAT_ROOMS_H
#include "config.h"
typedef struct { char names[MAX_ROOMS][MAX_ROOM_NAME]; } Chat;
void chat_init(Chat *chat);
int chat_find(const Chat *chat, const char *name);
int chat_create(Chat *chat, const char *name);
#endif
