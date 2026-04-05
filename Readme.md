# Simulating a Simple Memory Allocator in C

## What does this memory allocator do?

1. Keeping a list of free memory locations inside the heap which can then be demanded for use.
2. Incase the the request for heap space exceeds the free space, then memory allocator asks the OS to grow the heap.
3. Be able to free the already occupied space from the heap.

