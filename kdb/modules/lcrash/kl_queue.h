/*
 * $Id: kl_queue.h 1122 2004-12-21 23:26:23Z tjm $
 *
 * This file is part of libutil.
 * A library which provides auxiliary functions.
 * libutil is part of lkcdutils -- utilities for Linux kernel crash dumps.
 *
 * Created by Silicon Graphics, Inc.
 * Contributions by IBM, NEC, and others
 *
 * Copyright (C) 1999 - 2002 Silicon Graphics, Inc. All rights reserved.
 * Copyright (C) 2001, 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Copyright 2000 Junichi Nomura, NEC Solutions <j-nomura@ce.jp.nec.com>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version. See the file COPYING for more
 * information.
 */

#ifndef __KL_QUEUE_H
#define __KL_QUEUE_H

/* List element header
 */
typedef struct element_s {
	struct element_s    *next;
	struct element_s    *prev;
} element_t;

/* Some useful macros
 */
#define ENQUEUE(list, elem) \
	kl_enqueue((element_t **)list, (element_t *)elem)
#define DEQUEUE(list) kl_dequeue((element_t **)list)
#define FINDQUEUE(list, elem) \
	kl_findqueue((element_t **)list, (element_t *)elem)
#define REMQUEUE(list, elem) kl_remqueue((element_t **)list, (element_t *)elem)

typedef struct list_of_ptrs {
	element_t		elem;
	unsigned long long 	val64;
} list_of_ptrs_t;

#define FINDLIST_QUEUE(list, elem, compare) \
	kl_findlist_queue((list_of_ptrs_t **)list, \
		(list_of_ptrs_t *)elem, compare)

/**
 ** Function prototypes
 **/

/* Add a new element to the tail of a doubly linked list.
 */
void kl_enqueue(
	element_t**	/* ptr to head of list */,
	element_t*	/* ptr to element to add to the list */);

/* Remove an element from the head of a doubly linked list. A pointer
 * to the element will be returned. In the event that the list is
 * empty, a NULL pointer will be returned.
 */
element_t *kl_dequeue(
	element_t**	/* ptr to list head (first item removed) */);

/* Checks to see if a particular element is in a list. If it is, a
 * value of one (1) will be returned. Otherwise, a value of zero (0)
 * will be returned.
 */
int kl_findqueue(
	element_t**	/* ptr to head of list */,
	element_t*	/* ptr to element to find on list */);

/* Walks through a list of pointers to queues and looks for a
 * particular list.
 */
int kl_findlist_queue(
	list_of_ptrs_t** 	/* ptr to list of lists */,
	list_of_ptrs_t* 	/* ptr to list to look for */,
	int(*)(void *, void *)	/* ptr to compare function */);

/* Remove specified element from doubly linked list.
 */
void kl_remqueue(
	element_t**		/* ptr to head of list */,
	element_t*		/* ptr to element to remove from list */);

#endif /* __KL_QUEUE_H */
