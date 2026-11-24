#ifndef CHAT_LOGGER_H
#define CHAT_LOGGER_H
int logger_init(void);
void logger_event(const char *level, const char *event, unsigned long long id);
void logger_close(void);
#endif
