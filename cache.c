#include "csapp.h"
#include <pthread.h>
#include "cache.h"

cache_block *cache_pool[MAX_BLOCK_NUM];
static int cur_time = 1;                       //Not strictly IRU, so we do not protect on this variable
sem_t global_write_sem;                        //Only allow one writer access the cache!
void cache_init(){
    int i;
    for(i = 0; i < MAX_BLOCK_NUM; i++){
        cache_pool[i] = (cache_block*)malloc(sizeof(cache_block));
        cache_pool[i]->bytes = 0;
        cache_pool[i]->uri[0] = 0;
        cache_pool[i]->buf = NULL;
        Sem_init(&(cache_pool[i]->reader_sem), 0, 1);
        Sem_init(&(cache_pool[i]->reader_writer_sem), 0, 1);
        cache_pool[i]->reader_cnt = 0;
        cache_pool[i]->last_visit = 0;
    }
    Sem_init(&global_write_sem, 0, 1);
}

cache_block *cache_exist(char *uri){
    int i;
    cur_time++;

    for(i = 0; i < MAX_BLOCK_NUM; i++){
        cache_wait_read(cache_pool[i]);
        // now perform reading        
        if(strcmp(cache_pool[i]->uri, uri) == 0){
            // found the block
            cache_pool[i]->last_visit = cur_time;
            printf("cache found! in %d block: %s\n", i, cache_pool[i]->uri);
            return cache_pool[i];
        }
        
        // not the block want
        cache_read_done(cache_pool[i]);
    }
    
    printf("no cache\n");
    return NULL;
}

void cache_store(char* uri, char *buf_store, int bytes_store){
    int i, find_i;
    P(&global_write_sem);

    cur_time++;
    int max_last = cur_time;
    for(i = 0; i < MAX_BLOCK_NUM; i++){
        if(cache_pool[i]->last_visit <= max_last){
            max_last = cache_pool[i]->last_visit;
            find_i = i;
        }
    }

    // Now performing write on find_i
    P(&(cache_pool[find_i]->reader_writer_sem));

    // if the buf is not NULL, free it
    if(cache_pool[find_i]->buf != NULL){
        free(cache_pool[find_i]->buf);
        printf("evicting %s\n",cache_pool[find_i]->uri);
    }
    // set content of buf 
    cache_pool[find_i]->buf = buf_store;

    // copy uri    
    strcpy(cache_pool[find_i]->uri, uri);
    printf("saving %s\n",uri);

    // set number of bytes    
    cache_pool[find_i]->bytes = bytes_store;
    // update last visit
    cache_pool[find_i]->last_visit = cur_time;

    V(&(cache_pool[find_i]->reader_writer_sem));
    
    V(&global_write_sem);
}

void cache_wait_read(cache_block *cache_entry){
    P(&(cache_entry->reader_sem));
    if(cache_entry->reader_cnt == 0){
        // currently no reader, wait for writer
        P(&(cache_entry->reader_writer_sem));
    }
    cache_entry->reader_cnt++;
    V(&(cache_entry->reader_sem));    
}

void cache_read_done(cache_block *cache_entry){
    P(&(cache_entry->reader_sem));
    cache_entry->reader_cnt--;
    if(cache_entry->reader_cnt == 0){
        V(&(cache_entry->reader_writer_sem));
    }
    V(&(cache_entry->reader_sem));
}


