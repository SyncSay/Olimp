#include "src/core/core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DATA_SIZE (256 * 1024 * 1024)  // 256 MB

// ============ Timer ============
static double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ============ Simulate: User Sessions ============
static void test_user_sessions() {
    printf("\n=== USER SESSIONS ===\n");
    printf("  Simulating: 10000 users, login -> 3 actions -> logout\n");

    unlink("test_data.bin");
    unlink("test_wal.bin");

    Core *core = core_init("test_data.bin", "test_wal.bin", 50000, DATA_SIZE);
    if (!core) { printf("  FAIL: init\n"); return; }

    char data[512];
    int *sessions = calloc(10000, sizeof(int));
    for (int i = 0; i < 10000; i++) sessions[i] = -1;

    int puts = 0, updates = 0, deletes = 0, gets = 0;

    double start = get_time_sec();

    // Phase 1: All users login (PUT)
    for (int i = 0; i < 10000; i++) {
        snprintf(data, sizeof(data),
                 "{\"user_id\":%d,\"action\":\"login\",\"ip\":\"192.168.%d.%d\",\"ts\":%ld}",
                 i, rand() % 256, rand() % 256, time(NULL));
        sessions[i] = core_put(core, data, strlen(data) + 1);
        if (sessions[i] < 0) { printf("  FAIL: login user %d\n", i); goto cleanup; }
        puts++;
    }
    printf("  Phase 1 (login):  %d puts, count=%lu, live=%lu\n", puts, core->count, core->live_count);

    // Phase 2: Each user does 3 actions (UPDATE)
    const char *actions[] = {"view_page", "add_to_cart", "checkout", "search", "logout"};
    for (int action = 0; action < 3; action++) {
        for (int i = 0; i < 10000; i++) {
            uint64_t size;
            void *ptr = core_get(core, sessions[i], &size);  // read current state
            gets++;
            if (!ptr) { printf("  FAIL: get user %d\n", i); goto cleanup; }

            snprintf(data, sizeof(data),
                     "{\"user_id\":%d,\"action\":\"%s\",\"session_id\":%d,\"ts\":%ld}",
                     i, actions[rand() % 5], sessions[i], time(NULL));
            core_update(core, sessions[i], data, strlen(data) + 1);
            updates++;
        }
        printf("  Phase 2 (action %d): %d gets, %d updates, live=%lu, dead=%lu MB\n",
               action + 1, gets, updates, core->live_count, core->dead_bytes / (1024*1024));
    }

    // Phase 3: Users logout (DELETE)
    for (int i = 0; i < 10000; i++) {
        core_delete(core, sessions[i]);
        deletes++;
        sessions[i] = -1;
    }
    printf("  Phase 3 (logout): %d deletes, count=%lu, live=%lu, dead=%lu MB\n",
           deletes, core->count, core->live_count, core->dead_bytes / (1024*1024));

    double elapsed = get_time_sec() - start;

    printf("  ---\n");
    printf("  Total: puts=%d, gets=%d, updates=%d, deletes=%d\n", puts, gets, updates, deletes);
    printf("  Store: %lu MB, dead: %lu MB\n", core->store->used / (1024*1024), core->dead_bytes / (1024*1024));
    printf("  Time: %.2f sec, ops/sec: %.0f\n", elapsed, (puts+gets+updates+deletes) / elapsed);

cleanup:
    free(sessions);
    core_close(core);
    unlink("test_data.bin");
    unlink("test_wal.bin");
}

// ============ Simulate: Key-Value Store ============
static void test_kv_store() {
    printf("\n=== KEY-VALUE STORE ===\n");

    unlink("test_data.bin");
    unlink("test_wal.bin");

    int max_keys = 20000;
    Core *core = core_init("test_data.bin", "test_wal.bin", max_keys * 2, DATA_SIZE);
    if (!core) { printf("  FAIL: init\n"); return; }

    char key[64], value[1024];
    int *indices = calloc(max_keys, sizeof(int));
    for (int i = 0; i < max_keys; i++) indices[i] = -1;

    int puts = 0, gets = 0, updates = 0, deletes = 0;
    int hot_keys = max_keys / 10;  // 10% hot keys

    double start = get_time_sec();

    // Preload
    for (int i = 0; i < max_keys; i++) {
        snprintf(key, sizeof(key), "user:%d:profile", i);
        snprintf(value, sizeof(value),
                 "{\"name\":\"user_%d\",\"age\":%d,\"city\":\"city_%d\",\"visits\":0}",
                 i, 20 + rand() % 50, rand() % 100);
        indices[i] = core_put(core, value, strlen(value) + 1);
        puts++;
    }
    printf("  Preload: %d keys, store=%lu MB\n", max_keys, core->store->used / (1024*1024));

    // Workload: 100000 random operations
    int ops = 100000;
    for (int op = 0; op < ops; op++) {
        int r = rand() % 100;
        int k;

        // 80% operations on hot keys
        if (rand() % 100 < 80) {
            k = rand() % hot_keys;
        } else {
            k = rand() % max_keys;
        }

        if (r < 5 && core->live_count < core->max_blob) {
            // PUT (5%) — new key
            snprintf(key, sizeof(key), "extra:%d", op);
            snprintf(value, sizeof(value), "{\"temp\":%d}", op);
            core_put(core, value, strlen(value) + 1);
            puts++;
        } else if (r < 60) {
            // GET (55%)
            if (indices[k] >= 0) {
                uint64_t size;
                void *ptr = core_get(core, indices[k], &size);
                if (ptr) gets++;
            }
        } else if (r < 90) {
            // UPDATE (30%)
            if (indices[k] >= 0) {
                snprintf(value, sizeof(value),
                         "{\"name\":\"user_%d\",\"age\":%d,\"visits\":%d}",
                         k, 20 + rand() % 50, op);
                core_update(core, indices[k], value, strlen(value) + 1);
                updates++;
            }
        } else {
            // DELETE (5%)
            if (indices[k] >= 0) {
                core_delete(core, indices[k]);
                indices[k] = -1;
                deletes++;
            }
        }

        if ((op + 1) % 25000 == 0) {
            printf("  %d%%: live=%lu, dead=%lu MB, store=%lu MB\n",
                   (op + 1) * 100 / ops, core->live_count,
                   core->dead_bytes / (1024*1024), core->store->used / (1024*1024));
        }
    }

    double elapsed = get_time_sec() - start;

    printf("  ---\n");
    printf("  puts=%d, gets=%d, updates=%d, deletes=%d\n", puts, gets, updates, deletes);
    printf("  Store: %lu MB, dead: %lu MB, live: %lu\n",
           core->store->used / (1024*1024), core->dead_bytes / (1024*1024), core->live_count);
    printf("  Time: %.2f sec, ops/sec: %.0f\n", elapsed, (puts+gets+updates+deletes) / elapsed);

    free(indices);
    core_close(core);
    unlink("test_data.bin");
    unlink("test_wal.bin");
}

// ============ Simulate: Write-Ahead Log Benchmark ============
static void test_write_heavy() {
    printf("\n=== WRITE-HEAVY (Time Series) ===\n");
    printf("  Simulating: IoT sensor data, 1000 devices, 60 readings each\n");

    unlink("test_data.bin");
    unlink("test_wal.bin");

    int devices = 1000;
    int readings = 60;
    int total = devices * readings;
    Core *core = core_init("test_data.bin", "test_wal.bin", total + 1000, DATA_SIZE);
    if (!core) { printf("  FAIL: init\n"); return; }

    char data[256];
    int *device_indices = calloc(devices, sizeof(int));
    int puts = 0, updates = 0;

    double start = get_time_sec();

    // Each device sends readings
    for (int r = 0; r < readings; r++) {
        for (int d = 0; d < devices; d++) {
            snprintf(data, sizeof(data),
                     "{\"device\":%d,\"temp\":%.1f,\"humidity\":%.1f,\"ts\":%d}",
                     d, 20.0 + (rand() % 150) / 10.0, 40.0 + (rand() % 400) / 10.0, r);

            if (r == 0) {
                // First reading: PUT
                device_indices[d] = core_put(core, data, strlen(data) + 1);
                puts++;
            } else {
                // Subsequent: UPDATE
                core_update(core, device_indices[d], data, strlen(data) + 1);
                updates++;
            }
        }

        if ((r + 1) % 10 == 0) {
            printf("  Reading %d/%d: store=%lu MB, dead=%lu MB, live=%lu\n",
                   r + 1, readings, core->store->used / (1024*1024),
                   core->dead_bytes / (1024*1024), core->live_count);
        }
    }

    double elapsed = get_time_sec() - start;
    int total_ops = puts + updates;

    printf("  ---\n");
    printf("  Puts=%d, Updates=%d, Total=%d\n", puts, updates, total_ops);
    printf("  Final store: %lu MB, dead: %lu MB\n",
           core->store->used / (1024*1024), core->dead_bytes / (1024*1024));
    printf("  Time: %.2f sec, ops/sec: %.0f, readings/sec: %.0f\n",
           elapsed, total_ops / elapsed, total / elapsed);

    free(device_indices);
    core_close(core);
    unlink("test_data.bin");
    unlink("test_wal.bin");
}

// ============ MAIN ============
int main() {
    srand(time(NULL));
    printf("=== DATABASE WORKLOAD SIMULATION ===\n");

    test_user_sessions();    // CRUD: login -> actions -> logout
    test_kv_store();         // Key-value with hot keys
    test_write_heavy();      // Time-series / IoT

    printf("\n=== DONE ===\n");
    return 0;
}
