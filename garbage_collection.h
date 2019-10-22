//
// Created by matthew on 10/6/19.
//

#ifndef ADDRESSINDIRECTION_GARBAGE_COLLECTION_H
#define ADDRESSINDIRECTION_GARBAGE_COLLECTION_H

#include "indirection.h"

typedef struct CompactionResultNode {
    struct CompactionResultNode * next;
    slot_t oldSlot;
    slot_t newSlot;
} compact_res_t;

/**
 * Do not call this function while holding the GC locks. Doing so will cause a deadlock
 *
 * This function cleans up the free list and returns any extra pages to the OS
 * @param addrInfo
 */
void rebuildFreeList(addr_ind_info_t * addrInfo);

/**
 * If running in a multi-threaded context, you must acquire the GC locks before calling this function
 *
 * Only call this function if you are able to remap slots from the returned compact_res_t pointer
 * @param addrInfo
 * @return
 */
compact_res_t * compactAddressTable(addr_ind_info_t * addrInfo);

#endif //ADDRESSINDIRECTION_GARBAGE_COLLECTION_H
