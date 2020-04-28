#ifndef AMDKCL_LIST_H
#define AMDKCL_LIST_H

#if !defined(HAVE_LIST_BULK_MOVE_TAIL)
#include <linux/list.h>
static inline void list_bulk_move_tail(struct list_head *head,
				       struct list_head *first,
				       struct list_head *last)
{
		first->prev->next = last->next;
		last->next->prev = first->prev;

		head->prev->next = first;
		first->prev = head->prev;

		last->next = head;
		head->prev = last;
}
#endif

#if !defined(HAVE_LIST_ROTATE_TO_FRONT)
#include <linux/list.h>
static inline void list_rotate_to_front(struct list_head *list,
					struct list_head *head)
{
	list_move_tail(head, list);
}
#endif

#if !defined(HAVE_LIST_IS_FIRST)
#include <linux/list.h>
static inline int list_is_first(const struct list_head *list,
					const struct list_head *head)
{
	return list->prev == head;
}
#endif

#endif /*AMDKCL_LIST_H*/
