// hash_table.h - custom string-keyed hash table (separate chaining + FNV-1a + auto-resize).
//
//  * Key   : node->id (std::string). The table stores Node* and does NOT own the nodes.
//  * Chains: singly linked lists of HashEntry (insert at head).
//  * Growth: capacity doubles and every entry is rehashed once count / capacity > 0.75.
//  * Hash  : pluggable function pointer so tests can swap in deliberately weak hashes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "node.h"

typedef uint64_t (*HashFn)(const std::string& key);

uint64_t HashFnv1a(const std::string& key);          // default: 64-bit FNV-1a
// Deliberately terrible hashes, used only by tests/benchmarks to stress collision handling:
uint64_t HashWeakConstant(const std::string& key);   // everything collides
uint64_t HashWeakFirstChar(const std::string& key);  // ~60 distinct values
uint64_t HashWeakLength(const std::string& key);     // a handful of distinct values

struct HashEntry {
    Node*      node;
    uint64_t   hash;    // cached full hash: cheap pre-check before comparing strings, free rehash
    HashEntry* next;
};

struct HashTable {
    HashEntry** buckets     = nullptr;
    size_t      capacity    = 0;     // always a power of two
    size_t      count       = 0;
    HashFn      hashFn      = nullptr;
    size_t      resizeCount = 0;
};

struct HashTableStats {
    size_t   count = 0, capacity = 0, resizeCount = 0;
    double   loadFactor = 0.0;
    size_t   emptyBuckets = 0, usedBuckets = 0;
    size_t   maxChain = 0;           // "peak chain depth"
    double   avgChainUsed = 0.0;     // count / usedBuckets
    double   avgProbesHit = 0.0;     // expected key comparisons for a successful lookup
    size_t   histogram[8] = {0};     // buckets with chain length 0,1,2,...,6,7+
};

void  HashTableInit(HashTable& t, size_t initialCapacity = 16, HashFn fn = HashFnv1a);
void  HashTableDestroy(HashTable& t);                 // frees entries + bucket array, not the nodes
bool  HashTableInsert(HashTable& t, Node* node);      // false if the id already exists
Node* HashTableFind(const HashTable& t, const std::string& key, int* probes = nullptr);
bool  HashTableRemove(HashTable& t, const std::string& key);   // true if something was removed
void  HashTableResize(HashTable& t, size_t newCapacity);       // rehash into a new bucket array
double HashTableLoadFactor(const HashTable& t);
HashTableStats HashTableComputeStats(const HashTable& t);      // O(capacity), call on demand
