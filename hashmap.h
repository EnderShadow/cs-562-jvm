//
// Created by matthew on 10/24/19.
//

#ifndef JVM_HASHMAP_H
#define JVM_HASHMAP_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct hashmap hashmap_t;
typedef struct ht_entry {
    void *key;
    void *value;
} entry_t;

hashmap_t *ht_createHashmap(size_t (*hash_fn)(void *), bool (*equality_fn)(void *, void *), float loadFactor);
void ht_destroyHashmap(hashmap_t *hashmap);

void *ht_put(hashmap_t *hashmap, void *key, void *value);
void *ht_delete(hashmap_t *hashmap, void *key);
void *ht_get(hashmap_t *hashmap, void *key);
bool ht_contains(hashmap_t *hashmap, void *key);

entry_t *ht_entries(hashmap_t *hashmap, size_t *numEntries);

#endif //JVM_HASHMAP_H
