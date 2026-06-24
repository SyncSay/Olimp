#include "core.h"
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

void wal_write_entry(Core *core, uint64_t index, uint64_t old_addr,
                      uint64_t olde_size, uint64_t new_addr, uint64_t new_size) {
    WALEntry *w = &core->wal[core->count++];
    w->index = index;
    w->old_addres = old_addr;
    w->old_size = olde_size;
    w->new_addres = new_addr;
    w->new_size = new_size;

    uint64_t fd = core->wal_fd;

    if (fd >= 0) {
        write(fd, w, sizeof(WALEntry));
        fsync(fd);
    } else {
        abort(); }   // overflow or division by 0 (usually for integers or floats, but this works too)
}

// Pthread func wal_write
void *wal_write_entry_pth(void *arg) {
    Task *T = (Task *)arg;
    wal_write_entry(T->core, T->idx, T->old_addr, T->old_size, T->new_addr,
                    T->old_size);
    free(T);
    return NULL;
}

static void apply_wal(Core *core, WALEntry *wal) {

    uint64_t idx = wal->index;
    uint64_t count = core->count;

    if (idx >= count) {
        count = idx+1;
    }
    core->entries[idx].addr = wal->new_addres;
    core->entries[idx].size = wal->new_size;
}


// -- API --
void core_init(Core *core, const char *data_path, const char *wal_path) {
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
            core->store.used =
                st.st_size - sizeof(uint64_t) - sizeof(BEntry) * core->count;
            pread(core->data_fd, core->store.data, core->store.used,
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
        perror("error with wal file descriptor!");
    }
}

int core_put(Core *core, const void *data, uint64_t size, uint64_t max_blob, uint64_t data_size) {
  if (core->count >= max_blob)
    return -1; // You can insert STANDART_BLOB instead of max_blob
  if (core->store.used >= data_size)
    return -1; // You can insert STANDART_DATASIZE instead of data_size

  uint64_t addr = core->store.used;
  memcpy(core->store.data + addr, data, size);
  core->store.used += size;

  uint64_t idx = core->count;
  core->entries[idx].addr = addr;
  core->entries[idx].size = size;
  core->count++;

  pthread_t thread_id;


}
