/*
 * AppArmor security module
 *
 * This file contains AppArmor /sys/kernel/secrutiy/apparmor interface functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 *
 * This file contain functions providing an interface for <= AppArmor 2.4
 * compatibility.  It is dependent on CONFIG_SECURITY_APPARMOR_COMPAT_24
 * being set (see Makefile).
 */

#include <linux/security.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/namei.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/policy.h"


/* apparmor/matching */
static ssize_t aa_matching_read(struct file *file, char __user *buf,
				size_t size, loff_t *ppos)
{
	const char matching[] = "pattern=aadfa audit perms=crwxamlk/ "
	    "user::other";

	return simple_read_from_buffer(buf, size, ppos, matching,
				       sizeof(matching) - 1);
}

const struct file_operations aa_fs_matching_fops = {
	.read = aa_matching_read,
};

/* apparmor/features */
static ssize_t aa_features_read(struct file *file, char __user *buf,
				size_t size, loff_t *ppos)
{
	const char features[] = "file=3.1 capability=2.0 network=1.0 "
	    "change_hat=1.5 change_profile=1.1 " "aanamespaces=1.1 rlimit=1.1";

	return simple_read_from_buffer(buf, size, ppos, features,
				       sizeof(features) - 1);
}

const struct file_operations aa_fs_features_fops = {
	.read = aa_features_read,
};

/**
 * __next_namespace - find the next namespace to list
 * @root: root namespace to stop search at (NOT NULL)
 * @ns: current ns position (NOT NULL)
 *
 * Find the next namespace from @ns under @root and handle all locking needed
 * while switching current namespace.
 *
 * Returns: next namespace or NULL if at last namespace under @root
 * NOTE: will not unlock root->lock
 */
static struct aa_namespace *__next_namespace(struct aa_namespace *root,
					     struct aa_namespace *ns)
{
	struct aa_namespace *parent;

	/* is next namespace a child */
	if (!list_empty(&ns->sub_ns)) {
		struct aa_namespace *next;
		next = list_first_entry(&ns->sub_ns, typeof(*ns), base.list);
		read_lock(&next->lock);
		return next;
	}

	/* check if the next ns is a sibling, parent, gp, .. */
	parent = ns->parent;
	while (parent) {
		read_unlock(&ns->lock);
		list_for_each_entry_continue(ns, &parent->sub_ns, base.list) {
			read_lock(&ns->lock);
			return ns;
		}
		if (parent == root)
			return NULL;
		ns = parent;
		parent = parent->parent;
	}

	return NULL;
}

/**
 * __first_profile - find the first profile in a namespace
 * @root: namespace that is root of profiles being displayed (NOT NULL)
 * @ns: namespace to start in   (NOT NULL)
 *
 * Returns: unrefcounted profile or NULL if no profile
 */
static struct aa_profile *__first_profile(struct aa_namespace *root,
					  struct aa_namespace *ns)
{
	for ( ; ns; ns = __next_namespace(root, ns)) {
		if (!list_empty(&ns->base.profiles))
			return list_first_entry(&ns->base.profiles,
						struct aa_profile, base.list);
	}
	return NULL;
}

/**
 * __next_profile - step to the next profile in a profile tree
 * @profile: current profile in tree (NOT NULL)
 *
 * Perform a depth first taversal on the profile tree in a namespace
 *
 * Returns: next profile or NULL if done
 * Requires: profile->ns.lock to be held
 */
static struct aa_profile *__next_profile(struct aa_profile *p)
{
	struct aa_profile *parent;
	struct aa_namespace *ns = p->ns;

	/* is next profile a child */
	if (!list_empty(&p->base.profiles))
		return list_first_entry(&p->base.profiles, typeof(*p),
					base.list);

	/* is next profile a sibling, parent sibling, gp, subling, .. */
	parent = p->parent;
	while (parent) {
		list_for_each_entry_continue(p, &parent->base.profiles,
					     base.list)
				return p;
		p = parent;
		parent = parent->parent;
	}

	/* is next another profile in the namespace */
	list_for_each_entry_continue(p, &ns->base.profiles, base.list)
		return p;

	return NULL;
}

/**
 * next_profile - step to the next profile in where ever it may be
 * @root: root namespace  (NOT NULL)
 * @profile: current profile  (NOT NULL)
 *
 * Returns: next profile or NULL if there isn't one
 */
static struct aa_profile *next_profile(struct aa_namespace *root,
				       struct aa_profile *profile)
{
	struct aa_profile *next = __next_profile(profile);
	if (next)
		return next;

	/* finished all profiles in namespace move to next namespace */
	return __first_profile(root, __next_namespace(root, profile->ns));
}

/**
 * p_start - start a depth first traversal of profile tree
 * @f: seq_file to fill
 * @pos: current position
 *
 * Returns: first profile under current namespace or NULL if none found
 *
 * acquires first ns->lock
 */
static void *p_start(struct seq_file *f, loff_t *pos)
	__acquires(root->lock)
{
	struct aa_profile *profile = NULL;
	struct aa_namespace *root = aa_current_profile()->ns;
	loff_t l = *pos;
	f->private = aa_get_namespace(root);


	/* find the first profile */
	read_lock(&root->lock);
	profile = __first_profile(root, root);

	/* skip to position */
	for (; profile && l > 0; l--)
		profile = next_profile(root, profile);

	return profile;
}

/**
 * p_next - read the next profile entry
 * @f: seq_file to fill
 * @p: profile previously returned
 * @pos: current position
 *
 * Returns: next profile after @p or NULL if none
 *
 * may acquire/release locks in namespace tree as necessary
 */
static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct aa_profile *profile = p;
	struct aa_namespace *root = f->private;
	(*pos)++;

	return next_profile(root, profile);
}

/**
 * p_stop - stop depth first traversal
 * @f: seq_file we are filling
 * @p: the last profile writen
 *
 * Release all locking done by p_start/p_next on namespace tree
 */
static void p_stop(struct seq_file *f, void *p)
	__releases(root->lock)
{
	struct aa_profile *profile = p;
	struct aa_namespace *root = f->private, *ns;

	if (profile) {
		for (ns = profile->ns; ns && ns != root; ns = ns->parent)
			read_unlock(&ns->lock);
	}
	read_unlock(&root->lock);
	aa_put_namespace(root);
}

/**
 * seq_show_profile - show a profile entry
 * @f: seq_file to file
 * @p: current position (profile)    (NOT NULL)
 *
 * Returns: error on failure
 */
static int seq_show_profile(struct seq_file *f, void *p)
{
	struct aa_profile *profile = (struct aa_profile *)p;
	struct aa_namespace *root = f->private;

	if (profile->ns != root)
		seq_printf(f, ":%s://", aa_ns_name(root, profile->ns));
	seq_printf(f, "%s (%s)\n", profile->base.hname,
		   COMPLAIN_MODE(profile) ? "complain" : "enforce");

	return 0;
}

static const struct seq_operations aa_fs_profiles_op = {
	.start = p_start,
	.next = p_next,
	.stop = p_stop,
	.show = seq_show_profile,
};

static int profiles_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &aa_fs_profiles_op);
}

static int profiles_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

const struct file_operations aa_fs_profiles_fops = {
	.open = profiles_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = profiles_release,
};
