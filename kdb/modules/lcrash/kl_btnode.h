/*
 * $Id: kl_btnode.h 1122 2004-12-21 23:26:23Z tjm $
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

#ifndef __KL_BTNODE_H
#define __KL_BTNODE_H

/*
 * Node header struct for use in binary search tree routines
 */
typedef struct btnode_s {
	struct btnode_s     *bt_left;
	struct btnode_s     *bt_right;
	struct btnode_s     *bt_parent;
	char                *bt_key;
	int                  bt_height;
} btnode_t;

#define DUPLICATES_OK   1

/**
 ** btnode operation function prototypes
 **/

/* Return the hight of a given btnode_s struct in a tree. In the
 * event of an error (a NULL btnode_s pointer was passed in), a
 * value of -1 will be returned.
 */
int kl_btnode_height(
	btnode_t*	/* pointer to btnode_s struct */);

/* Insert a btnode_s struct into a tree. After the insertion, the
 * tree will be left in a reasonibly ballanced state. Note that, if
 * the DUPLICATES_OK flag is set, duplicate keys will be inserted
 * into the tree (otherwise return an error). In the event of an
 * error, a value of -1 will be returned.
 */
int kl_insert_btnode(
	btnode_t**	/* pointer to root of tree */,
	btnode_t*	/* pointer to btnode_s struct to insert */,
	int		/* flags (DUPLICATES_OK) */);

/* Finds a btnode in a tree and removes it, making sure to keep
 * the tree in a reasonably balanced state. As part of the
 * delete_btnode() operation, a call will be made to the free
 * function (passed in as a parameter) to free any application
 * specific data.
 */
int kl_delete_btnode(
	btnode_t**	/* pointer to the root of the btree */,
	btnode_t*	/* pointer to btnode_s struct to delete */,
	void(*)(void*)	/* pointer to function to actually free the node */,
	int		/* flags */);

/* Traverse a tree looking for a particular key. In the event that
 * duplicate keys are allowed in the tree, returns the first occurance
 * of the search key found. A pointer to an int should be passed in
 * to hold the maximum depth reached in the search. Upon success,
 * returns a pointer to a btnode_s struct. Otherwise, a NULL pointer
 * will be returned.
 */
btnode_t *_kl_find_btnode(
	btnode_t*	/* pointer to btnode_s struct to start search with */,
	char*		/* key we are looking for */,
	int*		/* pointer to where max depth vlaue will be placed */,
	size_t          /* if nonzero compare only first n chars of key */);
#define kl_find_btnode(A, B, C) _kl_find_btnode(A, B, C, 0)

btnode_t *kl_first_btnode(
	btnode_t *	/* pointer to any btnode in a btree */);

btnode_t *kl_next_btnode(
	btnode_t *	/* pointer to current btnode */);

btnode_t *kl_prev_btnode(
	btnode_t *	/* Pointer to current btnode */);

#endif /* __KL_BTNODE_H */
