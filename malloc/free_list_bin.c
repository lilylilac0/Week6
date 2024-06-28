//
// >>>> malloc challenge! <<<<
//
// Your task is to improve utilization and speed of the following malloc
// implementation.
// Initial implementation is the same as the one implemented in simple_malloc.c.
// For the detailed explanation, please refer to simple_malloc.c.

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define N 4 // binの個数

//
// Interfaces to get memory pages from OS
//

void *mmap_from_system(size_t size);
void munmap_to_system(void *ptr, size_t size);

//
// Struct definitions
//

typedef struct my_metadata_t{
  size_t size;
  struct my_metadata_t *next;
} my_metadata_t;

typedef struct my_heap_t{
  my_metadata_t *free_head[N];
  my_metadata_t dummy;
} my_heap_t;

//
// Static variables (DO NOT ADD ANOTHER STATIC VARIABLES!)
//
my_heap_t my_heap;

//
// Helper functions (feel free to add/remove/edit!)
//

void my_add_to_free_list(my_metadata_t*, int);
void my_remove_from_free_list(my_metadata_t*, my_metadata_t*, int);
int get_bin_index(size_t);
void merge_free_blocks(int);

// Add a free slot to the beginning of the free list.
void my_add_to_free_list(my_metadata_t *metadata, int bin){
  assert(!metadata->next);
  metadata->next = my_heap.free_head[bin];
  my_heap.free_head[bin] = metadata;
}

// Remove a free slot from the free list.
void my_remove_from_free_list(my_metadata_t *metadata, my_metadata_t *prev, int bin){
  if (prev){
    prev->next = metadata->next;
  }
  else{
    my_heap.free_head[bin] = metadata->next;
  }
  metadata->next = NULL;
}

//sizeからどのbinに格納するか計算し、binのindexを返す。
int get_bin_index(size_t size){
  for (int i = 0; i <= N - 1; i++){
    size_t bin_size[N+1] = {0, 64, 256, 1024, 4096};
    if((bin_size[i] <= size) && (size < bin_size[i + 1])){
      return i;
    }
  }
  return N-1;
}

//右側が空き領域であれば結合
//受け取ったbinのリスト全要素に対して行う
//my_free()の中で使う
void merge_free_blocks(int bin){
  my_metadata_t *current = my_heap.free_head[bin]; //ここで引数のmetadataはこの時点でfree_list[bin]の先頭
  my_metadata_t *next_metadata = (my_metadata_t*)((char*)current + sizeof(my_metadata_t) + current->size);
    if (next_metadata == current->next){
      current->size += sizeof(my_metadata_t) + next_metadata->size;
      current->next = next_metadata->next;
    }
}

//
// Interfaces of malloc (DO NOT RENAME FOLLOWING FUNCTIONS!)
//

// This is called at the beginning of each challenge.
void my_initialize(){
  for (int i = 0; i <= N - 1; i++){
    my_heap.free_head[i] = &my_heap.dummy;
  }
  my_heap.dummy.size = 0;
  my_heap.dummy.next = NULL;
}

// my_malloc() is called every time an object is allocated.
// |size| is guaranteed to be a multiple of 8 bytes and meets 8 <= |size| <=
// 4000. You are not allowed to use any library functions other than
// mmap_from_system() / munmap_to_system().
void *my_malloc(size_t size){
  int bin = get_bin_index(size);
  my_metadata_t *metadata = my_heap.free_head[bin];
  my_metadata_t *best_metadata = NULL;
  my_metadata_t *prev = NULL;
  my_metadata_t *best_prev = NULL;

  // Best-fit
  while(1){
    while (metadata){
      if (metadata->size >= size &&
          (best_metadata == NULL || metadata->size < best_metadata->size)){
        best_prev = prev;
        best_metadata = metadata;
      }
      prev = metadata;
      metadata = metadata->next;
   }

   if(!best_metadata && bin < N-1){//現在のbinに空きがなければもう一つ大きいbinへgo!
      bin++;
      metadata = my_heap.free_head[bin];
      prev = NULL;
   }
   else break;
  }
  

  metadata = best_metadata;
  prev = best_prev;

  // now, metadata points to the first free slot
  // and prev is the previous entry.

  if (!metadata){

    // There was no free slot available. We need to request a new memory region
    // from the system by calling mmap_from_system().
    //
    //     | metadata | free slot |
    //     ^
    //     metadata
    //     <---------------------->
    //            buffer_size
    size_t buffer_size = 4096;
    //fprintf(stderr, "Doing mmap for bin [%d]\n", bin);
    my_metadata_t *metadata = (my_metadata_t *)mmap_from_system(buffer_size);
    metadata->size = buffer_size - sizeof(my_metadata_t);
    metadata->next = NULL;
    // Add the memory region to the free list.
    my_add_to_free_list(metadata, bin);
    // Now, try my_malloc() again. This should succeed.
    return my_malloc(size);
  }

  // |ptr| is the beginning of the allocated object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  void *ptr = metadata + 1;
  size_t remaining_size = metadata->size - size;
  // Remove the free slot from the free list.
  my_remove_from_free_list(metadata, prev, bin);

  if (remaining_size > sizeof(my_metadata_t)){
    // Shrink the metadata for the allocated object
    // to separate the rest of the region corresponding to remaining_size.
    // If the remaining_size is not large enough to make a new metadata,
    // this code path will not be taken and the region will be managed
    // as a part of the allocated object.
    metadata->size = size;
    // Create a new metadata for the remaining free slot.
    //
    // ... | metadata | object | metadata | free slot | ...
    //     ^          ^        ^
    //     metadata   ptr      new_metadata
    //                 <------><---------------------->
    //                   size       remaining size
    my_metadata_t *new_metadata = (my_metadata_t *)((char *)ptr + size);
    new_metadata->size = remaining_size - sizeof(my_metadata_t);
    new_metadata->next = NULL;
    // Add the remaining free slot to the free list.
    my_add_to_free_list(new_metadata, bin);
  }
  return ptr;
}

// This is called every time an object is freed.  You are not allowed to
// use any library functions other than mmap_from_system / munmap_to_system.
void my_free(void *ptr){
  // Look up the metadata. The metadata is placed just prior to the object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  my_metadata_t *metadata = (my_metadata_t *)ptr - 1;
  int bin = get_bin_index(metadata->size);
  // Add the free slot to the free list.
  my_add_to_free_list(metadata, bin);
  merge_free_blocks(bin);
}

void print_free_list_sizes() {
  printf("Free list sizes:\n");
  for (int i = 0; i < N; i++) {
    int count = 0;
    my_metadata_t *current = my_heap.free_head[i];
    while (current != &my_heap.dummy && current != NULL) {
      count++;
      current = current->next;
    }
    printf("Bin %d: %d elements\n", i, count);
  }
  printf("\n");
}

// This is called at the end of each challenge.
void my_finalize(){
  // Nothing is here for now.
  // feel free to add something if you want!
  print_free_list_sizes();
}

void test(){
  // Implement here!
  assert(1 == 1); /* 1 is 1. That's always true! (You can remove this.) */
  char *a = malloc(16);
  char *b = malloc(16);
  //assert((char*)a + 16 + sizeof(my_metadata_t) == (char*)b);
  free(b);
  free(a);
  //print_free_list(); // free_listをprintする関数を別で作っておく
  print_free_list_sizes();
}

