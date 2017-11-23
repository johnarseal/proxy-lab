#include "csapp.h"
#include <pthread.h>
#include "cache.h"

cache_block *cache_first_block;               //the header of the linked list
static int cur_time = 1;                       //Not strictly IRU, so we do not protect on this variable
sem_t global_write_sem;                        //Only allow one writer access the cache!
static int cur_cache_size;

cache_block *cache_block_init(){
    cache_block *cache_entry = (cache_block*)malloc(sizeof(cache_block));
    cache_entry->bytes = 0;
    cache_entry->uri[0] = 0;
    cache_entry->buf = NULL;
    Sem_init(&(cache_entry->reader_sem), 0, 1);
    Sem_init(&(cache_entry->reader_writer_sem), 0, 1);
    cache_entry->reader_cnt = 0;
    cache_entry->last_visit = 0;
    cache_entry->next = NULL;
    return cache_entry;
}

void cache_init(){
    Sem_init(&global_write_sem, 0, 1);
    cache_first_block = cache_block_init();
    cur_cache_size = 0;
}

cache_block *cache_exist(char *uri){
    cur_time++;

    cache_block *cur_block = cache_first_block;
    while(cur_block != NULL){
        cache_wait_read(cur_block);
        // now perform reading        
        if(strcmp(cur_block->uri, uri) == 0){
            // found the block
            cur_block->last_visit = cur_time;
            printf("cache found! %s\n", cur_block->uri);
            return cur_block;
        }
        
        // not the block want
        cache_read_done(cur_block);
        cur_block = cur_block->next;
    }
    
    printf("no cache\n");
    return NULL;
}
void fill_cache_block(cache_block *cache_entry, char* uri, char *buf_store, int bytes_store){
    // Now performing write on find_i
    P(&(cache_entry->reader_writer_sem));

    // if the buf is not NULL, free it
    if(cache_entry->buf != NULL){
        free(cache_entry->buf);
        cur_cache_size -= cache_entry->bytes;
        printf("evicting %s\n",cache_entry->uri);
    }
    // set content of buf 
    cache_entry->buf = buf_store;

    // copy uri    
    strcpy(cache_entry->uri, uri);
    printf("saving %s\n",uri);

    // set number of bytes    
    cache_entry->bytes = bytes_store;
    cur_cache_size += bytes_store;
    // update last visit
    cache_entry->last_visit = cur_time;

    V(&(cache_entry->reader_writer_sem));   

}
void cache_store(char* uri, char *buf_store, int bytes_store){
    P(&global_write_sem);

    cur_time++;
    cache_block *cur_block = cache_first_block;
    cache_block *found_block = cur_block;

    printf("cur cache size:%d\n",cur_cache_size);
    // Now I am the only writer
    if(cur_cache_size < MAX_CACHE_SIZE){
        // if current size is smaller than MAX_CACHE_SIZE, directly append at the last
        while(cur_block->next != NULL){
            cur_block = cur_block->next;
        }
        cur_block->next = cache_block_init();
        found_block = cur_block->next;
    }
    else{
        // if current size is bigger than MAX_CACHE_SIZE, evict one
        int max_last = cur_time;
        while(cur_block != NULL){
            if(cur_block->last_visit <= max_last){
                max_last = found_block->last_visit;
                found_block = cur_block;
            }
            cur_block = cur_block->next;
        }
    }
    fill_cache_block(found_block, uri, buf_store, bytes_store);
    
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


