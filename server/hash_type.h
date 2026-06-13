#pragma once

#include "hashtable.h"
#include <string>

struct HEntry {
    HNode node;
    std::string field;
    std::string val;
};

struct HLookup {
    HNode node;
    std::string field;
};

bool hentry_eq(HNode *node, HNode *key);
