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
