# What is FOS?
It's a dummy Operating System given to us - **students** - for assignments/tasks through out the semester.
# Why did i upload it on github?
Well i found it very interestng making my own OS so i want to keep it, Also you may find it interesting too and start editing it yourself who knows?.
# Is this all the work i've done through out the semester?
No, i did some easy functions like new commands for **command_prompt.c** but most of them are replaced and the others aren't really that useful. This is the last task that was given to us and the most intersting part of it was applying what i have learnt and reading the already implemented functions and see how an OS works from inside.
# What did i do?
## kern/kheap.c
  
 ```c
#include <inc/memlayout.h>
#include <kern/kheap.h>
#include <kern/memory_manager.h>

#define Ceil(a, b)					\
({									\
	typeof(a) _a = a;				\
	typeof(b) _b = b;				\
	(_a + _b - 1) / _b;				\
})

void *NextFreeHeapPageAddress = (void *)KERNEL_HEAP_START;

//****************************************************************
// Stored Blocks
//****************************************************************
struct StoredBlock{
	void *StartAddress;
	uint32 NumberOfPages;
} _StoredBlock[40959]; // Ceil(KERNEL_HEAP_MAX - KERNEL_HEAP_START, PAGE_SIZE) = Ceil(09fff000, 4096) = Ceil(167768064, 4096) = 40959
uint32 _StoredBlockSize = 0;

void AddStoredBlock(void *Address, uint32 NumberOfPages){
	_StoredBlock[_StoredBlockSize].StartAddress = Address;
	_StoredBlock[_StoredBlockSize].NumberOfPages = NumberOfPages;

	_StoredBlockSize++;
}

int GetStoredBlockIndex(const void *Address){
	for(int i = 0; i < _StoredBlockSize; i++){
		if(_StoredBlock[i].StartAddress == Address)
			return i;
	}
	return -1;
}

void DeleteStoredBlock(const int index){
	_StoredBlock[index].StartAddress = _StoredBlock[_StoredBlockSize - 1].StartAddress;
	_StoredBlock[index].NumberOfPages = _StoredBlock[_StoredBlockSize - 1].NumberOfPages;

	_StoredBlockSize--;
}

//****************************************************************
// Dynamic Allocation
//****************************************************************
void* kmalloc(unsigned int size)
{
	int NumberOfPages = Ceil(size, PAGE_SIZE); // 512
	int RemainingPagesInHeap = Ceil(KERNEL_HEAP_MAX - (uint32)NextFreeHeapPageAddress, PAGE_SIZE);
	if(NumberOfPages > RemainingPagesInHeap)
		return NULL;

	struct Frame_Info *FI = NULL;

	AddStoredBlock(NextFreeHeapPageAddress, NumberOfPages);

	for(int i = 0; i < NumberOfPages; i++){
		// Map VA to free Frame
		allocate_frame(&FI);
		map_frame(ptr_page_directory, FI, NextFreeHeapPageAddress, PERM_WRITEABLE);

		NextFreeHeapPageAddress += PAGE_SIZE;
	}

	return _StoredBlock[_StoredBlockSize - 1].StartAddress;
}

void kfree(void* virtual_address)
{
	const int index = GetStoredBlockIndex(virtual_address);
	if(index == -1)
		return;

	void *VA = virtual_address;
	for(int i = 0; i < _StoredBlock[index].NumberOfPages; i++){
		unmap_frame(ptr_page_directory, VA);
		VA += PAGE_SIZE;
	}

	DeleteStoredBlock(index);
}

unsigned int kheap_virtual_address(unsigned int physical_address)
{
	const int roundedPA = ROUNDDOWN(physical_address, PAGE_SIZE);

	for(int i = 0; i < _StoredBlockSize; i++){
		unsigned int ret = SearchBlock(roundedPA, (unsigned int)_StoredBlock[i].StartAddress, _StoredBlock[i].NumberOfPages);
		if(ret != 0)
			return ret;
	}

	return 0;
}

unsigned int SearchBlock(const unsigned int PA, unsigned int VA, const uint32 Size){
	int CurrentPA;
	for(int i = 0; i < Size; i++, VA += PAGE_SIZE){
		CurrentPA = kheap_physical_address(VA);// Get PA Of Current VA
		if(CurrentPA == PA)
			return VA;
	}
	return 0;
}

unsigned int kheap_physical_address(unsigned int virtual_address)
{
	unsigned int VA = ROUNDDOWN(virtual_address, PAGE_SIZE);
	uint32 *ptr_page_table;
	struct Frame_Info *FI = get_frame_info(ptr_page_directory, (void *)VA, &ptr_page_table);
	if(!FI)
		return 0;

	unsigned int PA = to_physical_address(FI);
	return PA;
}

```
  
- Implemented **kmalloc/kfree/kheap_virtual_address/kheap_physical_address** functions (used by the kernel)
  - kmalloc : Dynamically allocate space in memory - similar to malloc() in c
  - kfree : Free dynamically allocated space in memory - similar to free() in c
  - kheap_virtual_address : Returns the virtual address mapped to the given physical address
  - kheap_physical_address : Returns the physical address that the given virtual address mapped to
- Struct array to save information about every block allocated that will be needed by kfree() and some helper functions to use the array
  - why size so big **(40959)**? that's the maximum number of pages that can be allocated in kernel heap
## kern/trap.c

```c
void page_fault_handler(struct Env * curenv, uint32 fault_va)
{
	const uint32 size = env_page_ws_get_size(curenv);
	if(size < curenv->page_WS_max_size)
		FaultedPagePlacement(curenv, fault_va, size);
	else
		FaultedPageReplacement(curenv, fault_va);
}

void FaultedPagePlacement(struct Env * curenv, uint32 fault_va, uint32 index){
	// Step 1: allocate and map a frame for the faulted page
	struct Frame_Info *FI;
	allocate_frame(&FI);
	uint32 rVA = ROUNDDOWN(fault_va, PAGE_SIZE);
	map_frame(curenv->env_page_directory, FI, (void *)rVA, PERM_USER | PERM_WRITEABLE);

	// Step 2: read the faulted page from page file to memory
	int ret = pf_read_env_page(curenv, (void *)rVA);

	// Step 3: If the page does not exist on page file, then CHECK if it is a stack page.
	// If so this means that it is a new stack page, add a new empty page with this faulted address to page file
	if(ret == E_PAGE_NOT_EXIST_IN_PF){
		if(USTACKBOTTOM <= rVA && rVA < USTACKTOP){
			ret = pf_add_empty_env_page(curenv, rVA, 1);

			if (ret == E_NO_PAGE_FILE_SPACE)
				panic("ERROR: No enough virtual space on the page file");
		} else
			panic("page does not exist on page file OR it is a stack page");

	}

	// Step 4: Reflect the changes in the page working set
	env_page_ws_set_entry(curenv, index, rVA);
}

void FaultedPageReplacement(struct Env * curenv, uint32 fault_va){
	const uint32 index = NthChanceClock(curenv);
	uint32 victimVA = curenv->ptr_pageWorkingSet[index].virtual_address;
	uint32 *PageTablePTR;
	get_page_table(curenv->env_page_directory, (void *)victimVA, &PageTablePTR);

	// If the victim page was modified then update its page in page file.
	if((PageTablePTR[PTX(victimVA)] & PERM_MODIFIED) == PERM_MODIFIED){
		// Change the modified permission as changes will be saved
		//PageTablePTR[PTX(victimVA)] &= ~PERM_MODIFIED;

		// Update page
		struct Frame_Info *ptr_frame_info = get_frame_info(curenv->env_page_directory, (void *)victimVA, &PageTablePTR);
		pf_update_env_page(curenv, (void *)victimVA, ptr_frame_info);
	}

	// Unmap the victim.
	unmap_frame(curenv->env_page_directory, (void *)victimVA);

	// Reflect the changes in the page working set.
	env_page_ws_clear_entry(curenv, index);

	// Place the new Page
	FaultedPagePlacement(curenv, fault_va, index);
}

uint32 NthChanceClock(struct Env * curenv){
	uint32 index = getVictim(curenv);
	int sweeps = page_WS_max_sweeps - curenv->ptr_pageWorkingSet[index].sweeps;
	// All pages should be updated
	updateSweeps(curenv, sweeps);

	return index;
}

int getVictim(struct Env * curenv){
	uint32 index = 0, mx = 0;
	struct WorkingSetElement *wsPTR = curenv->ptr_pageWorkingSet;
	struct Frame_Info *FI;
	uint32 *PageTablePTR;

	for(int i = 0; i < (curenv->page_WS_max_size); i++){
		// if use bit = 1: then clear use and also clear counter
		get_page_table(curenv->env_page_directory, (void *)wsPTR[i].virtual_address, &PageTablePTR);
		if((PageTablePTR[PTX(wsPTR[i].virtual_address)] & PERM_USED) == PERM_USED){
			PageTablePTR[PTX(wsPTR[i].virtual_address)] &= ~PERM_USED;
			wsPTR[i].sweeps = 0;
			/*FI = get_frame_info(curenv->env_page_directory, (void *)wsPTR[i].virtual_address, &PageTablePTR);
			pf_update_env_page(curenv, (void *)wsPTR[i].virtual_address, FI);*/
		}
		// if use bit = 0: then increment counter while it is less than N
		else
			wsPTR[i].sweeps = MIN(wsPTR[i].sweeps + 1, page_WS_max_sweeps);

		// The victim page will be first page with the counter that reaches MaxNumOfClocks
		if(mx < wsPTR[i].sweeps){
			mx = wsPTR[i].sweeps;
			index = i;
		}
	}

	return index;
}

void updateSweeps(struct Env * curenv, int sweeps){
	struct WorkingSetElement *wsPTR = curenv->ptr_pageWorkingSet;
	for(int i = 0; i < (curenv->page_WS_max_size); i++){
		wsPTR[i].sweeps = MIN(wsPTR[i].sweeps + sweeps, page_WS_max_sweeps);
	}
}

```

- Implemented **page_fault_handler** function
  - What does it do? each user should have a working set that saves the pages allocated in memory so when the page isn't in memory it throws a page fault error and the function is called to allocate a new page and put it in the working set
  - How does it work?
    >- if the size of the page working set < its max size, then do
    >Placement:
    >   1. allocate and map a frame for the faulted page
    >   2. read the faulted page from page file to memory
    >   3. If the page does not exist on page file, then CHECK if it is a stack page. If so this means that it is a new stack page, add a new empty page with this faulted address to page file
    >   4. Reflect the changes in the page working set
    >- else, do Replacement:
    >   1. implement the Nth Chance Clock algorithm to find victim virtual address from page working set to replace
    >   2. for the victim page:
    >      - If the victim page was not modified, then:
    >        1. unmap it
    >      - Else
    >        1. update its page in page file,
    >        2. then, unmap it
    >   3. Reflect the changes in the page working set
    >   4. Apply Placement steps that are described before
