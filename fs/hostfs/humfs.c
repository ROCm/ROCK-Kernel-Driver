/* 
 * Copyright (C) 2004 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/kdev_t.h>
#include "linux/init.h"
#include "linux/workqueue.h"
#include <asm/irq.h>
#include "hostfs.h"
#include "mem.h"
#include "os.h"
#include "mode.h"
#include "aio.h"
#include "irq_user.h"
#include "irq_kern.h"
#include "filehandle.h"
#include "metadata.h"

#define HUMFS_VERSION 2

static int humfs_stat_file(const char *path, struct externfs_data *ed, 
			   dev_t *dev_out, unsigned long long *inode_out, 
			   int *mode_out, int *nlink_out, int *uid_out, 
			   int *gid_out, unsigned long long *size_out, 
			   unsigned long *atime_out, unsigned long *mtime_out, 
			   unsigned long *ctime_out, int *blksize_out, 
			   unsigned long long *blocks_out)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int err, mode, perms, major, minor;
	char type;

	err = host_stat_file(data_path, NULL, inode_out, mode_out, 
			     nlink_out, NULL, NULL, size_out, atime_out, 
			     mtime_out, ctime_out, blksize_out, blocks_out);
	if(err)
		return(err);

	err = (*mount->meta->ownerships)(path, &perms, uid_out, gid_out, 
					 &type, &major, &minor, mount);
	if(err)
		return(err);

	*mode_out = (*mode_out & ~S_IRWXUGO) | perms;

	mode = 0;
	switch(type){
	case 'c':
		mode = S_IFCHR;
		*dev_out = MKDEV(major, minor);
		break;
	case 'b':
		mode = S_IFBLK;
		*dev_out = MKDEV(major, minor);
		break;
	case 's':
		mode = S_IFSOCK;
		break;
	default:
		break;
	}

	if(mode != 0)
		*mode_out = (*mode_out & ~S_IFMT) | mode;

	return(0);
}

static int meta_type(const char *path, int *dev_out, void *m)
{
	struct humfs *mount = m;
	int err, type, maj, min;
	char c;

	err = (*mount->meta->ownerships)(path, NULL, NULL, NULL, &c, &maj, 
					 &min, mount);
	if(err)
		return(err);

	if(c == 0)
		return(0);

	if(dev_out)
		*dev_out = MKDEV(maj, min);

	switch(c){
	case 'c':
		type = OS_TYPE_CHARDEV;
		break;
	case 'b':
		type = OS_TYPE_BLOCKDEV;
		break;
	case 'p':
		type = OS_TYPE_FIFO;
		break;
	case 's':
		type = OS_TYPE_SOCK;
		break;
	default:
		type = -EINVAL;
		break;
	}

	return(type);
}

static int humfs_file_type(const char *path, int *dev_out, 
			   struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int type;

	type = meta_type(path, dev_out, mount);
	if(type != 0)
		return(type);

	return(host_file_type(data_path, dev_out));
}

static char *humfs_data_name(struct inode *inode)
{
	struct externfs_data *ed = inode_externfs_info(inode);
	struct humfs *mount = container_of(ed, struct humfs, ext);

	return(inode_name_prefix(inode, mount->data));
}

static struct externfs_inode *humfs_init_file(struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct humfs_file *hf;

	hf = (*mount->meta->init_file)();
	if(hf == NULL)
		return(NULL);

	hf->data.fd = -1;
	return(&hf->ext);
}

static int humfs_open_file(struct externfs_inode *ext, char *path, int uid, 
			   int gid, struct inode *inode, 
			   struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	struct openflags flags;
	char tmp[HOSTFS_BUFSIZE], *file;
	int err = -ENOMEM;

	file = get_path(data_path, tmp, sizeof(tmp));
	if(file == NULL)
		goto out;

	flags = of_rdwr(OPENFLAGS());
	if(mount->direct)
		flags = of_direct(flags);

	if(path == NULL)
		path = "";
	err = (*mount->meta->open_file)(hf, path, inode, mount);
	if(err)
		goto out_free;

	err = open_filehandle(file, flags, 0, &hf->data);
	if(err == -EISDIR)
		goto out;
	else if(err == -EPERM){
		flags = of_set_rw(flags, 1, 0);
		err = open_filehandle(file, flags, 0, &hf->data);
	}
	
	if(err)
		goto out_close;

	hf->mount = mount;
	is_reclaimable(&hf->data, humfs_data_name, inode);

 out_free:
	free_path(file, tmp);
 out:
	return(err);
	
 out_close:
	(*mount->meta->close_file)(hf);
	goto out_free;
}

static void *humfs_open_dir(char *path, int uid, int gid, 
			    struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };

	return(host_open_dir(data_path));
}

static void humfs_close_dir(void *stream, struct externfs_data *ed)
{
	os_close_dir(stream);
}

static char *humfs_read_dir(void *stream, unsigned long long *pos, 
			    unsigned long long *ino_out, int *len_out, 
			    struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);

	return(generic_host_read_dir(stream, pos, ino_out, len_out, mount));
}

LIST_HEAD(humfs_replies);

struct humfs_aio {
	struct aio_context aio;
	struct list_head list;
	void (*completion)(char *, int, void *);
	char *buf;
	int real_len;
	int err;
	void *data;
};

static int humfs_reply_fd = -1;

struct humfs_aio last_task_aio, last_intr_aio;
struct humfs_aio *last_task_aio_ptr, *last_intr_aio_ptr;

void humfs_work_proc(void *unused)
{
	struct humfs_aio *aio;
	unsigned long flags;

	while(!list_empty(&humfs_replies)){
		local_irq_save(flags);
		aio = list_entry(humfs_replies.next, struct humfs_aio, list);

		last_task_aio = *aio;
		last_task_aio_ptr = aio;

		list_del(&aio->list);
		local_irq_restore(flags);

		if(aio->err >= 0)
			aio->err = aio->real_len;
		(*aio->completion)(aio->buf, aio->err, aio->data);
		kfree(aio);
	}
}

DECLARE_WORK(humfs_work, humfs_work_proc, NULL);

static irqreturn_t humfs_interrupt(int irq, void *dev_id, 
				   struct pt_regs *unused)
{
	struct aio_thread_reply reply;
	struct humfs_aio *aio;
	int err, fd = (int) dev_id;

	while(1){
		err = os_read_file(fd, &reply, sizeof(reply));
		if(err < 0){
			if(err == -EAGAIN)
				break;
			printk("humfs_interrupt - read returned err %d\n", 
			       -err);
			return(IRQ_HANDLED);
		}
		aio = reply.data;
		aio->err = reply.err;
		list_add(&aio->list, &humfs_replies);
		last_intr_aio = *aio;
		last_intr_aio_ptr = aio;
	}

	if(!list_empty(&humfs_replies))
		schedule_work(&humfs_work);
	reactivate_fd(fd, HUMFS_IRQ);
	return(IRQ_HANDLED);
}

static int init_humfs_aio(void)
{
	int fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err){
		printk("init_humfs_aio - pipe failed, err = %d\n", -err);
		goto out;
	}

	err = um_request_irq(HUMFS_IRQ, fds[0], IRQ_READ, humfs_interrupt,
			     SA_INTERRUPT | SA_SAMPLE_RANDOM, "humfs", 
			     (void *) fds[0]);
	if(err){
		printk("init_humfs_aio - : um_request_irq failed, err = %d\n",
		       err);
		goto out_close;
	}

	humfs_reply_fd = fds[1];
	goto out;
	
 out_close:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
 out:
	return(0);
}

__initcall(init_humfs_aio);

static int humfs_aio(enum aio_type type, int fd, unsigned long long offset,
		     char *buf, int len, int real_len,
		     void (*completion)(char *, int, void *), void *arg)
{
	struct humfs_aio *aio;
	int err = -ENOMEM;

	aio = kmalloc(sizeof(*aio), GFP_KERNEL);
	if(aio == NULL)
		goto out;
	*aio = ((struct humfs_aio) { .aio	= INIT_AIO_CONTEXT,
				     .list	= LIST_HEAD_INIT(aio->list),
				     .completion= completion,
				     .buf	= buf,
				     .err	= 0,
				     .real_len	= real_len,
				     .data	= arg });

	err = submit_aio(type, fd, buf, len, offset, humfs_reply_fd, aio);
	if(err)
		(*completion)(buf, err, arg);

 out:
	return(err);
}

static int humfs_read_file(struct externfs_inode *ext,
			   unsigned long long offset, char *buf, int len,
			   int ignore_start, int ignore_end,
			   void (*completion)(char *, int, void *), void *arg, 
			   struct externfs_data *ed)
{
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	int fd = filehandle_fd(&hf->data);

	if(fd < 0){
		(*completion)(buf, fd, arg);
		return(fd);
	}

	return(humfs_aio(AIO_READ, fd, offset, buf, len, len, completion, 
			 arg));
}

static int humfs_write_file(struct externfs_inode *ext,
			    unsigned long long offset, 
			    const char *buf, int start, int len, 
			    void (*completion)(char *, int, void *), void *arg,
			    struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	int err, orig_len = len, fd = filehandle_fd(&hf->data);

	if(fd < 0){
		(*completion)((char *) buf, fd, arg);
		return(fd);
	}

	if(mount->direct)
		len = PAGE_SIZE;
	else {
		offset += start;
		buf += start;
	}

	err = humfs_aio(AIO_WRITE, fd, offset, (char *) buf, len, orig_len, 
			completion, arg);

	if(err < 0)
		return(err);

	if(mount->direct)
		err = orig_len;

	return(err);
}

static int humfs_map_file_page(struct externfs_inode *ext, 
			       unsigned long long offset, char *buf, int w, 
			       struct externfs_data *ed)
{
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	unsigned long long size, need;
	int err, fd = filehandle_fd(&hf->data);

	if(fd < 0)
		return(fd);

	err = os_fd_size(fd, &size);
	if(err)
		return(err);

	need = offset + PAGE_SIZE;
	if(size < need){
		err = os_truncate_fd(fd, need);
		if(err)
			return(err);
	}
	
	return(physmem_subst_mapping(buf, fd, offset, w));
}

static void humfs_close_file(struct externfs_inode *ext,
			     unsigned long long size)
{
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	int fd;

	if(hf->data.fd == -1)
		return;

	fd = filehandle_fd(&hf->data);
	physmem_forget_descriptor(fd);
	truncate_file(&hf->data, size);
	close_file(&hf->data);

	(*hf->mount->meta->close_file)(hf);
}

/* XXX Assumes that you can't make a normal file */

static int humfs_make_node(const char *path, int mode, int uid, int gid, 
			   int type, int major, int minor, 
			   struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct file_handle fh;
	const char *data_path[3] = { mount->data, path, NULL };
	int err;
	char t;

	err = host_create_file(data_path, S_IRWXUGO, &fh);
	if(err)
		goto out;

	close_file(&fh);

	switch(type){
	case S_IFCHR:
		t = 'c';
		break;
	case S_IFBLK:
		t = 'b';
		break;
	case S_IFIFO:
		t = 'p';
		break;
	case S_IFSOCK:
		t = 's';
		break;
	default:
		err = -EINVAL;
		printk("make_node - bad node type : %d\n", type);
		goto out_rm;
	}

	err = (*mount->meta->make_node)(path, mode, uid, gid, t, major, minor, 
					mount);
	if(err)
		goto out_rm;

 out:
	return(err);

 out_rm:
	host_unlink_file(data_path);
	goto out;
}
		
static int humfs_create_file(struct externfs_inode *ext, char *path, int mode,
			     int uid, int gid, struct inode *inode, 
			     struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int err;

	err = (*mount->meta->create_file)(hf, path, mode, uid, gid, inode, 
					  mount);
	if(err)
		goto out;

	err = host_create_file(data_path, S_IRWXUGO, &hf->data);
	if(err)
		goto out_rm;

	
	is_reclaimable(&hf->data, humfs_data_name, inode);

	return(0);

 out_rm:
	(*mount->meta->remove_file)(path, mount);
	(*mount->meta->close_file)(hf);
 out:
	return(err);
}

static int humfs_set_attr(const char *path, struct externfs_iattr *attrs, 
			  struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int (*chown)(const char *, int, int, int, struct humfs *);
	int err;

	chown = mount->meta->change_ownerships;
	if(attrs->ia_valid & EXTERNFS_ATTR_MODE){
		err = (*chown)(path, attrs->ia_mode, -1, -1, mount);
		if(err)
			return(err);
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_UID){
		err = (*chown)(path, -1, attrs->ia_uid, -1, mount);
		if(err)
			return(err);
	}
	if(attrs->ia_valid & EXTERNFS_ATTR_GID){
		err = (*chown)(path, -1, -1, attrs->ia_gid, mount);
		if(err)
			return(err);
	}

	attrs->ia_valid &= ~(EXTERNFS_ATTR_MODE | EXTERNFS_ATTR_UID | 
			     EXTERNFS_ATTR_GID);

	return(host_set_attr(data_path, attrs));
}

static int humfs_make_symlink(const char *from, const char *to, int uid, 
			      int gid, struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	struct humfs_file *hf;
	const char *data_path[3] = { mount->data, from, NULL };
	int err = -ENOMEM;

	hf = (*mount->meta->init_file)();
	if(hf == NULL)
		goto out;

	err = (*mount->meta->create_file)(hf, from, S_IRWXUGO, uid, gid, NULL, 
					  mount);
	if(err)
		goto out_close;

	err = host_make_symlink(data_path, to);
	if(err)
		(*mount->meta->remove_file)(from, mount);

 out_close:
	(*mount->meta->close_file)(hf);
 out:
	return(err);
}

static int humfs_link_file(const char *to, const char *from, int uid, int gid, 
			   struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path_from[3] = { mount->data, from, NULL };
	const char *data_path_to[3] = { mount->data, to, NULL };
	int err;

	err = (*mount->meta->create_link)(to, from, mount);
	if(err)
		return(err);

	err = host_link_file(data_path_to, data_path_from);
	if(err)
		(*mount->meta->remove_file)(from, mount);
	
	return(err);
}

static int humfs_unlink_file(const char *path, struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int err;

	err = (*mount->meta->remove_file)(path, mount);
	if (err)
		return err;

	(*mount->meta->remove_file)(path, mount);
	return(host_unlink_file(data_path));
}

static void humfs_invisible(struct externfs_inode *ext)
{
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);
	struct humfs *mount = hf->mount;
	
	(*mount->meta->invisible)(hf);
	not_reclaimable(&hf->data);
}

static int humfs_make_dir(const char *path, int mode, int uid, int gid, 
			  struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int err;

	err = (*mount->meta->create_dir)(path, mode, uid, gid, mount);
	if(err)
		return(err);
	
	err = host_make_dir(data_path, S_IRWXUGO);
	if(err)
		(*mount->meta->remove_dir)(path, mount);

	return(err);
}

static int humfs_remove_dir(const char *path, int uid, int gid, 
			    struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, path, NULL };
	int err;

	err = host_remove_dir(data_path);
	if (err)
		return err;

	(*mount->meta->remove_dir)(path, mount);

	return(err);
}

static int humfs_read_link(char *file, int uid, int gid, char *buf, int size, 
			   struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, file, NULL };

	return(host_read_link(data_path, buf, size));
}

struct humfs *inode_humfs_info(struct inode *inode)
{
	return(container_of(inode_externfs_info(inode), struct humfs, ext));
}

static int humfs_rename_file(char *from, char *to, struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path_from[3] = { mount->data, from, NULL };
	const char *data_path_to[3] = { mount->data, to, NULL };
	int err;

	err = (*mount->meta->rename_file)(from, to, mount);
	if(err)
		return(err);
	
	err = host_rename_file(data_path_from, data_path_to);
	if(err)
		(*mount->meta->rename_file)(to, from, mount);

	return(err);
}

static int humfs_stat_fs(long *bsize_out, long long *blocks_out, 
			 long long *bfree_out, long long *bavail_out, 
			 long long *files_out, long long *ffree_out, 
			 void *fsid_out, int fsid_size, long *namelen_out, 
			 long *spare_out, struct externfs_data *ed)
{
	struct humfs *mount = container_of(ed, struct humfs, ext);
	const char *data_path[3] = { mount->data, NULL };
	int err;

	/* XXX Needs to maintain this info as metadata */
	err = host_stat_fs(data_path, bsize_out, blocks_out, bfree_out, 
			   bavail_out, files_out, ffree_out, fsid_out, 
			   fsid_size, namelen_out, spare_out);
	if(err)
		return(err);

	*blocks_out = mount->total / *bsize_out;
	*bfree_out = (mount->total - mount->used) / *bsize_out;
	*bavail_out = (mount->total - mount->used) / *bsize_out;
	return(0);
}

int humfs_truncate_file(struct externfs_inode *ext, __u64 size, 
			struct externfs_data *ed)
{
	struct humfs_file *hf = container_of(ext, struct humfs_file, ext);

	return(truncate_file(&hf->data, size));
}

char *humfs_path(char *dir, char *file)
{
	int need_slash, len = strlen(dir) + strlen(file);
	char *new;

	need_slash = (dir[strlen(dir) - 1] != '/');
	if(need_slash)
		len++;

	new = kmalloc(len + 1, GFP_KERNEL);
	if(new == NULL)
		return(NULL);

	strcpy(new, dir);
	if(need_slash)
		strcat(new, "/");
	strcat(new, file);

	return(new);
}

DECLARE_MUTEX(meta_sem);
struct list_head metas = LIST_HEAD_INIT(metas);

static struct humfs_meta_ops *find_meta(const char *name)
{
	struct list_head *ele;
	struct humfs_meta_ops *m;
 
	down(&meta_sem);
	list_for_each(ele, &metas){
		m = list_entry(ele, struct humfs_meta_ops, list);
		if(!strcmp(m->name, name))
			goto out;
	}
	m = NULL;
 out:
	up(&meta_sem);
	return(m);
}

void register_meta(struct humfs_meta_ops *ops)
{
	down(&meta_sem);
	list_add(&ops->list, &metas);
	up(&meta_sem);
}
 
void unregister_meta(struct humfs_meta_ops *ops)
{
	down(&meta_sem);
	list_del(&ops->list);
	up(&meta_sem);
}
 
static struct humfs *read_superblock(char *root)
{
	struct humfs *mount;
	struct humfs_meta_ops *meta = NULL;
	struct file_handle *fh;
	const char *path[] = { root, "superblock", NULL };
	u64 used, total;
	char meta_buf[33], line[HOSTFS_BUFSIZE], *newline;
	unsigned long long pos;
	int version, i, n, err;

	fh = kmalloc(sizeof(*fh), GFP_KERNEL);
	if(fh == NULL)
		return(ERR_PTR(-ENOMEM));

	err = host_open_file(path, 1, 0, fh);
	if(err){
		printk("Failed to open %s/%s, errno = %d\n", path[0],
		       path[1], err);
		return(ERR_PTR(err));
	}

	used = 0;
	total = 0;
	pos = 0;
	i = 0;
	while(1){
		n = read_file(fh, pos, &line[i], sizeof(line) - i - 1);
		if((n == 0) && (i == 0))
			break;
		if(n < 0)
			return(ERR_PTR(n));

		pos += n;
		if(n > 0)
			line[n + i] = '\0';

		newline = strchr(line, '\n');
		if(newline == NULL){
			printk("read_superblock - line too long : '%s'\n", 
			       line);
			return(ERR_PTR(-EINVAL));
		}
		newline++;

		if(sscanf(line, "version %d\n", &version) == 1){
			if(version != HUMFS_VERSION){
				printk("humfs version mismatch - want version "
				       "%d, got version %d.\n", HUMFS_VERSION,
				       version);
				return(ERR_PTR(-EINVAL));
			}
		}
		else if(sscanf(line, "used %Lu\n", &used) == 1) ;
		else if(sscanf(line, "total %Lu\n", &total) == 1) ;
		else if(sscanf(line, "metadata %32s\n", meta_buf) == 1){
			meta = find_meta(meta_buf);
			if(meta == NULL){
				printk("read_superblock - meta api \"%s\" not "
				       "registered\n", meta_buf);
				return(ERR_PTR(-EINVAL));
			}
		}
		
		else {
			printk("read_superblock - bogus line : '%s'\n", line);
			return(ERR_PTR(-EINVAL));
		}

		i = newline - line;
		memmove(line, newline, sizeof(line) - i);
		i = strlen(line);
	}

	if(used == 0){
		printk("read_superblock - used not specified or set to "
		       "zero\n");
		return(ERR_PTR(-EINVAL));
	}
	if(total == 0){
		printk("read_superblock - total not specified or set to "
		       "zero\n");
		return(ERR_PTR(-EINVAL));
	}
	if(used > total){
		printk("read_superblock - used is greater than total\n");
		return(ERR_PTR(-EINVAL));
	}

	if(meta == NULL){
		meta = find_meta("shadow_fs");
	}

	if(meta == NULL){
		printk("read_superblock - valid meta api was not specified\n");
		return(ERR_PTR(-EINVAL));
	}

	mount = (*meta->init_mount)(root);
	if(IS_ERR(mount))
		return(mount);

	*mount = ((struct humfs) { .total	= total,
				   .used	= used,
				   .meta	= meta });
	return(mount);
}

struct externfs_file_ops humfs_no_mmap_file_ops = {
	.stat_file		= humfs_stat_file,
	.file_type		= humfs_file_type,
	.access_file		= NULL,
	.open_file		= humfs_open_file,
	.open_dir		= humfs_open_dir,
	.read_dir		= humfs_read_dir,
	.read_file		= humfs_read_file,
	.write_file		= humfs_write_file,
	.map_file_page		= NULL,
	.close_file		= humfs_close_file,
	.close_dir		= humfs_close_dir,
	.invisible		= humfs_invisible,
	.create_file		= humfs_create_file,
	.set_attr		= humfs_set_attr,
	.make_symlink		= humfs_make_symlink,
	.unlink_file		= humfs_unlink_file,
	.make_dir		= humfs_make_dir,
	.remove_dir		= humfs_remove_dir,
	.make_node		= humfs_make_node,
	.link_file		= humfs_link_file,
	.read_link		= humfs_read_link,
	.rename_file		= humfs_rename_file,
	.statfs			= humfs_stat_fs,
	.truncate_file		= humfs_truncate_file
};

struct externfs_file_ops humfs_mmap_file_ops = {
	.stat_file		= humfs_stat_file,
	.file_type		= humfs_file_type,
	.access_file		= NULL,
	.open_file		= humfs_open_file,
	.open_dir		= humfs_open_dir,
	.invisible		= humfs_invisible,
	.read_dir		= humfs_read_dir,
	.read_file		= humfs_read_file,
	.write_file		= humfs_write_file,
	.map_file_page		= humfs_map_file_page,
	.close_file		= humfs_close_file,
	.close_dir		= humfs_close_dir,
	.create_file		= humfs_create_file,
	.set_attr		= humfs_set_attr,
	.make_symlink		= humfs_make_symlink,
	.unlink_file		= humfs_unlink_file,
	.make_dir		= humfs_make_dir,
	.remove_dir		= humfs_remove_dir,
	.make_node		= humfs_make_node,
	.link_file		= humfs_link_file,
	.read_link		= humfs_read_link,
	.rename_file		= humfs_rename_file,
	.statfs			= humfs_stat_fs,
	.truncate_file		= humfs_truncate_file
};

static struct externfs_data *mount_fs(char *mount_arg)
{
	char *root, *data, *flags;
	struct humfs *mount;
	struct externfs_file_ops *file_ops;
	int err, do_mmap = 0;

	if(mount_arg == NULL){
		printk("humfs - no host directory specified\n");
		return(NULL);
	}

	flags = strchr((char *) mount_arg, ',');
	if(flags != NULL){
		do {
			*flags++ = '\0';

			if(!strcmp(flags, "mmap"))
				do_mmap = 1;

			flags = strchr(flags, ',');
		} while(flags != NULL);
	}

	err = -ENOMEM;
	root = host_root_filename(mount_arg);
	if(root == NULL)
		goto err;

	mount = read_superblock(root);
	if(IS_ERR(mount)){
		err = PTR_ERR(mount);
		goto err_free_root;
	}

	data = humfs_path(root, "data/");
	if(data == NULL)
		goto err_free_mount;

	if(CHOOSE_MODE(do_mmap, 0)){
		printk("humfs doesn't support mmap in tt mode\n");
		do_mmap = 0;
	}

	mount->data = data;
	mount->mmap = do_mmap;

	file_ops = do_mmap ? &humfs_mmap_file_ops : &humfs_no_mmap_file_ops;
	init_externfs(&mount->ext, file_ops);

	return(&mount->ext);

 err_free_mount:
	kfree(mount);
 err_free_root:
	kfree(root);
 err:
	return(NULL);
}

struct externfs_mount_ops humfs_mount_ops = {
	.init_file		= humfs_init_file,
	.mount			= mount_fs,
};

static int __init init_humfs(void)
{
	return(register_externfs("humfs", &humfs_mount_ops));
}

static void __exit exit_humfs(void)
{
	unregister_externfs("humfs");
}

__initcall(init_humfs);
__exitcall(exit_humfs);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
