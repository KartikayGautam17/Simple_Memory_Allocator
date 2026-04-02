#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>

int main(){
    
}

typedef struct master {
    uint8_t marker;
    bool lock;
};

typedef struct block {
    uint8_t marker;
    uint32_t length;
    bool in_use; // for determining whether the block is in use or it is free
    struct block *prev;
    struct block *next;
};

