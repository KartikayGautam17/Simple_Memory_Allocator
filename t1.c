#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include<unistd.h>

int main(){
    
}

char * heap_start = NULL;

const int INIT_BYTES = 0x55;
const int BLOCK_MARKER = 0xDD;

typedef struct master {
    uint8_t marker;
    bool lock;
    uint32_t block_count;
};

typedef struct block {
    uint8_t marker;
    uint32_t length;
    bool in_use; // for determining whether the block is in use or it is free
    struct block *prev;
    struct block *next;
};

typedef struct block heap_block;
typedef struct master header;

int * malloc_init (size_t size){
    if (heap_start == NULL){
        heap_start = sbrk(0);
        sbrk(4096);
    }
    char * heap_end = sbrk(0);
    long int length = heap_end - heap_start;
    if (*heap_start != INIT_BYTES){
        *heap_start = INIT_BYTES;
        header * malloc_header = (header * ) heap_start;
        malloc_header -> block_count = 1;
        heap_block * first_block = (heap_block *) ((char *) heap_start + sizeof(header));
        first_block -> marker = BLOCK_MARKER;
        first_block -> length = length - sizeof(header) - sizeof(heap_block);
        first_block -> prev = NULL;
        first_block -> next = NULL;
    }
}