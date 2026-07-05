#ifndef __MM__
#define __MM__

#include <stdint.h>    // for uint32_t
#include <string.h>    // for memset
#include <assert.h>    // for assert
#include <stddef.h>    /*for size_t*/

typedef enum {
    MM_FALSE = 0,
    MM_TRUE = 1
} vm_bool_t;

typedef struct block_meta_data_ {
	vm_bool_t is_free;  /*Free or allocated*/
	uint32_t block_size; /* Size of block */
    uint32_t offset;
	struct block_meta_data_ *prev_block;
		/* ptr to prev meta block downward in Data VM page */
	struct block_meta_data_ *next_block;
		/*ptr to next block upward in Data VM page */ 
} block_meta_data_t;

/*Forward Declaration*/
struct vm_page_family_;

#define offset_of(container_structure, field_name) \
    (size_t)&(((container_structure *)0)->field_name)

#define MM_GET_PAGE_FROM_META_BLOCK(block_meta_data_ptr)    \
    ((void *)((char *)block_meta_data_ptr - block_meta_data_ptr->offset))

#define NEXT_META_BLOCK(block_meta_data_ptr)    \
    (block_meta_data_ptr->next_block)

#define NEXT_META_BLOCK_BY_SIZE(block_meta_data_ptr) \
    ((block_meta_data_t *)((char *)(block_meta_data_ptr + 1) + block_meta_data_ptr->block_size))

#define PREV_META_BLOCK(block_meta_data_ptr)    \
    (block_meta_data_ptr->prev_block)

#define mm_bind_blocks_for_allocation(allocated_meta_block, free_meta_block)  \
{                                                                           \
    free_meta_block->prev_block = allocated_meta_block;                     \
    free_meta_block->next_block = allocated_meta_block->next_block;         \
    allocated_meta_block->next_block = free_meta_block;                     \
    if(free_meta_block->next_block) {                                       \
        free_meta_block->next_block->prev_block = free_meta_block;          \
    }                                                                       \                                                              \
} 

#define MM_MAX_STRUCT_NAME 32  // max length

typedef struct vm_page_family_{
    char struct_name[MM_MAX_STRUCT_NAME];
    uint32_t struct_size;
} vm_page_family_t;

typedef struct vm_page_for_families_{
    struct vm_page_for_families_ *next;
    vm_page_family_t vm_page_family[0];
} vm_page_for_families_t;

void
mm_init();

#define MAX_FAMILIES_PER_VM_PAGE  \
    (SYSTEM_PAGE_SIZE - sizeof(vm_page_for_families_t)) / sizeof(vm_page_family_t)

#define ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_ptr, curr)         \
{                                                                           \
    uint32_t count = 0;                                                     \
    for(curr = (vm_page_family_t *)&vm_page_for_families_ptr->vm_page_family[0];     \
        curr->struct_size &&  count < MAX_FAMILIES_PER_VM_PAGE;             \
        curr++,count++ ) {

#define ITERATE_PAGE_FAMILIES_END(vm_page_for_families_ptr, curr)       }}

vm_page_family_t *
lookup_page_family_by_name (char *struct_name);

void 
mm_audit_meta_blocks(block_meta_data_t *first_meta_block);

#endif
