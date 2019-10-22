#ifndef ADDRESSINDIRECTION_INDIRECTION_IMPL_H
#define ADDRESSINDIRECTION_INDIRECTION_IMPL_H

#include "indirection.h"
#include <stdlib.h>
#include <stdatomic.h>

#define PAGE_SIZE getpagesize()
#define ADDR_PER_PAGE (PAGE_SIZE / sizeof(void *))

typedef struct FreeSlotNode {
    struct FreeSlotNode * next;
    slot_t slot;
} free_slot_t;

struct AddressIndirectionInfo {
	void ** addressTable;
	free_slot_t * freeSlot;
	atomic_size_t numFragmentedFree;
	size_t numPages;
	size_t numAddresses;
	pthread_mutex_t freeListMutex;
	pthread_mutex_t slotAllocationMutex;
};

/**
 *
 * @param addrInfo
 * @return the maximum number of addresses that can currently be stored
 */
size_t maxNumAddresses(addr_ind_info_t * addrInfo);

#endif //ADDRESSINDIRECTION_INDIRECTION_IMPL_H
