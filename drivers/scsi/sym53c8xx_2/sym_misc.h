/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef SYM_MISC_H
#define SYM_MISC_H

/*
 *  A la VMS/CAM-3 queue management.
 */
typedef struct sym_quehead {
	struct sym_quehead *flink;	/* Forward  pointer */
	struct sym_quehead *blink;	/* Backward pointer */
} SYM_QUEHEAD;

#define sym_que_init(ptr) do { \
	(ptr)->flink = (ptr); (ptr)->blink = (ptr); \
} while (0)

static __inline struct sym_quehead *sym_que_first(struct sym_quehead *head)
{
	return (head->flink == head) ? 0 : head->flink;
}

static __inline struct sym_quehead *sym_que_last(struct sym_quehead *head)
{
	return (head->blink == head) ? 0 : head->blink;
}

static __inline void __sym_que_add(struct sym_quehead * new,
	struct sym_quehead * blink,
	struct sym_quehead * flink)
{
	flink->blink	= new;
	new->flink	= flink;
	new->blink	= blink;
	blink->flink	= new;
}

static __inline void __sym_que_del(struct sym_quehead * blink,
	struct sym_quehead * flink)
{
	flink->blink = blink;
	blink->flink = flink;
}

static __inline int sym_que_empty(struct sym_quehead *head)
{
	return head->flink == head;
}

static __inline void sym_que_splice(struct sym_quehead *list,
	struct sym_quehead *head)
{
	struct sym_quehead *first = list->flink;

	if (first != list) {
		struct sym_quehead *last = list->blink;
		struct sym_quehead *at   = head->flink;

		first->blink = head;
		head->flink  = first;

		last->flink = at;
		at->blink   = last;
	}
}

static __inline void sym_que_move(struct sym_quehead *orig,
	struct sym_quehead *dest)
{
	struct sym_quehead *first, *last;

	first = orig->flink;
	if (first != orig) {
		first->blink = dest;
		dest->flink  = first;
		last = orig->blink;
		last->flink  = dest;
		dest->blink  = last;
		orig->flink  = orig;
		orig->blink  = orig;
	} else {
		dest->flink  = dest;
		dest->blink  = dest;
	}
}

#define sym_que_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned int)(&((type *)0)->member)))


#define sym_insque(new, pos)		__sym_que_add(new, pos, (pos)->flink)

#define sym_remque(el)			__sym_que_del((el)->blink, (el)->flink)

#define sym_insque_head(new, head)	__sym_que_add(new, head, (head)->flink)

static __inline struct sym_quehead *sym_remque_head(struct sym_quehead *head)
{
	struct sym_quehead *elem = head->flink;

	if (elem != head)
		__sym_que_del(head, elem->flink);
	else
		elem = NULL;
	return elem;
}

#define sym_insque_tail(new, head)	__sym_que_add(new, (head)->blink, head)

static __inline struct sym_quehead *sym_remque_tail(struct sym_quehead *head)
{
	struct sym_quehead *elem = head->blink;

	if (elem != head)
		__sym_que_del(elem->blink, head);
	else
		elem = 0;
	return elem;
}

/*
 *  This one may be useful.
 */
#define FOR_EACH_QUEUED_ELEMENT(head, qp) \
	for (qp = (head)->flink; qp != (head); qp = qp->flink)
/*
 *  FreeBSD does not offer our kind of queue in the CAM CCB.
 *  So, we have to cast.
 */
#define sym_qptr(p)	((struct sym_quehead *) (p))

/*
 *  Simple bitmap operations.
 */ 
#define sym_set_bit(p, n)	(((u32 *)(p))[(n)>>5] |=  (1<<((n)&0x1f)))
#define sym_clr_bit(p, n)	(((u32 *)(p))[(n)>>5] &= ~(1<<((n)&0x1f)))
#define sym_is_bit(p, n)	(((u32 *)(p))[(n)>>5] &   (1<<((n)&0x1f)))

/*
 * The below round up/down macros are to be used with a constant 
 * as argument (sizeof(...) for example), for the compiler to 
 * optimize the whole thing.
 */
#define _U_(a,m)	(a)<=(1<<m)?m:

/*
 * Round up logarithm to base 2 of a 16 bit constant.
 */
#define _LGRU16_(a) \
( \
 _U_(a, 0)_U_(a, 1)_U_(a, 2)_U_(a, 3)_U_(a, 4)_U_(a, 5)_U_(a, 6)_U_(a, 7) \
 _U_(a, 8)_U_(a, 9)_U_(a,10)_U_(a,11)_U_(a,12)_U_(a,13)_U_(a,14)_U_(a,15) \
 16)

#endif /* SYM_MISC_H */
