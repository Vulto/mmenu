#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

/* Linked only into the fault-injection build; production allocation is unchanged. */
static size_t AllocationCalls;

static int ShouldFailAllocation(void)
{
    const char *value = getenv("MMENU_FAIL_ALLOC_AT");
    char *end = NULL;
    unsigned long long target;

    if (!value || !*value) return 0;
    errno = 0;
    target = strtoull(value, &end, 10);
    if (errno || !end || *end || target == 0) return 0;
    AllocationCalls++;
    if (AllocationCalls == target) {
        errno = ENOMEM;
        return 1;
    }
    return 0;
}

void *TestMalloc(size_t size)
{
    if (ShouldFailAllocation()) return NULL;
    return malloc(size);
}

void *TestRealloc(void *ptr, size_t size)
{
    if (ShouldFailAllocation()) return NULL;
    return realloc(ptr, size);
}
