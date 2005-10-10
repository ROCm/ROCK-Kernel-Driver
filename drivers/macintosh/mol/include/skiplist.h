/* 
 *   Creation Date: <2003/03/03 22:59:04 samuel>
 *   Time-stamp: <2003/08/15 23:40:38 samuel>
 *   
 *	<skiplist.h>
 *	
 *	Skiplist implementation
 *   
 *   Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *   
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *   
 */

#ifndef _H_SKIPLIST
#define _H_SKIPLIST

#define SKIPLIST_MAX_HEIGHT	16

/* data (of datasize) is stored before the skiplist_el */
typedef struct skiplist_el {
	int			key;
	struct skiplist_el	*next[1];		/* level 0 */
	/* level 1..n are optionally stored here */
} skiplist_el_t, *skiplist_iter_t;

typedef struct {
	int			nel;
	int			slevel;			/* start level */
	int			datasize;		/* size of data (stored before each key) */
	
	skiplist_el_t		*root[SKIPLIST_MAX_HEIGHT];
	skiplist_el_t		nil_el;

	skiplist_el_t		*freelist;		/* key = level, linked list in next[0] */
} skiplist_t;

static inline int
skiplist_getnext( skiplist_t *sl, skiplist_iter_t *iterator, char **data ) 
{
	skiplist_el_t *el = *iterator;
	*data = (char*)el - sl->datasize;
	*iterator = el->next[0];
	return el != &sl->nil_el;
}

static inline int
skiplist_iter_getkey( skiplist_t *sl, char *data )
{
	return ((skiplist_el_t*)(data + sl->datasize))->key;
}

static inline skiplist_iter_t
skiplist_iterate( skiplist_t *sl )
{
	return sl->root[0];
}

static inline int
skiplist_needalloc( skiplist_t *sl )
{
	return !sl->freelist;
}

typedef void	(*skiplist_el_callback)( char *data, int ind, int n, void *usr1, void *usr2 );

extern void	skiplist_init( skiplist_t *sl, int datasize );
extern int	skiplist_prealloc( skiplist_t *sl, char *buf, unsigned int size,
				   skiplist_el_callback callback, void *usr1, void *usr2 );

extern char	*skiplist_insert( skiplist_t *sl, int key );
extern char	*skiplist_delete( skiplist_t *sl, int key );
extern char	*skiplist_lookup( skiplist_t *sl, int key );


#endif   /* _H_SKIPLIST */
