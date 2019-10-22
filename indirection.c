#define _GNU_SOURCE
#include "indirection_impl.h"
#include "garbage_collection.h"
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>

inline void * getRawAddress(addr_ind_info_t * addrInfo, slot_t slot) {
    if(!addrInfo || slot >= maxNumAddresses(addrInfo))
        return NULL;
    return addrInfo->addressTable[slot];
}

inline void setRawAddress(addr_ind_info_t * addrInfo, slot_t slot, void * address) {
    if(!addrInfo || slot == 0 || slot >= maxNumAddresses(addrInfo))
        return;
    addrInfo->addressTable[slot] = address;
}

addr_ind_info_t * createAddressIndirectionInfo() {
    addr_ind_info_t * addrInfo = malloc(sizeof(addr_ind_info_t));
    if(addrInfo == NULL)
        return NULL;

    addrInfo->addressTable = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(addrInfo->addressTable == MAP_FAILED) {
        free(addrInfo);
        return NULL;
    }
    addrInfo->freeSlot = NULL;
    addrInfo->numFragmentedFree = 0;
    addrInfo->numPages = 1;
    addrInfo->numAddresses = 1;
    addrInfo->addressTable[0] = NULL;
    
    pthread_mutex_init(&addrInfo->freeListMutex, NULL);
    pthread_mutex_init(&addrInfo->slotAllocationMutex, NULL);

    return addrInfo;
}

void destroyAndFreeAddressIndirectionInfo(addr_ind_info_t * addrInfo) {
    if(!addrInfo)
        return;
    pthread_mutex_destroy(&addrInfo->freeListMutex);
    pthread_mutex_destroy(&addrInfo->slotAllocationMutex);
    munmap(addrInfo->addressTable, addrInfo->numPages * PAGE_SIZE);
    free_slot_t * freeSlot = addrInfo->freeSlot;
    while(freeSlot) {
        free_slot_t * temp = freeSlot;
        freeSlot = freeSlot->next;
        free(temp);
    }
    free(addrInfo);
}

/**
 *
 * @param addrInfo
 * @return the maximum number of addresses that can currently be stored
 */
size_t maxNumAddresses(addr_ind_info_t * addrInfo) {
    if(!addrInfo)
        return 0;
    return addrInfo->numPages * ADDR_PER_PAGE;
}

/**
 *
 * @param addrInfo
 * @return if 0 is returned, then no slot was allocated. This indicates a failure to expand the indirection mapping or that we ran out of slots
 */
slot_t allocateSlot(addr_ind_info_t * addrInfo) {
    if(!addrInfo || addrInfo->numAddresses == ((size_t) 1 << sizeof(slot_t)) - 1)
        return 0;

    pthread_mutex_lock(&addrInfo->freeListMutex);
    if(addrInfo->freeSlot != NULL) {
        --addrInfo->numFragmentedFree;
        free_slot_t * freeSlot = addrInfo->freeSlot;
        addrInfo->freeSlot = freeSlot->next;
        pthread_mutex_unlock(&addrInfo->freeListMutex);
        slot_t slot = freeSlot->slot;
        free(freeSlot);
        return slot;
    }
    pthread_mutex_unlock(&addrInfo->freeListMutex);
    pthread_mutex_lock(&addrInfo->slotAllocationMutex);
    if(addrInfo->numAddresses == maxNumAddresses(addrInfo)) {
        size_t size = addrInfo->numPages * PAGE_SIZE;
        void ** newAddressTable = mremap(addrInfo->addressTable, size, size + PAGE_SIZE, MREMAP_MAYMOVE);
        if(newAddressTable == MAP_FAILED)
            return 0;
        addrInfo->addressTable = newAddressTable;
        ++addrInfo->numPages;
    }
    slot_t slot = addrInfo->numAddresses++;
    pthread_mutex_unlock(&addrInfo->slotAllocationMutex);
    return slot;
}

/**
 *
 * @param addrInfo
 * @param slot
 * @return returns -1 if it failed to append the slot to a free list, otherwise returns 0
 * Even if freeSlot returns -1, the slot no longer points to a valid address
 */
int freeSlot(addr_ind_info_t * addrInfo, slot_t slot) {
    if(!addrInfo)
        return 0;
    if(slot == 0)
        return 0;
    setRawAddress(addrInfo, slot, NULL);
    pthread_mutex_lock(&addrInfo->slotAllocationMutex);
    if(slot + 1 == addrInfo->numAddresses) {
        --addrInfo->numAddresses;
        pthread_mutex_unlock(&addrInfo->slotAllocationMutex);
        return 0;
    }
    ++addrInfo->numFragmentedFree;
    pthread_mutex_unlock(&addrInfo->slotAllocationMutex);
    free_slot_t * freeSlot = malloc(sizeof(free_slot_t));
    if(freeSlot == NULL)
        return -1;
    freeSlot->slot = slot;
    pthread_mutex_lock(&addrInfo->freeListMutex);
    freeSlot->next = addrInfo->freeSlot;
    addrInfo->freeSlot = freeSlot;
    pthread_mutex_unlock(&addrInfo->freeListMutex);
    return 0;
}

/**
 * This function cleans up the free list and returns any extra pages to the OS
 * @param addrInfo
 */
void rebuildFreeList(addr_ind_info_t * addrInfo) {
    pthread_mutex_lock(&addrInfo->freeListMutex);
    pthread_mutex_lock(&addrInfo->slotAllocationMutex);

    // trim free slots at the end of the addressTable
    while(addrInfo->addressTable[addrInfo->numAddresses - 1] == NULL && addrInfo->numAddresses > 1)
        --addrInfo->numAddresses;

    // release unneeded pages
    size_t tableSize = addrInfo->numPages * PAGE_SIZE;
    size_t newSize = (addrInfo->numAddresses * sizeof(void *) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if(newSize < tableSize)
        mremap(addrInfo->addressTable, tableSize, newSize, 0);

    // rebuild free list
    slot_t curSlot = 1;
    free_slot_t * prevSlot = NULL;
    free_slot_t * freeSlot = addrInfo->freeSlot;
    size_t numFragmentedFree = 0;
    while(freeSlot) {
        while(curSlot < addrInfo->numAddresses && addrInfo->addressTable[curSlot])
            ++curSlot;

        if(curSlot < addrInfo->numAddresses) {
            freeSlot->slot = curSlot;
            prevSlot = freeSlot;
            freeSlot = freeSlot->next;
            ++numFragmentedFree;
        }
        else {
            if(prevSlot)
                prevSlot->next = NULL;
            else // this should never happen, but might as well take care of it anyway
                addrInfo->freeSlot = NULL;

            // free the extra free_slot_t instances
            while(freeSlot) {
                free_slot_t * temp = freeSlot;
                freeSlot = freeSlot->next;
                free(temp);
            }
        }
    }

    // Maybe we ran out of free blocks before finishing rebuilding the free list
    while(curSlot < addrInfo->numAddresses) {
        if(addrInfo->addressTable[curSlot]) {
            ++curSlot;
        }
        else {
            freeSlot = malloc(sizeof(free_slot_t));
            if(!freeSlot) {
                // Oh well, some items aren't in the free list because we couldn't allocate memory
                break;
            }
            else {
                freeSlot->slot = curSlot;
                freeSlot->next = addrInfo->freeSlot;
                addrInfo->freeSlot = freeSlot;
                ++numFragmentedFree;
            }
        }
    }
    
    addrInfo->numFragmentedFree = numFragmentedFree;

    pthread_mutex_unlock(&addrInfo->slotAllocationMutex);
    pthread_mutex_unlock(&addrInfo->freeListMutex);
}

/**
 * If running in a multi-threaded context, you must acquire the GC locks before calling this function
 *
 * Only call this function if you are able to remap slots from the returned compact_res_t pointer
 * @param addrInfo
 * @return
 */
compact_res_t * compactAddressTable(addr_ind_info_t * addrInfo) {
    if(!addrInfo)
        return NULL;
    
    size_t numFragmentedFree = addrInfo->numFragmentedFree;
    // Move allocations at the end of the table to the front
    compact_res_t * compactionResult = NULL;
    void ** addressTable = addrInfo->addressTable;
    slot_t start = 1;
    slot_t end = addrInfo->numAddresses - 1;
    while(1) {
        while(start < end && addressTable[start] != NULL)
            ++start;
        while(start < end && addressTable[end] == NULL) {
            --end;
            --numFragmentedFree;
        }
        if(start < end && addressTable[start] == NULL && addressTable[end] != NULL) {
            compact_res_t * newResult = malloc(sizeof(compact_res_t));
            if(newResult == NULL)
                break;
            addressTable[start] = addressTable[end];
            addressTable[end] = NULL;
            newResult->next = compactionResult;
            newResult->oldSlot = end;
            newResult->newSlot = start;
            compactionResult = newResult;
            addrInfo->numAddresses = end;
            --numFragmentedFree;
        }
        else
            break;
    }
    
    addrInfo->numFragmentedFree = numFragmentedFree;

    // release unneeded pages
    size_t tableSize = addrInfo->numPages * PAGE_SIZE;
    size_t newSize = (addrInfo->numAddresses * sizeof(void *) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if(newSize < tableSize)
        mremap(addressTable, tableSize, newSize, 0);

    // free the free list since everything is compacted (unless a compact_res_t failed to malloc)
    free_slot_t * freeSlot = addrInfo->freeSlot;
    addrInfo->freeSlot = NULL;
    while(freeSlot != NULL) {
        free_slot_t * temp = freeSlot;
        freeSlot = freeSlot->next;
        free(temp);
    }

    return compactionResult;
}