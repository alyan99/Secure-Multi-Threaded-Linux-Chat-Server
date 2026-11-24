#ifndef CHAT_SECURITY_H
#define CHAT_SECURITY_H
#include <stddef.h>
typedef struct { double tokens, updated; } RateLimit;
double monotonic_now(void);
void rate_init(RateLimit *limit, double capacity, double now);
int rate_allow(RateLimit *limit, double capacity, double refill, double now);
int validate_name(const char *text, size_t capacity);
int validate_password(const char *password);
#endif
