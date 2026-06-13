#pragma once

#include <stddef.h>
#include <stdint.h>


struct HeapItem {
    uint64_t val = 0;
    size_t *ref = NULL;

    HeapItem() = default;
    HeapItem(uint64_t v, size_t *r) : val(v), ref(r) {}
};

void heap_update(HeapItem *a, size_t pos, size_t len);
