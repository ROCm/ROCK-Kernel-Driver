/*
 * fs/nfs/idmap.c
 *
 *  UID and GID to name mapping for clients.
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Marius Aamodt Eriksen <marius@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/sched.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/rpc_pipe_fs.h>

#include <linux/nfs_fs_sb.h>
#include <linux/nfs_fs.h>

#include <linux/nfs_idmap.h>

#define IDMAP_HASH_SZ          128
#define IDMAP_HASH_TYPE_NAME   0x01
#define IDMAP_HASH_TYPE_ID     0x02
#define IDMAP_HASH_TYPE_INSERT 0x04

struct idmap_hashent {
	uid_t     ih_id;
	char      ih_name[IDMAP_NAMESZ];
	u_int32_t ih_namelen;
};

struct idmap {
	char                  idmap_path[48];
	struct dentry        *idmap_dentry;
	wait_queue_head_t     idmap_wq;
	struct idmap_msg      idmap_im;
	struct nfs_server    *idmap_server;
	struct semaphore      idmap_lock;
	struct semaphore      idmap_im_lock;
	struct semaphore      idmap_hash_lock;
	struct idmap_hashent  idmap_id_hash[IDMAP_HASH_SZ];
	struct idmap_hashent  idmap_name_hash[IDMAP_HASH_SZ];
};

static ssize_t   idmap_pipe_upcall(struct file *, struct rpc_pipe_msg *, char *,
                     size_t);
static ssize_t   idmap_pipe_downcall(struct file *, const char *, size_t);
void             idmap_pipe_destroy_msg(struct rpc_pipe_msg *);

static int       validate_ascii(char *, u_int32_t);

static u_int32_t fnvhash32(void *, u_int32_t);
static int       idmap_cache_lookup(struct idmap *, int, char *, u_int32_t *, uid_t *);

static struct rpc_pipe_ops idmap_upcall_ops = {
        .upcall         = idmap_pipe_upcall,
        .downcall       = idmap_pipe_downcall,
        .destroy_msg    = idmap_pipe_destroy_msg,
};

void *
nfs_idmap_new(struct nfs_server *server)
{
	struct idmap *idmap;

        if ((idmap = kmalloc(sizeof(*idmap), GFP_KERNEL)) == NULL)
                return (NULL);

	memset(idmap, 0, sizeof(*idmap));

	idmap->idmap_server = server;

	snprintf(idmap->idmap_path, sizeof(idmap->idmap_path),
	    "%s/idmap", idmap->idmap_server->client->cl_pathname);

        idmap->idmap_dentry = rpc_mkpipe(idmap->idmap_path,
	    idmap->idmap_server, &idmap_upcall_ops, 0);
        if (IS_ERR(idmap->idmap_dentry))
		goto err_free;

        init_MUTEX(&idmap->idmap_lock);
        init_MUTEX(&idmap->idmap_im_lock);
        init_MUTEX(&idmap->idmap_hash_lock);
	init_waitqueue_head(&idmap->idmap_wq);

	return (idmap);

 err_free:
	kfree(idmap);
	return (NULL);
}

void
nfs_idmap_delete(struct nfs_server *server)
{
	struct idmap *idmap = server->idmap;

	if (!idmap)
		return;
	rpc_unlink(idmap->idmap_path);
	server->idmap = NULL;
	kfree(idmap);
}

/*
 * Name -> ID
 */
int
nfs_idmap_id(struct nfs_server *server, u_int8_t type, char *name, 
    u_int namelen, uid_t *id)
{
	struct rpc_pipe_msg msg;
	struct idmap *idmap = server->idmap;
	struct idmap_msg *im;
	DECLARE_WAITQUEUE(wq, current);
	int ret = -1, hashtype = IDMAP_HASH_TYPE_NAME, xnamelen = namelen;

	if (idmap == NULL)
		return (-1);

	im = &idmap->idmap_im;

	if (namelen > IDMAP_NAMESZ || namelen == 0)
		return (-1);

	down(&idmap->idmap_lock);
	down(&idmap->idmap_im_lock);

	if (name[xnamelen - 1] == '\0')
		xnamelen--;

	if (idmap_cache_lookup(idmap, hashtype, name, &xnamelen, id) == 0) {
		ret = 0;
		goto out;
	}

	memset(im, 0, sizeof(*im));
	memcpy(im->im_name, name, namelen);
	/* Make sure the string is NULL terminated */
	if (namelen != xnamelen) {
		/* We cannot fit a NULL character */
		if (namelen == IDMAP_NAMESZ) {
			ret = -1;
			goto out;
		}
		im->im_name[namelen] = '\0';
	} 

	im->im_type = type;
	im->im_conv = IDMAP_CONV_NAMETOID;

	memset(&msg, 0, sizeof(msg));
	msg.data = im;
	msg.len = sizeof(*im);

	add_wait_queue(&idmap->idmap_wq, &wq);
	if (rpc_queue_upcall(idmap->idmap_dentry->d_inode, &msg) < 0) {
		remove_wait_queue(&idmap->idmap_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	up(&idmap->idmap_im_lock);
	schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(&idmap->idmap_wq, &wq);
	down(&idmap->idmap_im_lock);

	/*
	 * XXX Race condition here, with testing for status.  Go ahead
	 * and and do the cace lookup anyway.
	 */
	if (im->im_status & IDMAP_STATUS_SUCCESS) {
		ret = 0;
		*id = im->im_id;

		hashtype |= IDMAP_HASH_TYPE_INSERT;
		ret = idmap_cache_lookup(idmap, hashtype, name, &xnamelen, id);
	}

 out:
	memset(im, 0, sizeof(*im));
	up(&idmap->idmap_im_lock);
	up(&idmap->idmap_lock);
	return (ret);
}

/*
 * ID -> Name
 */
int
nfs_idmap_name(struct nfs_server *server, u_int8_t type, uid_t id,
    char *name, u_int *namelen)
{
	struct rpc_pipe_msg msg;
	struct idmap *idmap = server->idmap;
	struct idmap_msg *im;
	DECLARE_WAITQUEUE(wq, current);
	int ret = -1, hashtype = IDMAP_HASH_TYPE_ID;
	u_int len;

	if (idmap == NULL)
		return (-1);

	im = &idmap->idmap_im;

	if (*namelen < IDMAP_NAMESZ || *namelen == 0)
		return (-1);

	down(&idmap->idmap_lock);
	down(&idmap->idmap_im_lock);

	if (idmap_cache_lookup(idmap, hashtype, name, namelen, &id) == 0) {
		ret = 0;
		goto out;
	}

	memset(im, 0, sizeof(*im));
	im->im_type = type;
	im->im_conv = IDMAP_CONV_IDTONAME;
	im->im_id = id;

	memset(&msg, 0, sizeof(msg));
	msg.data = im;
	msg.len = sizeof(*im);

	add_wait_queue(&idmap->idmap_wq, &wq);

	if (rpc_queue_upcall(idmap->idmap_dentry->d_inode, &msg) < 0) {
		remove_wait_queue(&idmap->idmap_wq, &wq);
		goto out;
	}

	/*
	 * XXX add timeouts here
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	up(&idmap->idmap_im_lock);
	schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(&idmap->idmap_wq, &wq);
	down(&idmap->idmap_im_lock);

	if (im->im_status & IDMAP_STATUS_SUCCESS) {
		if ((len = validate_ascii(im->im_name, IDMAP_NAMESZ)) == -1)
			goto out;
		ret = 0;
		memcpy(name, im->im_name, len);
		*namelen = len;

		hashtype |= IDMAP_HASH_TYPE_INSERT;
		ret = idmap_cache_lookup(idmap, hashtype, name, namelen, &id);
	}

 out:
	memset(im, 0, sizeof(*im));
	up(&idmap->idmap_im_lock);
	up(&idmap->idmap_lock);
	return (ret);
}

static ssize_t
idmap_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
    char *dst, size_t buflen)
{
        char *data = (char *)msg->data + msg->copied;
        ssize_t mlen = msg->len - msg->copied;
        ssize_t left;

        if (mlen > buflen)
                mlen = buflen;

        left = copy_to_user(dst, data, mlen);
	if (left < 0) {
		msg->errno = left;
		return left;
	}
	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
        return mlen;
}

static ssize_t
idmap_pipe_downcall(struct file *filp, const char *src, size_t mlen)
{
        struct rpc_inode *rpci = RPC_I(filp->f_dentry->d_inode);
	struct nfs_server *server = rpci->private;
	struct idmap *idmap = server->idmap;
	struct idmap_msg im_in, *im = &idmap->idmap_im;
	int match = 0, hashtype, badmsg = 0, namelen_in, namelen;

        if (mlen != sizeof(im_in))
                return (-ENOSPC);

        if (copy_from_user(&im_in, src, mlen) != 0)
		return (-EFAULT);

	down(&idmap->idmap_im_lock);

	namelen_in = validate_ascii(im_in.im_name, IDMAP_NAMESZ);
	namelen = validate_ascii(im->im_name, IDMAP_NAMESZ);

	badmsg = !(im_in.im_status & IDMAP_STATUS_SUCCESS) || namelen_in <= 0;

	switch (im_in.im_conv) {
	case IDMAP_CONV_IDTONAME:
		match = im->im_id == im_in.im_id;
		break;
	case IDMAP_CONV_NAMETOID:
		match = namelen == namelen_in &&
		    memcmp(im->im_name, im_in.im_name, namelen) == 0;
		break;
	default:
		badmsg = 1;
		break;
	}

	match = match && im->im_type == im_in.im_type;

	if (match) {
		memcpy(im, &im_in, sizeof(*im));
		wake_up(&idmap->idmap_wq);
	} else if (!badmsg) {
		hashtype = im_in.im_conv == IDMAP_CONV_IDTONAME ?
		    IDMAP_HASH_TYPE_ID : IDMAP_HASH_TYPE_NAME;
		hashtype |= IDMAP_HASH_TYPE_INSERT;
		idmap_cache_lookup(idmap, hashtype, im_in.im_name, &namelen_in,
		    &im_in.im_id);
	}

	up(&idmap->idmap_im_lock);
	return (mlen);
}

void
idmap_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct idmap_msg *im = msg->data;
	struct idmap *idmap = container_of(im, struct idmap, idmap_im); 

	if (msg->errno >= 0)
		return;
	down(&idmap->idmap_im_lock);
	im->im_status = IDMAP_STATUS_LOOKUPFAIL;
	wake_up(&idmap->idmap_wq);
	up(&idmap->idmap_im_lock);
}

static int
validate_ascii(char *string, u_int32_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (string[i] == '\0')
			break;

		if (string[i] & 0x80)
			return (-1);
	}

	if (string[i] != '\0')
		return (-1);

	return (i);
}

/* 
 * Fowler/Noll/Vo hash
 *    http://www.isthe.com/chongo/tech/comp/fnv/
 */

#define FNV_P_32 ((u_int32_t)0x01000193) /* 16777619 */
#define FNV_1_32 ((u_int32_t)0x811c9dc5) /* 2166136261 */

static u_int32_t
fnvhash32(void *buf, u_int32_t buflen)
{
	u_char *p, *end = (u_char *)buf + buflen;
	u_int32_t hash = FNV_1_32;

	for (p = buf; p < end; p++) {
		hash *= FNV_P_32;
		hash ^= (u_int32_t)*p;
	}

	return (hash);
}

/*
 * ->ih_namelen == 0 indicates negative entry
 */
static int
idmap_cache_lookup(struct idmap *idmap, int type, char *name, u_int32_t *namelen,
    uid_t *id)
{
	u_int32_t hash;
	struct idmap_hashent *he = NULL;
	int insert = type & IDMAP_HASH_TYPE_INSERT;
	int ret = -1;

	/*
	 * XXX technically, this is not needed, since we will always
	 * hold idmap_im_lock when altering the hash tables.  but
	 * semantically that just hurts.
	 *
	 * XXX cache negative responses
	 */
	down(&idmap->idmap_hash_lock);

	if (*namelen > IDMAP_NAMESZ || *namelen == 0)
		goto out;

	if (type & IDMAP_HASH_TYPE_NAME) {
		hash = fnvhash32(name, *namelen) % IDMAP_HASH_SZ;
		he = &idmap->idmap_name_hash[hash];

		/*
		 * Testing he->ih_namelen == *namelen implicitly tests
		 * namelen != 0, and thus a non-negative entry.
		 */
		if (!insert && he->ih_namelen == *namelen && 
		    memcmp(he->ih_name, name, *namelen) == 0) {
			*id = he->ih_id;
			ret = 0;
			goto out;
		}
	}

	if (type & IDMAP_HASH_TYPE_ID) {
		hash = fnvhash32(id, sizeof(*id)) % IDMAP_HASH_SZ;
		he = &idmap->idmap_id_hash[hash];

		if (!insert && *id == he->ih_id && he->ih_namelen != 0 && 
		    *namelen >= he->ih_namelen) {
			memcpy(name, he->ih_name, he->ih_namelen);
			*namelen = he->ih_namelen;
			ret = 0;
			goto out;
		}
	}

	if (insert && he != NULL) {
		he->ih_id = *id;
		memcpy(he->ih_name, name, *namelen);
		he->ih_namelen = *namelen;
		ret = 0;
	}

 out:
	up(&idmap->idmap_hash_lock);
	return (ret);
}
