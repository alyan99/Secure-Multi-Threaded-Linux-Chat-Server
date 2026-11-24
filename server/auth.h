#ifndef CHAT_AUTH_H
#define CHAT_AUTH_H
#include "config.h"
#include <pthread.h>
#include <sodium.h>
typedef struct { char name[MAX_USERNAME]; char hash[crypto_pwhash_STRBYTES]; } Account;
typedef struct {
    Account accounts[MAX_USERS];
    size_t count;
    int directory_fd, lock_fd;
    pthread_mutex_t mutex;
    char dummy_hash[crypto_pwhash_STRBYTES];
} Auth;
/* Status: 0 success, 1 duplicate/bad credentials, 2 capacity/storage failure. */
int auth_init(Auth *auth);
int register_user(Auth *auth, const char *name, const char *password);
int login_user(Auth *auth, const char *name, const char *password);
void auth_cleanup(Auth *auth);
#endif
