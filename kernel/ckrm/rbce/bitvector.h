/*
 * Copyright (C) Hubertus Franke, IBM Corp. 2003
 * 
 * Bitvector package
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 15 Nov 2003
 *        Created
 */

#ifndef BITVECTOR_H
#define BITVECTOR_H

typedef struct {
	int size;				// maxsize in longs
	unsigned long bits[0];	// bit vector
} bitvector_t;

#define BITS_2_LONGS(sz)  (((sz)+BITS_PER_LONG-1)/BITS_PER_LONG)
#define BITS_2_BYTES(sz)  (((sz)+7)/8)

#if 0
#define CHECK_VEC(vec) (vec)              /* check against NULL */
#else
#define CHECK_VEC(vec) (1)                /* assume no problem */
#endif

#define CHECK_VEC_VOID(vec)   do { if (!CHECK_VEC(vec)) return; } while(0)
#define CHECK_VEC_RC(vec, val) do { if (!CHECK_VEC(vec)) return (val); } while(0)

inline static void
bitvector_zero(bitvector_t *bitvec)
{
	int sz;

	CHECK_VEC_VOID(bitvec);
	sz = BITS_2_BYTES(bitvec->size);
	memset(bitvec->bits, 0, sz);
	return;
}

inline static unsigned long
bitvector_bytes(unsigned long size)
{
	return sizeof(bitvector_t) + BITS_2_BYTES(size);
}

inline static void
bitvector_init(bitvector_t *bitvec, unsigned long size)
{
	bitvec->size = size;
	bitvector_zero(bitvec);
	return;
}

inline static bitvector_t *
bitvector_alloc(unsigned long size)
{
	bitvector_t *vec = (bitvector_t*) kmalloc(bitvector_bytes(size), GFP_KERNEL);
	if (vec) {
		vec->size = size;
		bitvector_zero(vec);
	}
	return vec;
}

inline static void
bitvector_free(bitvector_t *bitvec) 
{ 
	CHECK_VEC_VOID(bitvec);
	kfree(bitvec); 
	return;
}

#define def_bitvec_op(name,mod1,op,mod2) \
inline static int \
name(bitvector_t *res, bitvector_t *op1, bitvector_t *op2) \
{ \
	unsigned int i, size; \
 \
	CHECK_VEC_RC(res, 0); \
	CHECK_VEC_RC(op1, 0); \
	CHECK_VEC_RC(op2, 0); \
	size = res->size; \
	if (((size != (op1)->size) || (size != (op2)->size))) { \
		return 0; \
	} \
	size = BITS_2_LONGS(size); \
	for (i = 0; i < size; i++) { \
		(res)->bits[i] = (mod1 (op1)->bits[i]) op (mod2 (op2)->bits[i]); \
	} \
	return 1; \
}

def_bitvec_op(bitvector_or     , ,|, );
def_bitvec_op(bitvector_and    , ,&, );
def_bitvec_op(bitvector_xor    , ,^, );
def_bitvec_op(bitvector_or_not , ,|,~);
def_bitvec_op(bitvector_not_or ,~,|, );
def_bitvec_op(bitvector_and_not, ,&,~);
def_bitvec_op(bitvector_not_and,~,&, );

inline static void
bitvector_set(int idx, bitvector_t *vec)
{
	set_bit(idx, vec->bits);
	return;
}

inline static void
bitvector_clear(int idx, bitvector_t *vec)
{
	clear_bit(idx, vec->bits);
	return;
}

inline static int
bitvector_test(int idx, bitvector_t *vec)
{
	return test_bit(idx, vec->bits);
}

#ifdef DEBUG
inline static void
bitvector_print(int flag, bitvector_t *vec)
{
	unsigned int i; 
	int sz;
	extern int rbcedebug;

	if ((rbcedebug & flag) == 0) {
		return;
	}
	if (vec == NULL) { 
		printk("v<0>-NULL\n"); 
		return; 
	}
	printk("v<%d>-", sz = vec->size);
	for (i = 0; i < sz; i++) {
		printk("%c", test_bit(i, vec->bits) ? '1' : '0');
	}
	return;
}
#else 
#define bitvector_print(x, y)
#endif

#endif // BITVECTOR_H
