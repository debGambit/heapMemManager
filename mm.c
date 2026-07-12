#include <stdio.h>
#include <memory.h>
#include <unistd.h>   /*for getpagesize*/
#include <sys/mman.h> /* for using mmap() */
#include <errno.h>
#include "mm.h"
#include "css.h"

static size_t SYSTEM_PAGE_SIZE = 0;
static vm_page_for_families_t *first_vm_page_for_families = NULL;
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

static inline uint32_t
mm_max_page_allocatable_memory (int units) {
    return (uint32_t)(units*SYSTEM_PAGE_SIZE - offset_of(vm_page_t, page_memory));
}

#define MAX_PAGE_ALLOCATABLE_MEMORY(units) \
    (mm_max_page_allocatable_memory(units))

vm_page_t *
allocate_vm_page (vm_page_family_t *vm_page_family) {
    vm_page_t *vm_page = (vm_page_t *) mm_get_new_vm_page_from_kernel(1);
    /*Initialize lower most META block of the VM page*/
    MARK_VM_PAGE_EMPTY(vm_page);
    vm_page->block_meta_data.block_size = mm_max_page_allocatable_memory(1);
    vm_page->block_meta_data.offset = offset_of(vm_page_t, block_meta_data);
    init_glthread(&(vm_page->block_meta_data.priority_thread_glue));
    vm_page->next = NULL;
    vm_page->prev = NULL;
    /*Set the back pointer to the page family*/
    vm_page->page_family = vm_page_family;
    /*If it is a first VM data page for a given page family*/
    if(!vm_page_family->first_page) {
        vm_page_family->first_page = vm_page;
        return vm_page;
    }

    vm_page->next = vm_page_family->first_page;
    vm_page_family->first_page->prev = vm_page;
    vm_page_family->first_page = vm_page;
    
    return vm_page;
}

void mm_instantiate_new_page_family(
    char *struct_name, 
    uint32_t struct_size) {
    
    vm_page_family_t *vm_page_family_curr = NULL;
    vm_page_for_families_t *new_vm_page_for_families = NULL;

    if(struct_size > SYSTEM_PAGE_SIZE) {
        printf("Error : %s() Structure %s size exceeds system page size\n", 
            __func__, struct_name);
        return;
    }

    if(!first_vm_page_for_families) {
        first_vm_page_for_families = 
            (vm_page_for_families_t *) mm_get_new_vm_page_from_kernel(1);
        first_vm_page_for_families->next = NULL;
        strncpy(first_vm_page_for_families->vm_page_family[0].struct_name,
        struct_name, MM_MAX_STRUCT_NAME);
        first_vm_page_for_families->vm_page_family[0].struct_size = struct_size;
        first_vm_page_for_families->vm_page_family[0].first_page = NULL;
        init_glthread(&first_vm_page_for_families->vm_page_family[0].free_block_priority_list_head);
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
    }
    strncpy(vm_page_family_curr->struct_name, struct_name,
        MM_MAX_STRUCT_NAME);
    vm_page_family_curr->struct_size = struct_size;
    vm_page_family_curr->first_page = NULL;
    init_glthread(&first_vm_page_for_families->vm_page_family[0].free_block_priority_list_head);
    return;
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

static int 
free_blocks_comparison_function(
    void * _block_meta_data1, void * _block_meta_data2) {
    block_meta_data_t *block_meta_data1 = (block_meta_data_t *)_block_meta_data1;
    block_meta_data_t *block_meta_data2 = (block_meta_data_t *)_block_meta_data2;

    if(block_meta_data2->block_size > block_meta_data1->block_size){
        return 1;
    } else if(block_meta_data2->block_size < block_meta_data1->block_size) {
        return -1;
    }
    return 0;
}

/*Add a given free meta block of Free data block to a priority queue of a given Page family*/
static void
mm_add_free_block_meta_data_to_free_block_list (
    vm_page_family_t* vm_page_family, block_meta_data_t *free_block) {
    assert(free_block->is_free == MM_TRUE);
    glthread_priority_insert(&(vm_page_family->free_block_priority_list_head), 
            &(free_block->priority_thread_glue), 
            free_blocks_comparison_function,
            offset_of(block_meta_data_t, priority_thread_glue));
}

static vm_page_t *
mm_family_new_page_add(vm_page_family_t *vm_page_family){
    vm_page_t *vm_page = allocate_vm_page(vm_page_family);

    if(!vm_page)
        return NULL;

    /* The new page is like one free block, add it to the
     * free block list*/
    mm_add_free_block_meta_data_to_free_block_list(
        vm_page_family, &vm_page->block_meta_data);

    return vm_page;
}

/* Fn to mark block_meta_data as being Allocated for 'size' bytes of application data. 
   Return TRUE if block allocation succeeds*/
static vm_bool_t
mm_split_free_data_block_for_allocation(
            vm_page_family_t *vm_page_family,
            block_meta_data_t *block_meta_data,
            uint32_t size) {
    block_meta_data_t *next_block_meta_data = NULL;
    assert(block_meta_data->is_free == MM_TRUE);
    if(block_meta_data->block_size < size) {
        return MM_FALSE;
    }

    uint32_t remaining_size = block_meta_data->block_size - size;
    block_meta_data->is_free = MM_FALSE;
    block_meta_data->block_size = size;
    remove_glthread(&block_meta_data->priority_thread_glue); //remove meta block from pq
    /*Case 1 : No split*/
    if(remaining_size == 0){
        return MM_TRUE;
    }

    /* Case 3 : Partial Split : Soft Internal Fragmentation*/
    else if(sizeof(block_meta_data_t) < remaining_size &&
        remaining_size <  (sizeof(block_meta_data) + vm_page_family->struct_size)) {
        /*New Meta Block is to be created*/
        next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
        next_block_meta_data->is_free = MM_TRUE;
        next_block_meta_data->block_size = remaining_size - sizeof(block_meta_data_t);
        next_block_meta_data->offset = block_meta_data->offset + 
                    sizeof(block_meta_data_t) + block_meta_data->block_size;
        init_glthread(&next_block_meta_data->priority_thread_glue);
        mm_add_free_block_meta_data_to_free_block_list(vm_page_family, next_block_meta_data);
        mm_bind_blocks_for_allocation(block_meta_data, next_block_meta_data);
    }

    /* Case 4 : Partial Split : Hard Internal Fragmentation */
    else if(remaining_size < sizeof(block_meta_data)) {
        /* No need to do anything*/
    }

    /* Case 2 : Full Split : New Meta Block is Created */
    else {
        /*New Meta Block is to be created*/
        next_block_meta_data = NEXT_META_BLOCK_BY_SIZE(block_meta_data);
        next_block_meta_data->is_free = MM_TRUE;
        next_block_meta_data->block_size = remaining_size - sizeof(block_meta_data_t);
        next_block_meta_data->offset = block_meta_data->offset + 
                    sizeof(block_meta_data_t) + block_meta_data->block_size;
        init_glthread(&next_block_meta_data->priority_thread_glue);
        mm_add_free_block_meta_data_to_free_block_list(vm_page_family, next_block_meta_data);
        mm_bind_blocks_for_allocation(block_meta_data, next_block_meta_data);
    }

    return MM_TRUE;
}

static block_meta_data_t *
mm_allocate_free_data_block(
    vm_page_family_t *vm_page_family,
    uint32_t req_size) {
    vm_bool_t status = MM_FALSE;
    vm_page_t *vm_page = NULL;

    block_meta_data_t *biggest_block_meta_data = 
        mm_get_biggest_free_block_page_family(vm_page_family);

    if(!biggest_block_meta_data ||
        biggest_block_meta_data->block_size < req_size) {
        /* Add a new page to the page family to satisfy the request*/
        vm_page = mm_family_new_page_add(vm_page_family);

        /* Allocate the free block from this page now*/
        status = mm_split_free_data_block_for_allocation(vm_page_family,
                &vm_page->block_meta_data, req_size);
        if(status) {
            return &vm_page->block_meta_data;
        }
        return NULL;
    }
    /*The biggest block meta data can satisfy the request*/
    if(biggest_block_meta_data) {
        status = mm_split_free_data_block_for_allocation(vm_page_family,
                    biggest_block_meta_data, req_size);
    }
    if(status) {
        return biggest_block_meta_data;
    }

    return NULL;
}

/*The public fn to be invoked by the application for Dynamic Memory Allocation*/
void *
xcalloc(char *struct_name, int units) {
    vm_page_family_t *pg_family = 
        lookup_page_family_by_name(struct_name);
    if(!pg_family) {
        printf("Error : Structure %s not registered with Memory Manager\n", struct_name);
        return NULL;
    }
    if(units * pg_family->struct_size > MAX_PAGE_ALLOCATABLE_MEMORY(1)) {
        printf("Error : Memory Requested Exceeds Page Size\n");
        return NULL;
    }

    /* Find the page which can satisfy the request */
    block_meta_data_t *free_block_meta_data = NULL;
    free_block_meta_data = mm_allocate_free_data_block(
        pg_family, units * pg_family->struct_size);
    
    if(free_block_meta_data) {
        memset((char *)(free_block_meta_data + 1), 0, 
            free_block_meta_data->block_size);
        return (void *)(free_block_meta_data + 1);
    }

    return NULL;
}

void
mm_vm_page_delete_and_free (vm_page_t *vm_page) {
    if(!vm_page) {
        return;
    }
    vm_page_family_t *vm_page_family = vm_page->page_family;
    if(vm_page->prev) {
        vm_page->prev->next = vm_page->next;   
    }
    if(vm_page->next) {
        vm_page->next->prev = vm_page->prev;
    }

    if(vm_page_family->first_page == vm_page) {
        vm_page_family->first_page = vm_page->next;
    }
    vm_page->next = NULL;
    vm_page->prev = NULL;
    mm_return_vm_page_to_kernel((void *)vm_page, 1);
}

vm_bool_t
mm_is_vm_page_empty (vm_page_t *vm_page) {
    if(!vm_page) {
        return MM_FALSE;
    }

    block_meta_data_t first_meta_block = vm_page->block_meta_data;
    if(first_meta_block.prev_block == NULL &&
        first_meta_block.next_block == NULL&&
        first_meta_block.is_free == MM_TRUE) {
        return MM_TRUE;
    }

    return MM_FALSE;
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

void
mm_print_vm_page_details(vm_page_t *vm_page){
    printf("\t\t next = %p, prev = %p\n", (void *)vm_page->next, (void *)vm_page->prev);
    printf("\t\t page family = %s\n", vm_page->page_family->struct_name);

    uint32_t j = 0;
    block_meta_data_t *curr;
    ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page, curr){

        printf(ANSI_COLOR_YELLOW "\t\t\t%-14p Block %-3u %s  block_size = %-6u  "
                "offset = %-6u  prev = %-14p  next = %p\n"
                ANSI_COLOR_RESET, (void *)curr,
                j++, curr->is_free ? "F R E E D" : "ALLOCATED",
                curr->block_size, curr->offset, 
                (void *)curr->prev_block,
                (void *)curr->next_block);
    } ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page, curr);
}


void
mm_print_memory_usage(char *struct_name){

    uint32_t i = 0;
    vm_page_t *vm_page = NULL;
    vm_page_family_t *vm_page_family_curr; 
    uint32_t cumulative_vm_pages_claimed_from_kernel = 0;
    vm_page_for_families_t *vm_page_for_families_curr = NULL;

    printf("\nPage Size = %zu Bytes\n", SYSTEM_PAGE_SIZE);

    for(vm_page_for_families_curr = first_vm_page_for_families;
            vm_page_for_families_curr;
            vm_page_for_families_curr = vm_page_for_families_curr->next){

        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_curr, vm_page_family_curr){
            if(struct_name){
                if(strncmp(struct_name, vm_page_family_curr->struct_name,
                            strlen(vm_page_family_curr->struct_name))){
                    continue;
                }
            }

            printf(ANSI_COLOR_GREEN "vm_page_family : %s, struct size = %u\n" 
                ANSI_COLOR_RESET,
                vm_page_family_curr->struct_name,
                vm_page_family_curr->struct_size);

            ITERATE_VM_PAGE_PER_FAMILY_BEGIN(vm_page_family_curr, vm_page){
            
                cumulative_vm_pages_claimed_from_kernel++;
                mm_print_vm_page_details(vm_page);

            } ITERATE_VM_PAGE_PER_FAMILY_END(vm_page_family_curr, vm_page);
            printf("\n");

        } ITERATE_PAGE_FAMILIES_END(vm_page_for_families_curr, vm_page_family_curr);

    }

    printf(ANSI_COLOR_MAGENTA "# Of VM Pages in Use : %u (%lu Bytes).\n"
        ANSI_COLOR_RESET,
        cumulative_vm_pages_claimed_from_kernel,
        SYSTEM_PAGE_SIZE * cumulative_vm_pages_claimed_from_kernel);

}

void
mm_print_block_usage(){

    vm_page_t *vm_page_curr;
    vm_page_family_t *vm_page_family_curr;
    block_meta_data_t *block_meta_data_curr;
    uint32_t total_block_count, free_block_count,
             occupied_block_count;
    uint32_t application_memory_usage;
    vm_page_for_families_t *vm_page_for_families_curr = NULL;

    for(vm_page_for_families_curr = first_vm_page_for_families;
            vm_page_for_families_curr;
            vm_page_for_families_curr = vm_page_for_families_curr->next) {
        ITERATE_PAGE_FAMILIES_BEGIN(vm_page_for_families_curr, vm_page_family_curr){

            total_block_count = 0;
            free_block_count = 0;
            application_memory_usage = 0;
            occupied_block_count = 0;
            ITERATE_VM_PAGE_PER_FAMILY_BEGIN(vm_page_family_curr, vm_page_curr){

                ITERATE_VM_PAGE_ALL_BLOCKS_BEGIN(vm_page_curr, block_meta_data_curr){
            
                    total_block_count++;
                    
                    /*Sanity Checks*/
                    if(block_meta_data_curr->is_free == MM_FALSE){
                        assert(IS_GLTHREAD_LIST_EMPTY(&block_meta_data_curr->\
                                    priority_thread_glue));
                    }
                    if(block_meta_data_curr->is_free == MM_TRUE){
                        assert(!IS_GLTHREAD_LIST_EMPTY(&block_meta_data_curr->\
                                    priority_thread_glue));
                    }

                    if(block_meta_data_curr->is_free == MM_TRUE){
                        free_block_count++;
                    }
                    else{
                        application_memory_usage += 
                            block_meta_data_curr->block_size + \
                            sizeof(block_meta_data_t);
                        occupied_block_count++;
                    }
                } ITERATE_VM_PAGE_ALL_BLOCKS_END(vm_page_curr, block_meta_data_curr);
            } ITERATE_VM_PAGE_PER_FAMILY_END(vm_page_family_curr, vm_page_curr);

            printf("%-20s   TBC : %-4u    FBC : %-4u    OBC : %-4u AppMemUsage : %u\n",
                vm_page_family_curr->struct_name, total_block_count,
                free_block_count, occupied_block_count, application_memory_usage);
        
        } ITERATE_PAGE_FAMILIES_END(gb_heap_segment_start, vm_page_family_curr); 
    }

    
}




