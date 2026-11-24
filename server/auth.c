#include "auth.h"
#include "security.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int find_user(Auth *auth, const char *name)
{
    for (size_t i = 0; i < auth->count; ++i)
        if (strcmp(auth->accounts[i].name, name) == 0)
            return (int)i;
    return -1;
}
static int hash_password(char *hash, const char *password)
{
    return crypto_pwhash_str_alg(hash, password, strlen(password),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_ARGON2ID13);
}
static int secure_file(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
           st.st_nlink == 1 && fchmod(fd, 0600) == 0;
}
static int load_users(Auth *auth)
{
    int fd = openat(auth->directory_fd, "users.db", O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    if (!secure_file(fd)) {
        close(fd);
        return -1;
    }
    FILE *file = fdopen(fd, "r");
    if (!file) {
        close(fd);
        return -1;
    }
    char line[MAX_USERNAME + crypto_pwhash_STRBYTES + 4];
    int status = 0;
    while (fgets(line, sizeof(line), file)) {
        char *tab = strchr(line, '\t');
        char *newline = strchr(line, '\n');
        if (!tab || !newline || newline[1] || auth->count == MAX_USERS) {
            status = -1;
            break;
        }
        *tab++ = '\0';
        *newline = '\0';
        size_t length = strlen(tab);
        if (!validate_name(line, MAX_USERNAME) || find_user(auth, line) >= 0 ||
            length >= crypto_pwhash_STRBYTES || strncmp(tab, "$argon2id$", 10) != 0 ||
            crypto_pwhash_str_needs_rehash(tab, crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                         crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
            status = -1;
            break;
        }
        Account *user = &auth->accounts[auth->count++];
        memcpy(user->name, line, strlen(line) + 1);
        memcpy(user->hash, tab, length + 1);
    }
    if (ferror(file))
        status = -1;
    sodium_memzero(line, sizeof(line));
    if (fclose(file) != 0)
        status = -1;
    return status;
}
/* Caller holds auth mutex; hashing never happens under this lock. */
static int save_users(Auth *auth)
{
    int fd = openat(auth->directory_fd, "users.tmp",
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlinkat(auth->directory_fd, "users.tmp", 0);
        return -1;
    }
    int status = 0;
    for (size_t i = 0; i < auth->count; ++i)
        if (fprintf(file, "%s\t%s\n", auth->accounts[i].name, auth->accounts[i].hash) < 0)
            status = -1;
    if (fflush(file) != 0 || fsync(fd) != 0)
        status = -1;
    if (fclose(file) != 0)
        status = -1;
    if (status == 0 && renameat(auth->directory_fd, "users.tmp",
                                auth->directory_fd, "users.db") != 0)
        status = -1;
    if (status != 0)
        unlinkat(auth->directory_fd, "users.tmp", 0);
    else
        /* Rename committed. A failed directory fsync cannot roll it back. */
        (void)fsync(auth->directory_fd);
    return status;
}
int auth_init(Auth *auth)
{
    memset(auth, 0, sizeof(*auth));
    auth->directory_fd = auth->lock_fd = -1;
    if (pthread_mutex_init(&auth->mutex, NULL) != 0)
        return -1;
    if (mkdir("data", 0700) < 0 && errno != EEXIST)
        goto fail;
    auth->directory_fd = open("data", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    struct stat st;
    if (auth->directory_fd < 0 || fstat(auth->directory_fd, &st) < 0 ||
        st.st_uid != geteuid() || fchmod(auth->directory_fd, 0700) < 0)
        goto fail;
    auth->lock_fd = openat(auth->directory_fd, "users.lock",
                          O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (auth->lock_fd < 0 || !secure_file(auth->lock_fd) ||
        flock(auth->lock_fd, LOCK_EX | LOCK_NB) < 0)
        goto fail;
    /* A crash can leave a temporary snapshot. Only its owner can enter data/. */
    if (unlinkat(auth->directory_fd, "users.tmp", 0) < 0 && errno != ENOENT)
        goto fail;
    if (load_users(auth) < 0 || hash_password(auth->dummy_hash, "dummy-not-a-user") < 0)
        goto fail;
    return 0;
fail:
    auth_cleanup(auth);
    return -1;
}
int register_user(Auth *auth, const char *name, const char *password)
{
    if (!validate_name(name, MAX_USERNAME) || !validate_password(password))
        return 1;
    char hash[crypto_pwhash_STRBYTES] = {0};
    pthread_mutex_lock(&auth->mutex);
    int exists = find_user(auth, name) >= 0;
    pthread_mutex_unlock(&auth->mutex);
    if (exists)
        return 1;
    if (hash_password(hash, password) < 0) {
        sodium_memzero(hash, sizeof(hash));
        return 2;
    }
    int status = 0;
    pthread_mutex_lock(&auth->mutex);
    if (find_user(auth, name) >= 0)
        status = 1;
    else if (auth->count == MAX_USERS)
        status = 2;
    else {
        Account *user = &auth->accounts[auth->count++];
        memcpy(user->name, name, strlen(name) + 1);
        memcpy(user->hash, hash, sizeof(hash));
        if (save_users(auth) < 0) {
            sodium_memzero(&auth->accounts[--auth->count], sizeof(Account));
            status = 2;
        }
    }
    pthread_mutex_unlock(&auth->mutex);
    sodium_memzero(hash, sizeof(hash));
    return status;
}
int login_user(Auth *auth, const char *name, const char *password)
{
    if (!validate_name(name, MAX_USERNAME) || !validate_password(password))
        return 1;
    char hash[crypto_pwhash_STRBYTES] = {0};
    pthread_mutex_lock(&auth->mutex);
    int index = find_user(auth, name);
    memcpy(hash, index >= 0 ? auth->accounts[index].hash : auth->dummy_hash, sizeof(hash));
    pthread_mutex_unlock(&auth->mutex);
    int valid = crypto_pwhash_str_verify(hash, password, strlen(password)) == 0;
    sodium_memzero(hash, sizeof(hash));
    return valid && index >= 0 ? 0 : 1;
}
void auth_cleanup(Auth *auth)
{
    if (auth->lock_fd >= 0)
        close(auth->lock_fd);
    if (auth->directory_fd >= 0)
        close(auth->directory_fd);
    pthread_mutex_destroy(&auth->mutex);
    sodium_memzero(auth, sizeof(*auth));
}
