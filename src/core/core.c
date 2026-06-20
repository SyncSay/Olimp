#include "core.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>  // binsearch and etc...
#include <stddef.h>  //macro (NULL) and support type(size_t)
#include <string.h>
#include <signal.h>  //debug


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


}
