/* $Id: dlist.c,v 1.6 2003/08/25 16:03:35 schindler Exp $ */

#include "platform.h"
#include "dlist.h"

/*
**  Initialize linked list
*/

static void diva_q_init(diva_entity_queue_t * q)
{
	memset(q, 0x00, sizeof(*q));
}

/*
**  Remove element from linked list
*/
static void diva_q_remove(diva_entity_queue_t * q, diva_entity_link_t * what)
{
	if (!what->prev) {
		if ((q->head = what->next)) {
			q->head->prev = 0;
		} else {
			q->tail = 0;
		}
	} else if (!what->next) {
		q->tail = what->prev;
		q->tail->next = 0;
	} else {
		what->prev->next = what->next;
		what->next->prev = what->prev;
	}
	what->prev = what->next = 0;
}

/*
**  Add element to the tail of linked list
*/
static void diva_q_add_tail(diva_entity_queue_t * q, diva_entity_link_t * what)
{
	what->next = 0;
	if (!q->head) {
		what->prev = 0;
		q->head = q->tail = what;
	} else {
		what->prev = q->tail;
		q->tail->next = what;
		q->tail = what;
	}
}

static diva_entity_link_t *diva_q_find(const diva_entity_queue_t * q,
				const void *what, diva_q_cmp_fn_t cmp_fn)
{
	diva_entity_link_t *diva_current = q->head;

	while (diva_current) {
		if (!(*cmp_fn) (what, diva_current)) {
			break;
		}
		diva_current = diva_current->next;
	}

	return (diva_current);
}

static diva_entity_link_t *diva_q_get_head(diva_entity_queue_t * q)
{
	return (q->head);
}

static diva_entity_link_t *diva_q_get_next(diva_entity_link_t * what)
{
	return ((what) ? what->next : 0);
}

