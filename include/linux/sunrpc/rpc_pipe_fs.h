#ifndef _LINUX_SUNRPC_RPC_PIPE_FS_H
#define _LINUX_SUNRPC_RPC_PIPE_FS_H

#ifdef __KERNEL__

struct rpc_pipe_msg {
	struct list_head list;
	void *data;
	size_t len;
	size_t copied;
	int errno;
};

struct rpc_pipe_ops {
	ssize_t (*upcall)(struct file *, struct rpc_pipe_msg *, char __user *, size_t);
	ssize_t (*downcall)(struct file *, const char __user *, size_t);
	void (*destroy_msg)(struct rpc_pipe_msg *);
};

struct rpc_inode {
	struct inode vfs_inode;
	void *private;
	struct list_head pipe;
	int pipelen;
	int nreaders;
	wait_queue_head_t waitq;
#define RPC_PIPE_WAIT_FOR_OPEN	1
	int flags;
	struct rpc_pipe_ops *ops;
};

static inline struct rpc_inode *
RPC_I(struct inode *inode)
{
	return container_of(inode, struct rpc_inode, vfs_inode);
}

extern void rpc_inode_setowner(struct inode *, void *);
extern int rpc_queue_upcall(struct inode *, struct rpc_pipe_msg *);

extern struct dentry *rpc_mkdir(char *, struct rpc_clnt *);
extern int rpc_rmdir(char *);
extern struct dentry *rpc_mkpipe(char *, void *, struct rpc_pipe_ops *, int flags);
extern int rpc_unlink(char *);

#endif
#endif
