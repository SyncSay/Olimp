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

#define STANDART_BLOB 32768
#define STANDART_DATASIZE (16 * 1024 ** 2)
#define STANDART_WALSIZE (10 * 1024 * 1024 * 8)


typedef struct {
  uint64_t used;
  uint8_t data[];  //you can use STANDART_DATASIZE
} BStore; // Blob Store

typedef struct {
    uint64_t addr; //offset blob
    uint64_t size;
} BEntry;

typedef struct {
  uint64_t index; // name blob
    uint64_t old_addres;
    uint64_t old_size; // needed to delete and rewrite the WAL log
    uint64_t new_addres;
    uint64_t new_size;
    bool is_old; // was the blob written
} WALEntry;


typedef struct {
    BStore store;
    uint64_t count; // need for fast read and metrics
    uint64_t wal_count;
    WALEntry wal[STANDART_WALSIZE / sizeof(WALEntry)];

    int data_fd;
    int wal_fd;
    int auto_save_sec;

    BEntry entries[]; // you can use STANDART_BLOB
} Core;

//--Allocator--

//-- API --

void core_init(Core *core, const char *data_path, const char *wal_path);
void core_close(Core *core);
int core_put(Core *core, const void *data, uint64_t size); // retern blobs index
void *core_get(Core *core, uint64_t idx,
               uint64_t *size); // retern blobs pointer
void core_update(Core *core, uint64_t idx, const void *data, uint64_t size);
void core_delete(Core *core, uint64_t idx);
void core_save(Core *core);
void core_auto_save(Core *core, int sec);
bool core_upload(Core *core, const char* main_wal, const char* sub_wal); // Load another WAL into the main one
