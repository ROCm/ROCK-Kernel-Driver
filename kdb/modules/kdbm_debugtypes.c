/* this one has some additional address validation - untested */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 *
 * Most of this code is borrowed and adapted from the lkcd command "lcrash"
 * and its supporting libarary.
 *
 * This module provides kdb commands for casting memory structures.
 * It loads symbolic debugging info (provided from lcrash -o), and provides
 *  "print" "px", "pd"
 * (this information originally comes from the lcrash "kerntypes" file)
 *
 * A key here is tacking a file of debug info onto this module, for
 * load with it at insmod time.
 *
 * Careful of porting the klib KL_XXX functions (they call thru a jump table
 * that we don't use here)
 *
 * Usage:
 *  in order for the insmod kdbm_debugtypes.ko to succeed in loading types
 *  you must first use  lcrash -t kerntypes.xxxx -o debug_info
 *  and echo debug_info > /proc/kdb/debug_info_name
 */

#define VMALLOC_START_IA64 0xa000000200000000
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdb.h>
#include <linux/kdbprivate.h>
#include <linux/fs.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/fcntl.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/err.h>
#include "lcrash/klib.h"
#include "lcrash/kl_stringtab.h"
#include "lcrash/kl_btnode.h"
#include "lcrash/lc_eval.h"

MODULE_AUTHOR("SGI");
MODULE_DESCRIPTION("Load symbolic debugging information");
MODULE_LICENSE("GPL");

#undef next_node /* collision with nodemask.h */
static char	*stringstorage, **stringp_array;
static void	*filestorage;
static long	num_strings, num_kltypes, num_dsyms, stringstorage_size;
extern int	have_debug_file;
extern dbg_sym_t *types_tree_head;
extern dbg_sym_t *typedefs_tree_head;
extern kltype_t	 *kltype_array;
extern dbg_sym_t *dsym_types_array;
extern dbg_sym_t *type_tree;
extern dbg_sym_t *typedef_tree;

/*
 * use a pointer's value as an index in the stringp_array (num_strings) and
 * translate it to string address
 *
 * Return 0 for success, 1 for failure
 */
static int
index_to_char_ptr(char **ptrp)
{
	long	i;

	i = (long)*ptrp;
	/* we use a value of -1 to mean this was a null pointer */
	if (i == -1) {
		*ptrp = NULL;
		return 0;
	}
	if (i > num_strings-1) {
		printk("Could not translate character string index %#lx\n", i);
		return 1;
	}
	*ptrp = *(stringp_array+i);
	return 0;
}

/*
 * use a pointer's value as an index in the kltype_array (num_kltypes) and
 * translate it to the kltype_t address
 *
 * return 0 for success, 1 for failure
 */
static int
index_to_kltype_ptr(kltype_t **ptrp)
{
	long	i;

	i = (long)*ptrp;
	/* we use a value of -1 to mean this was a null pointer */
	if (i == -1) {
		*ptrp = NULL;
		return 0;
	}
	if (i > num_kltypes-1) {
		printk("Could not translate kl_type string index %#lx\n", i);
		return 1;
	}
	*ptrp = kltype_array+i;
	return 0;
}

/*
 * look up a pointer in the dsym_types_array (num_dsyms) and
 * translate it to the index in the array
 *
 * return 0 for success, 1 for failure
 */
static int
index_to_dbg_ptr(dbg_sym_t **ptrp)
{
	long	i;

	i = (long)*ptrp;
	/* we use a value of -1 to mean this was a null pointer */
	if (i == -1) {
		*ptrp = NULL;
		return 0;
	}
	if (i > num_dsyms-1) {
		printk("Could not translate dbg_sym_t index %#lx\n", i);
		return 1;
	}
	*ptrp = dsym_types_array+i;
	return 0;
}


/*
 * Work on the image of the file built by lcrash.
 * Unpack the strings, and resolve the pointers in the arrays of kltype_t's
 * and dbg_sym_t's to pointers.
 *
 * see lcrash's lib/libklib/kl_debug.c, which generates this file
 *
 * Return the pointers to the heads of the two binary trees by means of
 * pointer arguments.
 *
 * Return 0 for sucess, 1 for any error.
 */
static int
trans_file_image(void *file_storage, long file_size,  dbg_sym_t **type_treepp,
		dbg_sym_t **typedef_treepp)
{
	int		len;
	long		i, section_size, *lp, element_size;
	long		head_types_tree, head_typedefs_tree;
	char		*ptr, *stringsection, *kltypesection, *dbgsection;
	void		*kltypestorage, *dbgstorage;
	kltype_t	*klp;
	dbg_sym_t	*dbgp;

	/* 1) the strings */
	lp = (long *)file_storage;
	stringsection = (char *)lp;
	section_size = *lp++;
	num_strings = *lp++;
	lp++; /* element size does not apply the strings section */

	stringstorage_size = section_size - (3*sizeof(long));
	stringstorage = (char *)lp;

	stringp_array = (char **)vmalloc(num_strings * sizeof(char *));
	if (! stringp_array) {
		printk("vmalloc of %ld string pointers failed\n", num_strings);
		return 1;
	}
	ptr = stringstorage;
	for (i=0; i<num_strings; i++) {
		*(stringp_array+i) = ptr;
		len = strlen(ptr) + 1;
		ptr += len;
	}

	/* 2) the kltypes */
	kltypesection = (char *)(stringsection + section_size);
	lp = (long *)kltypesection;
	section_size = *lp++;
	num_kltypes = *lp++;
	element_size = *lp++;
	/* sanity check: */
	if (element_size != sizeof(kltype_t)) {
		printk("size of kltype_t:%ld does not match\n", element_size);
		goto bad;
	}
	kltypestorage = (void *)lp;
	kltype_array = (kltype_t *)kltypestorage;

	/* 3) the dbg_sym_t's */
	dbgsection = (char *)kltypesection + section_size;
	lp = (long *)dbgsection;
	section_size = *lp++;
	/* sanity check: */
	if ((dbgsection + section_size) != ((char *)file_storage+file_size)) {
		printk("dbg_sym_ts do not end at end of file\n");
		goto bad;
	}
	num_dsyms = *lp++;
	element_size = *lp++;
	/* sanity check: */
	if (element_size != sizeof(dbg_sym_t)) {
		printk("kdb: size of dbg_sym_t does not match lkcd\'s\n");
		goto bad;
	}

	/* two special words ahead of the structures themselves */
	head_types_tree = *lp++;
	head_typedefs_tree = *lp++;

	dbgstorage = (void *)lp;
	dsym_types_array = (dbg_sym_t *)dbgstorage;

	/* return the heads of the two binary trees */
	*type_treepp = dsym_types_array+head_types_tree;
	*typedef_treepp = dsym_types_array+head_typedefs_tree;

	/* translate the indices in our our array of kltype_t's to pointers */
	/*  (see write_kltype() for the fields that can be translated) */
	klp = kltype_array;
	for (i=0; i<num_kltypes; i++, klp++) {
		if (index_to_char_ptr(&klp->kl_name))
			goto bad;
		if (index_to_char_ptr(&klp->kl_typestr))
			goto bad;
		if (index_to_kltype_ptr(&klp->kl_member))
			goto bad;
		if (index_to_kltype_ptr(&klp->kl_next))
			goto bad;
		if (index_to_kltype_ptr(&klp->kl_realtype))
			goto bad;
		if (index_to_kltype_ptr(&klp->kl_indextype))
			goto bad;
		if (index_to_kltype_ptr(&klp->kl_elementtype))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&klp->kl_ptr))
			goto bad;
	}

	/* translate the indices in our our array of dbg_sym_t's to pointers */
	/*  (see write_dbgtype() for the fields that can be translated) */
	dbgp = dsym_types_array;
	for (i=0; i<num_dsyms; i++, dbgp++) {
		if (index_to_char_ptr(&dbgp->sym_bt.bt_key))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&dbgp->sym_bt.bt_left))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&dbgp->sym_bt.bt_right))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&dbgp->sym_bt.bt_parent))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&dbgp->sym_next))
			goto bad;
		if (index_to_dbg_ptr((dbg_sym_t **)&dbgp->sym_link))
			goto bad;
		if (index_to_kltype_ptr(&dbgp->sym_kltype))
			goto bad;
	}

	vfree(stringp_array);
	return 0;
bad:
	printk("trans_file_image() returning an error\n");
	vfree(stringp_array);
	return 1;
}

/* there is /proc interface to this string */
extern char kdb_debug_info_filename[];
/*
 * This is the module initialization function.
 */
static int __init
kdbm_debuginfo_init(void)
{
	int		len;
	long		ret, file_size;
	ssize_t		sizeread;
	mm_segment_t	fs;
	struct file	*file;
	loff_t		inode_size, pos;

	len = strlen(kdb_debug_info_filename);
	if (!len) {
		printk("kdb: no file name in /proc/kdb/debug_info_name\n");
		return -ENODEV;
	}

	fs = get_fs();     /* save previous value of address limits */
	set_fs (get_ds()); /* use kernel limit */

        file = filp_open(kdb_debug_info_filename, O_RDONLY, 0);
	if (IS_ERR(file)) {
		set_fs(fs);
		printk (
		  "kdb: open of %s (from /proc/kdb/debug_info_name) failed\n",
			kdb_debug_info_filename);
		return -ENODEV;
	}
	if (!file->f_op || (!file->f_op->read && !file->f_op->llseek)) {
		printk ("file has no operation for read or seek\n");
		set_fs(fs);
		return -ENODEV;
	}
	inode_size = file->f_dentry->d_inode->i_size;

	/*
	 * File has a header word on it that contains the size of the
	 * file.  We don't need it, but can use it as a sanity check.
	 */
	pos = 0;
	sizeread = file->f_op->read(file, (char *)&file_size,
					sizeof(file_size), &pos);
	if (sizeread != sizeof(file_size)) {
		printk("could not read %d bytes from %s\n",
			(int)sizeof(file_size), kdb_debug_info_filename);
		ret = filp_close(file, NULL);
		set_fs(fs);
		return -ENODEV;
	}
	if (inode_size != file_size) {
		printk("file says %ld, inode says %lld\n",
				file_size, inode_size);
		ret = filp_close(file, NULL);
		set_fs(fs);
		return -ENODEV;
	}

	/* space for the rest of the file: */
	file_size -= sizeof(long);
	filestorage = (void *)vmalloc(file_size);

	pos = sizeof(file_size); /* position after the header word */
	sizeread = file->f_op->read(file, (char *)filestorage,
                                        file_size, &pos);
	if (sizeread != file_size) {
		printk("could not read %ld bytes from %s\n",
			file_size, kdb_debug_info_filename);
		ret = filp_close(file, NULL);
		set_fs(fs);
		vfree (filestorage);
		return -ENODEV;
	}

	ret = filp_close(file, NULL);
	set_fs(fs); /* restore address limits before returning to user space */

	if (trans_file_image(filestorage, file_size, &types_tree_head,
						     &typedefs_tree_head)){
		vfree (filestorage);
		return -ENODEV;
	}
	printk("kdbm_debuginfo loaded %s\n", kdb_debug_info_filename);
	/* set the lcrash code's binary tree head nodes */
	type_tree = types_tree_head;
	typedef_tree = typedefs_tree_head;

	have_debug_file = 1;

	return 0;
}

/*
 * This is the module exit function.
 */
static void __exit
kdbm_debuginfo_exit(void)
{
	printk("kdbm_debuginfo unloaded %s\n", kdb_debug_info_filename);
	vfree (filestorage);
	have_debug_file = 0;
	return;
}

module_init(kdbm_debuginfo_init);
module_exit(kdbm_debuginfo_exit);
