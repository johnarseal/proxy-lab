/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_BLOCK_NUM 21
#define MAX_URI_LEN 256

#ifndef STRUCT_CACHE_DEFINE
#define STRUCT_CACHE_DEFINE
typedef struct cache_block cache_block;

struct cache_block{
    int bytes;                                  // How many bytes of data in the block
    char uri[MAX_URI_LEN];                      // the uri this block is caching
    char *buf;                                  // the buffer it is pointing to
    int reader_cnt;                             // number of reader
    sem_t reader_sem;                           // sem to protect reader_cnt
    sem_t reader_writer_sem;                    // sem to protect the whole block
    int last_visit;                             // record last visit time
    cache_block *next;                          // next block in the list
};


#endif

void cache_init();
cache_block *cache_exist(char *uri);
void cache_store(char* uri, char *buf_store, int bytes_store);
void cache_read_done(cache_block *cache_entry);
void cache_wait_read(cache_block *cache_entry);
