#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

// This next bunch of stuff makes it so that by default, the functions are
// compiled with their normal names, like "malloc"... *but*, they can
// be given alternate names (e.g., "my_malloc").  If you want to actually
// replace the default heap manager with your own, you want to use the
// default names.  But for testing purposes, it's a much better idea
// to leave the defaults in place and name yours something else.
#ifndef MALLOC
  #define MALLOC malloc
#endif
#ifndef FREE
  #define FREE free
#endif
#ifndef REALLOC
  #define REALLOC realloc
#endif
#ifndef CALLOC
  #define CALLOC calloc
#endif
#ifndef REALLOCARRAY
  #define REALLOCARRAY reallocarray
#endif


// Blocks always start with this, and the data follows.
typedef struct {
  size_t size;
  int is_used;
} BlockHeader;

// "Constants" for the BlockHeader.is_used field.
#define BLOCK_FREE 0
#define BLOCK_USED 1

// Block data should always start on an address that is a multiple of this.
#define ALIGN_BYTES 8

// We'll set this to point to the start of the first BlockHeader.
BlockHeader * first_block = NULL;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

// Given a pointer, return a pointer to the byte offset bytes after it
// (or before it, if offset is negative).
static void * offset_ptr (void * ptr, ssize_t offset)
{
  // We want byte-based pointer arithmetic, so cast to a byte pointer.
  uint8_t * p = ptr;
  return p + offset;
}

// Round up num so that it's evenly divisible by sz.
static size_t round_up (size_t num, size_t sz)
{
  size_t remainder = num % sz;
  if (remainder == 0) return num;
  return num + sz - remainder;
}

// Merge block b with the following block if the following block is empty.
// After merging, it should try to merge again (with new *new* next block).
static void try_merge (BlockHeader * b)
{
  while (true)
  {
    // Get a pointer to the next block after b.
    BlockHeader * nb = (BlockHeader *)offset_ptr(b, b->size);

    // If the block is not free, we can't merge -- we're done.  Return.
    if (nb->is_used == BLOCK_USED){
      return;
    }

    // Update b's size since it merged with nb.
    b->size += nb->size;
  }
}

// Go through each block, trying to merge every free one (by calling try_merge()).
static void try_merge_all ()
{
  BlockHeader * b = first_block;

  // Loop until you reach the sentinel.
  while (b->size != 0) // Until the sentinel
  {
    // If b is free, try to merge it with the next block.
    if (b->is_used == BLOCK_FREE){
      try_merge(b);
    }
    // Set b to point at the next block.
    b = (BlockHeader *)offset_ptr(b, b->size);
  }
}

// Possibly split the block in two so that the first block has a size of sz
// and the second block contains the leftovers.  If the second block would
// be smaller than sizeof(BlockHeader)+24 bytes, don't bother splitting.
// sz should be an even multiple of ALIGN_BYTES.
static void try_split (BlockHeader * b, size_t sz)
{
  // If the desired size is greater/equal to the existing block size, we
  // should not split the block!  Handle this case.
  if (sz >= b->size){
    return;
  }

  // If we split, how much leftover space is there?
  size_t leftover = b->size - sz;

  // If there isn't enough leftover space to be worth splitting, just return.
  if (leftover  < sizeof(BlockHeader)+24){
    return;
  }

  // Set nb to point where the new block header will be.
  BlockHeader * nb = (BlockHeader *)offset_ptr(b, sz);

  // Set up the new header
  nb-> size = round_up(leftover, ALIGN_BYTES); //ensuring that size of new block w/ header is aligned
  nb->is_used = BLOCK_FREE;

  // Adjust the size of b
  b->size = sz;
}

// Expand the program break just large enough for a sentinel and then set up
// that new memory as a sentinel (size=0 used=1).
static void add_sentinel ()
{
  // We're going to add the sentinel just past the program break.
  // Get a pointer to there.
  BlockHeader * b = sbrk(0);

  // Use sbrk() to push the break forward far enough for the sentinel.
  b = sbrk(sizeof(BlockHeader));

  // Now b is pointing at valid memory to be used by the sentinel.  Set up
  // b properly!
  b->size = 0; 
  b->is_used = 1;
}

// Find the last block before the sentinel.  If it's unused, shrink the heap
// by giving that  memory back to the OS.  Do that by setting the program
// break back down (calling sbrk() with a negative value).  Don't forget
// to re-add the sentinel!
static void try_release_memory ()
{
  BlockHeader * prev = NULL;
  BlockHeader * b = first_block;
  while (b->size != 0)
  {
    prev = b;
    b = (BlockHeader *)offset_ptr(b, b->size);
  }
  if (!prev) return;
  if (prev->is_used) return;
  sbrk( -(prev->size + sizeof(BlockHeader)) );
  add_sentinel();
}


// If the heap hasn't been initialized, initialize it.  Do this by setting
// first_block to start at the first block, and make that first block a
// sentinel.  You'll have to push the program break forward to fit the
// sentinel, and you'll want to make sure that it is aligned on an address
// that's a multiple of ALIGN_BYTES.
static void heap_init ()
{
  if (first_block) return; // Already initialized

  // We want our first block to be aligned such that it's evenly divisible
  // by ALIGN_BYTES, so we get the initial program break using sbrk(0) and
  // put it in a uintptr_t, which is just an integer type large enough to
  // store any address.
  // If it's not evenly divisible by ALIGN_BYTES, move the break up so that
  // it is.
  // Note: sbrk(X) adjusts the program break by pushing it X bytes higher
  // (or lower if X is negative).  It returns the old program break before
  // adjustment.  sbrk(0) therefore just gets the current break.

  uintptr_t initial_break = (uintptr_t)sbrk(0);
  if (initial_break % ALIGN_BYTES)
  {
    // It's not on an even multiple.  Use sbrk() to push it forward until
    // it is.
    uintptr_t alignmentDiff = initial_break % ALIGN_BYTES; //calculate difference from alignment
    uintptr_t alignedBytes = ALIGN_BYTES - alignmentDiff; //check difference needed for alignment
    initial_break = (uintptr_t)sbrk(alignedBytes); //push forward for alignment
  }

  // sbrk(0) returns current program break without adjusting it.
  first_block = sbrk(0); // Now definitely divisible by ALIGN_BYTES

  // The last thing you should do is add the sentinel.  You'll have to
  // implement add_sentinel()!
  add_sentinel();
}


// ---------------------------------------------------------------------------
//  Heap interface functions
// ---------------------------------------------------------------------------

void * MALLOC (size_t sz)
{
  heap_init(); // Make sure our heap is initialized

  // We're given the requested size of the data portion of a block.  However,
  // we are more interested in the size of the entire block (including the
  // header).  Furthermore, we want the block size to be an even multiple of
  // ALIGN_BYTES so that the next header has natural alignment.  Thus, we
  // adjust sz here (adding the header size and rounding up if necessary).
  sz = round_up(sz+sizeof(BlockHeader), ALIGN_BYTES);

  // Search through the list of blocks looking for free ones.
  // If you find a free one that's at least sz bytes long, you've found your
  // block!  Set it as used, and then try to split it by calling try_split()
  // (which you'll have to go up and implement!).  Then return a pointer to
  // the block's data array (right after the header).
  BlockHeader * b = first_block;

  while (b->size != 0) // Until the sentinel
  {
    if (b->is_used == BLOCK_FREE && b-> size >= sz)
    {
      // If this block is big enough, use it and try to split it.  Then
      // return the pointer to the data portion of the block (just
      // after the header).
      b->is_used = BLOCK_USED;
      try_split(b,sz);
      return offset_ptr(b, sizeof(BlockHeader));
    }

    // Set b to point at the start of the next block.
    b = (BlockHeader *)offset_ptr(b, b->size);
  }

  // If you got here, you didn't find a block to use.  Create a new block.

  // First check that we are overwriting the sentinel.
  ASSERT(b->size == 0);
  ASSERT(b->is_used == BLOCK_USED);

  // The old sentinel becomes our new block header; set the size.  It's
  // already marked as used!
  b->size = sz;

  // Now expand the program break using sbrk() to make room for the new
  // block's data after its header.
  sbrk(sz-sizeof(BlockHeader));

  // Add a new sentinel.
  add_sentinel();

  // Return pointer to the block's data; remember it is *after* the header!
  return offset_ptr(b, sizeof(BlockHeader));
}


void FREE (void * ptr)
{
  // Special case -- free(NULL) is legal and does nothing.
  if (!ptr) return;

  // We are given the pointer to the *data* portion of a block.  We want the
  // block's header.  We know that's right before the data, so we can get it
  // by just offsetting the pointer.
  BlockHeader * b = offset_ptr(ptr, -sizeof(BlockHeader));

  // Double check that the block is marked as used!
  ASSERT(b->is_used == BLOCK_USED);

  // Mark the block as free
  b->is_used = BLOCK_FREE;

  // Try to merge blocks (you'll have to finish try_merge_all()!).
  try_merge_all();

  // Try to release memory back (you'll have to finish try_release_memory()!).
  try_release_memory();
}


void * REALLOC (void * ptr, size_t sz)
{
  // Special case -- if ptr is NULL, this is equivalent to malloc().
  if (ptr == NULL) return MALLOC(sz);

  // Special case -- if sz is 0, this is equivalent to free().
  if (sz == 0) { FREE(ptr); return NULL; }

  // Keep track of original sz in case it's useful later
  size_t orig_sz = sz;

  // Adjust sz so it includes the header size and is a multiple of
  // ALIGN_BYTES so that the *following* block will be aligned.
  sz = round_up(sz + sizeof(BlockHeader), ALIGN_BYTES);

  // Set b to the BlockHeader associated with ptr
  BlockHeader * b = (BlockHeader *)offset_ptr(ptr, -sizeof(BlockHeader));

  // Are we trying to grow the allocation?
  if (sz > b->size)
  {
    // We want b to be bigger.  So try to merge it with the next block (which
    // will only work if the next block is free).
    try_merge(b);

    // Now try to split b so that it's just sz.  The idea is that if merging
    // was successful, b may now be bigger than we need, and this will take
    // care of that.
    try_split(b, sz);

    // Check to see if b is big enough.  If so, we're done!  Return!
    if (b->size >= sz){
      return ptr;
    }

    // If we got here, we couldn't grow the existing block, and need to
    // allocate a new block, copy the contents of the current block to the
    // new one, free the old one, and return the new one.
    void * new_ptr = MALLOC(sz-sizeof(BlockHeader));
    memcpy(new_ptr, ptr, orig_sz);
    FREE(ptr);

    return new_ptr;
  }

  // We are trying to shrink the allocation (or it's not changing size).

  // Try to split the block.
  // If it split, you may be able to merge, so try that.
  // If it split, you may be able to release memory, so try that.
  try_split(b, sz);
  try_merge_all();
  try_release_memory();

  // Whether we split or not, the user's data hasn't moved.  Return ptr.
  return ptr;
}


// Once you implement malloc(), this should just work.
void * CALLOC (size_t nmemb, size_t size)
{
  // We actually are supposed to check for overflow, but don't.
  void * ptr = MALLOC(nmemb * size);
  if (!ptr) return NULL;
  memset(ptr, 0, size);
  return ptr;
}


// Once you implement realloc(), this should just work.
void * REALLOCARRAY (void * ptr, size_t nmemb, size_t size)
{
  // We actually are supposed to check for overflow, but don't.
  return REALLOC(ptr, nmemb * size);
}
