/*
 * fs/fs-writeback.c
 *
 * Copyright (C) 2004, Novell, Inc.
 *
 * Contains all the fshooks support functions.
 *
 */

#include <linux/fshooks.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>

struct fshook_list fshooks[fshook_COUNT];
static int initialized;

int fshook_register(enum FShook type, fshook_pre_t *pre, fshook_post_t *post, void *ctx)
{
	struct fshook *hook;

	if (unlikely(type < 0 || type >= fshook_COUNT || (!pre && !post)))
		return -EINVAL;
	if (unlikely(!initialized))
		return -EPERM;
	hook = kmalloc(sizeof(*hook), GFP_KERNEL);
	if (unlikely(!hook))
		return -ENOMEM;
	hook->next = NULL;
	hook->pre = pre;
	hook->post = post;
	hook->ctx = ctx;
	down_write(&fshooks[type].lock);
	if (fshooks[type].last)
		fshooks[type].last->next = hook;
	else
		fshooks[type].first = hook;
	fshooks[type].last = hook;
	mb();
	up_write(&fshooks[type].lock);
	return 0;
}

EXPORT_SYMBOL(fshook_register);

int fshook_deregister(enum FShook type, fshook_pre_t *pre, fshook_post_t *post, void *ctx)
{
	struct fshook *hook, *prev;

	if (unlikely(type < 0 || type >= fshook_COUNT || (!pre && !post)))
		return -EINVAL;
	if (unlikely(!initialized))
		return -EPERM;
	if (unlikely(!fshooks[type].first))
		return -ENOENT;
	down_write(&fshooks[type].lock);
	for (hook = fshooks[type].first, prev = NULL; hook; hook = (prev = hook)->next) {
		if (ctx == hook->ctx
		    && (!pre || !hook->pre || pre == hook->pre)
		    && (!post || !hook->post || post == hook->post)) {
			if (pre == hook->pre)
				hook->pre = NULL;
			if (post == hook->post)
				hook->post = NULL;
			if (!hook->pre && !hook->post) {
				if (prev)
					prev->next = hook->next;
				else
					fshooks[type].first = hook->next;
				if (hook == fshooks[type].last)
					fshooks[type].last = prev;
			}
			else
				prev = hook;
			break;
		}
	}
	mb();
	up_write(&fshooks[type].lock);
	if (unlikely(!hook))
		return -ENOENT;
	if (prev != hook)
		kfree(hook);
	return 0;
}

EXPORT_SYMBOL(fshook_deregister);

member_type(struct fshook_generic_info, result) fshook_run_pre(enum FShook type, fshook_info_t info)
{
	if (unlikely(type >= fshook_COUNT))
		BUG();
	if (fshooks[type].first) {
		struct fshook_generic_info *gen = (struct fshook_generic_info *)info.gen;
		const struct fshook *hook;

		gen->type = type;
		gen->result = 0;
		down_read(&fshooks[type].lock);
		for (hook = fshooks[type].first; hook; hook = hook->next) {
			if (hook->pre) {
				int err = hook->pre(info, hook->ctx);

				if (!gen->result && err) {
					gen->result = -abs(err);
					if (unlikely(!IS_ERR(ERR_PTR(gen->result))))
						BUG();
				}
			}
		}
		up_read(&fshooks[type].lock);
		return info.gen->result;
	}
	return 0;
}

void fshook_run_post(fshook_info_t info, member_type(struct fshook_generic_info, result) result)
{
	if (fshooks[info.gen->type].first) {
		struct fshook_generic_info *gen = (struct fshook_generic_info *)info.gen;
		const struct fshook *hook;

		gen->result = result;
		down_read(&fshooks[gen->type].lock);
		for (hook = fshooks[gen->type].first; hook; hook = hook->next) {
			if (hook->post)
				hook->post(info, hook->ctx);
		}
		up_read(&fshooks[gen->type].lock);
	}
}

#include <linux/init.h>

void __init fshooks_init(void)
{
	enum FShook type;

	for (type = 0; type < fshook_COUNT; ++type) {
		init_rwsem(&fshooks[type].lock);
	}
	initialized = 1;
}
