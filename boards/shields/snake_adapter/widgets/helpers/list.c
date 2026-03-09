#include <stdlib.h>
#include <zephyr/kernel.h>
#include "list.h"

Snake_List* create_list() {
    Snake_List *list = (Snake_List *)k_malloc(sizeof(Snake_List));
    if (!list) {
        return NULL;
    }
    list->length = 0;
    list->head = NULL;
    list->tail = NULL;
    return list;
}

Snake_Node* create_node(uint8_t x, uint8_t y) {
    Snake_Node *node = (Snake_Node *)k_malloc(sizeof(Snake_Node));
    if (!node) {
        return NULL;
    }
    node->x = x;
    node->y = y;
    return node;
}

bool empty_list(Snake_List *list) {
    if (!list) {
        return true;
    }
    return list->head == NULL && list->tail == NULL;
}

uint8_t list_length(Snake_List *list) {
    if (!list) {
        return 0;
    }
    return list->length;
}

void prepend(Snake_List *list, uint8_t x, uint8_t y) {
    if (!list) {
        return;
    }
    Snake_Node *node = create_node(x, y);
    if (!node) {
        return;
    }
    if (empty_list(list)) {
        list->head = node;
        list->tail = node;

        node->next = node;
        node->prev = node;

        list->length = 1;
        return;
    }

    node->next = list->head;
    node->prev = list->tail;

    list->head->prev = node;
    list->tail->next = node;

    list->head = node;
    list->length++;
}

void remove_tail(Snake_List *list) {
    if (!list || empty_list(list)) {
        return;
    }
    if (list->head == list->tail) {
        k_free(list->head);
        list->head = NULL;
        list->tail = NULL;
        list->length = 0;
        return;
    }
    list->head->prev = list->tail->prev;
    list->tail->prev->next = list->head;
    k_free(list->tail);
    list->tail = list->head->prev;
    list->length--;
}

void clean_list(Snake_List *list) {
    if (!list) {
        return;
    }
    while(!empty_list(list)) {
        remove_tail(list);
    }
}
