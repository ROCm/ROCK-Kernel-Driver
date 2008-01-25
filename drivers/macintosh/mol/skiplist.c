/*
 *   Creation Date: <2003/03/03 23:19:47 samuel>
 *   Time-stamp: <2004/02/21 16:24:56 samuel>
 *
 *	<skiplist.c>
 *
 *	Skiplist implementation
 *
 *   Copyright (C) 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "archinclude.h"
#include "skiplist.h"
#include "alloc.h"

#define SKIPLIST_END 		INT_MAX		/* this key is reserved */

/*
 *	Skiplist Example:
 *
 *	level 0   ->   el1   ->   el2	->   el3   -->   el4   --> null_el
 *	level 1        -->        el2   ->   el3   -->   el4   --> null_el
 *	level 2        -->        el2        -->         el4   --> null_el
 *	level 3        -->        el2        ----->                null_el
 *	level 4               ------------>                        null_el
 *	...
 *	SKIPLIST_MAX_HEIGHT-1          ------------>                        null_el
 */

static unsigned int mol_rand_seed = 152;

static inline int
_cntlz( int val )
{
	int ret;
	asm volatile("cntlzw %0,%1" : "=r" (ret) : "r"(val) );
	return ret;
}

static unsigned long
mol_random( void )
{
	unsigned int t;
	asm( "mftb %0" : "=r"(t) : );
	mol_rand_seed = mol_rand_seed*69069L+1;
        return mol_rand_seed^t;
}

static void
mol_random_entropy( void )
{
	unsigned int entropy;
	asm( "mftb %0" : "=r" (entropy) : );
        mol_rand_seed ^= entropy;
}

static inline void
set_level_next( skiplist_level_t *level, skiplist_el_t *el )
{
	level->next = el;
#ifdef __darwin__
	level->next_phys = el ? tophys_mol(el) : 0;
#endif
}


/************************************************************************/
/*	skiplist operations						*/
/************************************************************************/

int
skiplist_prealloc( skiplist_t *sl, char *buf, unsigned int size,
		   skiplist_el_callback callback, void *usr1, void *usr2 )
{
	skiplist_el_t *p, *head;
	unsigned int s;
	int n, count;

	head = NULL;
	for( count=0 ;; size -= s, buf += s, count++ ) {
		for( n=0; n<SKIPLIST_MAX_HEIGHT-1 && (mol_random() & 0x40) ; n++ )
			;
		s = sl->datasize + sizeof(skiplist_t) + n*sizeof(skiplist_level_t);
		if( s > size )
			break;
		p = (skiplist_el_t*)(buf + sl->datasize);
		p->key = n;
		set_level_next( &p->level[0], head );
		head = p;
	}

	/* note: the callback is allowed to manipulate the skiplist */
	for( n=0, p=head; p; p=p->level[0].next, n++ ) {
		if( callback )
			(*callback)( (char*)p - sl->datasize, n, count, usr1, usr2 );
		if( !p->level[0].next ) {
			p->level[0] = sl->freelist;
			set_level_next( &sl->freelist, head );
			break;
		}
	}
	return count;
}

char *
skiplist_insert( skiplist_t *sl, int key )
{
	skiplist_el_t *pleft = (skiplist_el_t*)((char*)&sl->root[0] - offsetof(skiplist_el_t, level));
	skiplist_level_t el = sl->freelist;
	skiplist_el_t *p = el.next;
	int n, slev;

	if( !p )
		return NULL;
	sl->freelist = p->level[0];
	n = p->key;
	p->key = key;

	/* pick a good search level (the -3 is benchmarked) */
	sl->nel++;
	slev = 31 - _cntlz(sl->nel) - 3;
	if( slev > SKIPLIST_MAX_HEIGHT-1 )
		slev = SKIPLIST_MAX_HEIGHT-1;
	else if( slev < 0 )
		slev = 0;
	sl->slevel = slev;

	/* insert element */
	if( slev < n )
		slev = n;
	for( ; slev >= 0; slev-- ) {
		for( ; pleft->level[slev].next->key < key ; pleft=pleft->level[slev].next )
			;
		if( slev <= n ) {
			p->level[slev] = pleft->level[slev];
			pleft->level[slev] = el;
		}
	}

	return (char*)p - sl->datasize;
}

char *
skiplist_delete( skiplist_t *sl, int key )
{
	skiplist_el_t *p = (skiplist_el_t*)((char*)&sl->root[0] - offsetof(skiplist_el_t, level));
	skiplist_level_t delptr;
	int n, level = -1;

	delptr.next = 0;

	for( n=SKIPLIST_MAX_HEIGHT-1; n>=0; n-- ) {
		for( ; p->level[n].next->key < key ; p=p->level[n].next )
			;
		if( p->level[n].next->key != key )
			continue;

		if( level < 0 ) {
			delptr = p->level[n];
			level = n;
		}
		p->level[n] = delptr.next->level[n];
	}
	if( level < 0 )
		return NULL;

	/* put on freelist */
	p = delptr.next;
	p->key = level;
	p->level[0] = sl->freelist;
	sl->freelist = delptr;
	sl->nel--;

	return (char*)p - sl->datasize;
}

char *
skiplist_lookup( skiplist_t *sl, int key )
{
	skiplist_el_t *p = (skiplist_el_t*)((char*)&sl->root[0] - offsetof(skiplist_el_t, level));
	int n = sl->slevel;

	for( ;; ) {
		if( p->level[n].next->key < key ) {
			p = p->level[n].next;
			continue;
		}
		if( p->level[n].next->key > key ) {
			if( --n < 0 )
				break;
			continue;
		}
		return (char*)p->level[n].next - sl->datasize;
	}
	return NULL;
}

void
skiplist_init( skiplist_t *sl, int datasize )
{
	skiplist_level_t nilptr;
	int i;

	mol_random_entropy();

	memset( sl, 0, sizeof(*sl) );

	sl->nil_el.key = SKIPLIST_END;
	sl->datasize = datasize;

	/* remember: the nil element is of level 0 */
	set_level_next( &nilptr, &sl->nil_el );
	sl->nil_el.level[0] = nilptr;

	for( i=0; i < SKIPLIST_MAX_HEIGHT ; i++ )
		sl->root[i] = nilptr;
}
