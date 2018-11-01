#ifndef AMDKCL_LIST_H
#define AMDKCL_LIST_H

#if defined(BUILD_AS_DKMS)
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

#endif /*AMDKCL_LIST_H*/
