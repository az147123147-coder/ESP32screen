#ifndef _LIST_H
#define _LIST_H

#include <stdint.h>
#include <stddef.h>


#define ATK_LIST_INIT(name)          {&(name), &(name)}


struct atk_list_node
{
    struct atk_list_node *next;      /* 下一个节点 */
    struct atk_list_node *prev;      /* 上一个节点 */
};
typedef struct atk_list_node atk_list_node_t;

#define atk_list_entry(ptr, type, member)                                    \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

#define atk_list_for_each_safe(pos, n, head)                                 \
    for (pos = (head)->next, n = pos->next;                                 \
        pos != (head);                                                     \
        pos = n, n = pos->next)

#define atk_list_for_each_entry_safe(pos, n, head, type, member)             \
    for (pos = atk_list_entry((head)->next, type, member),                   \
        n = atk_list_entry(pos->member.next, type, member);                 \
        &pos->member != (head);                                            \
        pos = n, n = atk_list_entry(n->member.next, type, member))

#define atk_list_first_entry(head, type, member)                             \
atk_list_entry((head)->next, type, member)
    
/* 列表初始化 */
static __inline void atk_list_init(atk_list_node_t *node)
{
    node->next = node;
    node->prev = node;
}

/* 首部添加列表项 */
static __inline void atk_list_add(atk_list_node_t *head, atk_list_node_t *entry)
{
    head->next->prev = entry;
    entry->next = head->next;

    head->next = entry;
    entry->prev = head;
    
    return;
}

/* 尾部添加列表项 */
static __inline void atk_list_add_tail(atk_list_node_t *head, atk_list_node_t *entry)
{ 
    head->prev->next = entry;
    entry->prev = head->prev;

    head->prev = entry;
    entry->next = head;
    
    return;
}

/* 删除列表项 */
static __inline void atk_list_del(atk_list_node_t *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;

    entry->next = entry;
    entry->prev = entry;

    return;
}

/* 判断是否为空 */
static __inline char atk_list_empty(const atk_list_node_t *head)
{
    return (head->next == head);
}

/* 返回最后一个节点 */
static __inline atk_list_node_t *atk_list_tail(atk_list_node_t *head)
{
    atk_list_node_t *node;

    if (head->next == head)
    {
        return NULL;
    }
    
    node = head; 
    while (node->next != head)
    {
        node = node->next;
    }

    return node;
}

/* 获取某个节点的下一个节点 */
static __inline atk_list_node_t *atk_list_node_next(atk_list_node_t *head)
{
        atk_list_node_t *node;
        node = head->next;
        return node;
}

/* 获取某个节点的上一个节点 */
static __inline atk_list_node_t *atk_list_node_prv(atk_list_node_t *head)
{
        atk_list_node_t *node;
        node = head->prev;
        return node;
}

/* 获取大小 */
static __inline  uint32_t atk_list_len(const atk_list_node_t *head)
{
    uint32_t len;
    const atk_list_node_t *node;

    len = 0;
    node = head;
    
    while (node->next != head)
    {
        node = node->next;
        len ++;
    }

    return len;
}

#define atk_list_tail_entry(head, type, member)                              \
    atk_list_entry(atk_list_tail(head), type, member)

#define atk_list_node_next_entry(head, type, member)                              \
    atk_list_entry(atk_list_node_next(head), type, member)

#define atk_list_node_prv_entry(head, type, member)                              \
    atk_list_entry(atk_list_node_prv(head), type, member)

#endif
