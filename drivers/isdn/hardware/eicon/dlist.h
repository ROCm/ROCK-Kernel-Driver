/* $Id: dlist.h,v 1.5 2003/08/25 16:03:35 schindler Exp $ */

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

#endif
