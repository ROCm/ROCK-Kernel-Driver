/*
 * Immunix SubDomain LSM interface 
 * 
 * Copyright (C) 2002 WireX Communications, Inc
 *
 * Chris Wright <chris@wirex.com>
 * 
 * Copyright (C) 2003-2004 Immunix, Inc
 * 
 * Tony Jones <tony@immunix.com>
 */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

/* Needed for pipefs superblock PIPEFS_MAGIC */
#include <linux/pipe_fs_i.h>

/* Ugh, cut-n-paste from net/socket.c */
#define SOCKFS_MAGIC 0x534F434B

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/namei.h>
#include <linux/statfs.h>
#endif

#include <asm/mman.h>

#include "subdomain.h"
#include "inline.h"

// main SD lock, manipulated by SD_[RW]LOCK and SD_[RW]UNLOCK (see subdomain.h)
rwlock_t sd_lock = RW_LOCK_UNLOCKED;

/* Flag values, also controllable via subdomainfs/control */

/* Complain mode (used to be 'bitch' mode) */
int subdomain_complain=0;
/* Debug mode */
int subdomain_debug=0;
/* Audit mode */
int subdomain_audit=0;
/* OWLSM mode */
int subdomain_owlsm=0;

#ifdef MODULE
MODULE_PARM(subdomain_complain, "i");
MODULE_PARM_DESC(subdomain_complain, "Toggle SubDomain Complain Mode");
MODULE_PARM(subdomain_debug, "i");
MODULE_PARM_DESC(subdomain_debug, "Toggle SubDomain Debug Mode");
MODULE_PARM(subdomain_audit, "i");
MODULE_PARM_DESC(subdomain_audit, "Toggle SubDomain Audit Mode");
MODULE_PARM(subdomain_owlsm, "i");
MODULE_PARM_DESC(subdomain_owlsm, "Toggle SubDomain OWLSM Mode");
#else
static int __init sd_complainmode(char *str)
{
	get_option(&str, &subdomain_complain);
	return 1;
}
__setup("subdomain_complain=", sd_complainmode);
static int __init sd_debugmode(char *str)
{
	get_option(&str, &subdomain_debug);
	return 1;
}
__setup("subdomain_debug=", sd_debugmode);
static int __init sd_auditmode(char *str)
{
	get_option(&str, &subdomain_audit);
	return 1;
}
__setup("subdomain_audit=", sd_auditmode);
static int __init sd_owlsmmode(char *str)
{
	get_option(&str, &subdomain_owlsm);
	return 1;
}
__setup("subdomain_owlsm=", sd_owlsmmode);
#endif

static int subdomain_ptrace (struct task_struct *parent, struct task_struct *child)
{
	/* XXX This only protects TRACEME and ATTACH.  We used to guard
	 * all of sys_ptrace(2)...
	 */
	int error;
	struct subdomain *sd;

	error = cap_ptrace(parent, child);

	SD_RLOCK;

       	sd = SD_SUBDOMAIN(current->security);

	if (!error && __sd_is_confined(sd)){
		SD_WARN("REJECTING access to syscall 'ptrace' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK; 

	return error;
}

static int subdomain_capget (struct task_struct *target, kernel_cap_t * effective,
			 kernel_cap_t * inheritable, kernel_cap_t * permitted)
{
	return cap_capget(target, effective, inheritable, permitted);
}

static int subdomain_capset_check (struct task_struct *target,
			       kernel_cap_t * effective,
			       kernel_cap_t * inheritable,
			       kernel_cap_t * permitted)
{
	return cap_capset_check(target, effective, inheritable, permitted);
}

static void subdomain_capset_set (struct task_struct *target,
			      kernel_cap_t * effective,
			      kernel_cap_t * inheritable,
			      kernel_cap_t * permitted)
{
	cap_capset_set(target, effective, inheritable, permitted);
	return;
}

static int subdomain_capable (struct task_struct *tsk, int cap)
{
	int error;

	/* cap_capable returns 0 on success, else -EPERM */
	error = cap_capable(tsk, cap);

	if (error == 0){
		struct subdomain *sd, sdcopy;
		
		SD_RLOCK;
       		sd = __get_sdcopy(&sdcopy, tsk);
		SD_RUNLOCK;

		error = sd_capability(cap, sd);

		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_sysctl (ctl_table * table, int op)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

       	sd = SD_SUBDOMAIN(current->security);

	if ((op & 002) && __sd_is_confined(sd) && !capable(CAP_SYS_ADMIN)){
		SD_WARN("REJECTING access to syscall 'sysctl (write)' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK; 

	return error;
}

static int subdomain_syslog (int type)
{
	return cap_syslog(type);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,8)
static int subdomain_netlink_send (struct sock *sk, struct sk_buff *skb)
{
	return cap_netlink_send(sk, skb);
}
#else
static int subdomain_netlink_send (struct sk_buff *skb)
{
	return cap_netlink_send(skb);
}
#endif

static int subdomain_netlink_recv (struct sk_buff *skb)
{
	return cap_netlink_recv(skb);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,6)
static void subdomain_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{
	cap_bprm_apply_creds(bprm, unsafe);
	return;
}
#else
static void subdomain_bprm_compute_creds (struct linux_binprm *bprm)
{
	cap_bprm_compute_creds(bprm);
	return;
}
#endif

static int subdomain_bprm_set_security (struct linux_binprm *bprm)
{
	/* handle capability bits with setuid, etc */
	cap_bprm_set_security(bprm);
	/* already set based on script name */
	if (bprm->sh_bang)
		return 0;
	return sd_register(bprm->file);
}

static int subdomain_sb_mount (char *dev_name, struct nameidata *nd, char *type,
			unsigned long flags, void *data)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

       	sd = SD_SUBDOMAIN(current->security);

	if (__sd_is_confined(sd)){
		SD_WARN("REJECTING access to syscall 'mount' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK; 

	return error;
}

static int subdomain_umount (struct vfsmount *mnt, int flags)
{
	int error = 0;
	struct subdomain *sd;

	SD_RLOCK;

       	sd = SD_SUBDOMAIN(current->security);

	if (__sd_is_confined(sd)){
		SD_WARN("REJECTING access to syscall 'umount' (%s(%d) profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}

	SD_RUNLOCK; 

	return error;
}

static int subdomain_inode_create (struct inode *inode, struct dentry *dentry,
			       int mask)
{
	struct subdomain sdcopy,
			 *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	/* At a minimum, need write perm to create */
	error = sd_perm_dentry(dentry, sd, MAY_WRITE);

	put_sdcopy(sd);

	return error;
}

/*
 * Don't allow users to create hard links to files they don't own,
 * unless they have CAP_FOWNER. 
 *      
 * The last two checks are here as a workaround for atd(8), to be
 * removed one day. 
 */
static __INLINE__ int do_owlsm_link(struct dentry *old_dentry, struct inode *inode,
                                struct dentry *new_dentry)
{   
	struct inode* i = old_dentry->d_inode;

	if (current->fsuid != i->i_uid && !capable(CAP_FOWNER) &&
	    current->uid != i->i_uid && current->uid) {
		struct sd_path_data data;
		char *name;

		sd_path_begin(old_dentry, &data);

		name = sd_path_getname(&data);
		if (name){
			SD_WARN("REJECTING hard link to %s inode# %lu (owner %d.%d) by uid %d euid %d (%s(%d)) [owlsm]\n",
				name,
				old_dentry->d_inode->i_ino,
				i->i_uid, i->i_gid,
				current->uid, current->euid,
				current->comm, current->pid);

			sd_put_name(name);
			
			do {
				name = sd_path_getname(&data);
				if (name){
					SD_WARN("Inode# %lu is also reachable via path %s [owlsm]\n",
						old_dentry->d_inode->i_ino,
						name);

					sd_put_name(name);
				}
			}while (name);
		}

		if (sd_path_end(&data) != 0){
			SD_ERROR("%s: An error occured while translating dentry %p inode# %lu to a pathname. Error %d\n",
				__FUNCTION__,
				old_dentry,
				old_dentry->d_inode->i_ino,
				data.errno);
		}

		return -EPERM;
	}
	return 0;

}

static int subdomain_inode_link (struct dentry *old_dentry, struct inode *inode,
			     struct dentry *new_dentry)
{
	int error = 0;

	if (subdomain_owlsm){
		error = do_owlsm_link(old_dentry, inode, new_dentry);
	}

	if (error == 0){
		struct subdomain sdcopy,
		       *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_link(new_dentry, old_dentry, sd);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_unlink (struct inode *inode, struct dentry *dentry)
{
	struct subdomain sdcopy,
			 *sd;
	int error;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(dentry, sd, MAY_WRITE|KERN_COD_MAY_LINK);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_symlink (struct inode *inode, struct dentry *dentry,
				    const char *name)
{
	struct subdomain sdcopy,
			 *sd;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	error=sd_symlink(dentry, name, sd);

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_mknod (struct inode *inode, struct dentry *dentry,
			      int mode, dev_t dev)
{
	struct subdomain sdcopy,
			 *sd;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	if (__sd_is_confined(sd) && (S_ISCHR(mode) || S_ISBLK(mode))){
		SD_WARN("REJECTING access to syscall 'mknod' type:%c (%s(%d) profile %s active %s)\n",
			S_ISCHR(mode) ? 'c' : 'b',
			current->comm, current->pid,
			sd->profile->name, sd->active->name);
		error = -EPERM;
	}else if (S_ISFIFO(mode) || S_ISSOCK(mode)){
		error = sd_perm_dentry(dentry, sd, MAY_WRITE);
	}

	put_sdcopy(sd);

	return error;
}

static int subdomain_inode_rename (struct inode *old_inode,
			       struct dentry *old_dentry,
			       struct inode *new_inode,
			       struct dentry *new_dentry)
{
	struct subdomain sdcopy,
			 *sd;
	int error = 0;

	sd = get_sdcopy(&sdcopy);

	error = sd_perm_dentry(old_dentry, sd, 
			MAY_READ|MAY_WRITE|KERN_COD_MAY_LINK);

	if (!error){
		error = sd_perm_dentry(new_dentry, sd, MAY_WRITE);
	}
	
	put_sdcopy(sd);

	return error;
}

/*
 * Don't follow links that we don't own in +t
 * directories, unless the link is owned by the
 * owner of the directory.
 */
static __INLINE__ int do_owlsm_follow_link(struct dentry *dentry,
				   struct nameidata *nameidata)
{
	struct inode *inode = dentry->d_inode;
	struct inode *parent = dentry->d_parent->d_inode;
	if (S_ISLNK(inode->i_mode) &&
		(parent->i_mode & S_ISVTX) &&
		inode->i_uid != parent->i_uid &&
		current->fsuid != inode->i_uid) {
			char *name=NULL;

			if (nameidata){
				name=__sd_get_name(dentry, nameidata->mnt);
			}
					
			SD_WARN("REJECTING follow of symlink %s (owner %d.%d) by uid %d euid %d (%s(%d)) [owlsm]\n",
				name ? name : "",
				inode->i_uid, inode->i_gid,
				current->uid, current->euid,
				current->comm, current->pid);

			if (name){
				sd_put_name(name);
			}
				
			return -EPERM;
	}
	return 0;
}

static int subdomain_inode_follow_link (struct dentry *dentry, 
					struct nameidata *nameidata)
{       
	int error = 0;

	if (subdomain_owlsm){
		error = do_owlsm_follow_link(dentry, nameidata);
	}
		
	return error;
}

static int subdomain_inode_permission (struct inode *inode, int mask,
					struct nameidata *nd)
{
	int error = 0;

	/* Do not perform check on pipes or sockets
	 * Same as subdomain_file_permission
	 */
	if (inode->i_sb->s_magic != PIPEFS_MAGIC &&
            inode->i_sb->s_magic != SOCKFS_MAGIC) {
		struct subdomain sdcopy, 
			 	 *sd;

		sd = get_sdcopy(&sdcopy);
		error = sd_perm(inode, sd, nd, mask);
		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_inode_setattr (struct dentry *dentry, struct iattr *iattr)
{
struct subdomain sdcopy,
		 *sd;
int error = 0;

	if (dentry->d_inode->i_sb->s_magic != PIPEFS_MAGIC &&
	    dentry->d_inode->i_sb->s_magic != SOCKFS_MAGIC) {

		sd = get_sdcopy(&sdcopy);

		/* 
	 	 * Mediate any attempt to change attributes of a file 
		 * (chmod, chown, chgrp, etc)
	 	 */
		error = sd_attr(dentry, sd, iattr);

		put_sdcopy(sd);
	}

	return error;
}

static int subdomain_file_permission (struct file *file, int mask)
{
	struct subdomain sdcopy, *sd;
	struct sdprofile *f_profile;
	int error = 0;

#if defined (PRINTK_TEMPFIX) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10) &&\
            (defined(CONFIG_SMP) || defined(CONFIG_PREEMPT))
	/* ugly hack.  This is slightly racey, but lets us not have
	 * grab a spin lock just to test if there is data.  The
         * worst thing that can happen is we call dump_sdprintk,
	 * and it doesn't have any data to print, which is ok
	 */
	if (unlikely(sd_log_buf_has_data))
		dump_sdprintk();
#endif

	sd = get_sdcopy(&sdcopy);

	f_profile = SD_PROFILE(file->f_security);

	if (__sd_is_confined(sd) && f_profile && f_profile != sd->active) {
		char *name;
		if (file->f_dentry->d_inode->i_sb->s_magic != PIPEFS_MAGIC &&
		    file->f_dentry->d_inode->i_sb->s_magic != SOCKFS_MAGIC) {
			name = __sd_get_name(file->f_dentry, file->f_vfsmnt);
			error = -ENOMEM;
			if (name) {
				/* subdomain overloads permission bits for
				 * internal use, we don't want to expose those
				 * to possible external access,
			  	 * We allow only exec, write or read 
			 	 */
				error = sd_file_perm(name , sd, 
					mask & (MAY_EXEC|MAY_WRITE|MAY_READ), 
					TRUE);
				sd_put_name(name);
			}
		} else
			error = 0;
	}

	put_sdcopy(sd);

	return error;
}

static int subdomain_file_alloc_security (struct file *file)
{
struct subdomain sdcopy,
		 *sd;

	sd = get_sdcopy(&sdcopy);

	if (__sd_is_confined(sd)){
		file->f_security = get_sdprofile(sd->active);
	}

	put_sdcopy(sd);

	return 0;
}

static void subdomain_file_free_security (struct file *file)
{
	struct sdprofile *p = SD_PROFILE(file->f_security);
	put_sdprofile(p);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12)
static int subdomain_file_mmap (struct file *file, unsigned long reqprot,
				unsigned long prot, unsigned long flags)
#else
static int subdomain_file_mmap (struct file *file, unsigned long prot,
			    unsigned long flags)
#endif
{
int error = 0, 
    mask = 0;

struct subdomain sdcopy,
		 *sd;

struct sdprofile *f_profile;

	sd = get_sdcopy(&sdcopy);

	f_profile = file ? SD_PROFILE(file->f_security) : NULL;

	if (prot & PROT_READ)
		mask |= MAY_READ;
	if (prot & PROT_WRITE)
		mask |= MAY_WRITE;
	if (prot & PROT_EXEC)
		mask |= MAY_EXEC;

	SD_DEBUG("%s: 0x%x\n" , __FUNCTION__, mask);

	/* Don't check if no subdomain's, profiles haven't changed, or
	 * mapping in the executable
	 */
	if (file && __sd_sub_defined(sd) && 
    	    f_profile != sd->active &&
	    !(flags & MAP_EXECUTABLE)) {
		char *name = __sd_get_name(file->f_dentry, file->f_vfsmnt);
		SD_DEBUG("%s: name %s 0x%lx, 0x%lx\n", 
			__FUNCTION__,
			name ? name : "NULL", 
			prot, flags);
		error = -ENOMEM;
		if (name) {
			error = sd_file_perm(name, sd, 
				mask,
				TRUE);

			sd_put_name(name);
		}
	}

	put_sdcopy(sd);

	return error;
}

static int subdomain_task_alloc_security (struct task_struct *p)
{
	return sd_fork(p);
}

static void subdomain_task_free_security (struct task_struct *p)
{
	sd_release(p);
}

static int subdomain_task_post_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return cap_task_post_setuid(id0, id1, id2, flags);
}

static void subdomain_task_reparent_to_init (struct task_struct *p)
{
	cap_task_reparent_to_init(p);
	/* shouldn't be necessary anymore */
	// p->euid = p->fsuid = 0;
	return;
}

#ifdef SUBDOMAIN_PROCATTR
static int subdomain_getprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	int error = -EACCES; /* default to a perm denied */
	struct subdomain sdcopy,
			 *sd;
	char *str=value;
	size_t len;

	if (strcmp(name, "current") != 0){
		/* 
	 	 * Subdomain only supports the "current" process attribute
		 */
		error = -EINVAL;
		goto out;
	}

	if (!size){
		error = -ERANGE;
		goto out;
	}

	/* must be task querying itself or admin */
	if (current != p && !capable(CAP_SYS_ADMIN)){
		error = -EPERM;
		goto out;
	}

	SD_RLOCK;

       	sd = __get_sdcopy(&sdcopy, p);

	SD_RUNLOCK;

	if (__sd_is_confined(sd)){
		size_t lena, lenm, lenp=0;
		const char *enforce_str = " (enforce)";
		const char *complain_str = " (complain)";
		const char *mode_str = SUBDOMAIN_COMPLAIN(sd) ? complain_str : enforce_str;

		lenm=strlen(mode_str);

		lena = strlen(sd->active->name);

		len = lena;
		if (sd->active != sd->profile){
			lenp = strlen(sd->profile->name);
			len += (lenp + 1);	/* +1 for ^ */
		}
		/* DONT null terminate strings we output via proc */
		len += (lenm + 1); /* for \n */

		if (len <= size){
			if (lenp){
				memcpy(str, sd->profile->name, lenp);
				str+=lenp;
				*str++='^';
			}

			memcpy(str, sd->active->name, lena);
			str+=lena;
			memcpy(str, mode_str, lenm);
			str+=lenm;
			*str++='\n';
			error = len;
		}else{
			error = -ERANGE;
		}
	}else{
		const char *unconstrained_str = SD_UNCONSTRAINED "\n";
		len = strlen(unconstrained_str);

		/* DONT null terminate strings we output via proc */
		if (len <= size){
			memcpy(str, unconstrained_str, len);
			error = len;
		}else{
			error = -ERANGE;
		}
	}
			
	put_sdcopy(sd);

out:
	return error;
}

static int subdomain_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
const char *cmd_changehat = "changehat ",
	   *cmd_setprofile = "setprofile ";

	int error = -EACCES; /* default to a perm denied */
	char *cmd = (char *)value;
	
	/* only support messages to current */
	if (strcmp(name, "current") != 0){
		error = -EINVAL;
		goto out;
	}

	if (!size){
		error = -ERANGE;
		goto out;
	}

	/* CHANGE HAT */
	if (size > strlen(cmd_changehat) &&
	    strncmp(cmd, cmd_changehat, strlen(cmd_changehat)) == 0){
		char *hatinfo = cmd + strlen(cmd_changehat);
		size_t infosize = size - strlen(cmd_changehat);

		/* Only the current process may change it's hat */
		if (current != p){
			SD_WARN("%s: Attempt by foreign task %s(%d) [user %d] to changehat of task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
			goto out;
		}

		error = sd_setprocattr_changehat(hatinfo, infosize);
		if (error == 0){
			/* success, 
			 * set return to #bytes in orig request
			 */
			error = size;
		}

	/* SET NEW PROFILE */
	}else if (size > strlen(cmd_setprofile) &&
		  strncmp(cmd, cmd_setprofile, strlen(cmd_setprofile)) == 0){
		int confined;

		/* only an unconfined process with admin capabilities
		 * may change the profile of another task
		 */

		if (!capable(CAP_SYS_ADMIN)){
			SD_WARN("%s: Unprivileged attempt by task %s(%d) [user %d] to assign new profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);
			error = -EACCES;
			goto out;
		}
		
		SD_RLOCK;
		confined = sd_is_confined();
		SD_RUNLOCK;

		if (!confined){
			char *profile = cmd + strlen(cmd_setprofile);
			size_t profilesize = size - strlen(cmd_setprofile);

			error = sd_setprocattr_setprofile(p, profile, profilesize);
			if (error == 0){
				/* success, 
			 	 * set return to #bytes in orig request
			 	 */
				error = size;
			}
		}else{
			SD_WARN("%s: Attempt by confined task %s(%d) [user %d] to assign new profile to task %s(%d)\n",
				__FUNCTION__,
				current->comm,
				current->pid,
				current->uid,
				p->comm,
				p->pid);

			error = -EACCES;
		}
	}else{
		/* unknown operation */
		SD_WARN("%s: Unknown setprocattr command by task %s(%d) [user %d] for task %s(%d)\n",
			__FUNCTION__,
			current->comm,
			current->pid,
			current->uid,
			p->comm,
			p->pid);

		error = -EINVAL;
	}

out:
	return error;
}
#endif // SUBDOMAIN_PROCATTR

struct security_operations subdomain_ops = {
	.ptrace =			subdomain_ptrace,
	.capget =			subdomain_capget,
	.capset_check =			subdomain_capset_check,
	.capset_set =			subdomain_capset_set,
	.sysctl =			subdomain_sysctl,
	.capable =			subdomain_capable,
	.syslog =			subdomain_syslog,
	
	.netlink_send =			subdomain_netlink_send,
	.netlink_recv =			subdomain_netlink_recv,
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,6)
	.bprm_apply_creds =		subdomain_bprm_apply_creds,
#else
	.bprm_compute_creds =		subdomain_bprm_compute_creds,
#endif
	.bprm_set_security =		subdomain_bprm_set_security,

	.sb_mount =			subdomain_sb_mount,
	.sb_umount =			subdomain_umount,
	
	.inode_create =			subdomain_inode_create,
	.inode_link =			subdomain_inode_link,
	.inode_unlink =			subdomain_inode_unlink,
	.inode_symlink =		subdomain_inode_symlink,
	.inode_mknod =			subdomain_inode_mknod,
	.inode_rename =			subdomain_inode_rename,
	.inode_follow_link = 		subdomain_inode_follow_link,
	.inode_permission =		subdomain_inode_permission,
	.inode_setattr =		subdomain_inode_setattr,

	.file_permission =		subdomain_file_permission,
	.file_alloc_security =		subdomain_file_alloc_security,
	.file_free_security =		subdomain_file_free_security,
	.file_mmap =			subdomain_file_mmap,

	.task_alloc_security =		subdomain_task_alloc_security,
	.task_free_security =		subdomain_task_free_security,
	.task_post_setuid =		subdomain_task_post_setuid,
	.task_reparent_to_init =	subdomain_task_reparent_to_init,

#ifdef SUBDOMAIN_PROCATTR
	.getprocattr =			subdomain_getprocattr,
	.setprocattr =			subdomain_setprocattr,
#endif
};

#ifdef SUBDOMAIN_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int sd_statfs(struct super_block *sb, struct kstatfs *buf)
#else
static int sd_statfs(struct super_block *sb, struct statfs *buf)
#endif
{
	buf->f_type = SD_ID_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = 255;
	return 0;
}

static struct super_operations sdfs_ops = {
	.statfs =	sd_statfs,
};

static int sd_fill_super(struct super_block *sb, void *data, int silent)
{
	struct dentry *root;
	struct inode *inode;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SD_ID_MAGIC;
	sb->s_op = &sdfs_ops;
	inode = sd_new_inode(sb, S_IFDIR | 0751, 2);

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM ;
	}
	sb->s_root = root;
	if (sd_fill_root(root)) {
	//	d_genocide(root);
		dput(root);
		return -ENOMEM;
	}
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static struct super_block *sd_get_sb(struct file_system_type *fs_type,
                                     int flags, const char *dev_name, 
				     void *data)
{
        return get_sb_single(fs_type, flags, data, sd_fill_super);
}
#else
struct super_block * sd_read_super(struct super_block *sb, void *data, int silent)      
{       
	struct dentry *root;
	struct inode *inode;
	
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SD_ID_MAGIC;
	sb->s_op = &sdfs_ops;
	inode = sd_new_inode(sb, S_IFDIR | 0755, 2);
	
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return NULL;
	}
	sb->s_root = root;
	if (sd_fill_root(root)) {
	//      d_genocide(root);
		dput(root);
		return NULL;
	}
	return sb;      
}
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0


static struct file_system_type sd_fs_type = {
	.name =		"subdomainfs",
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	.get_sb =	sd_get_sb,
	.kill_sb = 	kill_litter_super,
#else
	.read_super =   sd_read_super,
	.fs_flags =     FS_SINGLE|FS_LITTER,
#endif
	.owner =	THIS_MODULE,
};

#endif /* SUBDOMAIN_FS */

static int __init subdomain_init(void)
{
	int error=0;
	char *complainmsg = ": complainmode enabled";

	/*
	 * CREATE SUBDOMAINFS
	 */
#ifdef SUBDOMAIN_FS
 	if ((error = register_filesystem(&sd_fs_type))) {
 		SD_ERROR("Unable to activate SubDomain filesystem\n");
		goto registerfs_out;
 	}
#else
#error SUBDOMAIN_FS must be enabled
#endif

	

	/*
	 * CREATE NULL COMPLAIN PROFILE 
	 */
	null_complain_profile = alloc_sdprofile();
	if (!null_complain_profile) {
		SD_ERROR("Unable to allocate null-complain-profile\n");
		error = -ENOMEM;
		goto complain1_out;
	}
	null_complain_profile->name = kmalloc(strlen("null-complain-profile")+1, GFP_KERNEL);
	if (!null_complain_profile->name) {
		error = -ENOMEM;
		goto complain2_out;
	}
	strcpy(null_complain_profile->name, "null-complain-profile");
	get_sdprofile(null_complain_profile);
	null_complain_profile->flags.complain = 1;

	/*
	 * REGISTER SubDomain WITH LSM
	 */
	if ((error = register_security(&subdomain_ops))) {
		SD_WARN("Unable to load SubDomain\n");
		goto register_out;
	}
	SD_INFO("SubDomain (version %s) initialized%s\n", 
		subdomain_version(),
		subdomain_complain ? complainmsg : "");

	/* DONE */
	return error;

register_out:

complain2_out:
	free_sdprofile(null_complain_profile);
	null_complain_profile=NULL;

complain1_out:
#ifdef SUBDOMAIN_FS
	(void)unregister_filesystem(&sd_fs_type);
#endif

registerfs_out:
	return error;

}

static int subdomain_exit_removeall_iter(struct subdomain *sd, void *cookie)
{
	/* SD_WLOCK held here */

	if (__sd_is_confined(sd)){
		SD_DEBUG("%s: Dropping profiles %s(%d) profile %s(%p) active %s(%p)\n", 
			__FUNCTION__,
			sd->task->comm, sd->task->pid,
			sd->profile->name, sd->profile,
			sd->active->name, sd->active);
		put_sdprofile(sd->profile);
		put_sdprofile(sd->active);
		sd->profile = sd->active = NULL;
	}

	return 0;
}

static void __exit subdomain_exit(void)
{
	/* Remove profiles from the global profile list.
	 * This is just for tidyness as there is no way to reference this
	 * list once the SubDomain lsm hooks are detached (below)
	 */
	sd_profilelist_release();

	/* Remove profiles from active tasks
	 * If this is not done,  if module is reloaded after being removed,
	 * old profiles (still refcounted in memory) will become 'magically'
	 * reattached
	 */
	
	SD_WLOCK;
	sd_subdomainlist_iterate(subdomain_exit_removeall_iter, NULL);
	SD_WUNLOCK;

	/* Free up list of active subdomain */
	sd_subdomainlist_release();

	put_sdprofile(null_complain_profile);
#ifdef SUBDOMAIN_FS
	if (unregister_filesystem(&sd_fs_type))
		SD_WARN("Unable to properly deactivate SubDomain fs\n");
#endif

	if (unregister_security(&subdomain_ops))
		SD_WARN("Unable to properly unregister SubDomain\n");
	SD_INFO("SubDomain protection removed\n");
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
security_initcall(subdomain_init);
#else
module_init(subdomain_init);
#endif
module_exit(subdomain_exit);

MODULE_DESCRIPTION("SubDomain process confinement");
MODULE_AUTHOR("Immunix");
MODULE_LICENSE("GPL");
