#include "core.h"
#include <bits/pthreadtypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>  //macro (NULL) and support type(size_t)
#include <string.h>
#include <signal.h> //debag
#include<pthread.h>


// work with file
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


// -- support func --


//    uint64_t index;
//    uint64_t old_addres;
//    uint64_t old_size;
//    uint64_t new_addres;
//    uint64_t new_size;
//    bool is_old;
//    WALEntry;

void wal_write_entry(Core *restrict core, uint64_t index, uint64_t old_addr,
                      uint64_t olde_size, uint64_t new_addr, uint64_t new_size, bool is_old) {
    WALEntry *w = &core->wal[core->count++];
    w->index = index;
    w->old_addres = old_addr;
    w->old_size = olde_size;
    w->new_addres = new_addr;
    w->new_size = new_size;
    w->is_old = is_old;

    uint64_t fd = core->wal_fd;

    if (fd >= 0) {
        write(fd, w, sizeof(WALEntry));
        fsync(fd);
    } else {
        abort(); }   // overflow or division by 0 (usually for integers or floats, but this works too)
}

// Pthread func wal_write
void* wal_write_entry_pth(void *arg) {
    Task *task = (Task *)arg;
    wal_write_entry(task->core, task->idx, task->old_addr, task->old_size, task->new_addr,
                    task->old_size, task->is_old);
    free(task);
    return NULL;
}

static void apply_wal(Core *restrict core, WALEntry *restrict wal) {

    uint64_t idx = wal->index;
    uint64_t count = core->count;

    if (idx >= count) {
        count = idx+1;
    }
    core->entries[idx].addr = wal->new_addres;
    core->entries[idx].size = wal->new_size;
}


// -- API --
void core_init(Core *restrict core, const char *restrict data_path, const char *wal_path) {
    memset(core, 0, sizeof(Core));

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
            pread(core->data_fd, &core->entries, sizeof(BEntry) * core->count,
                  sizeof(uint64_t));
            core->store->used =
                st.st_size - sizeof(uint64_t) - sizeof(BEntry) * core->count;
            pread(core->data_fd, core->store->data, core->store->used,
                  sizeof(uint64_t) + sizeof(BEntry) * core->count);
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

        ftruncate(core->wal_fd, 0); //clean WAL
    }

    core->wal_count = 0;
    core->auto_save_sec = 0;
}

void core_close(Core *core) {
    core_save(core);
    if (core->data_fd >= 0) {
        close(core->data_fd); }
    else {
      perror("error with data file descriptor!");
    };
    if (core->wal_fd >= 0) {
        close(core->wal_fd);
    } else {
        perror("error with WAL file descriptor!");
    }
}

int core_put(Core *restrict core, const void *data, uint64_t size, uint64_t max_blob, uint64_t data_size) {
  if (core->count >= max_blob)
    return -1; // You can insert STANDART_BLOB instead of max_blob
  if (core->store->used >= data_size)
    return -1; // You can insert STANDART_DATASIZE instead of data_size

  uint64_t addr = core->store->used;
  memcpy(core->store->data + addr, data, size);
  core->store->used += size;

  uint64_t idx = core->count;
  core->entries[idx].addr = addr;
  core->entries[idx].size = size;
  core->count++;

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
  *size = core->entries[idx].addr;
  return core->store->data + core->entries[idx].addr;
}

void core_update(Core *restrict core, uint64_t idx, const void *data, uint64_t size, uint64_t data_size) {
  if (idx >= core->count)
      return;

  uint64_t old_addr = core->entries[idx].addr;
  uint64_t old_size = core->entries[idx].size;

  uint64_t use = core->store->used;

  // write new entries at the tail
  if (+size > data_size)
      return;
  uint64_t new_addr = core->store->used;
  memcpy(core->store->data + new_addr, data, size);
  core->store->used += size;

  core->entries[idx].addr = new_addr;
  core->entries[idx].size = size;

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
  return;
}

void core_delete(Core *restrict core, uint64_t idx) {
  if (idx >= core->count)
      return;
  pthread_t stream_id;
  if (idx != core->count - 1) {
      core->entries[idx] = core->entries[core->count - 1];
      Task *restrict task = malloc(sizeof(Task));
      task->core = core;
      task->idx = idx;
      task->old_addr = core->entries[core->count - 1].addr;
      task->old_size = core->entries[core->count - 1].size;
      task->new_addr = core->entries[core->count - 1].addr;
      task->new_size = core->entries[core->count - 1].size;
      task->is_old = true;
      pthread_create(&stream_id, NULL, wal_write_entry_pth, task);
      pthread_detach(stream_id);
  }
  core->count--;
  Task *restrict task = malloc(sizeof(Task));
  task->core = core;
  task->idx = core->count;
  task->old_addr = 0;
  task->old_size = 0;
  task->new_addr = 0;
  task->new_size = 0;
  task->is_old = false;
  pthread_create(&stream_id, NULL, wal_write_entry_pth, task);
  pthread_detach(stream_id);
  return;
}
