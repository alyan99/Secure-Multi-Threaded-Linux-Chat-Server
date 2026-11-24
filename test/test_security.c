#include "security.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(validate_name("alice_12", MAX_USERNAME));
    assert(!validate_name("", MAX_USERNAME));
    assert(!validate_name("bad name", MAX_USERNAME));
    assert(!validate_name("../file", MAX_USERNAME));
    assert(validate_password("strongPass1"));
    assert(!validate_password("short"));
    assert(!validate_password("space password"));
    RateLimit bucket;
    rate_init(&bucket, 2, 100);
    assert(rate_allow(&bucket, 2, 1, 100));
    assert(rate_allow(&bucket, 2, 1, 100));
    assert(!rate_allow(&bucket, 2, 1, 100));
    assert(!rate_allow(&bucket, 2, 1, 99));
    assert(rate_allow(&bucket, 2, 1, 101));
    assert(!rate_allow(&bucket, 2, 1, 101));
    assert(monotonic_now() > 0);
    puts("PASS security");
    return 0;
}
