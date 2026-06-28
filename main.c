#include "src/core/core.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("=== core_init ===\n");
    Core *core = core_init("data.bin", "wal.bin", 100, 1024 * 1024);
    if (!core) {
        printf("FAIL: core_init returned NULL\n");
        return 1;
    }
    printf("OK\n");

    // -- core_put --
    printf("=== core_put ===\n");
    const char *hello = "Hello, World!";
    int idx = core_put(core, hello, strlen(hello) + 1);
    if (idx < 0) {
        printf("FAIL: core_put returned %d\n", idx);
        return 1;
    }
    printf("OK: idx=%d\n", idx);

    // -- core_get --
    printf("=== core_get ===\n");
    uint64_t size;
    char *data = core_get(core, idx, &size);
    if (!data) {
        printf("FAIL: core_get returned NULL\n");
        return 1;
    }
    printf("OK: size=%lu, data=\"%s\"\n", size, data);

    // -- core_update --
    printf("=== core_update ===\n");
    const char *updated = "Updated!";
    core_update(core, idx, updated, strlen(updated) + 1);
    data = core_get(core, idx, &size);
    if (!data || strcmp(data, updated) != 0) {
        printf("FAIL: core_update\n");
        return 1;
    }
    printf("OK: data=\"%s\"\n", data);

    // -- core_delete --
    printf("=== core_delete ===\n");
    uint64_t count_before = core->count;
    core_delete(core, idx);
    if (core->count != count_before - 1) {
        printf("FAIL: count did not decrease\n");
        return 1;
    }
    printf("OK: count=%lu\n", core->count);

    // -- core_save --
    printf("=== core_save ===\n");
    core_save(core);
    printf("OK: saved to data.bin\n");

    // -- core_close --
    printf("=== core_close ===\n");
    core_close(core);
    printf("OK\n");

    // -- check work --
    printf("=== reload ===\n");
    core = core_init("data.bin", "wal.bin", 100, 1024 * 1024);
    if (!core) {
        printf("FAIL: reload\n");
        return 1;
    }
    printf("OK: count=%lu, store_used=%lu\n", core->count, core->store->used);

    core_close(core);
    printf("=== ALL TESTS PASSED ===\n");
    return 0;
}
