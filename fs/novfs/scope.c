/*
 * Novell NCP Redirector for Linux
 * Author:           James Turner
 *
 * This file contains functions used to scope users.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/personality.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/synclink.h>
#include <linux/smp_lock.h>
#include <asm/semaphore.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include "vfs.h"

#define SHUTDOWN_INTERVAL     5
#define CLEANUP_INTERVAL      10
#define MAX_USERNAME_LENGTH   32

struct scope_list {
	struct list_head entry;
	struct schandle ScopeId;
	struct schandle SessionId;
	pid_t ScopePid;
	struct task_struct *ScopeTask;
	unsigned int ScopeHash;
	uid_t ScopeUid;
	u64 ScopeUSize;
	u64 ScopeUFree;
	u64 ScopeUTEnties;
	u64 ScopeUAEnties;
	int ScopeUserNameLength;
	unsigned char ScopeUserName[MAX_USERNAME_LENGTH];
};

static struct list_head Scope_List;
static struct semaphore Scope_Lock;
static struct semaphore Scope_Thread_Delay;
static int Scope_Thread_Terminate;
static struct timer_list Scope_Timer;
static unsigned int Scope_Hash_Val = 1;

static struct scope_list *Scope_Search4Scope(struct schandle *Id, bool Session,
					     bool Locked)
{
	struct scope_list *scope;
	struct scope_list *rscope = NULL;
	struct schandle *cur_scope;
	struct list_head *sl;
	int offset;

	DbgPrint("Scope_Search4Scope: 0x%p:%p 0x%x 0x%x\n",
		 Id->hTypeId, Id->hId, Session, Locked);

	if (Session)
		offset = offsetof(struct scope_list, SessionId);
	else
		offset = offsetof(struct scope_list, ScopeId);

	if (!Locked)
		down(&Scope_Lock);

	sl = Scope_List.next;
	DbgPrint("Scope_Search4Scope: 0x%p\n", sl);
	while (sl != &Scope_List) {
		scope = list_entry(sl, struct scope_list, entry);

		cur_scope = (session_t *) ((char *)scope + offset);
		if (SC_EQUAL(Id, cur_scope)) {
			rscope = scope;
			break;
		}

		sl = sl->next;
	}

	if (!Locked)
		up(&Scope_Lock);

	DbgPrint("Scope_Search4Scope: return 0x%p\n", rscope);
	return rscope;
}

static struct scope_list *Scope_Find_Scope(bool Create)
{
	struct scope_list *scope = NULL;
	struct scope_list *pscope = NULL;
	struct task_struct *task;
	struct schandle scopeId;
	int addscope = 0;

	task = current;

	DbgPrint("Scope_Find_Scope: %d %d %d %d\n", task->uid, task->euid,
		 task->suid, task->fsuid);

	/* scopeId = task->euid; */
	UID_TO_SCHANDLE(scopeId, task->euid);

	scope = Scope_Search4Scope(&scopeId, 0, 0);
	if (scope || (!Create))
		return scope;

	scope = kmalloc(sizeof(*pscope), GFP_KERNEL);
	if (!scope)
		return NULL;
	scope->ScopeId = scopeId;
	SC_INITIALIZE(scope->SessionId);
	scope->ScopePid = task->pid;
	scope->ScopeTask = task;
	scope->ScopeHash = 0;
	scope->ScopeUid = task->euid;
	scope->ScopeUserName[0] = '\0';

	if (!Daemon_CreateSessionId(&scope->SessionId)) {
		DbgPrint("Scope_Find_Scope2: %d %d %d %d\n", task->uid,
			 task->euid, task->suid, task->fsuid);
		memset(scope->ScopeUserName, 0, sizeof(scope->ScopeUserName));
		scope->ScopeUserNameLength = 0;
		Daemon_getpwuid(task->euid, sizeof(scope->ScopeUserName),
				scope->ScopeUserName);
		scope->ScopeUserNameLength = strlen(scope->ScopeUserName);
		addscope = 1;
	}

	scope->ScopeHash = Scope_Hash_Val++;
	DbgPrint("Scope_Find_Scope: Adding 0x%p\n"
		 "   ScopeId:             0x%p:%p\n"
		 "   SessionId:           0x%p:%p\n"
		 "   ScopePid:            %d\n"
		 "   ScopeTask:           0x%p\n"
		 "   ScopeHash:           %u\n"
		 "   ScopeUid:            %u\n"
		 "   ScopeUserNameLength: %u\n"
		 "   ScopeUserName:       %s\n",
		 scope,
		 scope->ScopeId.hTypeId, scope->ScopeId.hId,
		 scope->SessionId.hTypeId, scope->SessionId.hId,
		 scope->ScopePid,
		 scope->ScopeTask,
		 scope->ScopeHash,
		 scope->ScopeUid,
		 scope->ScopeUserNameLength,
		 scope->ScopeUserName);

	if (SC_PRESENT(scope->SessionId)) {
		down(&Scope_Lock);
		pscope = Scope_Search4Scope(&scopeId, 0, 1);
		if (!pscope)
			list_add(&scope->entry, &Scope_List);
		up(&Scope_Lock);

		if (pscope) {
			printk(KERN_ERR "Scope_Find_Scope scope not added "
			       "because it was already there...\n");
			Daemon_DestroySessionId(&scope->SessionId);
			kfree(scope);
			scope = pscope;
			addscope = 0;
		}
	} else {
		kfree(scope);
		scope = NULL;
	}

	if (addscope)
		Novfs_Add_to_Root(scope->ScopeUserName);

	return scope;
}

static int Scope_Validate_Scope(struct scope_list *Scope)
{
	struct scope_list *s;
	struct list_head *sl;
	int retVal = 0;

	DbgPrint("Scope_Validate_Scope: 0x%p\n", Scope);

	down(&Scope_Lock);

	sl = Scope_List.next;
	while (sl != &Scope_List) {
		s = list_entry(sl, struct scope_list, entry);

		if (s == Scope) {
			retVal = 1;
			break;
		}

		sl = sl->next;
	}

	up(&Scope_Lock);

	return retVal;
}

/* FIXME void stuff */
uid_t Scope_Get_Uid(void *foo)
{
	struct scope_list *scope = foo;
	uid_t uid = 0;

	if (!scope)
		scope = Scope_Find_Scope(1);

	if (scope && Scope_Validate_Scope(scope))
		uid = scope->ScopeUid;

	return uid;
}

char *Scope_Get_UserName(void)
{
	char *name = NULL;
	struct scope_list *Scope;

	Scope = Scope_Find_Scope(1);

	if (Scope && Scope_Validate_Scope(Scope))
		name = Scope->ScopeUserName;

	return name;
}

/* FIXME the void * needs to get fixed... */
session_t Scope_Get_SessionId(void *foo)
{
	session_t sessionId;
	struct scope_list *Scope = foo;

	DbgPrint("Scope_Get_SessionId: 0x%p\n", Scope);
	SC_INITIALIZE(sessionId);
	if (!Scope)
		Scope = Scope_Find_Scope(1);

	if (Scope && Scope_Validate_Scope(Scope))
		sessionId = Scope->SessionId;

	DbgPrint("Scope_Get_SessionId: return 0x%p:%p\n", sessionId.hTypeId,
		 sessionId.hId);
	return sessionId;
}

struct scope_list *Scope_Get_ScopefromName(struct qstr *name)
{
	struct scope_list *scope;
	struct scope_list *rscope = NULL;
	struct list_head *sl;

	DbgPrint("Scope_Get_ScopefromName: %.*s\n", name->len, name->name);

	down(&Scope_Lock);

	sl = Scope_List.next;
	while (sl != &Scope_List) {
		scope = list_entry(sl, struct scope_list, entry);

		if ((name->len == scope->ScopeUserNameLength) &&
		    (strncmp(scope->ScopeUserName, name->name, name->len) == 0)) {
			rscope = scope;
			break;
		}
		sl = sl->next;
	}

	up(&Scope_Lock);

	return rscope;
}

int Scope_Set_UserSpace(u64 *TotalSize, u64 *Free,
			u64 *TotalEnties, u64 *FreeEnties)
{
	struct scope_list *scope;
	int retVal = 0;

	scope = Scope_Find_Scope(1);

	if (scope) {
		if (TotalSize)
			scope->ScopeUSize = *TotalSize;
		if (Free)
			scope->ScopeUFree = *Free;
		if (TotalEnties)
			scope->ScopeUTEnties = *TotalEnties;
		if (FreeEnties)
			scope->ScopeUAEnties = *FreeEnties;
	}

	return retVal;
}

int Scope_Get_UserSpace(u64 *TotalSize, u64 *Free,
			u64 *TotalEnties, u64 *FreeEnties)
{
	struct scope_list *scope;
	int retVal = 0;

	u64 td, fd, te, fe;

	scope = Scope_Find_Scope(1);

	td = fd = te = fe = 0;
	if (scope) {

		retVal = Daemon_Get_UserSpace(&scope->SessionId,
					      &td, &fd, &te, &fe);

		scope->ScopeUSize = td;
		scope->ScopeUFree = fd;
		scope->ScopeUTEnties = te;
		scope->ScopeUAEnties = fe;
	}

	if (TotalSize)
		*TotalSize = td;
	if (Free)
		*Free = fd;
	if (TotalEnties)
		*TotalEnties = te;
	if (FreeEnties)
		*FreeEnties = fe;

	return retVal;
}

struct scope_list *Scope_Get_ScopefromPath(struct dentry *dentry)
{
	struct scope_list *scope = NULL;
	char *buf, *path, *cp;
	struct qstr name;

	buf = kmalloc(PATH_LENGTH_BUFFER, GFP_KERNEL);
	if (buf) {
		path = Scope_dget_path(dentry, buf, PATH_LENGTH_BUFFER, 0);
		if (path) {
			DbgPrint("Scope_Get_ScopefromPath: %s\n", path);

			if (*path == '/')
				path++;

			cp = path;
			if (*cp) {
				while (*cp && (*cp != '/'))
					cp++;

				*cp = '\0';
				name.hash = 0;
				name.len = (int)(cp - path);
				name.name = path;
				scope = Scope_Get_ScopefromName(&name);
			}
		}
		kfree(buf);
	}

	return scope;
}

static char *add_to_list(char *name, char *list, char *endoflist)
{
	while (*name && (list < endoflist))
		*list++ = *name++;

	if (list < endoflist)
		*list++ = '\0';

	return list;
}

char *Scope_Get_ScopeUsers(void)
{
	struct scope_list *scope;
	struct list_head *sl;
	int asize = 8 * MAX_USERNAME_LENGTH;
	char *list, *cp, *ep;

	DbgPrint("Scope_Get_ScopeUsers\n");

	do {			/* Copy list until done or out of memory */
		list = kmalloc(asize, GFP_KERNEL);

		DbgPrint("Scope_Get_ScopeUsers list=0x%p\n", list);
		if (list) {
			cp = list;
			ep = cp + asize;

			/*
			 * Add the tree and server entries
			 */
			cp = add_to_list(TREE_DIRECTORY_NAME, cp, ep);
			cp = add_to_list(SERVER_DIRECTORY_NAME, cp, ep);

			down(&Scope_Lock);

			sl = Scope_List.next;
			while ((sl != &Scope_List) && (cp < ep)) {
				scope = list_entry(sl, struct scope_list, entry);

				DbgPrint("Scope_Get_ScopeUsers found 0x%p %s\n",
					 scope, scope->ScopeUserName);

				cp = add_to_list(scope->ScopeUserName, cp, ep);

				sl = sl->next;
			}

			up(&Scope_Lock);

			if (cp < ep) {
				*cp++ = '\0';
				asize = 0;
			} else {	/* Allocation was to small, up size */
				asize *= 4;
				kfree(list);
				list = NULL;
			}
		} else {	/* if allocation fails return an empty list */

			break;
		}
	} while (!list);	/* list was to small try again */

	return list;
}

void *Scope_Lookup(void)
{
	return Scope_Find_Scope(1);
}

static void Scope_Timer_Function(unsigned long context)
{
	up(&Scope_Thread_Delay);
}

static int Scope_Cleanup_Thread(void *Args)
{
	struct scope_list *scope;
	struct scope_list *rscope;
	struct list_head *sl, cleanup;
	struct task_struct *task;

	DbgPrint("Scope_Cleanup_Thread: %d\n", current->pid);

	/*
	 * Setup and start que timer
	 */
	init_timer(&Scope_Timer);

	while (0 == Scope_Thread_Terminate) {
		DbgPrint("Scope_Cleanup_Thread: looping\n");
		if (Scope_Thread_Terminate)
			break;

		/*
		 * Check scope list for any terminated processes
		 */
		down(&Scope_Lock);

		sl = Scope_List.next;
		INIT_LIST_HEAD(&cleanup);

		while (sl != &Scope_List) {
			scope = list_entry(sl, struct scope_list, entry);
			sl = sl->next;

			rscope = NULL;
			rcu_read_lock();
			for_each_process(task) {
				if ((task->uid == scope->ScopeUid)
				    || (task->euid == scope->ScopeUid)) {
					rscope = scope;
					break;
				}
			}
			rcu_read_unlock();

			if (!rscope) {
				list_move(&scope->entry, &cleanup);
				DbgPrint("Scope_Cleanup_Thread: Scope=0x%p\n",
					 rscope);
			}
		}

		up(&Scope_Lock);

		sl = cleanup.next;
		while (sl != &cleanup) {
			scope = list_entry(sl, struct scope_list, entry);
			sl = sl->next;

			DbgPrint("Scope_Cleanup_Thread: Removing 0x%p\n"
				 "   ScopeId:       0x%p:%p\n"
				 "   SessionId:     0x%p:%p\n"
				 "   ScopePid:      %d\n"
				 "   ScopeTask:     0x%p\n"
				 "   ScopeHash:     %u\n"
				 "   ScopeUid:      %u\n"
				 "   ScopeUserName: %s\n",
				 scope,
				 scope->ScopeId,
				 scope->SessionId,
				 scope->ScopePid,
				 scope->ScopeTask,
				 scope->ScopeHash,
				 scope->ScopeUid, scope->ScopeUserName);
			if (!Scope_Search4Scope(&scope->SessionId, 1, 0)) {
				Novfs_Remove_from_Root(scope->ScopeUserName);
				Daemon_DestroySessionId(&scope->SessionId);
			}
			kfree(scope);
		}

		Scope_Timer.expires = jiffies + HZ * CLEANUP_INTERVAL;
		Scope_Timer.data = (unsigned long)0;
		Scope_Timer.function = Scope_Timer_Function;
		add_timer(&Scope_Timer);
		DbgPrint("Scope_Cleanup_Thread: sleeping\n");

		if (down_interruptible(&Scope_Thread_Delay))
			break;

		del_timer(&Scope_Timer);
	}
	Scope_Thread_Terminate = 0;

	printk(KERN_INFO "Scope_Cleanup_Thread: Exit\n");
	DbgPrint("Scope_Cleanup_Thread: Exit\n");
	return 0;
}

void Scope_Cleanup(void)
{
	struct scope_list *scope;
	struct list_head *sl;

	DbgPrint("Scope_Cleanup:\n");

	/*
	 * Check scope list for any terminated processes
	 */
	down(&Scope_Lock);

	sl = Scope_List.next;

	while (sl != &Scope_List) {
		scope = list_entry(sl, struct scope_list, entry);
		sl = sl->next;

		list_del(&scope->entry);

		DbgPrint("Scope_Cleanup: Removing 0x%p\n"
			 "   ScopeId:       0x%p:%p\n"
			 "   SessionId:     0x%p:%p\n"
			 "   ScopePid:      %d\n"
			 "   ScopeTask:     0x%p\n"
			 "   ScopeHash:     %u\n"
			 "   ScopeUid:      %u\n"
			 "   ScopeUserName: %s\n",
			 scope,
			 scope->ScopeId,
			 scope->SessionId,
			 scope->ScopePid,
			 scope->ScopeTask,
			 scope->ScopeHash,
			 scope->ScopeUid, scope->ScopeUserName);
		if (!Scope_Search4Scope(&scope->SessionId, 1, 1)) {
			Novfs_Remove_from_Root(scope->ScopeUserName);
			Daemon_DestroySessionId(&scope->SessionId);
		}
		kfree(scope);
	}

	up(&Scope_Lock);

}

/*
 *  Arguments:   struct dentry *dentry - starting entry
 *               char *Buf - pointer to memory buffer
 *               unsigned int Buflen - size of memory buffer
 *
 *  Returns:     pointer to path.
 *
 *  Abstract:    Walks the dentry chain building a path.
 */
char *Scope_dget_path(struct dentry *dentry, char *Buf, unsigned int Buflen,
		int Flags)
{
	char *retval = &Buf[Buflen];
	struct dentry *p = dentry;
	int len;

	*(--retval) = '\0';
	Buflen--;

	do {
		if (Buflen > p->d_name.len) {
			retval -= p->d_name.len;
			Buflen -= p->d_name.len;
			memcpy(retval, p->d_name.name, p->d_name.len);
			*(--retval) = '/';
			Buflen--;
			p = p->d_parent;
		} else {
			retval = NULL;
			break;
		}
	} while (!IS_ROOT(p));

	if (IS_ROOT(dentry))
		retval++;

	if (Flags) {
		len = strlen(p->d_sb->s_type->name);
		if (Buflen - len > 0) {
			retval -= len;
			Buflen -= len;
			memcpy(retval, p->d_sb->s_type->name, len);
			*(--retval) = '/';
			Buflen--;
		}
	}

	return retval;
}

void Scope_Init(void)
{
	INIT_LIST_HEAD(&Scope_List);
	init_MUTEX(&Scope_Lock);
	init_MUTEX_LOCKED(&Scope_Thread_Delay);

	kthread_run(Scope_Cleanup_Thread, NULL, "novfs_ST");
}

void Scope_Uninit(void)
{
	unsigned long expires = jiffies + HZ * SHUTDOWN_INTERVAL;

	printk(KERN_INFO "Scope_Uninit: Start\n");

	Scope_Thread_Terminate = 1;

	up(&Scope_Thread_Delay);

	mb();
	while (Scope_Thread_Terminate && (jiffies < expires))
		yield();

	/* down(&Scope_Thread_Delay); */
	printk(KERN_INFO "Scope_Uninit: Exit\n");

}


