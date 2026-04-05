#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>

char *heap_start = NULL;

const int INIT_BYTES = 0x55;
const uint8_t BLOCK_MARKER = 0xDD;
const int PAGE_SIZE = 0x1000;

typedef struct master {
    uint8_t marker;
    bool lock;
    uint32_t block_count;
    uint32_t page_count;
    int init_bytes;
} header;

typedef struct block {
    uint8_t marker;
    uint32_t length;
    bool in_use;
    struct block *prev;
    struct block *next;
} heap_block;

header *get_malloc_header() {
    assert(heap_start != NULL);
    header *head = (header *)heap_start;
    assert(head->init_bytes == INIT_BYTES);
    return head;
}

heap_block *get_last_block() {
    heap_block *block = (heap_block *)((char *)heap_start + sizeof(header));
    while (block->next != NULL) {
        block = block->next;
    }
    return block;
}

heap_block *get_prev_used_block(heap_block *ptr) {
    while (ptr && ptr->in_use == false && ptr->prev != NULL) {
        ptr = ptr->prev;
    }
    return ptr;
}

void malloc_init() {
    if (heap_start == NULL) {
        heap_start = (char *)sbrk(0);
        sbrk(PAGE_SIZE);
    }

    header *malloc_header = (header *)heap_start;

    if (malloc_header->init_bytes != INIT_BYTES) {
        malloc_header->init_bytes = INIT_BYTES;
        malloc_header->block_count = 1;
        malloc_header->page_count = 1;
        malloc_header->lock = false;

        heap_block *first_block = (heap_block *)((char *)heap_start + sizeof(header));
        first_block->marker = BLOCK_MARKER;
        first_block->length = PAGE_SIZE - sizeof(header) - sizeof(heap_block);
        first_block->in_use = false;
        first_block->prev = NULL;
        first_block->next = NULL;
    }
}

void *m_alloc(size_t size) {
    size = (size + 7) & ~7;

    malloc_init();
    header *malloc_header = get_malloc_header();

    while (malloc_header->lock) {
        sleep(1);
    }
    malloc_header->lock = true;

    heap_block *block = (heap_block *)((char *)heap_start + sizeof(header));
    heap_block *smallest_block = NULL;

    while (block != NULL) {
        if (block->marker == BLOCK_MARKER && !block->in_use && block->length >= size) {
            if (smallest_block == NULL || block->length < smallest_block->length) {
                smallest_block = block;
            }
        }
        block = block->next;
    }

    if (smallest_block == NULL) {
        heap_block *last = get_last_block();
        while (last->length < size) {
            sbrk(PAGE_SIZE);
            last->length += PAGE_SIZE;
            malloc_header->page_count++;
        }
        smallest_block = last;
    }

    smallest_block->in_use = true;

    int remaining = smallest_block->length - size - sizeof(heap_block);

    if (remaining > 0) {
        heap_block *new_block = (heap_block *)((char *)smallest_block + sizeof(heap_block) + size);
        new_block->marker = BLOCK_MARKER;
        new_block->length = remaining;
        new_block->in_use = false;
        new_block->next = smallest_block->next;
        new_block->prev = smallest_block;

        if (new_block->next)
            new_block->next->prev = new_block;

        smallest_block->next = new_block;
        smallest_block->length = size;
        malloc_header->block_count++;
    }

    malloc_header->lock = false;

    return (void *)((char *)smallest_block + sizeof(heap_block));
}

void reduce_heap_size() {
    heap_block *last_block = get_last_block();
    heap_block *prev_used = get_prev_used_block(last_block);

    if (prev_used == NULL) {
        prev_used = last_block;
    }

    void *new_end = (void *)((char *)prev_used + sizeof(heap_block) + prev_used->length);
    void *heap_end = sbrk(0);

    header *malloc_header = get_malloc_header();

    while ((char *)new_end < (char *)heap_end - PAGE_SIZE) {
        sbrk(-PAGE_SIZE);
        heap_end = sbrk(0);
        malloc_header->page_count--;
    }
}

bool m_free(void *ptr) {
    header *malloc_header = get_malloc_header();

    while (malloc_header->lock) {
        sleep(1);
    }
    malloc_header->lock = true;

    heap_block *block = (heap_block *)((char *)ptr - sizeof(heap_block));

    if (block->marker != BLOCK_MARKER) {
        malloc_header->lock = false;
        return false;
    }

    block->in_use = false;
    memset(ptr, 0, block->length);

    if (block->next && !block->next->in_use) {
        heap_block *next = block->next;
        block->length += sizeof(heap_block) + next->length;
        block->next = next->next;
        if (block->next)
            block->next->prev = block;
        malloc_header->block_count--;
    }

    if (block->prev && !block->prev->in_use) {
        heap_block *prev = block->prev;
        prev->length += sizeof(heap_block) + block->length;
        prev->next = block->next;
        if (prev->next)
            prev->next->prev = prev;
        malloc_header->block_count--;
    }

    reduce_heap_size();

    malloc_header->lock = false;
    return true;
}

/* ================= TESTING ================= */

void call_test(void (*test_func)(), const char *msg) {
    pid_t pid = fork();
    if (pid == 0) {
        test_func();
        exit(0);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            printf("%s crashed with signal %d\n", msg, WTERMSIG(status));
        } else {
            printf("%s passed\n", msg);
        }
    }
}

void test_basic_malloc() {
    char *ptr = (char *)m_alloc(1);
    heap_block *first_block = (void *)ptr - sizeof(heap_block);
    assert(first_block->marker == BLOCK_MARKER);
    *ptr = 'C';
    assert(*ptr == 'C');
}

void test_bigger_than_available_malloc() {
    uint16_t *ptr = (uint16_t *)m_alloc(5000);
    heap_block *first_block = (void *)ptr - sizeof(heap_block);

    for (uint16_t i = 0; i <= 2499; i++) {
        *(ptr + i) = i;
    }

    assert(first_block->marker == BLOCK_MARKER);
    assert(*ptr == 0);
    assert(*(ptr + 2) == 2);
    assert(*(ptr + 2499) == 2499);

    assert(*((uint8_t *)ptr + 4999) == (2499 >> 8));
    assert(*((uint8_t *)ptr + 4998) == (2499 & 0xFF));
}

void test_free() {
    uint8_t *first = (uint8_t *)m_alloc(2048);
    heap_block *first_block = (void *)first - sizeof(heap_block);

    assert(first_block->next != NULL);
    assert(first_block->length == 2048);

    heap_block *second_block = first_block->next;
    assert(second_block->marker == BLOCK_MARKER);
    assert(second_block->in_use == false);
    assert(second_block->next == NULL);

    m_free(first);

    assert(first_block->marker == BLOCK_MARKER);
    assert(first_block->next == NULL);
}

int main() {
    call_test(test_basic_malloc, "Basic Malloc");
    call_test(test_bigger_than_available_malloc, "Request more memory Malloc");
    call_test(test_free, "Basic Free");
    return 0;
}