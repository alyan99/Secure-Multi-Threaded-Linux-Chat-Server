#include "config.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    char output[OUTPUT_BUFFER_SIZE], line[BUFFER_SIZE];
    char password_prefix[MAX_USERNAME + 16];
    size_t output_start, output_length, line_length;
    int password_mode, terminal_changed, input_eof, write_closed, discarding;
    struct termios saved;
} Terminal;
static volatile sig_atomic_t stopping;
static void handle_signal(int number) { (void)number; stopping = 1; }
static void clear_memory(void *memory, size_t length)
{
    volatile unsigned char *bytes = memory;
    while (length--)
        *bytes++ = 0;
}
static void restore_terminal(Terminal *terminal)
{
    if (terminal->terminal_changed) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &terminal->saved);
        terminal->terminal_changed = 0;
        fputc('\n', stdout);
    }
}
static int connect_to_server(const struct sockaddr_in *address)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        goto fail;
    if (connect(fd, (const struct sockaddr *)address, sizeof(*address)) == 0)
        return fd;
    if (errno != EINPROGRESS)
        goto fail;
    struct pollfd ready = {fd, POLLOUT, 0};
    int status = poll(&ready, 1, 5000);
    if (status <= 0) {
        if (status == 0)
            errno = ETIMEDOUT;
        goto fail;
    }
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
        goto fail;
    if (error) {
        errno = error;
        goto fail;
    }
    return fd;
fail:
    close(fd);
    return -1;
}
static int append_output(Terminal *terminal, const char *line, size_t length)
{
    if (length + 1 > OUTPUT_BUFFER_SIZE - terminal->output_length) {
        fputs("Local send buffer full; wait and retry.\n", stderr);
        return -1;
    }
    if (terminal->output_start) {
        memmove(terminal->output, terminal->output + terminal->output_start,
                terminal->output_length);
        clear_memory(terminal->output + terminal->output_length,
                     OUTPUT_BUFFER_SIZE - terminal->output_length);
        terminal->output_start = 0;
    }
    memcpy(terminal->output + terminal->output_length, line, length);
    terminal->output_length += length;
    terminal->output[terminal->output_length++] = '\n';
    return 0;
}
static void handle_line(Terminal *terminal)
{
    terminal->line[terminal->line_length] = '\0';
    if (terminal->password_mode) {
        char command[BUFFER_SIZE];
        restore_terminal(terminal);
        int count = snprintf(command, sizeof(command), "%s%.*s",
                             terminal->password_prefix, MAX_PASSWORD, terminal->line);
        if (terminal->line_length > MAX_PASSWORD)
            fputs("Password too long (maximum 128 characters).\n", stderr);
        else if (count > 0 && (size_t)count < sizeof(command))
            (void)append_output(terminal, command, (size_t)count);
        clear_memory(command, sizeof(command));
        clear_memory(terminal->password_prefix, sizeof(terminal->password_prefix));
        terminal->password_mode = 0;
    } else if (isatty(STDIN_FILENO) &&
               (strncmp(terminal->line, "/login ", 7) == 0 ||
                strncmp(terminal->line, "/register ", 10) == 0)) {
        const char *argument = strchr(terminal->line, ' ') + 1;
        size_t length = strlen(argument);
        if (length && length < MAX_USERNAME && !strchr(argument, ' ')) {
            if (tcgetattr(STDIN_FILENO, &terminal->saved) < 0) {
                perror("password input");
            } else {
                struct termios hidden = terminal->saved;
                hidden.c_lflag &= (tcflag_t)~ECHO;
                if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0) {
                    perror("password input");
                } else {
                    terminal->terminal_changed = terminal->password_mode = 1;
                    snprintf(terminal->password_prefix, sizeof(terminal->password_prefix),
                             "%s %.*s ", strncmp(terminal->line, "/login ", 7) == 0 ?
                             "/login" : "/register", MAX_USERNAME - 1, argument);
                    fputs("Password: ", stdout);
                    fflush(stdout);
                }
            }
        } else {
            (void)append_output(terminal, terminal->line, terminal->line_length);
        }
    } else if (terminal->line_length) {
        (void)append_output(terminal, terminal->line, terminal->line_length);
    }
    clear_memory(terminal->line, sizeof(terminal->line));
    terminal->line_length = 0;
}
static void read_input(Terminal *terminal)
{
    unsigned char byte;
    ssize_t count = read(STDIN_FILENO, &byte, 1);
    if (count == 0) {
        terminal->input_eof = 1;
        if (terminal->line_length && !terminal->discarding)
            handle_line(terminal);
        restore_terminal(terminal);
        return;
    }
    if (count < 0) {
        if (errno != EINTR)
            terminal->input_eof = 1;
        return;
    }
    if (byte == '\n') {
        if (!terminal->discarding)
            handle_line(terminal);
        else {
            clear_memory(terminal->line, sizeof(terminal->line));
            terminal->line_length = 0;
            terminal->discarding = 0;
            restore_terminal(terminal);
            terminal->password_mode = 0;
        }
    } else if (!valid_wire_byte(byte) || terminal->line_length == BUFFER_SIZE - 1) {
        if (!terminal->discarding)
            fputs("Invalid or oversized input; line discarded.\n", stderr);
        terminal->discarding = 1;
    } else if (!terminal->discarding) {
        terminal->line[terminal->line_length++] = (char)byte;
    }
}
int main(int argc, char **argv)
{
    struct sockaddr_in address;
    struct sigaction action = {0};
    Terminal terminal = {0};
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: client_app [IPv4 PORT] | --help | --version");
        puts("Use /register NAME or /login NAME for a hidden password prompt.");
        puts("Use /help for commands; /quit or Ctrl+C exits. Plain TCP, no TLS.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("client_app " PROJECT_VERSION);
        return 0;
    }
    if (parse_endpoint(argc, argv, &address) < 0) {
        fputs("Usage: client_app [IPv4 PORT] | --help | --version\n", stderr);
        return 1;
    }
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        return 1;
    }
    int fd = connect_to_server(&address);
    if (fd < 0) {
        perror("connect");
        return stopping ? 0 : 1;
    }
    if (isatty(STDIN_FILENO))
        puts("Connected. /help lists commands; passwords are hidden with /login NAME.");
    int exit_status = 0;
    while (!stopping) {
        struct pollfd ready[2] = {
            {fd, (short)(POLLIN | (terminal.output_length ? POLLOUT : 0)), 0},
            {terminal.input_eof ? -1 : STDIN_FILENO, POLLIN, 0}
        };
        int count = poll(ready, 2, 100);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            exit_status = 1;
            break;
        }
        if (ready[0].revents & POLLIN) {
            char buffer[4096];
            ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
            if (received == 0)
                break;
            if (received > 0) {
                int valid = 1;
                for (ssize_t i = 0; i < received; ++i)
                    if (buffer[i] != '\n' && !valid_wire_byte((unsigned char)buffer[i]))
                        valid = 0;
                if (!valid) {
                    fputs("Invalid server output.\n", stderr);
                    exit_status = 1;
                    break;
                }
                fwrite(buffer, 1, (size_t)received, stdout);
                fflush(stdout);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("recv");
                exit_status = 1;
                break;
            }
        }
        if (ready[0].revents & POLLOUT) {
            ssize_t sent = send(fd, terminal.output + terminal.output_start,
                                terminal.output_length, MSG_NOSIGNAL);
            if (sent > 0) {
                clear_memory(terminal.output + terminal.output_start, (size_t)sent);
                terminal.output_start += (size_t)sent;
                terminal.output_length -= (size_t)sent;
                if (!terminal.output_length)
                    terminal.output_start = 0;
            } else if (sent == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                exit_status = 1;
                break;
            }
        }
        if (ready[1].revents & (POLLIN | POLLHUP))
            read_input(&terminal);
        if (ready[1].revents & (POLLERR | POLLNVAL))
            terminal.input_eof = 1;
        if (terminal.input_eof && !terminal.output_length && !terminal.write_closed) {
            (void)shutdown(fd, SHUT_WR);
            terminal.write_closed = 1;
        }
        if (ready[0].revents & (POLLERR | POLLNVAL)) {
            exit_status = 1;
            break;
        }
        if ((ready[0].revents & POLLHUP) && !(ready[0].revents & POLLIN))
            break;
    }
    restore_terminal(&terminal);
    clear_memory(&terminal, sizeof(terminal));
    close(fd);
    return exit_status;
}
