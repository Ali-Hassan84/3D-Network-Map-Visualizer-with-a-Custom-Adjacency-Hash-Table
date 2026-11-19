#include "hash_table.h"
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------- hash functions
uint64_t HashFnv1a(const std::string& key) {
    uint64_t h = 14695981039346656037ULL;                 // FNV offset basis
    for (size_t i = 0; i < key.size(); ++i) {
        h ^= static_cast<unsigned char>(key[i]);
        h *= 1099511628211ULL;                            // FNV prime
    }
    return h;
}
uint64_t HashWeakConstant(const std::string&) { return 42; }
uint64_t HashWeakFirstChar(const std::string& key) {
    return key.empty() ? 0 : static_cast<unsigned char>(key[0]);
}
uint64_t HashWeakLength(const std::string& key) { return key.size(); }

// ---------------------------------------------------------------- helpers
// Bucket index. capacity is a power of two, so "& (capacity-1)" replaces "% capacity".
// The xor-fold mixes the high bits in (FNV's multiply only pushes entropy upwards).
static inline size_t BucketIndex(uint64_t hash, size_t capacity) {
    return static_cast<size_t>((hash ^ (hash >> 32)) & (capacity - 1));
}

static size_t RoundUpPow2(size_t n) {
    size_t p = 8;
    while (p < n) p <<= 1;
    return p;
}

static HashEntry** AllocBuckets(size_t capacity) {
    HashEntry** b = static_cast<HashEntry**>(std::calloc(capacity, sizeof(HashEntry*)));
    if (!b) { std::fprintf(stderr, "fatal: out of memory\n"); std::abort(); }
    return b;
}

// ---------------------------------------------------------------- public API
void HashTableInit(HashTable& t, size_t initialCapacity, HashFn fn) {
    t.capacity    = RoundUpPow2(initialCapacity);
    t.buckets     = AllocBuckets(t.capacity);
    t.count       = 0;
    t.hashFn      = fn ? fn : HashFnv1a;
    t.resizeCount = 0;
}

void HashTableDestroy(HashTable& t) {
    for (size_t i = 0; i < t.capacity; ++i) {
        HashEntry* e = t.buckets[i];
        while (e) {
            HashEntry* next = e->next;
            delete e;
            e = next;
        }
    }
    std::free(t.buckets);
    t.buckets = nullptr;
    t.capacity = t.count = 0;
}

void HashTableResize(HashTable& t, size_t newCapacity) {
    newCapacity = RoundUpPow2(newCapacity);
    if (newCapacity == t.capacity) return;
    HashEntry** nb = AllocBuckets(newCapacity);
    for (size_t i = 0; i < t.capacity; ++i) {             // relink every entry, no reallocation
        HashEntry* e = t.buckets[i];
        while (e) {
            HashEntry* next = e->next;
            size_t idx = BucketIndex(e->hash, newCapacity);
            e->next = nb[idx];
            nb[idx] = e;
            e = next;
        }
    }
    std::free(t.buckets);
    t.buckets = nb;
    t.capacity = newCapacity;
    t.resizeCount++;
}

bool HashTableInsert(HashTable& t, Node* node) {
    uint64_t h = t.hashFn(node->id);
    size_t idx = BucketIndex(h, t.capacity);
    for (HashEntry* e = t.buckets[idx]; e; e = e->next)   // reject duplicate keys
        if (e->hash == h && e->node->id == node->id) return false;

    HashEntry* e = new HashEntry();
    e->node = node;
    e->hash = h;
    e->next = t.buckets[idx];                              // insert at head: O(1)
    t.buckets[idx] = e;
    t.count++;

    if (t.count * 4 > t.capacity * 3)                      // load factor > 0.75
        HashTableResize(t, t.capacity * 2);
    return true;
}

Node* HashTableFind(const HashTable& t, const std::string& key, int* probes) {
    uint64_t h = t.hashFn(key);
    int p = 0;
    for (HashEntry* e = t.buckets[BucketIndex(h, t.capacity)]; e; e = e->next) {
        ++p;
        if (e->hash == h && e->node->id == key) {
            if (probes) *probes = p;
            return e->node;
        }
    }
    if (probes) *probes = p;
    return nullptr;
}

bool HashTableRemove(HashTable& t, const std::string& key) {
    uint64_t h = t.hashFn(key);
    HashEntry** link = &t.buckets[BucketIndex(h, t.capacity)];   // pointer-to-pointer unlink
    while (*link) {
        HashEntry* e = *link;
        if (e->hash == h && e->node->id == key) {
            *link = e->next;
            delete e;
            t.count--;
            return true;
        }
        link = &e->next;
    }
    return false;
}

double HashTableLoadFactor(const HashTable& t) {
    return t.capacity ? static_cast<double>(t.count) / static_cast<double>(t.capacity) : 0.0;
}

HashTableStats HashTableComputeStats(const HashTable& t) {
    HashTableStats s;
    s.count = t.count;
    s.capacity = t.capacity;
    s.resizeCount = t.resizeCount;
    s.loadFactor = HashTableLoadFactor(t);
    double probeSum = 0.0;                                 // sum over chains of 1+2+...+len
    for (size_t i = 0; i < t.capacity; ++i) {
        size_t len = 0;
        for (HashEntry* e = t.buckets[i]; e; e = e->next) ++len;
        if (len == 0) s.emptyBuckets++; else s.usedBuckets++;
        if (len > s.maxChain) s.maxChain = len;
        s.histogram[len < 7 ? len : 7]++;
        probeSum += static_cast<double>(len) * static_cast<double>(len + 1) / 2.0;
    }
    if (s.usedBuckets) s.avgChainUsed = static_cast<double>(t.count) / static_cast<double>(s.usedBuckets);
    if (t.count)       s.avgProbesHit = probeSum / static_cast<double>(t.count);
    return s;
}
