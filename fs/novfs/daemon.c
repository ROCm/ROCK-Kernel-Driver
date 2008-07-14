/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains all the functions necessary for sending commands to our
 * daemon module.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/time.h>

#include "vfs.h"
#include "nwcapi.h"
#include "commands.h"
#include "nwerror.h"

#define QUEUE_SENDING 0
#define QUEUE_WAITING 1
#define QUEUE_TIMEOUT 2
#define QUEUE_ACKED   3
#define QUEUE_DONE    4

#define TIMEOUT_VALUE 10

#define DH_TYPE_UNDEFINED    0
#define DH_TYPE_STREAM       1
#define DH_TYPE_CONNECTION   2

struct daemon_queue {
	struct list_head list;	/* Must be first entry */
	spinlock_t lock;	/* Used to control access to list */
	struct semaphore semaphore;	/* Used to signal when data is available */
};

struct daemon_cmd {
	struct list_head list;	/* Must be first entry */
	atomic_t reference;
	unsigned int status;
	unsigned int flags;
	struct semaphore semaphore;
	unsigned long sequence;
	struct timer_list timer;
	void *request;
	unsigned long reqlen;
	void *data;
	int datalen;
	void *reply;
	unsigned long replen;
};

struct daemon_handle {
	struct list_head list;
	rwlock_t lock;
	struct novfs_schandle session;
};

struct daemon_resource {
	struct list_head list;
	int type;
	void *connection;
	unsigned char handle[6];
	mode_t mode;
	loff_t size;
};

struct drive_map {
	struct list_head list;	/* Must be first item */
	struct novfs_schandle session;
	unsigned long hash;
	int namelen;
	char name[1];
};

static void Queue_get(struct daemon_cmd * Que);
static void Queue_put(struct daemon_cmd * Que);
static void RemoveDriveMaps(void);
static int NwdConvertLocalHandle(struct novfs_xplat *pdata, struct daemon_handle * DHandle);
static int NwdConvertNetwareHandle(struct novfs_xplat *pdata, struct daemon_handle * DHandle);
static int set_map_drive(struct novfs_xplat *pdata, struct novfs_schandle Session);
static int unmap_drive(struct novfs_xplat *pdata, struct novfs_schandle Session);
static int NwdGetMountPath(struct novfs_xplat *pdata);
static int local_unlink(const char *pathname);


/*===[ Global variables ]=================================================*/
static struct daemon_queue Daemon_Queue;

static DECLARE_WAIT_QUEUE_HEAD(Read_waitqueue);

static atomic_t Sequence = ATOMIC_INIT(-1);
static atomic_t Daemon_Open_Count = ATOMIC_INIT(0);

static unsigned long Daemon_Command_Timeout = TIMEOUT_VALUE;

static DECLARE_MUTEX(DriveMapLock);
static LIST_HEAD(DriveMapList);

int novfs_max_iosize = PAGE_SIZE;

void novfs_daemon_queue_init()
{
	INIT_LIST_HEAD(&Daemon_Queue.list);
	spin_lock_init(&Daemon_Queue.lock);
	init_MUTEX_LOCKED(&Daemon_Queue.semaphore);
}

void novfs_daemon_queue_exit(void)
{
	/* Does nothing for now but we maybe should clear the queue. */
}

/*++======================================================================*/
static void novfs_daemon_timer(unsigned long data)
{
	struct daemon_cmd *que = (struct daemon_cmd *) data;

	if (QUEUE_ACKED != que->status) {
		que->status = QUEUE_TIMEOUT;
	}
	up(&que->semaphore);
}

/*++======================================================================*/
int Queue_Daemon_Command(void *request,
			 unsigned long reqlen,
			 void *data,
			 int dlen,
			 void **reply, unsigned long * replen, int interruptible)
{
	struct daemon_cmd *que;
	int retCode = 0;
	uint64_t ts1, ts2;

	ts1 = get_nanosecond_time();

	DbgPrint("Queue_Daemon_Command: 0x%p %d\n", request, reqlen);

	if (atomic_read(&Daemon_Open_Count)) {

		que = kmalloc(sizeof(*que), GFP_KERNEL);

		DbgPrint("Queue_Daemon_Command: que=0x%p\n", que);
		if (que) {
			atomic_set(&que->reference, 0);
			que->status = QUEUE_SENDING;
			que->flags = 0;

			init_MUTEX_LOCKED(&que->semaphore);

			que->sequence = atomic_inc_return(&Sequence);

			((struct novfs_command_request_header *) request)->SequenceNumber =
			    que->sequence;

			/*
			 * Setup and start que timer
			 */
			init_timer(&que->timer);
			que->timer.expires = jiffies + (HZ * Daemon_Command_Timeout);
			que->timer.data = (unsigned long) que;
			que->timer.function = novfs_daemon_timer;
			add_timer(&que->timer);

			/*
			 * Setup request
			 */
			que->request = request;
			que->reqlen = reqlen;
			que->data = data;
			que->datalen = dlen;
			que->reply = NULL;
			que->replen = 0;

			/*
			 * Added entry to queue.
			 */
			/*
			 * Check to see if interruptible and set flags.
			 */
			if (interruptible) {
				que->flags |= INTERRUPTIBLE;
			}

			Queue_get(que);

			spin_lock(&Daemon_Queue.lock);
			list_add_tail(&que->list, &Daemon_Queue.list);
			spin_unlock(&Daemon_Queue.lock);

			/*
			 * Signal that there is data to be read
			 */
			up(&Daemon_Queue.semaphore);

			/*
			 * Give a change to the other processes.
			 */
			yield();

			/*
			 * Block waiting for reply or timeout
			 */
			down(&que->semaphore);

			if (QUEUE_ACKED == que->status) {
				que->status = QUEUE_WAITING;
				mod_timer(&que->timer,
					  jiffies +
					  (HZ * 2 * Daemon_Command_Timeout));
				if (interruptible) {
					retCode =
					    down_interruptible(&que->semaphore);
				} else {
					down(&que->semaphore);
				}
			}

			/*
			 * Delete timer
			 */
			del_timer(&que->timer);

			/*
			 * Check for timeout
			 */
			if ((QUEUE_TIMEOUT == que->status)
			    && (NULL == que->reply)) {
				DbgPrint("Queue_Daemon_Command: Timeout\n");
				retCode = -ETIME;
			}
			*reply = que->reply;
			*replen = que->replen;

			/*
			 * Remove item from queue
			 */
			Queue_put(que);

		} else {	/* Error case with no memory */

			retCode = -ENOMEM;
			*reply = NULL;
			*replen = 0;
		}
	} else {
		retCode = -EIO;
		*reply = NULL;
		*replen = 0;

	}
	ts2 = get_nanosecond_time();
	ts2 = ts2 - ts1;

	DbgPrint("Queue_Daemon_Command: %llu retCode=%d \n", ts2, retCode);
	return (retCode);
}

static void Queue_get(struct daemon_cmd * Que)
{
	DbgPrint("Queue_get: que=0x%p %d\n", Que, atomic_read(&Que->reference));
	atomic_inc(&Que->reference);
}

static void Queue_put(struct daemon_cmd * Que)
{

	DbgPrint("Queue_put: que=0x%p %d\n", Que, atomic_read(&Que->reference));
	spin_lock(&Daemon_Queue.lock);

	if (atomic_dec_and_test(&Que->reference)) {
		/*
		 * Remove item from queue
		 */
		list_del(&Que->list);
		spin_unlock(&Daemon_Queue.lock);

		/*
		 * Free item memory
		 */
		kfree(Que);
	} else {
		spin_unlock(&Daemon_Queue.lock);
	}
}

struct daemon_cmd *get_next_queue(int Set_Queue_Waiting)
{
	struct daemon_cmd *que;

	DbgPrint("get_next_queue: que=0x%p\n", Daemon_Queue.list.next);

	spin_lock(&Daemon_Queue.lock);
	que = (struct daemon_cmd *) Daemon_Queue.list.next;

	while (que && (que != (struct daemon_cmd *) & Daemon_Queue.list.next)
	       && (que->status != QUEUE_SENDING)) {
		que = (struct daemon_cmd *) que->list.next;
	}

	if ((NULL == que) || (que == (struct daemon_cmd *) & Daemon_Queue.list)
	    || (que->status != QUEUE_SENDING)) {
		que = NULL;
	} else if (Set_Queue_Waiting) {
		que->status = QUEUE_WAITING;
	}

	if (que) {
		atomic_inc(&que->reference);
	}

	spin_unlock(&Daemon_Queue.lock);

	DbgPrint("get_next_queue: return=0x%p\n", que);
	return (que);
}

static struct daemon_cmd *find_queue(unsigned long sequence)
{
	struct daemon_cmd *que;

	DbgPrint("find_queue: 0x%x\n", sequence);

	spin_lock(&Daemon_Queue.lock);
	que = (struct daemon_cmd *) Daemon_Queue.list.next;

	while (que && (que != (struct daemon_cmd *) & Daemon_Queue.list.next)
	       && (que->sequence != sequence)) {
		que = (struct daemon_cmd *) que->list.next;
	}

	if ((NULL == que)
	    || (que == (struct daemon_cmd *) & Daemon_Queue.list.next)
	    || (que->sequence != sequence)) {
		que = NULL;
	}

	if (que) {
		atomic_inc(&que->reference);
	}

	spin_unlock(&Daemon_Queue.lock);

	DbgPrint("find_queue: return 0x%p\n", que);
	return (que);
}

int novfs_daemon_open_control(struct inode *Inode, struct file *File)
{
	DbgPrint("Daemon_Open_Control: pid=%d Count=%d\n", current->pid,
		 atomic_read(&Daemon_Open_Count));
	atomic_inc(&Daemon_Open_Count);

	return (0);
}

int novfs_daemon_close_control(struct inode *Inode, struct file *File)
{
	struct daemon_cmd *que;

	DbgPrint("Daemon_Close_Control: pid=%d Count=%d\n", current->pid,
		 atomic_read(&Daemon_Open_Count));

	if (atomic_dec_and_test(&Daemon_Open_Count)) {
		/*
		 * Signal any pending que itmes.
		 */

		spin_lock(&Daemon_Queue.lock);
		que = (struct daemon_cmd *) Daemon_Queue.list.next;

		while (que
		       && (que != (struct daemon_cmd *) & Daemon_Queue.list.next)
		       && (que->status != QUEUE_DONE)) {
			que->status = QUEUE_TIMEOUT;
			up(&que->semaphore);

			que = (struct daemon_cmd *) que->list.next;
		}
		spin_unlock(&Daemon_Queue.lock);

		RemoveDriveMaps();

		novfs_scope_cleanup();
	}

	return (0);
}

ssize_t novfs_daemon_cmd_send(struct file * file, char *buf, size_t len, loff_t * off)
{
	struct daemon_cmd *que;
	size_t retValue = 0;
	int Finished = 0;
	struct novfs_data_list *dlist;
	int i, dcnt, bcnt, ccnt, error;
	char *vadr;
	unsigned long cpylen;

	DbgPrint("Daemon_Send_Command: %u %lld\n", len, *off);
	if (len > novfs_max_iosize) {
		novfs_max_iosize = len;
	}

	while (!Finished) {
		que = get_next_queue(1);
		DbgPrint("Daemon_Send_Command: 0x%p\n", que);
		if (que) {
			retValue = que->reqlen;
			if (retValue > len) {
				retValue = len;
			}
			if (retValue > 0x80)
				novfs_dump(0x80, que->request);
			else
				novfs_dump(retValue, que->request);

			cpylen = copy_to_user(buf, que->request, retValue);
			if (que->datalen && (retValue < len)) {
				buf += retValue;
				dlist = que->data;
				dcnt = que->datalen;
				for (i = 0; i < dcnt; i++, dlist++) {
					if (DLREAD == dlist->rwflag) {
						bcnt = dlist->len;
						DbgPrint
						    ("Daemon_Send_Command%d: page=0x%p offset=0x%p len=%d\n",
						     i, dlist->page,
						     dlist->offset, dlist->len);
						if ((bcnt + retValue) <= len) {
							void *km_adr = NULL;

							if (dlist->page) {
								km_adr =
								    kmap(dlist->
									 page);
								vadr = km_adr;
								vadr +=
								    (unsigned long)
								    dlist->
								    offset;
							} else {
								vadr =
								    dlist->
								    offset;
							}

							ccnt =
							    copy_to_user(buf,
									 vadr,
									 bcnt);

							DbgPrint
							    ("Daemon_Send_Command: Copy %d from 0x%p to 0x%p.\n",
							     bcnt, vadr, buf);
							if (bcnt > 0x80)
								novfs_dump(0x80,
								       vadr);
							else
								novfs_dump(bcnt,
								       vadr);

							if (km_adr) {
								kunmap(dlist->
								       page);
							}

							retValue += bcnt;
							buf += bcnt;
						} else {
							break;
						}
					}
				}
			}
			Queue_put(que);
			break;
		}

		if (O_NONBLOCK & file->f_flags) {
			retValue = -EAGAIN;
			break;
		} else {
			if ((error =
			     down_interruptible(&Daemon_Queue.semaphore))) {
				DbgPrint
				    ("Daemon_Send_Command: after down_interruptible error...%d\n",
				     error);
				retValue = -EINTR;
				break;
			}
			DbgPrint
			    ("Daemon_Send_Command: after down_interruptible\n");
		}
	}

	*off = *off;

	DbgPrint("Daemon_Send_Command: return 0x%x\n", retValue);

	return (retValue);
}

ssize_t novfs_daemon_recv_reply(struct file *file, const char *buf, size_t nbytes, loff_t * ppos)
{
	struct daemon_cmd *que;
	size_t retValue = 0;
	void *reply;
	unsigned long sequence, cpylen;

	struct novfs_data_list *dlist;
	char *vadr;
	int i;

	DbgPrint("Daemon_Receive_Reply: buf=0x%p nbytes=%d ppos=%llx\n", buf,
		 nbytes, *ppos);

	/*
	 * Get sequence number from reply buffer
	 */

	cpylen = copy_from_user(&sequence, buf, sizeof(sequence));

	/*
	 * Find item based on sequence number
	 */
	que = find_queue(sequence);

	DbgPrint("Daemon_Receive_Reply: 0x%x 0x%p %d\n", sequence, que, nbytes);
	if (que) {
		do {
			retValue = nbytes;
			/*
			 * Ack packet from novfsd.  Remove timer and
			 * return
			 */
			if (nbytes == sizeof(sequence)) {
				que->status = QUEUE_ACKED;
				break;
			}

			if (NULL != (dlist = que->data)) {
				int thiscopy, left = nbytes;
				retValue = 0;

				DbgPrint
				    ("Daemon_Receive_Reply: dlist=0x%p count=%d\n",
				     dlist, que->datalen);
				for (i = 0;
				     (i < que->datalen) && (retValue < nbytes);
				     i++, dlist++) {
					DbgPrint("Daemon_Receive_Reply:\n"
						 "   dlist[%d].page:   0x%p\n"
						 "   dlist[%d].offset: 0x%p\n"
						 "   dlist[%d].len:    0x%x\n"
						 "   dlist[%d].rwflag: 0x%x\n",
						 i, dlist->page, i,
						 dlist->offset, i, dlist->len,
						 i, dlist->rwflag);

					if (DLWRITE == dlist->rwflag) {
						void *km_adr = NULL;

						if (dlist->page) {
							km_adr =
							    kmap(dlist->page);
							vadr = km_adr;
							vadr +=
							    (unsigned long) dlist->
							    offset;
						} else {
							vadr = dlist->offset;
						}

						thiscopy = dlist->len;
						if (thiscopy > left) {
							thiscopy = left;
							dlist->len = left;
						}
						cpylen =
						    copy_from_user(vadr, buf,
								   thiscopy);

						if (thiscopy > 0x80)
							novfs_dump(0x80, vadr);
						else
							novfs_dump(thiscopy, vadr);

						if (km_adr) {
							kunmap(dlist->page);
						}

						left -= thiscopy;
						retValue += thiscopy;
						buf += thiscopy;
					}
				}
				que->replen = retValue;
			} else {
				reply = kmalloc(nbytes, GFP_KERNEL);
				DbgPrint("Daemon_Receive_Reply: reply=0x%p\n",
					 reply);
				if (reply) {
					retValue = nbytes;
					que->reply = reply;
					que->replen = nbytes;

					retValue -=
					    copy_from_user(reply, buf,
							   retValue);
					if (retValue > 0x80)
						novfs_dump(0x80, reply);
					else
						novfs_dump(retValue, reply);

				} else {
					retValue = -ENOMEM;
				}
			}

			/*
			 * Set status that packet is done.
			 */
			que->status = QUEUE_DONE;

		} while (0);
		up(&que->semaphore);
		Queue_put(que);
	}

	DbgPrint("Daemon_Receive_Reply: return 0x%x\n", retValue);

	return (retValue);
}

int novfs_do_login(struct ncl_string *Server, struct ncl_string *Username,
struct ncl_string *Password, void **lgnId, struct novfs_schandle *Session)
{
	struct novfs_login_user_request *cmd;
	struct novfs_login_user_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, datalen;
	unsigned char *data;

	datalen = Server->len + Username->len + Password->len;
	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	data = (unsigned char *) cmd + sizeof(*cmd);
	cmd->Command.CommandType = VFS_COMMAND_LOGIN_USER;
	cmd->Command.SequenceNumber = 0;
	memcpy(&cmd->Command.SessionId, Session, sizeof(*Session));

	cmd->srvNameType = Server->type;
	cmd->serverLength = Server->len;
	cmd->serverOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Server->buffer, Server->len);
	data += Server->len;

	cmd->usrNameType = Username->type;
	cmd->userNameLength = Username->len;
	cmd->userNameOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Username->buffer, Username->len);
	data += Username->len;

	cmd->pwdNameType = Password->type;
	cmd->passwordLength = Password->len;
	cmd->passwordOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Password->buffer, Password->len);
	data += Password->len;

	retCode =	Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
								&replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.ErrorCode) {
			retCode = reply->Reply.ErrorCode;
		} else {
			retCode = 0;
			if (lgnId) {
				*lgnId = reply->loginIdentity;
			}
		}
		kfree(reply);
	}
	memset(cmd, 0, cmdlen);
	kfree(cmd);
	return (retCode);

}

int novfs_daemon_logout(struct qstr *Server, struct novfs_schandle *Session)
{
	struct novfs_logout_request *cmd;
	struct novfs_logout_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	cmdlen = offsetof(struct novfs_logout_request, Name) + Server->len;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_LOGOUT_USER;
	cmd->Command.SequenceNumber = 0;
	memcpy(&cmd->Command.SessionId, Session, sizeof(*Session));
	cmd->length = Server->len;
	memcpy(cmd->Name, Server->name, Server->len);

	retCode =
		Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_daemon_getpwuid(uid_t uid, int unamelen, char *uname)
{
	struct novfs_getpwuid_request cmd;
	struct novfs_getpwuid_reply *reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_GETPWUD;
	cmd.Command.SequenceNumber = 0;
	SC_INITIALIZE(cmd.Command.SessionId);
	cmd.uid = uid;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		} else {
			retCode = 0;
			memset(uname, 0, unamelen);
			replylen =
			    replylen - offsetof(struct
					    novfs_getpwuid_reply, UserName);
			if (replylen) {
				if (replylen > unamelen) {
					retCode = -EINVAL;
					replylen = unamelen - 1;
				}
				memcpy(uname, reply->UserName, replylen);
			}
		}
		kfree(reply);
	}
	return (retCode);

}

int novfs_daemon_getversion(char *Buf, int length)
{
	struct novfs_get_version_request cmd;
	struct novfs_get_version_reply *reply;
	unsigned long replylen = 0;
	int retVal = 0;

	cmd.Command.CommandType = VFS_COMMAND_GET_VERSION;
	cmd.Command.SequenceNumber = 0;
	SC_INITIALIZE(cmd.Command.SessionId);

	Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
			     &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.ErrorCode) {
			retVal = -EIO;
		} else {
			retVal =
			    replylen - offsetof(struct
					    novfs_get_version_reply, Version);
			if (retVal < length) {
				memcpy(Buf, reply->Version, retVal);
				Buf[retVal] = '\0';
			}
		}
		kfree(reply);
	}
	return (retVal);

}

static int daemon_login(struct novfs_login *Login, struct novfs_schandle *Session)
{
	int retCode = -ENOMEM;
	struct novfs_login lLogin;
	struct ncl_string server;
	struct ncl_string username;
	struct ncl_string password;

	if (!copy_from_user(&lLogin, Login, sizeof(lLogin))) {
		server.buffer = kmalloc(lLogin.Server.length, GFP_KERNEL);
		if (server.buffer) {
			server.len = lLogin.Server.length;
			server.type = NWC_STRING_TYPE_ASCII;
			if (!copy_from_user((void *)server.buffer, lLogin.Server.data, server.len)) {
				username.buffer =	kmalloc(lLogin.UserName.length,	GFP_KERNEL);
				if (username.buffer) {
					username.len = lLogin.UserName.length;
					username.type = NWC_STRING_TYPE_ASCII;
					if (!copy_from_user((void *)username.buffer, lLogin.UserName.data, username.len)) {
						password.buffer =	kmalloc(lLogin.Password.length,	GFP_KERNEL);
						if (password.buffer)
						{
							password.len = lLogin.Password.length;
							password.type = NWC_STRING_TYPE_ASCII;
							if (!copy_from_user((void *)password.buffer, lLogin.Password.data, password.len)) {
								retCode = novfs_do_login (&server, &username, &password, NULL, Session);
								if (!retCode) {
									char *username;
									username = novfs_scope_get_username();
									if (username) {
										novfs_add_to_root(username);
									}
								}
							}
							kfree(password.buffer);
						}
					}
					kfree(username.buffer);
				}
			}
			kfree(server.buffer);
		}
	}

	return (retCode);
}

static int daemon_logout(struct novfs_logout *Logout, struct novfs_schandle *Session)
{
	struct novfs_logout lLogout;
	struct qstr server;
	int retCode = 0;

	if (copy_from_user(&lLogout, Logout, sizeof(lLogout)))
		return -EFAULT;
	server.name = kmalloc(lLogout.Server.length, GFP_KERNEL);
	if (!server.name)
		return -ENOMEM;
	server.len = lLogout.Server.length;
	if (copy_from_user((void *)server.name, lLogout.Server.data, server.len))
		goto exit;
	retCode = novfs_daemon_logout(&server, Session);
exit:
	kfree(server.name);
	return (retCode);
}

int novfs_daemon_create_sessionId(struct novfs_schandle * SessionId)
{
	struct novfs_create_context_request cmd;
	struct novfs_create_context_reply *reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("Daemon_CreateSessionId: %d\n", current->pid);

	cmd.Command.CommandType = VFS_COMMAND_CREATE_CONTEXT;
	cmd.Command.SequenceNumber = 0;
	SC_INITIALIZE(cmd.Command.SessionId);

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.ErrorCode
		    && replylen > sizeof(struct novfs_command_reply_header)) {
			*SessionId = reply->SessionId;
			retCode = 0;
		} else {
			SessionId->hTypeId = 0;
			SessionId->hId = 0;
			retCode = -EIO;
		}
		kfree(reply);
	}
	DbgPrint("Daemon_CreateSessionId: SessionId=0x%llx\n", *SessionId);
	return (retCode);
}

int novfs_daemon_destroy_sessionId(struct novfs_schandle SessionId)
{
	struct novfs_destroy_context_request cmd;
	struct novfs_destroy_context_reply *reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("Daemon_DestroySessionId: 0x%p:%p\n", SessionId.hTypeId,
		 SessionId.hId);

	cmd.Command.CommandType = VFS_COMMAND_DESTROY_CONTEXT;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.ErrorCode) {
			struct drive_map *dm;
			struct list_head *list;

			retCode = 0;

			/*
			 * When destroying the session check to see if there are any
			 * mapped drives.  If there are then remove them.
			 */
			down(&DriveMapLock);
			list_for_each(list, &DriveMapList) {
				dm = list_entry(list, struct drive_map, list);
				if (SC_EQUAL(SessionId, dm->session)) {
					local_unlink(dm->name);
					list = list->prev;
					list_del(&dm->list);
					kfree(dm);
				}

			}
			up(&DriveMapLock);

		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int novfs_daemon_get_userspace(struct novfs_schandle SessionId, uint64_t * TotalSize,
			 uint64_t * Free, uint64_t * TotalEnties,
			 uint64_t * FreeEnties)
{
	struct novfs_get_user_space cmd;
	struct novfs_get_user_space_reply *reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("Daemon_Get_UserSpace: 0x%p:%p\n", SessionId.hTypeId,
		 SessionId.hId);

	cmd.Command.CommandType = VFS_COMMAND_GET_USER_SPACE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.ErrorCode) {

			DbgPrint("TotalSpace:  %llu\n", reply->TotalSpace);
			DbgPrint("FreeSpace:   %llu\n", reply->FreeSpace);
			DbgPrint("TotalEnties: %llu\n", reply->TotalEnties);
			DbgPrint("FreeEnties:  %llu\n", reply->FreeEnties);

			if (TotalSize)
				*TotalSize = reply->TotalSpace;
			if (Free)
				*Free = reply->FreeSpace;
			if (TotalEnties)
				*TotalEnties = reply->TotalEnties;
			if (FreeEnties)
				*FreeEnties = reply->FreeEnties;
			retCode = 0;
		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int novfs_daemon_set_mnt_point(char *Path)
{
	struct novfs_set_mount_path *cmd;
	struct novfs_set_mount_path_reply *reply;
	unsigned long replylen, cmdlen;
	int retCode = -ENOMEM;

	DbgPrint("Daemon_SetMountPoint: %s\n", Path);

	replylen = strlen(Path);

	cmdlen = sizeof(struct novfs_set_mount_path) + replylen;

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_SET_MOUNT_PATH;
	cmd->Command.SequenceNumber = 0;
	SC_INITIALIZE(cmd->Command.SessionId);
	cmd->PathLength = replylen;

	strcpy(cmd->Path, Path);

	replylen = 0;

	retCode =
		Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
				&replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.ErrorCode) {
			retCode = 0;
		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	kfree(cmd);
	return retCode;
}

int novfs_daemon_debug_cmd_send(char *Command)
{
	struct novfs_debug_request cmd;
	struct novfs_debug_reply *reply;
	struct novfs_debug_reply lreply;
	unsigned long replylen, cmdlen;
	struct novfs_data_list dlist[2];

	int retCode = -ENOMEM;

	DbgPrint("Daemon_SendDebugCmd: %s\n", Command);

	dlist[0].page = NULL;
	dlist[0].offset = (char *)Command;
	dlist[0].len = strlen(Command);
	dlist[0].rwflag = DLREAD;

	dlist[1].page = NULL;
	dlist[1].offset = (char *)&lreply;
	dlist[1].len = sizeof(lreply);
	dlist[1].rwflag = DLWRITE;

	cmdlen = offsetof(struct novfs_debug_request, dbgcmd);

	cmd.Command.CommandType = VFS_COMMAND_DBG;
	cmd.Command.SequenceNumber = 0;
	SC_INITIALIZE(cmd.Command.SessionId);
	cmd.cmdlen = strlen(Command);

	replylen = 0;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, dlist, 2, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		kfree(reply);
	}
	if (0 == retCode) {
		retCode = lreply.Reply.ErrorCode;
	}

	return (retCode);
}

int novfs_daemon_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int retCode = -ENOSYS;
	unsigned long cpylen;
	struct novfs_schandle session_id;
	session_id = novfs_scope_get_sessionId(NULL);

	switch (cmd) {
	case IOC_LOGIN:
		retCode = daemon_login((struct novfs_login *) arg, &session_id);
		break;

	case IOC_LOGOUT:
		retCode = daemon_logout((struct novfs_logout *)arg, &session_id);
		break;
	case IOC_DEBUGPRINT:
		{
			struct Ioctl_Debug {
				int length;
				char *data;
			} io;
			char *buf;
			io.length = 0;
			cpylen = copy_from_user(&io, (char *)arg, sizeof(io));
			if (io.length) {
				buf = kmalloc(io.length + 1, GFP_KERNEL);
				if (buf) {
					buf[0] = 0;
					cpylen =
					    copy_from_user(buf, io.data,
							   io.length);
					buf[io.length] = '\0';
					DbgPrint("%s", buf);
					kfree(buf);
					retCode = 0;
				}
			}
			break;
		}

	case IOC_XPLAT:
		{
			struct novfs_xplat data;

			cpylen =
			    copy_from_user(&data, (void *)arg, sizeof(data));
			retCode = ((data.xfunction & 0x0000FFFF) | 0xCC000000);

			switch (data.xfunction) {
			case NWC_GET_MOUNT_PATH:
				DbgPrint
				    ("[Daemon_ioctl] Call NwdGetMountPath\n");
				retCode = NwdGetMountPath(&data);
				break;
			}

			DbgPrint("[NOVFS XPLAT] status Code = %X\n", retCode);
			break;
		}

	}
	return (retCode);
}

static int daemon_added_resource(struct daemon_handle * DHandle, int Type, void *CHandle,
			  unsigned char * FHandle, unsigned long Mode, u_long Size)
{
	struct daemon_resource *resource;

	if (FHandle)
		DbgPrint
		    ("Daemon_Added_Resource: DHandle=0x%p Type=%d CHandle=0x%p FHandle=0x%x Mode=0x%x Size=%d\n",
		     DHandle, Type, CHandle, *(u32 *) & FHandle[2], Mode, Size);
	else
		DbgPrint
		    ("Daemon_Added_Resource: DHandle=0x%p Type=%d CHandle=0x%p\n",
		     DHandle, Type, CHandle);

	resource = kmalloc(sizeof(struct daemon_resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	resource->type = Type;
	resource->connection = CHandle;
	if (FHandle)
		memcpy(resource->handle, FHandle,
				sizeof(resource->handle));
	else
		memset(resource->handle, 0, sizeof(resource->handle));
	resource->mode = Mode;
	resource->size = Size;
	write_lock(&DHandle->lock);
	list_add(&resource->list, &DHandle->list);
	write_unlock(&DHandle->lock);
	DbgPrint("Daemon_Added_Resource: Adding resource=0x%p\n",
			resource);
	return 0;
}

static int daemon_remove_resource(struct daemon_handle * DHandle, int Type, void *CHandle,
			   unsigned long FHandle)
{
	struct daemon_resource *resource;
	struct list_head *l;
	int retVal = -ENOMEM;

	DbgPrint
	    ("Daemon_Remove_Resource: DHandle=0x%p Type=%d CHandle=0x%p FHandle=0x%x\n",
	     DHandle, Type, CHandle, FHandle);

	write_lock(&DHandle->lock);

	list_for_each(l, &DHandle->list) {
		resource = list_entry(l, struct daemon_resource, list);

		if ((Type == resource->type) &&
		    (resource->connection == CHandle)) {
			DbgPrint
			    ("Daemon_Remove_Resource: Found resource=0x%p\n",
			     resource);
			l = l->prev;
			list_del(&resource->list);
			kfree(resource);
			break;
		}
	}

	write_unlock(&DHandle->lock);

	return (retVal);
}

int novfs_daemon_lib_open(struct inode *inode, struct file *file)
{
	struct daemon_handle *dh;

	DbgPrint("Daemon_Library_open: inode=0x%p file=0x%p\n", inode, file);
	dh = kmalloc(sizeof(struct daemon_handle), GFP_KERNEL);
	if (!dh)
		return -ENOMEM;
	file->private_data = dh;
	INIT_LIST_HEAD(&dh->list);
	rwlock_init(&dh->lock);
	dh->session = novfs_scope_get_sessionId(NULL);
	return 0;
}

int novfs_daemon_lib_close(struct inode *inode, struct file *file)
{
	struct daemon_handle *dh;
	struct daemon_resource *resource;
	struct list_head *l;

	char commanddata[sizeof(struct novfs_xplat_call_request) + sizeof(struct nwd_close_conn)];
	struct novfs_xplat_call_request *cmd;
	struct xplat_call_reply *reply;
	struct nwd_close_conn *nwdClose;
	unsigned long cmdlen, replylen;

	DbgPrint("Daemon_Library_close: inode=0x%p file=0x%p\n", inode, file);
	if (file->private_data) {
		dh = (struct daemon_handle *) file->private_data;

		list_for_each(l, &dh->list) {
			resource = list_entry(l, struct daemon_resource, list);

			if (DH_TYPE_STREAM == resource->type) {
				novfs_close_stream(resource->connection,
						   resource->handle,
						   dh->session);
			} else if (DH_TYPE_CONNECTION == resource->type) {
				cmd = (struct novfs_xplat_call_request *) commanddata;
				cmdlen =
				    offsetof(struct novfs_xplat_call_request,
					     data) + sizeof(struct nwd_close_conn);
				cmd->Command.CommandType =
				    VFS_COMMAND_XPLAT_CALL;
				cmd->Command.SequenceNumber = 0;
				cmd->Command.SessionId = dh->session;
				cmd->NwcCommand = NWC_CLOSE_CONN;

				cmd->dataLen = sizeof(struct nwd_close_conn);
				nwdClose = (struct nwd_close_conn *) cmd->data;
				nwdClose->ConnHandle =
				    (void *) resource->connection;

				Queue_Daemon_Command((void *)cmd, cmdlen, NULL,
						     0, (void **)&reply,
						     &replylen, 0);
				if (reply)
					kfree(reply);
			}
			l = l->prev;
			list_del(&resource->list);
			kfree(resource);
		}
		kfree(dh);
		file->private_data = NULL;
	}

	return (0);
}

ssize_t novfs_daemon_lib_read(struct file * file, char *buf, size_t len,
			    loff_t * off)
{
	struct daemon_handle *dh;
	struct daemon_resource *resource;

	size_t thisread, totalread = 0;
	loff_t offset = *off;

	DbgPrint("Daemon_Library_read: file=0x%p len=%d off=%lld\n", file, len,
		 *off);

	if (file->private_data) {
		dh = file->private_data;
		read_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, struct daemon_resource, list);

			if (DH_TYPE_STREAM == resource->type) {
				while (len > 0 && (offset < resource->size)) {
					thisread = len;
					if (novfs_read_stream
					    (resource->connection,
					     resource->handle, buf, &thisread,
					     &offset, 1, dh->session)
					    || !thisread) {
						break;
					}
					len -= thisread;
					buf += thisread;
					offset += thisread;
					totalread += thisread;
				}
			}
		}
		read_unlock(&dh->lock);
	}
	*off = offset;
	DbgPrint("Daemon_Library_read return = 0x%x\n", totalread);
	return (totalread);
}

ssize_t novfs_daemon_lib_write(struct file * file, const char *buf, size_t len,
			     loff_t * off)
{
	struct daemon_handle *dh;
	struct daemon_resource *resource;

	size_t thiswrite, totalwrite = -EINVAL;
	loff_t offset = *off;
	int status;

	DbgPrint("Daemon_Library_write: file=0x%p len=%d off=%lld\n", file, len,
		 *off);

	if (file->private_data) {
		dh = file->private_data;
		write_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, struct daemon_resource, list);

			if ((DH_TYPE_STREAM == resource->type) && (len >= 0)) {
				totalwrite = 0;
				do {
					thiswrite = len;
					status =
					    novfs_write_stream(resource->
							       connection,
							       resource->handle,
							       (void *)buf,
							       &thiswrite,
							       &offset,
							       dh->session);
					if (status || !thiswrite) {
						/*
						 * If len is zero then the file will have just been
						 * truncated to offset.  Update size.
						 */
						if (!status && !len) {
							resource->size = offset;
						}
						totalwrite = status;
						break;
					}
					len -= thiswrite;
					buf += thiswrite;
					offset += thiswrite;
					totalwrite += thiswrite;
					if (offset > resource->size) {
						resource->size = offset;
					}
				} while (len > 0);
			}
		}
		write_unlock(&dh->lock);
	}
	*off = offset;
	DbgPrint("Daemon_Library_write return = 0x%x\n", totalwrite);

	return (totalwrite);
}

loff_t novfs_daemon_lib_llseek(struct file * file, loff_t offset, int origin)
{
	struct daemon_handle *dh;
	struct daemon_resource *resource;

	loff_t retVal = -EINVAL;

	DbgPrint("Daemon_Library_llseek: file=0x%p offset=%lld origin=%d\n",
		 file, offset, origin);

	if (file->private_data) {
		dh = file->private_data;
		read_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, struct daemon_resource, list);

			if (DH_TYPE_STREAM == resource->type) {
				switch (origin) {
				case 2:
					offset += resource->size;
					break;
				case 1:
					offset += file->f_pos;
				}
				if (offset >= 0) {
					if (offset != file->f_pos) {
						file->f_pos = offset;
						file->f_version = 0;
					}
					retVal = offset;
				}
			}
		}
		read_unlock(&dh->lock);
	}

	DbgPrint("Daemon_Library_llseek: ret %lld\n", retVal);

	return retVal;
}

int novfs_daemon_lib_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int retCode = -ENOSYS;
	struct daemon_handle *dh;
	void *handle = NULL;
	unsigned long cpylen;

	dh = file->private_data;

	DbgPrint("Daemon_Library_ioctl: file=0x%p 0x%x 0x%p dh=0x%p\n", file,
		 cmd, arg, dh);

	if (dh) {

		switch (cmd) {
		case IOC_LOGIN:
			retCode = daemon_login((struct novfs_login *)arg, &dh->session);
			break;

		case IOC_LOGOUT:
			retCode = daemon_logout((struct novfs_logout *)arg, &dh->session);
			break;

		case IOC_DEBUGPRINT:
			{
				struct Ioctl_Debug {
					int length;
					char *data;
				} io;
				char *buf;
				io.length = 0;
				cpylen =
				    copy_from_user(&io, (void *)arg,
						   sizeof(io));
				if (io.length) {
					buf =
					    kmalloc(io.length + 1,
							 GFP_KERNEL);
					if (buf) {
						buf[0] = 0;
						cpylen =
						    copy_from_user(buf, io.data,
								   io.length);
						buf[io.length] = '\0';
						DbgPrint("%s", buf);
						kfree(buf);
						retCode = 0;
					}
				}
				break;
			}

		case IOC_XPLAT:
			{
				struct novfs_xplat data;

				cpylen =
				    copy_from_user(&data, (void *)arg,
						   sizeof(data));
				retCode =
				    ((data.
				      xfunction & 0x0000FFFF) | 0xCC000000);

				switch (data.xfunction) {
				case NWC_OPEN_CONN_BY_NAME:
					DbgPrint
					    ("[VFS XPLAT] Call NwOpenConnByName\n");
					retCode =
					    novfs_open_conn_by_name(&data,
						    &handle, dh->session);
					if (!retCode)
						daemon_added_resource(dh,
								      DH_TYPE_CONNECTION,handle, 0, 0, 0);
					break;

				case NWC_OPEN_CONN_BY_ADDRESS:
					DbgPrint
					    ("[VFS XPLAT] Call NwOpenConnByAddress\n");
					retCode =
					    novfs_open_conn_by_addr(&data, &handle,
							     dh->session);
					if (!retCode)
						daemon_added_resource(dh,
								      DH_TYPE_CONNECTION,
								      handle, 0,
								      0, 0);
					break;

				case NWC_OPEN_CONN_BY_REFERENCE:

					DbgPrint
					    ("[VFS XPLAT] Call NwOpenConnByReference\n");
					retCode =
					    novfs_open_conn_by_ref(&data, &handle,
							    dh->session);
					if (!retCode)
						daemon_added_resource(dh,
								      DH_TYPE_CONNECTION,
								      handle, 0,
								      0, 0);
					break;

				case NWC_SYS_CLOSE_CONN:
					DbgPrint("[VFS XPLAT] Call NwSysCloseConn\n");
					retCode =
						novfs_sys_conn_close(&data, (unsigned long *)&handle, dh->session);
					daemon_remove_resource(dh, DH_TYPE_CONNECTION, handle, 0);
					break;

				case NWC_CLOSE_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwCloseConn\n");
					retCode =
					    novfs_conn_close(&data, &handle,
							dh->session);
					daemon_remove_resource(dh,
							       DH_TYPE_CONNECTION,
							       handle, 0);
					break;

				case NWC_LOGIN_IDENTITY:
					DbgPrint
					    ("[VFS XPLAT] Call NwLoginIdentity\n");
					retCode =
					    novfs_login_id(&data, dh->session);
					break;

				case NWC_RAW_NCP_REQUEST:
					DbgPrint
					    ("[VFS XPLAT] Send Raw NCP Request\n");
					retCode = novfs_raw_send(&data, dh->session);
					break;

				case NWC_AUTHENTICATE_CONN_WITH_ID:
					DbgPrint
					    ("[VFS XPLAT] Authenticate Conn With ID\n");
					retCode =
					    novfs_auth_conn(&data,
							     dh->session);
					break;

				case NWC_UNAUTHENTICATE_CONN:
					DbgPrint
					    ("[VFS XPLAT] UnAuthenticate Conn With ID\n");
					retCode =
					    novfs_unauthenticate(&data,
							     dh->session);
					break;

				case NWC_LICENSE_CONN:
					DbgPrint("Call NwLicenseConn\n");
					retCode =
					    novfs_license_conn(&data, dh->session);
					break;

				case NWC_LOGOUT_IDENTITY:
					DbgPrint
					    ("[VFS XPLAT] Call NwLogoutIdentity\n");
					retCode =
					    novfs_logout_id(&data,
							     dh->session);
					break;

				case NWC_UNLICENSE_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwUnlicense\n");
					retCode =
					    novfs_unlicense_conn(&data, dh->session);
					break;

				case NWC_GET_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetConnInfo\n");
					retCode =
					    novfs_get_conn_info(&data, dh->session);
					break;

				case NWC_SET_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetConnInfo\n");
					retCode =
					    novfs_set_conn_info(&data, dh->session);
					break;

				case NWC_SCAN_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwScanConnInfo\n");
					retCode =
					    novfs_scan_conn_info(&data, dh->session);
					break;

				case NWC_GET_IDENTITY_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetIdentityInfo\n");
					retCode =
					    novfs_get_id_info(&data,
							      dh->session);
					break;

				case NWC_GET_REQUESTER_VERSION:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetDaemonVersion\n");
					retCode =
					    novfs_get_daemon_ver(&data,
							       dh->session);
					break;

				case NWC_GET_PREFERRED_DS_TREE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetPreferredDsTree\n");
					retCode =
					    novfs_get_preferred_DS_tree(&data,
								  dh->session);
					break;

				case NWC_SET_PREFERRED_DS_TREE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetPreferredDsTree\n");
					retCode =
					    novfs_set_preferred_DS_tree(&data,
								  dh->session);
					break;

				case NWC_GET_DEFAULT_NAME_CONTEXT:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetDefaultNameContext\n");
					retCode =
					    novfs_get_default_ctx(&data,
								 dh->session);
					break;

				case NWC_SET_DEFAULT_NAME_CONTEXT:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetDefaultNameContext\n");
					retCode =
					    novfs_set_default_ctx(&data,
								 dh->session);
					break;

				case NWC_QUERY_FEATURE:
					DbgPrint
					    ("[VFS XPLAT] Call NwQueryFeature\n");
					retCode =
					    novfs_query_feature(&data, dh->session);
					break;

				case NWC_GET_TREE_MONITORED_CONN_REF:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetTreeMonitoredConn\n");
					retCode =
					    novfs_get_tree_monitored_conn(&data,
								    dh->
								    session);
					break;

				case NWC_ENUMERATE_IDENTITIES:
					DbgPrint
					    ("[VFS XPLAT] Call NwcEnumerateIdentities\n");
					retCode =
					    novfs_enum_ids(&data,
							      dh->session);
					break;

				case NWC_CHANGE_KEY:
					DbgPrint
					    ("[VFS XPLAT] Call NwcChangeAuthKey\n");
					retCode =
					    novfs_change_auth_key(&data,
							     dh->session);
					break;

				case NWC_CONVERT_LOCAL_HANDLE:
					DbgPrint
					    ("[VFS XPLAT] Call NwdConvertLocalHandle\n");
					retCode =
					    NwdConvertLocalHandle(&data, dh);
					break;

				case NWC_CONVERT_NETWARE_HANDLE:
					DbgPrint
					    ("[VFS XPLAT] Call NwdConvertNetwareHandle\n");
					retCode =
					    NwdConvertNetwareHandle(&data, dh);
					break;

				case NWC_SET_PRIMARY_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetPrimaryConn\n");
					retCode =
					    novfs_set_pri_conn(&data,
							      dh->session);
					break;

				case NWC_GET_PRIMARY_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetPrimaryConn\n");
					retCode =
					    novfs_get_pri_conn(&data,
							      dh->session);
					break;

				case NWC_MAP_DRIVE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcMapDrive\n");
					retCode =
					    set_map_drive(&data, dh->session);
					break;

				case NWC_UNMAP_DRIVE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcUnMapDrive\n");
					retCode =
					    unmap_drive(&data, dh->session);
					break;

				case NWC_ENUMERATE_DRIVES:
					DbgPrint
					    ("[VFS XPLAT] Call NwcEnumerateDrives\n");
					retCode =
					    novfs_enum_drives(&data,
							       dh->session);
					break;

				case NWC_GET_MOUNT_PATH:
					DbgPrint
					    ("[VFS XPLAT] Call NwdGetMountPath\n");
					retCode = NwdGetMountPath(&data);
					break;

				case NWC_GET_BROADCAST_MESSAGE:
					DbgPrint
					    ("[VSF XPLAT Call NwdGetBroadcastMessage\n");
					retCode =
					    novfs_get_bcast_msg(&data,
								   dh->session);
					break;

				case NWC_SET_KEY:
					DbgPrint("[VSF XPLAT Call NwdSetKey\n");
					retCode =
					    novfs_set_key_value(&data, dh->session);
					break;

				case NWC_VERIFY_KEY:
					DbgPrint
					    ("[VSF XPLAT Call NwdVerifyKey\n");
					retCode =
					    novfs_verify_key_value(&data,
							      dh->session);
					break;

				case NWC_RAW_NCP_REQUEST_ALL:
				case NWC_NDS_RESOLVE_NAME_TO_ID:
				case NWC_FRAGMENT_REQUEST:
				case NWC_GET_CONFIGURED_NSPS:
				default:
					break;

				}

				DbgPrint("[NOVFS XPLAT] status Code = %X\n",
					 retCode);
				break;
			}
		}
	}

	return (retCode);
}

unsigned int novfs_daemon_poll(struct file *file,
			 struct poll_table_struct *poll_table)
{
	struct daemon_cmd *que;
	unsigned int mask = POLLOUT | POLLWRNORM;

	que = get_next_queue(0);
	if (que)
		mask |= (POLLIN | POLLRDNORM);
	return mask;
}

static int NwdConvertNetwareHandle(struct novfs_xplat *pdata, struct daemon_handle * DHandle)
{
	int retVal;
	struct nwc_convert_netware_handle nh;
	unsigned long cpylen;

	DbgPrint("NwdConvertNetwareHandle: DHandle=0x%p\n", DHandle);

	cpylen =
	    copy_from_user(&nh, pdata->reqData,
			   sizeof(struct nwc_convert_netware_handle));

	retVal =
	    daemon_added_resource(DHandle, DH_TYPE_STREAM,
				  Uint32toHandle(nh.ConnHandle),
				  nh.NetWareHandle, nh.uAccessMode,
				  nh.uFileSize);

	return (retVal);
}

static int NwdConvertLocalHandle(struct novfs_xplat *pdata, struct daemon_handle * DHandle)
{
	int retVal = NWE_REQUESTER_FAILURE;
	struct daemon_resource *resource;
	struct nwc_convert_local_handle lh;
	struct list_head *l;
	unsigned long cpylen;

	DbgPrint("NwdConvertLocalHandle: DHandle=0x%p\n", DHandle);

	read_lock(&DHandle->lock);

	list_for_each(l, &DHandle->list) {
		resource = list_entry(l, struct daemon_resource, list);

		if (DH_TYPE_STREAM == resource->type) {
			lh.uConnReference =
			    HandletoUint32(resource->connection);

//sgled         memcpy(lh.NwWareHandle, resource->handle, sizeof(resource->handle));
			memcpy(lh.NetWareHandle, resource->handle, sizeof(resource->handle));	//sgled
			if (pdata->repLen >= sizeof(struct nwc_convert_local_handle)) {
				cpylen =
				    copy_to_user(pdata->repData, &lh,
						 sizeof(struct nwc_convert_local_handle));
				retVal = 0;
			} else {
				retVal = NWE_BUFFER_OVERFLOW;
			}
			break;
		}
	}

	read_unlock(&DHandle->lock);

	return (retVal);
}

static int NwdGetMountPath(struct novfs_xplat *pdata)
{
	int retVal = NWE_REQUESTER_FAILURE;
	int len;
	unsigned long cpylen;
	struct nwc_get_mount_path mp;

	cpylen = copy_from_user(&mp, pdata->reqData, pdata->reqLen);

	if (novfs_current_mnt) {

		len = strlen(novfs_current_mnt) + 1;
		if ((len > mp.MountPathLen) && mp.pMountPath) {
			retVal = NWE_BUFFER_OVERFLOW;
		} else {
			if (mp.pMountPath) {
				cpylen =
				    copy_to_user(mp.pMountPath,
						 novfs_current_mnt, len);
			}
			retVal = 0;
		}

		mp.MountPathLen = len;

		if (pdata->repData && (pdata->repLen >= sizeof(mp))) {
			cpylen = copy_to_user(pdata->repData, &mp, sizeof(mp));
		}
	}

	return (retVal);
}

static int set_map_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	int retVal;
	unsigned long cpylen;
	struct nwc_map_drive_ex symInfo;
	char *path;
	struct drive_map *drivemap, *dm;
	struct list_head *list;

	retVal = novfs_set_map_drive(pdata, Session);
	if (retVal)
		return retVal;
	if (copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;
	drivemap =
		kmalloc(sizeof(struct drive_map) + symInfo.linkOffsetLength,
				GFP_KERNEL);
	if (!drivemap)
		return -ENOMEM;

	path = (char *)pdata->reqData;
	path += symInfo.linkOffset;
	cpylen =
		copy_from_user(drivemap->name, path,
				symInfo.linkOffsetLength);

	drivemap->session = Session;
	drivemap->hash =
		full_name_hash(drivemap->name,
				symInfo.linkOffsetLength - 1);
	drivemap->namelen = symInfo.linkOffsetLength - 1;
	DbgPrint("NwdSetMapDrive: hash=0x%x path=%s\n",
			drivemap->hash, drivemap->name);

	dm = (struct drive_map *) & DriveMapList.next;

	down(&DriveMapLock);

	list_for_each(list, &DriveMapList) {
		dm = list_entry(list, struct drive_map, list);
		DbgPrint("NwdSetMapDrive: dm=0x%p\n"
				"   hash:    0x%x\n"
				"   namelen: %d\n"
				"   name:    %s\n",
				dm, dm->hash, dm->namelen, dm->name);

		if (drivemap->hash == dm->hash) {
			if (0 ==
					strcmp(dm->name, drivemap->name)) {
				dm = NULL;
				break;
			}
		} else if (drivemap->hash < dm->hash) {
			break;
		}
	}

	if (dm) {
		if ((dm == (struct drive_map *) & DriveMapList) ||
				(dm->hash < drivemap->hash)) {
			list_add(&drivemap->list, &dm->list);
		} else {
			list_add_tail(&drivemap->list,
					&dm->list);
		}
	}
	kfree(drivemap);
	up(&DriveMapLock);
	return (retVal);
}

static int unmap_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	int retVal = NWE_REQUESTER_FAILURE;
	struct nwc_unmap_drive_ex symInfo;
	char *path;
	struct drive_map *dm;
	struct list_head *list;
	unsigned long hash;


	retVal = novfs_unmap_drive(pdata, Session);
	if (retVal)
		return retVal;
	if (copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;

	path = kmalloc(symInfo.linkLen, GFP_KERNEL);
	if (!path)
		return -ENOMEM;
	if (copy_from_user(path,((struct nwc_unmap_drive_ex *) pdata->reqData)->linkData, symInfo.linkLen)) {
		kfree(path);
		return -EFAULT;
	}

	hash = full_name_hash(path, symInfo.linkLen - 1);
	DbgPrint("NwdUnMapDrive: hash=0x%x path=%s\n", hash,
			path);

	dm = NULL;

	down(&DriveMapLock);

	list_for_each(list, &DriveMapList) {
		dm = list_entry(list, struct drive_map, list);
		DbgPrint("NwdUnMapDrive: dm=0x%p %s\n"
				"   hash:    0x%x\n"
				"   namelen: %d\n",
				dm, dm->name, dm->hash, dm->namelen);

		if (hash == dm->hash) {
			if (0 == strcmp(dm->name, path)) {
				break;
			}
		} else if (hash < dm->hash) {
			dm = NULL;
			break;
		}
	}

	if (dm) {
		DbgPrint("NwdUnMapDrive: Remove dm=0x%p %s\n"
				"   hash:    0x%x\n"
				"   namelen: %d\n",
				dm, dm->name, dm->hash, dm->namelen);
		list_del(&dm->list);
		kfree(dm);
	}

	up(&DriveMapLock);
	return (retVal);
}

static void RemoveDriveMaps(void)
{
	struct drive_map *dm;
	struct list_head *list;

	down(&DriveMapLock);
	list_for_each(list, &DriveMapList) {
		dm = list_entry(list, struct drive_map, list);

		DbgPrint("RemoveDriveMap: dm=0x%p\n"
			 "   hash:    0x%x\n"
			 "   namelen: %d\n"
			 "   name:    %s\n",
			 dm, dm->hash, dm->namelen, dm->name);
		local_unlink(dm->name);
		list = list->prev;
		list_del(&dm->list);
		kfree(dm);
	}
	up(&DriveMapLock);
}

static int local_unlink(const char *pathname)
{
	int error;
	struct dentry *dentry;
	struct nameidata nd;
	struct inode *inode = NULL;

	DbgPrint("local_unlink: %s\n", pathname);
	error = path_lookup(pathname, LOOKUP_PARENT, &nd);
	DbgPrint("local_unlink: path_lookup %d\n", error);
	if (!error) {
		error = -EISDIR;
		if (nd.last_type == LAST_NORM) {
			dentry = lookup_create(&nd, 1);
			DbgPrint("local_unlink: lookup_hash 0x%p\n", dentry);

			error = PTR_ERR(dentry);
			if (!IS_ERR(dentry)) {
				if (nd.last.name[nd.last.len]) {
					error =
					    !dentry->
					    d_inode ? -ENOENT : S_ISDIR(dentry->
									d_inode->
									i_mode)
					    ? -EISDIR : -ENOTDIR;
				} else {
					inode = dentry->d_inode;
					if (inode) {
						atomic_inc(&inode->i_count);
					}
					error = vfs_unlink(nd.path.dentry->d_inode, dentry, nd.path.mnt);
					DbgPrint
					    ("local_unlink: vfs_unlink %d\n",
					     error);
				}
				dput(dentry);
			}
			mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

		}
		path_put(&nd.path);
	}

	if (inode) {
		iput(inode);	/* truncate the inode here */
	}

	DbgPrint("local_unlink: error=%d\n", error);
	return error;
}

