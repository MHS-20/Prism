#include "hash_type.h"
#include "common.h"

bool hentry_eq(HNode *node, HNode *key) {
    HEntry *he = container_of(node, HEntry, node);
    HLookup *hl = container_of(key, HLookup, node);
    return he->field == hl->field;
}
