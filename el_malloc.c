// el_malloc.c: implementation of explicit list malloc functions.

#include "el_malloc.h"

// Global control functions

// Global control variable for the allocator. Must be initialized in
// el_init().
el_ctl_t *el_ctl = NULL;

// Create an initial block of memory for the heap using
// mmap(). Initialize the el_ctl data structure to point at this
// block. The initializ size/position of the heap for the memory map
// are given in the argument symbol and EL_HEAP_START_ADDRESS.
// Initialize the lists in el_ctl to contain a single large block of
// available memory and no used blocks of memory.
int el_init(uint64_t initial_heap_size){
  el_ctl =
    mmap(EL_CTL_START_ADDRESS,
         EL_PAGE_BYTES,
         PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS,
         -1, 0);
  assert(el_ctl == EL_CTL_START_ADDRESS);

  void *heap = 
    mmap(EL_HEAP_START_ADDRESS,
         initial_heap_size,
         PROT_READ | PROT_WRITE,
         MAP_PRIVATE | MAP_ANONYMOUS,
         -1, 0);
  assert(heap == EL_HEAP_START_ADDRESS);

  el_ctl->heap_bytes = initial_heap_size;    // make the heap as big as possible to begin with
  el_ctl->heap_start = heap;                 // set addresses of start and end of heap
  el_ctl->heap_end   = PTR_PLUS_BYTES(heap,el_ctl->heap_bytes);

  if(el_ctl->heap_bytes < EL_BLOCK_OVERHEAD){
    fprintf(stderr,"el_init: heap size %ld to small for a block overhead %ld\n",
            el_ctl->heap_bytes,EL_BLOCK_OVERHEAD);
    return 1;
  }
 
  el_init_blocklist(&el_ctl->avail_actual);
  el_init_blocklist(&el_ctl->used_actual);
  el_ctl->avail = &el_ctl->avail_actual;
  el_ctl->used  = &el_ctl->used_actual;

  // establish the first available block by filling in size in
  // block/foot and null links in head
  size_t size = el_ctl->heap_bytes - EL_BLOCK_OVERHEAD;
  el_blockhead_t *ablock = el_ctl->heap_start;
  ablock->size = size;
  ablock->state = EL_AVAILABLE;
  el_blockfoot_t *afoot = el_get_footer(ablock);
  afoot->size = size;

  // Add initial block to availble list; avoid use of list add
  // functions in case those are buggy which will screw up the heap
  // initialization
  ablock->prev = el_ctl->avail->beg;
  ablock->next = el_ctl->avail->beg->next;
  ablock->prev->next = ablock;
  ablock->next->prev = ablock;
  el_ctl->avail->length++;
  el_ctl->avail->bytes += (ablock->size + EL_BLOCK_OVERHEAD);

  return 0;
}

// Clean up the heap area associated with the system which unmaps all
// pages associated with the heap.
void el_cleanup(){
  munmap(el_ctl->heap_start, el_ctl->heap_bytes);
  munmap(el_ctl, EL_PAGE_BYTES);
}

////////////////////////////////////////////////////////////////////////////////
// Pointer arithmetic functions to access adjacent headers/footers

// Compute the address of the foot for the given head which is at a
// higher address than the head.
el_blockfoot_t *el_get_footer(el_blockhead_t *head){
  size_t size = head->size;
  el_blockfoot_t *foot = PTR_PLUS_BYTES(head, sizeof(el_blockhead_t) + size);
  return foot;
}

// Compute the address of the head for the given foot which is at a
// lower address than the foot.
el_blockhead_t *el_get_header(el_blockfoot_t *foot){
  size_t size = foot->size;
  el_blockhead_t *head = PTR_MINUS_BYTES(foot, sizeof(el_blockhead_t) + size);
  return head;
}

// Returns a pointer to the block that is one block higher in memory
// from the given block, or NULL if the block above would be off the heap.
// DOES NOT follow next pointer, looks in adjacent memory.
el_blockhead_t *el_block_above(el_blockhead_t *block){
  el_blockhead_t *higher =
    PTR_PLUS_BYTES(block, block->size + EL_BLOCK_OVERHEAD);
  if((void *) higher >= (void*) el_ctl->heap_end){
    return NULL;
  }
  else{
    return higher;
  }
}

// Returns a pointer to the block that is one block lower in memory
// from the given block, or NULL if the block below would be outside the heap.
// DOES NOT follow block->next pointer, looks in adjacent memory
el_blockhead_t *el_block_below(el_blockhead_t *block){
  // Get foot of previous block
  el_blockfoot_t *prev_footer = PTR_MINUS_BYTES(block, sizeof(el_blockfoot_t));
  // If foot not in heap, return NULL
  if ((void *) prev_footer <= (void *) el_ctl->heap_start) {
    return NULL;
  }
  // Find head using pointer arithmetic
  el_blockhead_t *lower = PTR_MINUS_BYTES(block, prev_footer->size + EL_BLOCK_OVERHEAD);
  return lower;
}

// Block list operations
// Print an entire blocklist. The format appears as follows.
//
// {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//
// Note that the '@' column uses the actual address of items which
// relies on a consistent mmap() starting point for the heap.
void el_print_blocklist(el_blocklist_t *list){
  printf("{length: %3lu  bytes: %5lu}\n", list->length,list->bytes);
  el_blockhead_t *block = list->beg;
  for(int i=0; i<list->length; i++){
    printf("  ");
    block = block->next;
    printf("[%3d] head @ %p ", i, block);
    printf("{state: %c  size: %5lu}\n", block->state,block->size);
  }
}

// Print a single block during a sequential walk through the heap
void el_print_block(el_blockhead_t *block){
  el_blockfoot_t *foot = el_get_footer(block);
  printf("%p\n", block);
  printf("  state:      %c\n", block->state);
  printf("  size:       %lu (total: 0x%lx)\n", block->size, block->size+EL_BLOCK_OVERHEAD);
  printf("  prev:       %p\n", block->prev);
  printf("  next:       %p\n", block->next);
  printf("  user:       %p\n", PTR_PLUS_BYTES(block,sizeof(el_blockhead_t)));
  printf("  foot:       %p\n", foot);
  printf("  foot->size: %lu\n", foot->size);
}

// Print all blocks in the heap in the order that they appear from
// lowest addrses to highest address
void el_print_heap_blocks(){
  int i = 0;
  el_blockhead_t *cur = el_ctl->heap_start;
  while(cur != NULL){
    printf("[%3d] @ ",i);
    el_print_block(cur);
    cur = el_block_above(cur);
    i++;
  }
}  

// Print out stats on the heap for use in debugging. Shows the
// available and used list along with a linear walk through the heap blocks.
void el_print_stats(){
  printf("HEAP STATS (overhead per node: %lu)\n",EL_BLOCK_OVERHEAD);
  printf("heap_start:  %p\n",el_ctl->heap_start); 
  printf("heap_end:    %p\n",el_ctl->heap_end); 
  printf("total_bytes: %lu\n",el_ctl->heap_bytes);
  printf("AVAILABLE LIST: ");
  el_print_blocklist(el_ctl->avail);
  printf("USED LIST: ");
  el_print_blocklist(el_ctl->used);
  printf("HEAP BLOCKS:\n");
  el_print_heap_blocks();
}

// Initialize the specified list to be empty. Sets the beg/end
// pointers to the actual space and initializes those data to be the
// ends of the list.  Initializes length and size to 0.
void el_init_blocklist(el_blocklist_t *list){
  list->beg        = &(list->beg_actual); 
  list->beg->state = EL_BEGIN_BLOCK;
  list->beg->size  = EL_UNINITIALIZED;
  list->end        = &(list->end_actual); 
  list->end->state = EL_END_BLOCK;
  list->end->size  = EL_UNINITIALIZED;
  list->beg->next  = list->end;
  list->beg->prev  = NULL;
  list->end->next  = NULL;
  list->end->prev  = list->beg;
  list->length     = 0;
  list->bytes      = 0;
}  

// Adds a block to the front of list and adjusts block and list links
// Length is incremented and the bytes for the list are
// updated to include the new block's size and its overhead.
void el_add_block_front(el_blocklist_t *list, el_blockhead_t *block){
  block->prev = list->beg;
  block->next = list->beg->next;
  block->prev->next = block;
  block->next->prev = block;
  list->length += 1;
  list->bytes += block->size + EL_BLOCK_OVERHEAD;
}

// Unlink block from the list it is in
// Updates the length and bytes for that list including overhead
void el_remove_block(el_blocklist_t *list, el_blockhead_t *block){
  block->prev->next = block->next;
  block->next->prev = block->prev;
  list->length -= 1;
  list->bytes -= block->size + EL_BLOCK_OVERHEAD;
}

// Allocation-related functions

// Finds the first block in the available list with block size of at
// least `size` and returns a pointer to the found block or NULL if no
// block of sufficient size is available.
el_blockhead_t *el_find_first_avail(size_t size){
  el_blocklist_t *avail = el_ctl->avail;
  el_blockhead_t *curr = avail->beg->next;
  while (curr != avail->end) {
    if (curr->size >= size) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

// Set the given block to the given size and add a footer to
// it. Creates another block above it by creating a new header and
// assigning it the remaining space and updates existing metadata
// Returns a pointer to the newly created block. Does not do any 
// linking of blocks nor changes of list membership. Returns NULL if
// the parameter block does not have sufficient size for a split
// without making any changes or creating a new block.
el_blockhead_t *el_split_block(el_blockhead_t *block, size_t new_size){
  // Check if splittable (enough room for overhead)
  if (block->size < new_size + EL_BLOCK_OVERHEAD) {
    return NULL;
  }
  // Create new head for new block after the new foot and set size to old size - new_size - overhead
  el_blockhead_t *new_block = PTR_PLUS_BYTES(block, new_size + EL_BLOCK_OVERHEAD);
  new_block->size = block->size - new_size - EL_BLOCK_OVERHEAD;
  // Update size for current block to new_size
  block->size = new_size;
  // Create new foot for current block and set size to new_size
  el_blockfoot_t *new_foot = el_get_footer(block);
  new_foot->size = block->size;
  // Get old foot and set size to the size of new block
  el_blockfoot_t *old_foot = el_get_footer(new_block);
  old_foot->size = new_block->size;
  return new_block;
}

// Returns a pointer to a block of memory with at least the given size
// for use by the user, or NULL if no space is available. The pointer 
// returned is to the usable space, not the block header. Makes use of 
// find_first_avail() to find the block and el_split_block() to split it.
void *el_malloc(size_t nbytes){
  // Find first available block, return NULL if none found
  el_blockhead_t *avail = el_find_first_avail(nbytes);
  if (avail == NULL) {
    return NULL;
  }
  // Remove block from available list
  el_remove_block(el_ctl->avail, avail);
  // Split the block
  el_blockhead_t *split = el_split_block(avail, nbytes);
  // Set state of current block to used and add to front of used list
  avail->state = EL_USED;
  el_add_block_front(el_ctl->used, avail);
  // If split succeeded, set state of new block to available and add to available list
  if (split != NULL) {
    split->state = EL_AVAILABLE;
    el_add_block_front(el_ctl->avail, split);
  }
  // Return ptr to memory of first block in used list
  // el_ctl->used = used list, ->beg = dummy node, ->next = head of first block
  el_blockhead_t *ptr = PTR_PLUS_BYTES(el_ctl->used->beg->next, sizeof(el_blockhead_t));
  return ptr;
}

// De-allocation/free() related functions

// Attempt to merge the block lower with the next block in memory.
// Does nothing if lower is null or in use or if the next higher block
// is null (because lower is the last block) or not EL_AVAILABLE.
// Otherwise, locates the next block with el_block_above(), merges these 
// two into a single block, and adjusts size and pointer metadata accordingly. 
// Removes both lower and higher from the available list and re-adds lower 
// to the front of the available list
void el_merge_block_with_above(el_blockhead_t *lower){
  // Check current block to see if valid
  if (lower == NULL || lower->state != EL_AVAILABLE) {
    return;
  }
  // Get higher block and check if valid
  el_blockhead_t *above = el_block_above(lower);
  if (above == NULL || above->state != EL_AVAILABLE) {
    return;
  }
  // If both are valid, remove from the list
  el_remove_block(el_ctl->avail, lower);
  el_remove_block(el_ctl->avail, above);
  // Add size of above block + head + foot
  lower->size += above->size + EL_BLOCK_OVERHEAD;
  // Update size in higher block's foot to match
  el_blockfoot_t *foot = el_get_footer(above);
  foot->size = lower->size;
  // Add block back to available list
  el_add_block_front(el_ctl->avail, lower);
}

// Free the block pointed to by the given ptr. 
// Attempts to merge the free'd block with adjacent
// blocks using el_merge_block_with_above(). If called on a NULL
// pointer or the block is not in state EL_USED, prints an error
// and returns immediately without further action.
void el_free(void *ptr){
  // If ptr is null print error and return
  if (ptr == NULL) {
    printf("ERROR: el_free() not called on an EL_USED block\n");
    return;
  }
  // If state of block is not used print error and return
  el_blockhead_t *head = PTR_MINUS_BYTES(ptr, sizeof(el_blockhead_t));
  if (head->state != EL_USED) {
    printf("ERROR: el_free() not called on an EL_USED block\n");
    return;
  }
  // Remove block from used list, add to available list, and set state as available
  el_remove_block(el_ctl->used, head);
  el_add_block_front(el_ctl->avail, head);
  head->state = EL_AVAILABLE;
  // merge with block above; if lower block exists, merge lower block with current block
  el_merge_block_with_above(head);
  el_blockhead_t *below = el_block_below(head);
  if (below != NULL) {
    el_merge_block_with_above(below);
  }
}

// HEAP EXPANSION FUNCTIONS

// Attempts to append pages of memory to the heap with mmap(). npages
// is how many pages are to be appended with total bytes to be
// appended as npages * EL_PAGE_BYTES. Calls mmap() with the address
// of the pages to be at heap_end so that the heap grows
// contiguously. If this fails, prints an error and returns 1.
// Otherwise, adjusts heap size and end for the expanded heap.
// Creates a new block for the freshly allocated pages
// that is added to the available list. Also attempts to merge this
// block with the block below it. Returns 0 on success.
int el_append_pages_to_heap(int npages){
  // mmap given size of memory at heap_end
  void *appended_pages = mmap(el_ctl->heap_end, npages * EL_PAGE_BYTES,
  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  // If failed to map at requested address, print and return error
  if (appended_pages != el_ctl->heap_end) {
    printf("ERROR: Unable to mmap() additional %d pages\n", npages);
    return 1;
  }
  // update heap bytes
  el_ctl->heap_bytes += npages * EL_PAGE_BYTES;
  // Create new head at end of current heap / start of new allocation
  el_blockhead_t *head = el_ctl->heap_end;
  // Update end of heap to end of new allocation
  el_ctl->heap_end = PTR_PLUS_BYTES(el_ctl->heap_end, npages * EL_PAGE_BYTES);
  // Create foot at the updated end of heap
  el_blockfoot_t *foot = PTR_MINUS_BYTES(el_ctl->heap_end, sizeof(el_blockfoot_t));
  // Set size of block head and foot to allocated amount - overhead
  head->size = npages * EL_PAGE_BYTES - EL_BLOCK_OVERHEAD;
  foot->size = head->size;
  // Set state to available and add block to available list
  head->state = EL_AVAILABLE;
  el_add_block_front(el_ctl->avail, head);
  // Attempt to merge with block below
  el_blockhead_t *below = el_block_below(head);
  el_merge_block_with_above(below);
  // Return 0 for success
  return 0;
}