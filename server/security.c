#include "security.h"
#include "config.h"
#include <string.h>
#include <time.h>

double monotonic_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}
void rate_init(RateLimit *limit, double capacity, double now)
{
    limit->tokens = capacity;
    limit->updated = now;
}
int rate_allow(RateLimit *limit, double capacity, double refill, double now)
{
    double elapsed = now - limit->updated;
    if (elapsed > 0) {
        limit->tokens += elapsed * refill;
        if (limit->tokens > capacity)
            limit->tokens = capacity;
        limit->updated = now;
    }
    if (limit->tokens < 1)
        return 0;
    limit->tokens -= 1;
    return 1;
}
int validate_name(const char *text, size_t capacity)
{
    size_t length = strnlen(text, capacity);
    if (!length || length >= capacity)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}
int validate_password(const char *password)
{
    size_t length = strnlen(password, MAX_PASSWORD + 1);
    if (length < 8 || length > MAX_PASSWORD)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)password[i] < 33 || (unsigned char)password[i] > 126)
            return 0;
    return 1;
}
