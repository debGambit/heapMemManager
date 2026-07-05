#include <stdio.h>
#include <memory.h>
#include <unistd.h>   /*for getpagesize*/
#include <sys/mman.h> /* for using mmap() */
#include "mm.h"

static size_t SYSTEM_PAGE_SIZE = 0;
static  vm_page_for_families_t *first_vm_page_for_families = NULL;
void
mm_init() {
    SYSTEM_PAGE_SIZE = getpagesize();
}

/* Function to request VM page from kernel*/
static void *
mm_get_new_vm_page_from_kernel(int units) {
    char *vm_page = mmap(
        NULL,
        units*SYSTEM_PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_ANON | MAP_PRIVATE,
        0,
        0
    );

    if(vm_page == MAP_FAILED) {
        printf("Error : VM Page allocation failed\n");
        return NULL;
    }

    memset(vm_page, 0, units*SYSTEM_PAGE_SIZE);
    return (void *)vm_page;
}

/*Function to return a page to kernel*/
static void
mm_return_vm_page_to_kernel(void *vm_page, int units) {
    if(munmap(vm_page, units*SYSTEM_PAGE_SIZE)) {
        printf("Error : Could not return VM page to kernel\n");
    }
}

void mm_instantiate_new_page_family(
    char *struct_name, 
    uint32_t struct_size) {
    
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *new_vm_page_for_families = NULL;

    if(struct_size > SYSTEM_PAGE_SIZE) {
        printf("Error : %s() Structure %s size exceedssystem page size\n", 
            __FUNCTION__, struct_name);
        return;
    }

    if(!first_vm_page_for_families) {
        first_vm_page_for_families = 
            (vm_page_for_families_t *) mm_get_new_vm_page_from_kernel(1);
        first_vm_page_for_families->next = NULL;
        strncpy(first_vm_page_for_families->vm_page_family[0].struct_name,
        struct_name, MM_MAX_STRUCT_NAME);
        first_vm_page_for_families->vm_page_family[0].struct_size = struct_size;
        return;
    }
    uint32_t count = 0;
    ITERATE_PAGE_FAMILIES_BEGIN(first_vm_page_for_families, vm_page_family_curr){
        if(strncmp(vm_page_family_curr->struct_name,
            struct_name, MM_MAX_STRUCT_NAME) != 0) {
            count++;
            continue;
        }
        assert(0 && "Error : Structure already registered");
    } ITERATE_PAGE_FAMILIES_END(first_vm_page_for_families, vm_page_family_curr);

    if(count == MAX_FAMILIES_PER_VM_PAGE) {
        new_vm_page_for_families =
            (vm_page_for_families_t*) mm_get_new_vm_page_from_kernel(1);
        new_vm_page_for_families->next = first_vm_page_for_families;
        first_vm_page_for_families = new_vm_page_for_families;
        vm_page_family_curr = &first_vm_page_for_families->vm_page_family[0];
        /*
        strncpy(first_vm_page_for_families->vm_page_family[0].struct_name,
        struct_name, MM_MAX_STRUCT_NAME);
        first_vm_page_for_families->vm_page_family[0].struct_size = struct_size;
        */
    }
    strncpy(vm_page_family_curr->struct_name, struct_name,
        MM_MAX_STRUCT_NAME);
    vm_page_family_curr->struct_size = struct_size;
    return;
}

void
mm_print_registered_page_families() {
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *vm_page_for_families_curr = NULL;

    if(!first_vm_page_for_families) {
        printf("Error : No page families registered\n");
        return;
    }

    for(vm_page_for_families_curr = first_vm_page_for_families;
            vm_page_for_families_curr;
            vm_page_for_families_curr = vm_page_for_families_curr->next){

        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_curr, vm_page_family_curr){
            printf("Page Family : %s, Size = %u\n", vm_page_family_curr->struct_name, vm_page_family_curr->struct_size);
        } ITERATE_PAGE_FAMILIES_END(vm_page_for_families_curr, vm_page_family_curr);

    }

}

vm_page_family_t *
lookup_page_family_by_name (char *struct_name) {
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *vm_page_for_families_curr = NULL;

     if(!first_vm_page_for_families) {
        printf("Error : No page families registered\n");
        return NULL;
    }

    for(vm_page_for_families_curr = first_vm_page_for_families;
            vm_page_for_families_curr;
            vm_page_for_families_curr = vm_page_for_families_curr->next){

        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_curr, vm_page_family_curr){
            if(strncmp(vm_page_family_curr->struct_name,
                struct_name, MM_MAX_STRUCT_NAME) == 0) {
                return vm_page_family_curr;
            }
        } ITERATE_PAGE_FAMILIES_END(vm_page_for_families_curr, vm_page_family_curr);

    }

    return NULL;
}

void mm_audit_meta_blocks(block_meta_data_t *first_meta_block) {
    if (!first_meta_block) {
        return;
    }

    block_meta_data_t *curr = NULL;
    uint32_t free_block_count = 0;
    uint32_t allocated_block_count = 0;
    
    block_meta_data_t *largest_free_block = NULL;
    uint32_t largest_free_size = 0;
    
    block_meta_data_t *largest_alloc_block = NULL;
    uint32_t largest_alloc_size = 0;
    
    vm_bool_t prev_is_free = MM_FALSE;

    for (curr = first_meta_block; curr != NULL; curr = NEXT_META_BLOCK(curr)) {
        if (curr->is_free == MM_TRUE) {
            assert(prev_is_free == MM_FALSE && "Error: Two consecutive free blocks found.");
            free_block_count++;
            
            if (curr->block_size > largest_free_size) {
                largest_free_size = curr->block_size;
                largest_free_block = curr;
            }
            prev_is_free = MM_TRUE;
        } else {
            allocated_block_count++;
            
            if (curr->block_size > largest_alloc_size) {
                largest_alloc_size = curr->block_size;
                largest_alloc_block = curr;
            }
            prev_is_free = MM_FALSE;
        }
    }

    printf("Audit Complete:\n");
    printf("  Free Blocks: %u (Largest: %u bytes at %p)\n", 
           free_block_count, largest_free_size, (void*)largest_free_block);
    printf("  Allocated Blocks: %u (Largest: %u bytes at %p)\n", 
           allocated_block_count, largest_alloc_size, (void*)largest_alloc_block);
}

static void 
mm_union_free_blocks
    (block_meta_data_t *first,
    block_meta_data_t *second) {
        assert(first->is_free == MM_TRUE && second->is_free == MM_TRUE && "Error : Both blocks must be free to merge.");
        first->next_block = second->next_block;
        if(first->next_block) {
            first->next_block->prev_block = first;
        }
        first->block_size += sizeof(block_meta_data_t) + second->block_size;
}


