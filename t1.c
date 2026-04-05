#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include<unistd.h>
#include<assert.h>
#include<string.h>

int main(){
    
}

char * heap_start = NULL;

const int INIT_BYTES = 0x55;
const int BLOCK_MARKER = 0xDD;
const int PAGE_SIZE = 0x1000;

typedef struct master {
    uint8_t marker;
    bool lock;
    uint32_t block_count;
    uint32_t page_count;
    int init_bytes;
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


header * get_malloc_header(){
    assert(heap_start != NULL);
    header * head = (header *) heap_start;
    assert(head -> init_bytes == INIT_BYTES);
    return head;

};


heap_block * get_last_block(){
    heap_block * malloc_header = heap_start;
    heap_block * block = (heap_block *) (char *) heap_start + sizeof(heap_block);
    while (block -> next != NULL){
        block = block -> next;
    }
    return block;

};
heap_block * get_prev_used_block(heap_block *ptr){
    while (ptr -> in_use == false && ptr->prev != NULL){
        ptr = ptr->prev;
    }
    return ptr;
}

int * malloc_init (size_t size){
    if (heap_start == NULL){
        heap_start = (char *) sbrk(0);
        sbrk(4096);
    }
    char * heap_end = (char * )sbrk(0);
    long int length = heap_end - heap_start;
    if (*heap_start != INIT_BYTES){
        *heap_start = INIT_BYTES;
        header * malloc_header = (header * ) heap_start;
        malloc_header -> block_count = 1;
        malloc_header -> page_count = 1;
        heap_block * first_block = (heap_block *) ((char *) heap_start + sizeof(header)); //using char as it represents one byte 
        // and makes it easier for offsets
        first_block -> marker = BLOCK_MARKER;
        first_block -> length = length - sizeof(header) - sizeof(heap_block);
        first_block -> prev = NULL;
        first_block -> next = NULL;
    }
}

int * m_alloc(size_t size) {
    header * malloc_header = get_malloc_header();
    while (malloc_header -> lock) { sleep(1);}
    malloc_header -> lock = true;
    //the above lines are added for multithreading so that only one block is in use at a time;
    heap_block * block = (heap_block *) ((char *) heap_start + sizeof(header));
    heap_block * smallest_block = NULL;
    heap_block * last_block = block;
    while (block != NULL){
        assert(block->marker == BLOCK_MARKER);{ //asserting that block marker is valid and we are not pointing to garbage
            
            //Using Best-Fit algorithm for assignning the data
            if (block->in_use == false && (block->length + sizeof(header)) >= size ){
                if (smallest_block == NULL || smallest_block -> length > block->length){
                    smallest_block = block;
                }
            }
        }
    }

    //Condition 1: No block of suitable size is available
    //Find the last block, then increase the heap as required by the size
    if (smallest_block == NULL){
        heap_block * last_block = get_last_block();
        while (last_block -> length < size){
            sbrk(4096);
            last_block -> length += 4096;
            malloc_header -> page_count += 1;
            
        }
        smallest_block = last_block;
    }
    smallest_block -> in_use = true;
    
    //Creating a new free block of leftover space after the above operation, (splitting the smallest block)
    int required_size = smallest_block->length - size - sizeof(heap_block) - 1; 
    if (required_size <= 0){
        sbrk(4096); //grow heap incase leftover space is not enough
        malloc_header->page_count += 1;
        last_block->length += 4096;
        int required_size = smallest_block->length - size - sizeof(heap_block) - 1; //recalculate
    }
    int remaining_size = required_size + 1;
    
    malloc_header -> block_count+= 1;
    
    //updating the double linked list 
    heap_block * new_block = (heap_block *)((char *) smallest_block + sizeof(heap_block) + size);
    new_block->length = remaining_size;
    new_block->marker = BLOCK_MARKER;
    new_block->next = smallest_block->next;
    new_block->prev=smallest_block;
    if (new_block->next!=NULL) new_block ->next->prev=new_block;
    smallest_block->next = new_block;

    malloc_header->lock = false; //we are done for now
    return (int *) ((char*) smallest_block+sizeof(heap_block));
}

bool m_free(void * ptr){
    header * malloc_header = get_malloc_header();
    while (malloc_header -> lock) {
        sleep(1);
    }
    malloc_header->lock = true;
    
    //we want to point at the metadata area of the block
    heap_block * block = (char *) ptr - sizeof(heap_block);

    //if not a valid block
    if (block -> marker != BLOCK_MARKER) {
        return false;
    }
    block->in_use = false;
    // ptr = points to the content
    // block = points to the metadata of the block

    // |metadata|content.........| -> |metadata_of2nextblock|content_of2ndblock| ->....|

    memset(ptr,0,block->length);
    if (block -> next != NULL && block->next->in_use == false){
        heap_block * empty_block = block -> next;
        block -> next = empty_block -> next;
        if (block -> next!= NULL){
            block -> next -> prev = block;
        }
        else {
            block -> next = NULL; // already implied but explicitly assigning
        }
        block -> length +=  (uint32_t ) sizeof(heap_block) + empty_block -> length;
        
        //erasing the header of the block we merged
        memset((void *) empty_block,0,sizeof(heap_block));
        malloc_header->block_count -=1;
    }

    //merging previous block if it is empty
    if (block -> prev != NULL && block->prev->in_use == false){
        heap_block * current_block = block;
        block = block -> prev;
        block -> length += sizeof(heap_block) + current_block->length;
        block -> next = current_block -> next;
        if (block -> next != NULL){
            block -> next -> prev = block;
        }
        memset((void *) current_block,0,sizeof(heap_block));
        malloc_header ->block_count -= 1;
    }
    reduce_heap_size();
    malloc_header->lock = false;
    return true;
}

void reduce_heap_size(){
    heap_block * last_block = get_last_block();
    heap_block * prev_used_block = get_prev_used_block(last_block);

    if (prev_used_block == NULL){
        //only 1 block is there
        if (prev_used_block -> length > PAGE_SIZE){
            prev_used_block->length = PAGE_SIZE;
        }
        prev_used_block = last_block;
    }

    //if there is a difference between actual end of the heap and calculated end of the heap, then we can do this
    void * new_end = (void *) prev_used_block + sizeof(heap_block) + prev_used_block->length;
    void * heap_end = sbrk(0);
    header * malloc_header = get_malloc_header();
    while (new_end < heap_end - PAGE_SIZE){
        sbrk(-PAGE_SIZE);
        heap_end = sbrk(0);
        malloc_header -> block_count -= 1;
    }

    //if there is a some gap so that we can prepare a block in end
    if (heap_end - new_end > sizeof(heap_block) + 1){
        heap_block *free_block = (heap_block *) new_end;
        free_block -> in_use = false;
        free_block -> length = heap_end - new_end - sizeof(heap_block);
        free_block -> marker = BLOCK_MARKER;
        free_block -> next = NULL;
        free_block -> prev = last_block;
        last_block -> next = free_block;
    }
}