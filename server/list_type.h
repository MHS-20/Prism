#pragma once

#include <string>
#include <stddef.h>
#include <stdint.h>

struct LNode {
    LNode *prev = NULL;
    LNode *next = NULL;
    std::string val;
};

struct LList {
    LNode *head = NULL;
    LNode *tail = NULL;
    size_t len = 0;
};

void llist_push(LList *list, const std::string &val);
bool llist_pop(LList *list, std::string &out);
LNode *llist_index(LList *list, int64_t idx);
void llist_clear(LList *list);
