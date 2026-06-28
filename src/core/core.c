#include "core.h"
#include <bits/pthreadtypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>  //macro (NULL) and support type(size_t)
#include <string.h>
#include <signal.h>                           //debag
#include <pthread.h>  //f



// work with file
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


// -- support func --


//    uint64_t index;
//    uint64_t old_address;
//    uint64_t old_size;
//    uint64_t new_address;
//    uint64_t new_size;
//    bool is_old;
//    WALEntry;

static void wal_write_entry(Core *restrict core, uint64_t index, uint64_t old_addr,
                     uint64_t olde_size, uint64_t new_addr, uint64_t new_size,
                     bool is_old) {
    WALEntry *w;
    w = &core->wal[core->wal_count++];
    w->index = index;
    w->old_address = old_addr;
    w->old_size = olde_size;
    w->new_address = new_addr;
    w->new_size = new_size;
    w->is_old = is_old;

    if (core->wal_fd >= 0) {
        write(core->wal_fd, w, sizeof(WALEntry));
        fsync(core->wal_fd);
    } else {
        perror("check wal fd");
    }   // overflow or division by 0 (usually for integers or floats, but this works too)
}

// Pthread func wal_write
static void* wal_write_entry_pth(void *arg) {
    Task *task = (Task *)arg;
    wal_write_entry(task->core, task->idx, task->old_addr, task->old_size, task->new_addr,
                    task->new_size, task->is_old);
    free(task);
    return NULL;
}

static void apply_wal(Core *restrict core, WALEntry *restrict wal) {

    uint64_t idx = wal->index;
    uint64_t count = core->count;

    if (idx >= count) {
        count = idx+1;
    }
    core->entries[idx].addr = wal->new_address;
    core->entries[idx].size = wal->new_size;
    if (wal->new_size == 0) {
        core->entries[idx].deleted = true; // blob has deleted
    } else {
        core->entries[idx].deleted = false; // no delete
    }
}

// -- Compaction: remove deleted blobs and old data versions, reclaim space --
static void compact(Core *restrict core) {
    if (core->live_count == 0) {
        // All blobs deleted — just reset everything
        core->count = 0;
        core->store->used = 0;
        core->dead_bytes = 0;
        core->wal_count = 0;
        if (core->wal_fd >= 0) ftruncate(core->wal_fd, 0);
        return;
    }

    // Temporary buffer for live data — worst case same size as current
    uint8_t *new_data = malloc(core->store->used);
    if (!new_data) return; // Not enough memory for compaction, skip and try later

    uint64_t new_used = 0;
    uint64_t write_idx = 0;

    // Walk all entries, copy only alive blobs to new data area
    for (uint64_t i = 0; i < core->count; i++) {
        if (!core->entries[i].deleted) {
            // Copy blob data to new compacted position
            memcpy(new_data + new_used,
                   core->store->data + core->entries[i].addr,
                   core->entries[i].size);

            // Compact entries array — shift alive entries left to fill gaps
            if (write_idx != i) {
                core->entries[write_idx] = core->entries[i];
            }
            core->entries[write_idx].addr = new_used;
            new_used += core->entries[write_idx].size;
            write_idx++;
        }
    }

    // Replace old data with compacted version
    memcpy(core->store->data, new_data, new_used);
    core->store->used = new_used;
    core->count = write_idx;
    // live_count unchanged — all entries[0..count-1] are now alive
    core->dead_bytes = 0; // No more wasted space

    free(new_data);

    // WAL is no longer needed — data file is consistent after compaction
    core->wal_count = 0;
    if (core->wal_fd >= 0) ftruncate(core->wal_fd, 0);
}




// -- API --
Core *core_init(const char *restrict data_path, const char *restrict wal_path,
                uint64_t max_blob, uint64_t data_size) {
  /*
   * Now core_init creates and
   * allocates memory for the Core structure
   * and returns the address
  */
    Core *core = malloc(sizeof(Core) + sizeof(BEntry) * max_blob);
    if (!core) {
        perror("malloc core");
        return NULL;
    }
    memset(core, 0, sizeof(*core));

    core->max_blob = max_blob;
    core->data_size = data_size;

    core->store = malloc(sizeof(BStore) + data_size);
    if (!core->store) {
        perror("malloc store");
        free(core);
        return NULL;
    }
    core->store->used = 0;

    // open file
    core->data_fd = open(data_path, O_RDWR | O_CREAT | O_SYNC, 0644);
    core->wal_fd = open(wal_path, O_RDWR | O_CREAT | O_SYNC, 0644);
    /*
     *  O_RDWR: Opens a file for reading and writing.
     *  O_CREAT: Creates a file if it does not already exist.
     *  0644: Permissions (owner: read/write, group/others: read-only)
     *  O_SYNC: Blocks the write() operation until the data is physically written to the hard drive or SSD.
     */

    if (core->data_fd >= 0) {
        struct stat st;
        fstat(core->data_fd, &st);
        if (st.st_size > 0) {
            pread(core->data_fd, &core->count, sizeof(uint64_t), 0);
            pread(core->data_fd, core->entries, sizeof(BEntry) * core->count,
                  sizeof(uint64_t));
            core->store->used =
                st.st_size - sizeof(uint64_t) - sizeof(BEntry) * core->count;
            pread(core->data_fd, core->store->data, core->store->used,
                  sizeof(uint64_t) + sizeof(BEntry) * core->count);

            // Count live blobs and wasted bytes after loading from data file
            core->live_count = 0;
            core->dead_bytes = 0;
            for (uint64_t i = 0; i < core->count; i++) {
                if (!core->entries[i].deleted) {
                    core->live_count++;
                } else {
                    core->dead_bytes += core->entries[i].size;
                }
            }
        }
    }

    if (core->wal_fd >= 0) {
        struct stat st;
        fstat(core->wal_fd, &st);
        uint64_t wal_enties = st.st_size / sizeof(WALEntry);
        // i want add OpenMP, but will stack overflow (I think)
        for (uint64_t i = 0; i < wal_enties; i++) {
            WALEntry w;
            pread(core->wal_fd, &w, sizeof(WALEntry), i * sizeof(WALEntry));
            apply_wal(core, &w);
        }

        ftruncate(core->wal_fd, 0); // clean WAL

        // Recount live blobs and dead bytes after WAL recovery
        core->live_count = 0;
        core->dead_bytes = 0;
        for (uint64_t i = 0; i < core->count; i++) {
            if (!core->entries[i].deleted) {
                core->live_count++;
            } else {
                core->dead_bytes += core->entries[i].size;
            }
        }
    }

    core->wal_count = 0;
    core->auto_save_sec = 0;

    return core;
}

void core_close(Core *core) {
    if (core->data_fd >= 0) close(core->data_fd);
    if (core->wal_fd >= 0) close(core->wal_fd);
    free(core->store);
    free(core);
}

int core_put(Core *restrict core, const void *data, uint64_t size) {
  if (core->live_count >= core->max_blob)
    return -1;
  if (core->store->used + size > core->data_size)
    return -1;

  // Reuse a deleted slot if one exists, otherwise append to the end
  uint64_t idx = core->count;
  for (uint64_t i = 0; i < core->count; i++) {
      if (core->entries[i].deleted) {
          idx = i;
          break;
      }
  }

  uint64_t addr = core->store->used;
  memcpy(core->store->data + addr, data, size);
  core->store->used += size;

  core->entries[idx].addr = addr;
  core->entries[idx].size = size;
  core->entries[idx].deleted = false;

  if (idx == core->count) core->count++;
  core->live_count++;

  // stream wrapper
  Task *restrict task = malloc(sizeof(Task));
  task->core = core;
  task->idx = idx;
  task->old_addr = 0;       // as wal_write_entry(...){}
  task->old_size = 0;
  task->new_addr = addr;
  task->new_size = size;
  task->is_old = false;

  pthread_t thread_id;
  pthread_create(&thread_id, NULL, wal_write_entry_pth, task);
  pthread_detach(thread_id);
  return idx;
}

void *core_get(Core *restrict core, uint64_t idx, uint64_t *size) {
  if (idx >= core->count)
      return NULL;
  if (core->entries[idx].deleted == true)
      return NULL;
  *size = core->entries[idx].size;
  return core->store->data + core->entries[idx].addr;
}


// simplified the function, added parameters to the Core structure.
void core_update(Core *restrict core, uint64_t idx, const void *data, uint64_t size) {
  if (idx >= core->count)
      return;
  if (core->entries[idx].deleted)
      return;

  uint64_t old_addr = core->entries[idx].addr;
  uint64_t old_size = core->entries[idx].size;

  uint64_t use = core->store->used;

  // write new entries at the tail
  if (use + size > core->data_size)
      return;
  uint64_t new_addr = core->store->used;
  memcpy(core->store->data + new_addr, data, size);
  core->store->used += size;

  core->entries[idx].addr = new_addr;
  core->entries[idx].size = size;

  // Old version is now wasted space
  core->dead_bytes += old_size;

  pthread_t stream_id;
  Task *restrict task = malloc(sizeof(Task));  // like core_put
  task->core = core;
  task->idx = idx;
  task->old_addr = old_addr;
  task->old_size = old_size;
  task->new_addr = new_addr;
  task->new_size = size;
  task->is_old = true;
  pthread_create(&stream_id, NULL, wal_write_entry_pth, task);
  pthread_detach(stream_id);

  // Auto-compact if wasted space exceeds 20% of total used
  if (core->dead_bytes > core->store->used / 5) {
      compact(core);
  }
  return;
}

void core_delete(Core *restrict core, uint64_t idx) {
  if (idx >= core->count)
      return;
  if (core->entries[idx].deleted)
      return;

  // Save old metadata before marking as deleted (for WAL)
  uint64_t old_addr = core->entries[idx].addr;
  uint64_t old_size = core->entries[idx].size;

  core->entries[idx].deleted = true;
  core->live_count--;
  core->dead_bytes += old_size; // Deleted blob is now wasted space

  pthread_t stream_id;
  Task *restrict task = malloc(sizeof(Task));
  task->core = core;
  task->idx = idx;
  task->old_addr = old_addr;
  task->old_size = old_size;
  task->new_addr = 0;       // 0 mean delete
  task->new_size = 0;
  task->is_old = true;
  pthread_create(&stream_id, NULL, wal_write_entry_pth, task);
  pthread_detach(stream_id);

  // Auto-compact if wasted space exceeds 20% of total used
  if (core->dead_bytes > core->store->used / 5) {
      compact(core);
  }
  return;
}

void core_save(Core *core) {
  if (core->data_fd < 0)
      return;

  pwrite(core->data_fd, &core->count, sizeof(uint64_t), 0);
  pwrite(core->data_fd, core->entries, sizeof(BEntry) * core->count, sizeof(uint64_t));
  pwrite(core->data_fd, core->store->data, core->store->used,
         sizeof(uint64_t) + sizeof(BEntry) * core->count);
  ftruncate(core->data_fd, sizeof(uint64_t) + sizeof(BEntry) * core->count +
            core->store->used);
  fsync(core->data_fd);

  core->wal_count = 0;
  if (core->wal_fd >= 0) ftruncate(core->wal_fd, 0);
  return;
}

static Core *auto_save_core = NULL;

static void auto_save_handler(int sig) {  // private function
    if (auto_save_core) {
        core_save(auto_save_core);
        alarm(auto_save_core->auto_save_sec);
  }
}

void core_auto_save(Core *core, int sec) {
    core->auto_save_sec = sec;
    auto_save_core = core;
    signal(SIGALRM, auto_save_handler);
    if (sec > 0)
        alarm(sec);
    else {
        abort();  // no please
    }
}

bool core_upload(Core *core, const char* main_wal, const char* sub_wal) {
    // TODO: Load another WAL into the main one
    return false;
}
