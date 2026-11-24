#include "auth.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct { Auth *auth; int result; } Registration;
static void *register_concurrently(void *argument)
{
    Registration *registration = argument;
    registration->result = register_user(registration->auth, "bob", "strongPass2");
    return NULL;
}
int main(void)
{
    assert(sodium_init() >= 0);
    char path[] = "/tmp/chat-auth-XXXXXX";
    assert(mkdtemp(path));
    assert(chdir(path) == 0);
    Auth auth;
    assert(auth_init(&auth) == 0);
    assert(register_user(&auth, "alice", "strongPass1") == 0);
    assert(register_user(&auth, "alice", "otherPass1") == 1);
    assert(login_user(&auth, "alice", "strongPass1") == 0);
    assert(login_user(&auth, "alice", "wrongPass1") == 1);
    assert(login_user(&auth, "missing", "strongPass1") == 1);
    Registration registrations[2] = {{&auth, -1}, {&auth, -1}};
    pthread_t threads[2];
    for (int i = 0; i < 2; ++i)
        assert(pthread_create(&threads[i], NULL, register_concurrently, &registrations[i]) == 0);
    for (int i = 0; i < 2; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert((registrations[0].result == 0 && registrations[1].result == 1) ||
           (registrations[0].result == 1 && registrations[1].result == 0));
    struct stat st;
    assert(stat("data/users.db", &st) == 0 && (st.st_mode & 0777) == 0600);
    auth_cleanup(&auth);
    assert(auth_init(&auth) == 0);
    assert(login_user(&auth, "alice", "strongPass1") == 0);
    assert(login_user(&auth, "bob", "strongPass2") == 0);
    auth_cleanup(&auth);
    assert(unlink("data/users.db") == 0);
    assert(unlink("data/users.lock") == 0);
    assert(rmdir("data") == 0);
    assert(chdir("/") == 0);
    assert(rmdir(path) == 0);
    puts("PASS auth persistence and permissions");
    return 0;
}
