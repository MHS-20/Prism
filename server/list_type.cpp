#include "list_type.h"

void llist_push(LList *list, const std::string &val) {
    LNode *n = new LNode();
    n->val = val;
    n->next = list->head;
    if (list->head) {
        list->head->prev = n;
    } else {
        list->tail = n;
    }
    list->head = n;
    list->len++;
}

bool llist_pop(LList *list, std::string &out) {
    if (!list->head) return false;
    LNode *n = list->head;
    out = n->val;
    list->head = n->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }
    delete n;
    list->len--;
    return true;
}

LNode *llist_index(LList *list, int64_t idx) {
    if (idx < 0) idx = (int64_t)list->len + idx;
    if (idx < 0 || (size_t)idx >= list->len) return NULL;

    LNode *cur;
    if ((size_t)idx < list->len / 2) {
        cur = list->head;
        for (int64_t i = 0; i < idx; i++) cur = cur->next;
    } else {
        cur = list->tail;
        for (int64_t i = (int64_t)list->len - 1; i > idx; i--) cur = cur->prev;
    }
    return cur;
}

void llist_clear(LList *list) {
    LNode *cur = list->head;
    while (cur) {
        LNode *next = cur->next;
        delete cur;
        cur = next;
    }
    list->head = list->tail = NULL;
    list->len = 0;
}
