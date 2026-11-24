#include "logger.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static FILE *log_file;
int logger_init(void)
{
    if (mkdir("logs", 0700) < 0) {
        struct stat st;
        if (lstat("logs", &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid())
            return -1;
    }
    int fd = open("logs/server.log", O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink != 1 || fchmod(fd, 0600) < 0) {
        close(fd);
        return -1;
    }
    log_file = fdopen(fd, "a");
    if (!log_file) {
        close(fd);
        return -1;
    }
    return 0;
}
/* Main-thread only. Callers pass constant event names, never message text. */
void logger_event(const char *level, const char *event, unsigned long long id)
{
    if (!log_file)
        return;
    time_t now = time(NULL);
    struct tm local;
    char timestamp[32];
    if (!localtime_r(&now, &local) ||
        !strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local))
        return;
    fprintf(log_file, "[%s] %s %s connection=%llu\n", timestamp, level, event, id);
    fflush(log_file);
    /* Bound disk growth in the demonstration; older log entries are discarded. */
    long size = ftell(log_file);
    if (size > 1024 * 1024) {
        if (ftruncate(fileno(log_file), 0) == 0)
            (void)fseek(log_file, 0, SEEK_SET);
    }
}
void logger_close(void)
{
    if (log_file)
        fclose(log_file);
    log_file = NULL;
}
