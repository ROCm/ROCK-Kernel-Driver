#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/security.h>
#include <asm/uaccess.h>

/* selinuxfs pseudo filesystem for exporting the security policy API.
   Based on the proc code and the fs/nfsd/nfsctl.c code. */

#include "flask.h"
#include "avc.h"
#include "avc_ss.h"
#include "security.h"
#include "objsec.h"

/* Check whether a task is allowed to use a security operation. */
int task_has_security(struct task_struct *tsk,
		      u32 perms)
{
	struct task_security_struct *tsec;

	tsec = tsk->security;

	return avc_has_perm(tsec->sid, SECINITSID_SECURITY,
			    SECCLASS_SECURITY, perms, NULL, NULL);
}

enum sel_inos {
	SEL_ROOT_INO = 2,
	SEL_LOAD,	/* load policy */
	SEL_ENFORCE,	/* get or set enforcing status */
	SEL_CONTEXT,	/* validate context */
	SEL_ACCESS,	/* compute access decision */
	SEL_CREATE,	/* compute create labeling decision */
	SEL_RELABEL,	/* compute relabeling decision */
	SEL_USER,	/* compute reachable user contexts */
	SEL_POLICYVERS	/* return policy version for this kernel */
};

static ssize_t sel_read_enforce(struct file *filp, char *buf,
				size_t count, loff_t *ppos)
{
	char *page;
	ssize_t length;
	ssize_t end;

	if (count < 0 || count > PAGE_SIZE)
		return -EINVAL;
	if (!(page = (char*)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	memset(page, 0, PAGE_SIZE);

	length = snprintf(page, PAGE_SIZE, "%d", selinux_enforcing);
	if (length < 0) {
		free_page((unsigned long)page);
		return length;
	}

	if (*ppos >= length) {
		free_page((unsigned long)page);
		return 0;
	}
	if (count + *ppos > length)
		count = length - *ppos;
	end = count + *ppos;
	if (copy_to_user(buf, (char *) page + *ppos, count)) {
		count = -EFAULT;
		goto out;
	}
	*ppos = end;
out:
	free_page((unsigned long)page);
	return count;
}

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
static ssize_t sel_write_enforce(struct file * file, const char * buf,
				 size_t count, loff_t *ppos)

{
	char *page;
	ssize_t length;
	int new_value;

	if (count < 0 || count >= PAGE_SIZE)
		return -ENOMEM;
	if (*ppos != 0) {
		/* No partial writes. */
		return -EINVAL;
	}
	page = (char*)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	memset(page, 0, PAGE_SIZE);
	length = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;

	length = -EINVAL;
	if (sscanf(page, "%d", &new_value) != 1)
		goto out;

	if (new_value != selinux_enforcing) {
		length = task_has_security(current, SECURITY__SETENFORCE);
		if (length)
			goto out;
		selinux_enforcing = new_value;
		if (selinux_enforcing)
			avc_ss_reset(0);
	}
	length = count;
out:
	free_page((unsigned long) page);
	return length;
}
#else
#define sel_write_enforce NULL
#endif

static struct file_operations sel_enforce_ops = {
	.read		= sel_read_enforce,
	.write		= sel_write_enforce,
};

static ssize_t sel_read_policyvers(struct file *filp, char *buf,
                                   size_t count, loff_t *ppos)
{
	char *page;
	ssize_t length;
	ssize_t end;

	if (count < 0 || count > PAGE_SIZE)
		return -EINVAL;
	if (!(page = (char*)__get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	memset(page, 0, PAGE_SIZE);

	length = snprintf(page, PAGE_SIZE, "%u", POLICYDB_VERSION);
	if (length < 0) {
		free_page((unsigned long)page);
		return length;
	}

	if (*ppos >= length) {
		free_page((unsigned long)page);
		return 0;
	}
	if (count + *ppos > length)
		count = length - *ppos;
	end = count + *ppos;
	if (copy_to_user(buf, (char *) page + *ppos, count)) {
		count = -EFAULT;
		goto out;
	}
	*ppos = end;
out:
	free_page((unsigned long)page);
	return count;
}

static struct file_operations sel_policyvers_ops = {
	.read		= sel_read_policyvers,
};

static ssize_t sel_write_load(struct file * file, const char * buf,
			      size_t count, loff_t *ppos)

{
	ssize_t length;
	void *data;

	length = task_has_security(current, SECURITY__LOAD_POLICY);
	if (length)
		return length;

	if (*ppos != 0) {
		/* No partial writes. */
		return -EINVAL;
	}

	if ((count < 0) || (count > 64 * 1024 * 1024) || (data = vmalloc(count)) == NULL)
		return -ENOMEM;

	length = -EFAULT;
	if (copy_from_user(data, buf, count) != 0)
		goto out;

	length = security_load_policy(data, count);
	if (length)
		goto out;

	length = count;
out:
	vfree(data);
	return length;
}

static struct file_operations sel_load_ops = {
	.write		= sel_write_load,
};


static ssize_t sel_write_context(struct file * file, const char * buf,
				 size_t count, loff_t *ppos)

{
	char *page;
	u32 sid;
	ssize_t length;

	length = task_has_security(current, SECURITY__CHECK_CONTEXT);
	if (length)
		return length;

	if (count < 0 || count >= PAGE_SIZE)
		return -ENOMEM;
	if (*ppos != 0) {
		/* No partial writes. */
		return -EINVAL;
	}
	page = (char*)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	memset(page, 0, PAGE_SIZE);
	length = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;

	length = security_context_to_sid(page, count, &sid);
	if (length < 0)
		goto out;

	length = count;
out:
	free_page((unsigned long) page);
	return length;
}

static struct file_operations sel_context_ops = {
	.write		= sel_write_context,
};


/*
 * Remaining nodes use transaction based IO methods like nfsd/nfsctl.c
 */
static ssize_t sel_write_access(struct file * file, char *buf, size_t size);
static ssize_t sel_write_create(struct file * file, char *buf, size_t size);
static ssize_t sel_write_relabel(struct file * file, char *buf, size_t size);
static ssize_t sel_write_user(struct file * file, char *buf, size_t size);

static ssize_t (*write_op[])(struct file *, char *, size_t) = {
	[SEL_ACCESS] = sel_write_access,
	[SEL_CREATE] = sel_write_create,
	[SEL_RELABEL] = sel_write_relabel,
	[SEL_USER] = sel_write_user,
};

/* an argresp is stored in an allocated page and holds the
 * size of the argument or response, along with its content
 */
struct argresp {
	ssize_t size;
	char data[0];
};

#define PAYLOAD_SIZE (PAGE_SIZE - sizeof(struct argresp))

/*
 * transaction based IO methods.
 * The file expects a single write which triggers the transaction, and then
 * possibly a read which collects the result - which is stored in a
 * file-local buffer.
 */
static ssize_t TA_write(struct file *file, const char *buf, size_t size, loff_t *pos)
{
	ino_t ino =  file->f_dentry->d_inode->i_ino;
	struct argresp *ar;
	ssize_t rv = 0;

	if (ino >= sizeof(write_op)/sizeof(write_op[0]) || !write_op[ino])
		return -EINVAL;
	if (file->private_data)
		return -EINVAL; /* only one write allowed per open */
	if (size > PAYLOAD_SIZE - 1) /* allow one byte for null terminator */
		return -EFBIG;

	ar = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ar)
		return -ENOMEM;
	memset(ar, 0, PAGE_SIZE); /* clear buffer, particularly last byte */
	ar->size = 0;
	down(&file->f_dentry->d_inode->i_sem);
	if (file->private_data)
		rv = -EINVAL;
	else
		file->private_data = ar;
	up(&file->f_dentry->d_inode->i_sem);
	if (rv) {
		kfree(ar);
		return rv;
	}
	if (copy_from_user(ar->data, buf, size))
		return -EFAULT;

	rv =  write_op[ino](file, ar->data, size);
	if (rv>0) {
		ar->size = rv;
		rv = size;
	}
	return rv;
}

static ssize_t TA_read(struct file *file, char *buf, size_t size, loff_t *pos)
{
	struct argresp *ar;
	ssize_t rv = 0;

	if (file->private_data == NULL)
		rv = TA_write(file, buf, 0, pos);
	if (rv < 0)
		return rv;

	ar = file->private_data;
	if (!ar)
		return 0;
	if (*pos >= ar->size)
		return 0;
	if (*pos + size > ar->size)
		size = ar->size - *pos;
	if (copy_to_user(buf, ar->data + *pos, size))
		return -EFAULT;
	*pos += size;
	return size;
}

static int TA_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int TA_release(struct inode *inode, struct file *file)
{
	void *p = file->private_data;
	file->private_data = NULL;
	kfree(p);
	return 0;
}

static struct file_operations transaction_ops = {
	.write		= TA_write,
	.read		= TA_read,
	.open		= TA_open,
	.release	= TA_release,
};

/*
 * payload - write methods
 * If the method has a response, the response should be put in buf,
 * and the length returned.  Otherwise return 0 or and -error.
 */

static ssize_t sel_write_access(struct file * file, char *buf, size_t size)
{
	char *scon, *tcon;
	u32 ssid, tsid;
	u16 tclass;
	u32 req;
	struct av_decision avd;
	ssize_t length;

	length = task_has_security(current, SECURITY__COMPUTE_AV);
	if (length)
		return length;

	length = -ENOMEM;
	scon = kmalloc(size+1, GFP_KERNEL);
	if (!scon)
		return length;
	memset(scon, 0, size+1);

	tcon = kmalloc(size+1, GFP_KERNEL);
	if (!tcon)
		goto out;
	memset(tcon, 0, size+1);

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu %x", scon, tcon, &tclass, &req) != 4)
		goto out2;

	length = security_context_to_sid(scon, strlen(scon)+1, &ssid);
	if (length < 0)
		goto out2;
	length = security_context_to_sid(tcon, strlen(tcon)+1, &tsid);
	if (length < 0)
		goto out2;

	length = security_compute_av(ssid, tsid, tclass, req, &avd);
	if (length < 0)
		goto out2;

	length = snprintf(buf, PAYLOAD_SIZE, "%x %x %x %x %u",
			  avd.allowed, avd.decided,
			  avd.auditallow, avd.auditdeny,
			  avd.seqno);
out2:
	kfree(tcon);
out:
	kfree(scon);
	return length;
}

static ssize_t sel_write_create(struct file * file, char *buf, size_t size)
{
	char *scon, *tcon;
	u32 ssid, tsid, newsid;
	u16 tclass;
	ssize_t length;
	char *newcon;
	u32 len;

	length = task_has_security(current, SECURITY__COMPUTE_CREATE);
	if (length)
		return length;

	length = -ENOMEM;
	scon = kmalloc(size+1, GFP_KERNEL);
	if (!scon)
		return length;
	memset(scon, 0, size+1);

	tcon = kmalloc(size+1, GFP_KERNEL);
	if (!tcon)
		goto out;
	memset(tcon, 0, size+1);

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
		goto out2;

	length = security_context_to_sid(scon, strlen(scon)+1, &ssid);
	if (length < 0)
		goto out2;
	length = security_context_to_sid(tcon, strlen(tcon)+1, &tsid);
	if (length < 0)
		goto out2;

	length = security_transition_sid(ssid, tsid, tclass, &newsid);
	if (length < 0)
		goto out2;

	length = security_sid_to_context(newsid, &newcon, &len);
	if (length < 0)
		goto out2;

	if (len > PAYLOAD_SIZE) {
		printk(KERN_ERR "%s:  context size (%u) exceeds payload "
		       "max\n", __FUNCTION__, len);
		length = -ERANGE;
		goto out3;
	}

	memcpy(buf, newcon, len);
	length = len;
out3:
	kfree(newcon);
out2:
	kfree(tcon);
out:
	kfree(scon);
	return length;
}

static ssize_t sel_write_relabel(struct file * file, char *buf, size_t size)
{
	char *scon, *tcon;
	u32 ssid, tsid, newsid;
	u16 tclass;
	ssize_t length;
	char *newcon;
	u32 len;

	length = task_has_security(current, SECURITY__COMPUTE_RELABEL);
	if (length)
		return length;

	length = -ENOMEM;
	scon = kmalloc(size+1, GFP_KERNEL);
	if (!scon)
		return length;
	memset(scon, 0, size+1);

	tcon = kmalloc(size+1, GFP_KERNEL);
	if (!tcon)
		goto out;
	memset(tcon, 0, size+1);

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
		goto out2;

	length = security_context_to_sid(scon, strlen(scon)+1, &ssid);
	if (length < 0)
		goto out2;
	length = security_context_to_sid(tcon, strlen(tcon)+1, &tsid);
	if (length < 0)
		goto out2;

	length = security_change_sid(ssid, tsid, tclass, &newsid);
	if (length < 0)
		goto out2;

	length = security_sid_to_context(newsid, &newcon, &len);
	if (length < 0)
		goto out2;

	if (len > PAYLOAD_SIZE) {
		length = -ERANGE;
		goto out3;
	}

	memcpy(buf, newcon, len);
	length = len;
out3:
	kfree(newcon);
out2:
	kfree(tcon);
out:
	kfree(scon);
	return length;
}

static ssize_t sel_write_user(struct file * file, char *buf, size_t size)
{
	char *con, *user, *ptr;
	u32 sid, *sids;
	ssize_t length;
	char *newcon;
	int i, rc;
	u32 len, nsids;

	length = task_has_security(current, SECURITY__COMPUTE_USER);
	if (length)
		return length;

	length = -ENOMEM;
	con = kmalloc(size+1, GFP_KERNEL);
	if (!con)
		return length;
	memset(con, 0, size+1);

	user = kmalloc(size+1, GFP_KERNEL);
	if (!user)
		goto out;
	memset(user, 0, size+1);

	length = -EINVAL;
	if (sscanf(buf, "%s %s", con, user) != 2)
		goto out2;

	length = security_context_to_sid(con, strlen(con)+1, &sid);
	if (length < 0)
		goto out2;

	length = security_get_user_sids(sid, user, &sids, &nsids);
	if (length < 0)
		goto out2;

	length = sprintf(buf, "%u", nsids) + 1;
	ptr = buf + length;
	for (i = 0; i < nsids; i++) {
		rc = security_sid_to_context(sids[i], &newcon, &len);
		if (rc) {
			length = rc;
			goto out3;
		}
		if ((length + len) >= PAYLOAD_SIZE) {
			kfree(newcon);
			length = -ERANGE;
			goto out3;
		}
		memcpy(ptr, newcon, len);
		kfree(newcon);
		ptr += len;
		length += len;
	}
out3:
	kfree(sids);
out2:
	kfree(user);
out:
	kfree(con);
	return length;
}


static int sel_fill_super(struct super_block * sb, void * data, int silent)
{
	static struct tree_descr selinux_files[] = {
		[SEL_LOAD] = {"load", &sel_load_ops, S_IRUSR|S_IWUSR},
		[SEL_ENFORCE] = {"enforce", &sel_enforce_ops, S_IRUSR|S_IWUSR},
		[SEL_CONTEXT] = {"context", &sel_context_ops, S_IRUGO|S_IWUGO},
		[SEL_ACCESS] = {"access", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_CREATE] = {"create", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_RELABEL] = {"relabel", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_USER] = {"user", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_POLICYVERS] = {"policyvers", &sel_policyvers_ops, S_IRUGO},
		/* last one */ {""}
	};
	return simple_fill_super(sb, SELINUX_MAGIC, selinux_files);
}

static struct super_block *sel_get_sb(struct file_system_type *fs_type,
				      int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, sel_fill_super);
}

static struct file_system_type sel_fs_type = {
	.name		= "selinuxfs",
	.get_sb		= sel_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_sel_fs(void)
{
	return selinux_enabled ? register_filesystem(&sel_fs_type) : 0;
}

__initcall(init_sel_fs);
