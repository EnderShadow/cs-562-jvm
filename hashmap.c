//
// Created by matthew on 10/24/19.
//

#include "hashmap.h"

typedef struct entry_node {
    struct entry_node *prev;
    struct entry_node *next;
    void *key;
    void *value;
} entry_node_t;

struct hashmap {
    entry_node_t **data;
    size_t (*hash_fn)(void *);
    size_t length;
    size_t numEntries;
    float loadFactor;
    bool (*equality_fn)(void *, void *);
};

hashmap_t *ht_createHashmap(size_t (*hash_fn)(void *), bool (*equality_fn)(void *, void *), float loadFactor) {
    if(!hash_fn)
        return NULL;
    hashmap_t *hashmap = malloc(sizeof(hashmap_t *));
    if(!hashmap)
        return NULL;

    hashmap->data = calloc(16, sizeof(entry_node_t *));
    if(!hashmap->data) {
        free(hashmap);
        return NULL;
    }

    hashmap->hash_fn = hash_fn;
    hashmap->length = 16;
    hashmap->numEntries = 0;
    hashmap->loadFactor = loadFactor;
    hashmap->equality_fn = equality_fn;
    return hashmap;
}

void ht_destroyHashmap(hashmap_t *hashmap) {
    if(!hashmap)
        return;
    size_t length = hashmap->length;
    entry_node_t **data = hashmap->data;
    for(size_t i = 0; i < length; ++i) {
        entry_node_t *entry = data[i];
        while(entry) {
            entry_node_t *temp = entry;
            entry = entry->next;
            free(temp);
        }
    }
    free(hashmap->data);
    free(hashmap);
}

void expandHashmap(hashmap_t *hashmap) {
    size_t newLength = hashmap->length * 2;
    entry_node_t **newData = calloc(newLength, sizeof(entry_node_t *));
    if(!newData)
        return;

    size_t length = hashmap->length;
    entry_node_t **data = hashmap->data;
    for(size_t i = 0; i < length; ++i) {
        entry_node_t *entry = data[i];
        while(entry) {
            entry_node_t *temp = entry;
            entry = entry->next;
            size_t newLocation = hashmap->hash_fn(entry->key) & (newLength - 1);
            temp->next = newData[newLocation];
            if(temp->next)
                temp->next->prev = temp;
            newData[newLocation] = temp;
        }
    }
    free(hashmap->data);
    hashmap->data = newData;
    hashmap->length = newLength;
}

void *ht_put(hashmap_t *hashmap, void *key, void *value) {
    if(!hashmap)
        return NULL;

    if(hashmap->numEntries >= hashmap->length * hashmap->loadFactor)
        expandHashmap(hashmap);

    size_t hash = hashmap->hash_fn(key);
    size_t index = hash & (hashmap->length - 1);
    entry_node_t **data = hashmap->data;
    entry_node_t *entry = data[index];

    // try to update existing key
    while(entry) {
        if(hashmap->equality_fn(entry->key, key)) {
            void *oldValue = entry->value;
            entry->value = value;
            return oldValue;
        }
        entry = entry->next;
    }

    // create new key
    entry = malloc(sizeof(entry_node_t));
    if(entry) {
        entry->key = key;
        entry->value = value;
        entry->prev = NULL;
        entry->next = data[index];
        if(entry->next)
            entry->next->prev = entry;
        data[index] = entry;
        ++hashmap->numEntries;
    }
    return NULL;
}

void *ht_delete(hashmap_t *hashmap, void *key) {
    if(!hashmap)
        return NULL;

    size_t index = hashmap->hash_fn(key) & (hashmap->length - 1);
    entry_node_t *entry = hashmap->data[index];

    while(entry) {
        if(hashmap->equality_fn(entry->key, key)) {
            if(entry->prev)
                entry->prev->next = entry->next;
            if(entry->next)
                entry->next->prev = entry->prev;
            void *value = entry->value;
            free(entry);
            --hashmap->numEntries;
            return value;
        }
        entry = entry->next;
    }
    return NULL;
}

void *ht_get(hashmap_t *hashmap, void *key) {
    if(!hashmap)
        return NULL;

    size_t index = hashmap->hash_fn(key) & (hashmap->length - 1);
    entry_node_t *entry = hashmap->data[index];

    while(entry) {
        if(hashmap->equality_fn(entry->key, key))
            return entry->value;
        entry = entry->next;
    }
    return NULL;
}

bool ht_contains(hashmap_t *hashmap, void *key) {
    if(!hashmap)
        return false;

    size_t index = hashmap->hash_fn(key) & (hashmap->length - 1);
    entry_node_t *entry = hashmap->data[index];

    while(entry) {
        if(hashmap->equality_fn(entry->key, key))
            return true;
        entry = entry->next;
    }
    return false;
}

entry_t *ht_entries(hashmap_t *hashmap, size_t *numEntries) {
    if(!hashmap)
        return NULL;

    entry_t *entries = malloc(sizeof(entry_t) * hashmap->numEntries);
    if(!entries)
        return NULL;

    *numEntries = hashmap->numEntries;
    size_t index = 0;
    size_t length = hashmap->length;
    entry_node_t **data = hashmap->data;
    for(size_t i = 0; i < length; ++i) {
        entry_node_t *entry = data[i];
        while(entry) {
            entries[index].key = entry->key;
            entries[index].value = entry->value;
            ++index;
            entry = entry->next;
        }
    }
    
    return entries;
}
