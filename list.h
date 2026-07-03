#ifndef __LIST_H_IOX
#define __LIST_H_IOX

struct listnode
{
    struct listnode *next;
    struct listnode *prev;
};

#define list_declare(name) \
    struct listnode name = { \
        .next = &name, \
        .prev = &name, \
    }
    
#define node_to_item(node, container, member) \
    (container *) (((char*) (node)) - offsetof(container, member))

static inline void list_init(struct listnode *node)
{
    node->next = node;
    node->prev = node;
}

static inline void list_add_tail(struct listnode *head, struct listnode *item)
{
    item->next = head;
    item->prev = head->prev;
    head->prev->next = item;
    head->prev = item;
}

static inline void list_remove(struct listnode *item)
{
    item->next->prev = item->prev;
    item->prev->next = item->next;
}

static inline int list_empty(const struct listnode *head)
{
    return head->next == head;
}

#define list_for_each(node, list) \
    for (node = (list)->next; node != (list); node = node->next)


#endif

