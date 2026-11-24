#include "auth.h"
#include "chat.h"
#include "logger.h"
#include "security.h"
#include "thread_pool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd, peer, room, inflight, closing, drop, eof;
    uint64_t id;
    char username[MAX_USERNAME];
    char input[INPUT_BUFFER_SIZE], output[OUTPUT_BUFFER_SIZE];
    size_t input_length, output_start, output_length;
    double connected, line_started, close_at;
    RateLimit messages;
} Client;
typedef struct {
    int used, references;
    uint32_t ip;
    double last;
    RateLimit connections, authentication;
} Peer;
typedef struct {
    Client clients[MAX_CLIENTS];
    Peer peers[MAX_PEERS];
    ThreadPool pool;
    Auth auth;
    Chat chat;
    sem_t capacity;
    uint64_t next_id;
    unsigned long long rejected, busy, accepted;
} Server;
static volatile sig_atomic_t stop_requested;
static void handle_signal(int number) { (void)number; stop_requested = 1; }
static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static void queue_message(Client *client, const char *message)
{
    if (client->fd < 0 || client->drop)
        return;
    size_t length = strlen(message);
    if (length > OUTPUT_BUFFER_SIZE - client->output_length) {
        client->drop = 1;
        logger_event("WARN", "slow_recipient_disconnected", client->id);
        return;
    }
    if (client->output_start) {
        memmove(client->output, client->output + client->output_start, client->output_length);
        client->output_start = 0;
    }
    memcpy(client->output + client->output_length, message, length);
    client->output_length += length;
}
static void close_after_output(Client *client, const char *message)
{
    if (client->closing)
        return;
    queue_message(client, message);
    client->closing = 1;
    client->close_at = monotonic_now() + 2.0;
    sodium_memzero(client->input, sizeof(client->input));
    client->input_length = 0;
}
static void flush_output(Client *client)
{
    if (!client->output_length)
        return;
    size_t length = client->output_length > 4096 ? 4096 : client->output_length;
    ssize_t count = send(client->fd, client->output + client->output_start, length, MSG_NOSIGNAL);
    if (count > 0) {
        client->output_start += (size_t)count;
        client->output_length -= (size_t)count;
        if (!client->output_length)
            client->output_start = 0;
    } else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        client->drop = 1;
    }
}
static void remove_client(Server *server, Client *client)
{
    if (client->fd < 0)
        return;
    logger_event("INFO", "disconnected", client->id);
    close(client->fd);
    --server->peers[client->peer].references;
    sodium_memzero(client, sizeof(*client));
    client->fd = -1;
    /* Each registered connection owns exactly one permit. */
    if (sem_post(&server->capacity) < 0)
        perror("sem_post");
}
static Client *find_id(Server *server, uint64_t id)
{
    for (int i = 0; i < MAX_CLIENTS; ++i)
        if (server->clients[i].fd >= 0 && server->clients[i].id == id)
            return &server->clients[i];
    return NULL;
}
static Client *find_username(Server *server, const char *name)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        Client *client = &server->clients[i];
        if (client->fd >= 0 && !client->closing && !client->drop &&
            client->username[0] && strcmp(client->username, name) == 0)
            return client;
    }
    return NULL;
}
static int peer_index(Server *server, uint32_t ip, double now)
{
    int free_index = -1;
    for (int i = 0; i < MAX_PEERS; ++i) {
        Peer *peer = &server->peers[i];
        if (peer->used && peer->ip == ip)
            return i;
        if (!peer->used || (!peer->references && now - peer->last > 600))
            free_index = i;
    }
    if (free_index < 0)
        return -1;
    Peer *peer = &server->peers[free_index];
    memset(peer, 0, sizeof(*peer));
    peer->used = 1;
    peer->ip = ip;
    peer->last = now;
    rate_init(&peer->connections, 40, now);
    rate_init(&peer->authentication, 12, now);
    return free_index;
}
static void accept_clients(Server *server, int listener)
{
    for (int budget = 0; budget < 16; ++budget) {
        struct sockaddr_in address;
        socklen_t length = sizeof(address);
        int fd = accept(listener, (struct sockaddr *)&address, &length);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                perror("accept");
            return;
        }
        double now = monotonic_now();
        int peer_number = peer_index(server, address.sin_addr.s_addr, now);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i)
            if (server->clients[i].fd < 0) { slot = i; break; }
        int allowed = peer_number >= 0 &&
            rate_allow(&server->peers[peer_number].connections, 40, 2, now);
        if (peer_number >= 0)
            server->peers[peer_number].last = now;
        if (!allowed || slot < 0 || server->next_id == UINT64_MAX ||
            set_nonblocking(fd) < 0) {
            (void)send(fd, "ERR capacity_or_connection_rate\n", sizeof("ERR capacity_or_connection_rate\n") - 1, MSG_NOSIGNAL | MSG_DONTWAIT);
            close(fd);
            ++server->rejected;
            continue;
        }
        if (sem_trywait(&server->capacity) < 0) {
            close(fd);
            ++server->rejected;
            continue;
        }
        Client *client = &server->clients[slot];
        memset(client, 0, sizeof(*client));
        client->fd = fd;
        client->peer = peer_number;
        client->id = ++server->next_id;
        client->room = -1;
        client->connected = now;
        rate_init(&client->messages, 20, now);
        ++server->peers[peer_number].references;
        ++server->accepted;
        queue_message(client, "OK welcome; /register NAME PASSWORD then /login NAME PASSWORD; /help\n");
        logger_event("INFO", "connected", client->id);
    }
}
static void receive_input(Client *client)
{
    size_t space = INPUT_BUFFER_SIZE - client->input_length;
    if (!space)
        return;
    if (space > 4096)
        space = 4096;
    ssize_t count = recv(client->fd, client->input + client->input_length, space, 0);
    if (count > 0) {
        if (!client->input_length)
            client->line_started = monotonic_now();
        size_t old = client->input_length;
        client->input_length += (size_t)count;
        for (size_t i = old; i < client->input_length; ++i) {
            unsigned char c = (unsigned char)client->input[i];
            if (c != '\n' && !valid_wire_byte(c)) {
                close_after_output(client, "ERR invalid_input\n");
                return;
            }
        }
    } else if (count == 0) {
        client->eof = 1;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        client->drop = 1;
    }
}
static void process_task(const Task *task, Result *result, void *context)
{
    Auth *auth = context;
    result->status = parse_command(task->message, &result->command);
    if (result->status != 0)
        return;
    Command *command = &result->command;
    if (command->type == CMD_REGISTER || command->type == CMD_LOGIN) {
        if (!validate_name(command->argument, MAX_USERNAME) || !validate_password(command->text))
            result->status = 3;
        else if (command->type == CMD_REGISTER)
            result->status = register_user(auth, command->argument, command->text);
        else
            result->status = login_user(auth, command->argument, command->text);
        sodium_memzero(command->text, sizeof(command->text));
    }
}
static void consume_line(Client *client, size_t length)
{
    size_t remaining = client->input_length - length;
    memmove(client->input, client->input + length, remaining);
    sodium_memzero(client->input + remaining, client->input_length - remaining);
    client->input_length = remaining;
    client->line_started = remaining ? monotonic_now() : 0;
}
static void dispatch_one(Server *server, Client *client)
{
    if (client->inflight || client->closing || client->drop || !client->input_length)
        return;
    char *newline = memchr(client->input, '\n', client->input_length);
    size_t length = newline ? (size_t)(newline - client->input) : client->input_length;
    if (length >= BUFFER_SIZE) {
        close_after_output(client, "ERR line_too_long\n");
        return;
    }
    if (!newline) {
        if (client->eof)
            close_after_output(client, "ERR incomplete_line\n");
        return;
    }
    Task task = {0};
    task.sender_id = client->id;
    memcpy(task.message, client->input, length);
    double now = monotonic_now();
    int auth_command = strncmp(task.message, "/login ", 7) == 0 ||
                       strncmp(task.message, "/register ", 10) == 0;
    if (!rate_allow(&client->messages, 20, 5, now)) {
        close_after_output(client, "ERR message_rate\n");
        logger_event("WARN", "message_rate", client->id);
    } else if (auth_command && client->username[0]) {
        queue_message(client, "ERR logout_first\n");
    } else if (auth_command &&
               !rate_allow(&server->peers[client->peer].authentication, 12, 0.2, now)) {
        close_after_output(client, "ERR auth_rate\n");
        logger_event("WARN", "auth_rate", client->id);
    } else if (thread_pool_submit(&server->pool, &task)) {
        client->inflight = 1;
    } else {
        queue_message(client, "ERR busy_retry\n");
        ++server->busy;
    }
    server->peers[client->peer].last = now;
    sodium_memzero(&task, sizeof(task));
    if (!client->closing)
        consume_line(client, length + 1);
}
static void list_rooms(Server *server, Client *client)
{
    char line[MAX_ROOM_NAME + 64];
    queue_message(client, "OK rooms\n");
    for (int room = 0; room < MAX_ROOMS; ++room) {
        if (!server->chat.names[room][0])
            continue;
        int members = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *member = &server->clients[i];
            if (member->fd >= 0 && !member->drop && !member->closing &&
                member->username[0] && member->room == room)
                ++members;
        }
        snprintf(line, sizeof(line), "ROOM %.*s %d\n", MAX_ROOM_NAME - 1, server->chat.names[room], members);
        queue_message(client, line);
    }
    queue_message(client, "END rooms\n");
}
static void apply_result(Server *server, Client *client, Result *result)
{
    Command *command = &result->command;
    char line[BUFFER_SIZE + MAX_USERNAME + MAX_ROOM_NAME + 32];
    if (command->type == CMD_INVALID || result->status < 0) {
        queue_message(client, "ERR invalid_command\n");
        return;
    }
    if (command->type == CMD_HELP) {
        queue_message(client, "OK commands: /register NAME PASSWORD | /login NAME PASSWORD | /logout | /msg NAME TEXT | /create ROOM | /join ROOM | /leave | /rooms | /users | /help | /quit\n");
        return;
    }
    if (command->type == CMD_QUIT) {
        close_after_output(client, "BYE\n");
        return;
    }
    if (command->type == CMD_REGISTER) {
        const char *response = result->status == 0 ? "OK registered; login required\n" :
            result->status == 1 ? "ERR username_unavailable\n" :
            result->status == 3 ? "ERR name_or_password_policy\n" : "ERR account_storage\n";
        queue_message(client, response);
        logger_event(result->status == 0 ? "INFO" : "WARN", "registration_result", client->id);
        return;
    }
    if (command->type == CMD_LOGIN) {
        if (result->status != 0) {
            queue_message(client, "ERR credentials\n");
            logger_event("WARN", "login_failed", client->id);
        } else if (find_username(server, command->argument)) {
            queue_message(client, "ERR already_online\n");
        } else {
            memcpy(client->username, command->argument, sizeof(client->username));
            client->room = 0;
            queue_message(client, "OK logged_in general\n");
            logger_event("INFO", "login_succeeded", client->id);
        }
        return;
    }
    if (!client->username[0]) {
        queue_message(client, "ERR login_required\n");
        return;
    }
    switch (command->type) {
    case CMD_LOGOUT:
        sodium_memzero(client->username, sizeof(client->username));
        client->room = -1;
        client->connected = monotonic_now();
        queue_message(client, "OK logged_out\n");
        logger_event("INFO", "logout", client->id);
        break;
    case CMD_MESSAGE:
        if (client->room < 0) {
            queue_message(client, "ERR join_room_first\n");
            break;
        }
        snprintf(line, sizeof(line), "MSG %.*s %.*s %s\n", MAX_ROOM_NAME - 1, server->chat.names[client->room],
                 MAX_USERNAME - 1, client->username, command->text);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *recipient = &server->clients[i];
            if (recipient->fd >= 0 && !recipient->closing && recipient->username[0] &&
                recipient->room == client->room)
                queue_message(recipient, line);
        }
        break;
    case CMD_PRIVATE: {
        Client *recipient = find_username(server, command->argument);
        if (!recipient) {
            queue_message(client, "ERR user_offline\n");
            break;
        }
        snprintf(line, sizeof(line), "PRIVATE %s %s\n", client->username, command->text);
        queue_message(recipient, line);
        queue_message(client, "OK private_sent\n");
        break;
    }
    case CMD_CREATE: {
        int room = chat_create(&server->chat, command->argument);
        if (room < 0)
            queue_message(client, "ERR room_invalid_exists_or_full\n");
        else {
            client->room = room;
            queue_message(client, "OK room_created_and_joined\n");
            logger_event("INFO", "room_created", client->id);
        }
        break;
    }
    case CMD_JOIN: {
        int room = chat_find(&server->chat, command->argument);
        if (room < 0)
            queue_message(client, "ERR room_not_found\n");
        else {
            client->room = room;
            queue_message(client, "OK joined\n");
            logger_event("INFO", "room_joined", client->id);
        }
        break;
    }
    case CMD_LEAVE:
        client->room = -1;
        queue_message(client, "OK left_room\n");
        break;
    case CMD_ROOMS:
        list_rooms(server, client);
        break;
    case CMD_USERS:
        queue_message(client, "OK users\n");
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *member = &server->clients[i];
            if (member->fd >= 0 && !member->drop && !member->closing && member->username[0]) {
                snprintf(line, sizeof(line), "USER %s\n", member->username);
                queue_message(client, line);
            }
        }
        queue_message(client, "END users\n");
        break;
    default:
        queue_message(client, "ERR invalid_command\n");
        break;
    }
}
static void process_completed_tasks(Server *server)
{
    Result result;
    for (int budget = 0; budget < QUEUE_SIZE && thread_pool_get_result(&server->pool, &result); ++budget) {
        Client *client = find_id(server, result.sender_id);
        if (client) {
            client->inflight = 0;
            if (!client->closing && !client->drop)
                apply_result(server, client, &result);
        }
        sodium_memzero(&result, sizeof(result));
    }
}
static int setup_server(const struct sockaddr_in *address)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    if (fd < 0)
        return -1;
    if (set_nonblocking(fd) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(fd, (const struct sockaddr *)address, sizeof(*address)) < 0 ||
        listen(fd, MAX_CLIENTS) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
int main(int argc, char **argv)
{
    struct sockaddr_in address;
    struct sigaction action = {0};
    static Server server;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: server_app [IPv4 PORT] | --help | --version");
        puts("Run from the project folder. Default: localhost:8080; plain TCP.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("server_app " PROJECT_VERSION);
        return 0;
    }
    if (parse_endpoint(argc, argv, &address) < 0) {
        fputs("Usage: server_app [IPv4 PORT] | --help | --version\n", stderr);
        return 1;
    }
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0 ||
        sodium_init() < 0) {
        fputs("Signal or libsodium initialization failed\n", stderr);
        return 1;
    }
    for (int i = 0; i < MAX_CLIENTS; ++i)
        server.clients[i].fd = -1;
    if (logger_init() < 0) {
        perror("logger initialization");
        return 1;
    }
    if (auth_init(&server.auth) < 0) {
        fputs("Account storage unavailable, unsafe, malformed, or already locked\n", stderr);
        logger_close();
        return 1;
    }
    if (sem_init(&server.capacity, 0, MAX_CLIENTS) < 0) {
        perror("sem_init");
        auth_cleanup(&server.auth);
        logger_close();
        return 1;
    }
    if (thread_pool_init(&server.pool, process_task, &server.auth) < 0) {
        fputs("Worker initialization failed\n", stderr);
        sem_destroy(&server.capacity);
        auth_cleanup(&server.auth);
        logger_close();
        return 1;
    }
    chat_init(&server.chat);
    int listener = setup_server(&address);
    if (listener < 0) {
        perror("listener setup");
        thread_pool_destroy(&server.pool);
        sem_destroy(&server.capacity);
        auth_cleanup(&server.auth);
        logger_close();
        return 1;
    }
    printf("Listening on %s:%u; %d workers; %d clients; plain TCP\n",
           argc == 3 ? argv[1] : SERVER_IP, (unsigned)ntohs(address.sin_port),
           WORKER_COUNT, MAX_CLIENTS);
    fflush(stdout);
    logger_event("INFO", "server_started", 0);
    int draining = 0, exit_status = 0;
    double drain_started = 0;
    for (;;) {
        if (stop_requested && !draining) {
            draining = 1;
            drain_started = monotonic_now();
            close(listener);
            listener = -1;
            thread_pool_stop_accepting(&server.pool);
            logger_event("INFO", "shutdown_requested", 0);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                sodium_memzero(server.clients[i].input, sizeof(server.clients[i].input));
                server.clients[i].input_length = 0;
            }
        }
        process_completed_tasks(&server);
        double now = monotonic_now();
        int active = 0;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *client = &server.clients[i];
            if (client->fd < 0)
                continue;
            ++active;
            if (!draining) {
                dispatch_one(&server, client);
                if (client->eof && !client->inflight && !client->input_length)
                    close_after_output(client, "BYE\n");
                if (!client->username[0] && now - client->connected > AUTH_TIMEOUT)
                    close_after_output(client, "ERR authentication_timeout\n");
                if (client->input_length && now - client->line_started > LINE_TIMEOUT)
                    close_after_output(client, "ERR input_timeout\n");
            } else if (!client->inflight) {
                close_after_output(client, "NOTICE server_shutdown\n");
            }
            if (client->drop || (client->closing &&
                (!client->output_length || now > client->close_at)))
                remove_client(&server, client);
        }
        if (draining && ((!active && thread_pool_unfinished(&server.pool) == 0) ||
                         now - drain_started > SHUTDOWN_TIMEOUT))
            break;
        struct pollfd pollfds[MAX_CLIENTS + 1];
        pollfds[0] = (struct pollfd){listener, draining ? 0 : POLLIN, 0};
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *client = &server.clients[i];
            short events = 0;
            if (!draining && !client->closing && !client->eof && client->input_length < INPUT_BUFFER_SIZE)
                events |= POLLIN;
            if (client->output_length)
                events |= POLLOUT;
            pollfds[i + 1] = (struct pollfd){client->fd, events, 0};
        }
        int ready = poll(pollfds, MAX_CLIENTS + 1, 50);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            exit_status = 1;
            break;
        }
        if (!draining && (pollfds[0].revents & POLLIN))
            accept_clients(&server, listener);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            Client *client = &server.clients[i];
            short events = pollfds[i + 1].revents;
            /* Entries accepted this iteration did not participate in this poll. */
            if (client->fd < 0 || pollfds[i + 1].fd != client->fd)
                continue;
            if (events & POLLIN)
                receive_input(client);
            if (events & POLLOUT)
                flush_output(client);
            if (events & (POLLERR | POLLNVAL | POLLHUP))
                client->drop = 1;
        }
    }
    if (listener >= 0)
        close(listener);
    thread_pool_stop_accepting(&server.pool);
    for (int i = 0; i < MAX_CLIENTS; ++i)
        remove_client(&server, &server.clients[i]);
    /* Destruction wakes workers even if the result queue is full. Accepted
       hashing jobs finish before auth storage and synchronization are freed. */
    thread_pool_destroy(&server.pool);
    if (sem_destroy(&server.capacity) < 0)
        perror("sem_destroy");
    auth_cleanup(&server.auth);
    logger_event("INFO", "server_stopped", 0);
    logger_close();
    printf("Stopped. accepted=%llu rejected=%llu busy=%llu\n",
           server.accepted, server.rejected, server.busy);
    return exit_status;
}
