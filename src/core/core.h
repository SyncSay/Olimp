#pragma once

/*------------------------------------------------------
* Here I'm implementing:
* a blob key engine for the database
* writing/reading WAL logs
* storage structures
* a stack of blob addresses (shadow stack)
* auxiliary utilities

P.S.
Blob (from the English "Binary Large Object")
is a term denoting an array
of unstructured binary data stored as a single unit.
--------------------------------------------------------*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STANDARD_BLOB 32768
#define STANDARD_DATASIZE 6777216
#define STANDARD_WALSIZE 3886080


typedef struct {
  uint64_t used;
  uint8_t data[];  //you can use STANDART_DATASIZE
} BStore; // Blob Store

typedef struct {
    uint64_t addr; //offset blob
    uint64_t size;
    bool deleted;
} BEntry;

typedef struct {
  uint64_t index; // name blob
    uint64_t old_address;
    uint64_t old_size; // needed to delete and rewrite the WAL log
    uint64_t new_address;
    uint64_t new_size;
    bool is_old; // was the blob written
} WALEntry;


typedef struct {
    BStore* store;
    uint64_t count; // need for fast read and metrics
    uint64_t live_count; // active blob
    uint64_t wal_count;
    uint64_t dead_bytes;
    WALEntry wal[STANDARD_WALSIZE / sizeof(WALEntry)];

    // size data and blob
    uint64_t data_size;   // STANDARD_DATASIZE   this param is classic
    uint64_t max_blob;   // STANDATD_BLOB       and this

    int data_fd;
    int wal_fd;
    int auto_save_sec;

    BEntry entries[]; // you can use STANDARD_BLOB
} Core;

// -- continer for pthreads --
typedef struct {
    Core *core;
    uint64_t idx;
    uint64_t old_addr;
    uint64_t old_size;
    uint64_t new_addr;
    uint64_t new_size;
    bool is_old;
} Task;


//-- API --

Core *core_init(const char *data_path, const char *wal_path, uint64_t max_blob, uint64_t data_size);
void core_close(Core *core);
int core_put(Core *core, const void *data, uint64_t size); // retern blobs index
void *core_get(Core *core, uint64_t idx,
               uint64_t *size); // retern blobs pointer
void core_update(Core *core, uint64_t idx, const void *data, uint64_t size); // in data_size you can use STANDART_DATASIZE
void core_delete(Core *core, uint64_t idx);
void core_save(Core *core);
void core_auto_save(Core *core, int sec);
bool core_upload(Core *core, const char* main_wal, const char* sub_wal); // Load another WAL into the main one
