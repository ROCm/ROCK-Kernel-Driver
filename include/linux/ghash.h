/*
 * include/linux/ghash.h -- generic hashing with fuzzy retrieval
 *
 * (C) 1997 Thomas Schoebel-Theuer
 *
 * The algorithms implemented here seem to be a completely new invention,
 * and I'll publish the fundamentals in a paper.
 */

#ifndef _GHASH_H
#define _GHASH_H
/* HASHSIZE _must_ be a power of two!!! */


#define DEF_HASH_FUZZY_STRUCTS(NAME,HASHSIZE,TYPE) \
\
struct NAME##_table {\
	TYPE * hashtable[HASHSIZE];\
	TYPE * sorted_list;\
	int nr_entries;\
};\
\
struct NAME##_ptrs {\
	TYPE * next_hash;\
	TYPE * prev_hash;\
	TYPE * next_sorted;\
	TYPE * prev_sorted;\
};

#define DEF_HASH_FUZZY(LINKAGE,NAME,HASHSIZE,TYPE,PTRS,KEYTYPE,KEY,KEYCMP,KEYEQ,HASHFN)\
\
LINKAGE void insert_##NAME##_hash(struct NAME##_table * tbl, TYPE * elem)\
{\
	int ix = HASHFN(elem->KEY);\
	TYPE ** base = &tbl->hashtable[ix];\
	TYPE * ptr = *base;\
	TYPE * prev = NULL;\
\
	tbl->nr_entries++;\
	while(ptr && KEYCMP(ptr->KEY, elem->KEY)) {\
		base = &ptr->PTRS.next_hash;\
		prev = ptr;\
		ptr = *base;\
	}\
	elem->PTRS.next_hash = ptr;\
	elem->PTRS.prev_hash = prev;\
	if(ptr) {\
		ptr->PTRS.prev_hash = elem;\
	}\
	*base = elem;\
\
	ptr = prev;\
	if(!ptr) {\
		ptr = tbl->sorted_list;\
		prev = NULL;\
	} else {\
		prev = ptr->PTRS.prev_sorted;\
	}\
	while(ptr) {\
		TYPE * next = ptr->PTRS.next_hash;\
		if(next && KEYCMP(next->KEY, elem->KEY)) {\
			prev = ptr;\
			ptr = next;\
		} else if(KEYCMP(ptr->KEY, elem->KEY)) {\
			prev = ptr;\
			ptr = ptr->PTRS.next_sorted;\
		} else\
			break;\
	}\
	elem->PTRS.next_sorted = ptr;\
	elem->PTRS.prev_sorted = prev;\
	if(ptr) {\
		ptr->PTRS.prev_sorted = elem;\
	}\
	if(prev) {\
		prev->PTRS.next_sorted = elem;\
	} else {\
		tbl->sorted_list = elem;\
	}\
}\
\
LINKAGE void remove_##NAME##_hash(struct NAME##_table * tbl, TYPE * elem)\
{\
	TYPE * next = elem->PTRS.next_hash;\
	TYPE * prev = elem->PTRS.prev_hash;\
\
	tbl->nr_entries--;\
	if(next)\
		next->PTRS.prev_hash = prev;\
	if(prev)\
		prev->PTRS.next_hash = next;\
	else {\
		int ix = HASHFN(elem->KEY);\
		tbl->hashtable[ix] = next;\
	}\
\
	next = elem->PTRS.next_sorted;\
	prev = elem->PTRS.prev_sorted;\
	if(next)\
		next->PTRS.prev_sorted = prev;\
	if(prev)\
		prev->PTRS.next_sorted = next;\
	else\
		tbl->sorted_list = next;\
}\
\
LINKAGE TYPE * find_##NAME##_hash(struct NAME##_table * tbl, KEYTYPE pos)\
{\
	int ix = hashfn(pos);\
	TYPE * ptr = tbl->hashtable[ix];\
	while(ptr && KEYCMP(ptr->KEY, pos))\
		ptr = ptr->PTRS.next_hash;\
	if(ptr && !KEYEQ(ptr->KEY, pos))\
		ptr = NULL;\
	return ptr;\
}\
\
LINKAGE TYPE * find_##NAME##_hash_fuzzy(struct NAME##_table * tbl, KEYTYPE pos)\
{\
	int ix;\
	int offset;\
	TYPE * ptr;\
	TYPE * next;\
\
	ptr = tbl->sorted_list;\
	if(!ptr || KEYCMP(pos, ptr->KEY))\
		return NULL;\
	ix = HASHFN(pos);\
	offset = HASHSIZE;\
	do {\
		offset >>= 1;\
		next = tbl->hashtable[(ix+offset) & ((HASHSIZE)-1)];\
		if(next && (KEYCMP(next->KEY, pos) || KEYEQ(next->KEY, pos))\
		   && KEYCMP(ptr->KEY, next->KEY))\
			ptr = next;\
	} while(offset);\
\
	for(;;) {\
		next = ptr->PTRS.next_hash;\
		if(next) {\
			if(KEYCMP(next->KEY, pos)) {\
				ptr = next;\
				continue;\
			}\
		}\
		next = ptr->PTRS.next_sorted;\
		if(next && KEYCMP(next->KEY, pos)) {\
			ptr = next;\
			continue;\
		}\
		return ptr;\
	}\
	return NULL;\
}

/* LINKAGE - empty or "static", depending on whether you want the definitions to
 *	be public or not
 * NAME - a string to stick in names to make this hash table type distinct from
 * 	any others
 * HASHSIZE - number of buckets
 * TYPE - type of data contained in the buckets - must be a structure, one
 * 	field is of type NAME_ptrs, another is the hash key
 * PTRS - TYPE must contain a field of type NAME_ptrs, PTRS is the name of that
 * 	field
 * KEYTYPE - type of the key field within TYPE
 * KEY - name of the key field within TYPE
 * KEYCMP - pointer to function that compares KEYTYPEs to each other - the
 * 	prototype is int KEYCMP(KEYTYPE, KEYTYPE), it returns zero for equal,
 * 	non-zero for not equal
 * HASHFN - the hash function - the prototype is int HASHFN(KEYTYPE),
 * 	it returns a number in the range 0 ... HASHSIZE - 1
 * Call DEF_HASH_STRUCTS, define your hash table as a NAME_table, then call
 * DEF_HASH.
 */

#define DEF_HASH_STRUCTS(NAME,HASHSIZE,TYPE) \
\
struct NAME##_table {\
	TYPE * hashtable[HASHSIZE];\
	int nr_entries;\
};\
\
struct NAME##_ptrs {\
	TYPE * next_hash;\
	TYPE * prev_hash;\
};

#define DEF_HASH(LINKAGE,NAME,TYPE,PTRS,KEYTYPE,KEY,KEYCMP,HASHFN)\
\
LINKAGE void insert_##NAME##_hash(struct NAME##_table * tbl, TYPE * elem)\
{\
	int ix = HASHFN(elem->KEY);\
	TYPE ** base = &tbl->hashtable[ix];\
	TYPE * ptr = *base;\
	TYPE * prev = NULL;\
\
	tbl->nr_entries++;\
	while(ptr && KEYCMP(ptr->KEY, elem->KEY)) {\
		base = &ptr->PTRS.next_hash;\
		prev = ptr;\
		ptr = *base;\
	}\
	elem->PTRS.next_hash = ptr;\
	elem->PTRS.prev_hash = prev;\
	if(ptr) {\
		ptr->PTRS.prev_hash = elem;\
	}\
	*base = elem;\
}\
\
LINKAGE void remove_##NAME##_hash(struct NAME##_table * tbl, TYPE * elem)\
{\
	TYPE * next = elem->PTRS.next_hash;\
	TYPE * prev = elem->PTRS.prev_hash;\
\
	tbl->nr_entries--;\
	if(next)\
		next->PTRS.prev_hash = prev;\
	if(prev)\
		prev->PTRS.next_hash = next;\
	else {\
		int ix = HASHFN(elem->KEY);\
		tbl->hashtable[ix] = next;\
	}\
}\
\
LINKAGE TYPE * find_##NAME##_hash(struct NAME##_table * tbl, KEYTYPE pos)\
{\
	int ix = HASHFN(pos);\
	TYPE * ptr = tbl->hashtable[ix];\
	while(ptr && KEYCMP(ptr->KEY, pos))\
		ptr = ptr->PTRS.next_hash;\
	return ptr;\
}

#endif
