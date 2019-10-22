#ifndef ADDRESSINDIRECTION_INDRECTION_H
#define ADDRESSINDIRECTION_INDRECTION_H

#include <stdint.h>
#include "dataTypes.h"

typedef struct AddressIndirectionInfo addr_ind_info_t;

void * getRawAddress(addr_ind_info_t * addrInfo, slot_t slot);
void setRawAddress(addr_ind_info_t * addrInfo, slot_t slot, void * address);

addr_ind_info_t * createAddressIndirectionInfo();

void destroyAndFreeAddressIndirectionInfo(addr_ind_info_t * addrInfo);

/**
 *
 * @param addrInfo
 * @return if 0 is returned, then no slot was allocated
 */
slot_t allocateSlot(addr_ind_info_t * addrInfo);

/**
 *
 * @param addrInfo
 * @param slot
 * @return returns -1 if it failed to append the slot to a free list, otherwise returns 0
 * Even if freeSlot does return -1, the slot no longer points to a valid address
 */
int freeSlot(addr_ind_info_t * addrInfo, slot_t slot);

#endif //ADDRESSINDIRECTION_INDRECTION_H
