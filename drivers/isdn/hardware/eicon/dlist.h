/* $Id: dlist.h,v 1.1.2.2 2001/02/08 12:25:43 armin Exp $ */

#ifndef __DIVA_LINK_H__
#define __DIVA_LINK_H__

struct _diva_entity_link;
typedef struct _diva_entity_link {
	struct _diva_entity_link *prev;
	struct _diva_entity_link *next;
} diva_entity_link_t;

typedef struct _diva_entity_queue {
	diva_entity_link_t *head;
	diva_entity_link_t *tail;
} diva_entity_queue_t;

typedef int (*diva_q_cmp_fn_t) (const void *what,
				const diva_entity_link_t *);

void diva_q_remove(diva_entity_queue_t * q, diva_entity_link_t * what);
void diva_q_add_tail(diva_entity_queue_t * q, diva_entity_link_t * what);
diva_entity_link_t *diva_q_find(const diva_entity_queue_t * q,
				const void *what, diva_q_cmp_fn_t cmp_fn);

diva_entity_link_t *diva_q_get_head(diva_entity_queue_t * q);
diva_entity_link_t *diva_q_get_tail(diva_entity_queue_t * q);
diva_entity_link_t *diva_q_get_next(diva_entity_link_t * what);
diva_entity_link_t *diva_q_get_prev(diva_entity_link_t * what);
int diva_q_get_nr_of_entries(const diva_entity_queue_t * q);
void diva_q_init(diva_entity_queue_t * q);

#endif
