/*
 * Linux Security plug
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/sem.h>
#include <linux/sysctl.h>
#include <linux/shm.h>
#include <linux/msg.h>

/*
 * Values used in the task_security_ops calls
 */
/* setuid or setgid, id0 == uid or gid */
#define LSM_SETID_ID	1

/* setreuid or setregid, id0 == real, id1 == eff */
#define LSM_SETID_RE	2

/* setresuid or setresgid, id0 == real, id1 == eff, uid2 == saved */
#define LSM_SETID_RES	4

/* setfsuid or setfsgid, id0 == fsuid or fsgid */
#define LSM_SETID_FS	8

/* forward declares to avoid warnings */
struct sk_buff;
struct net_device;
struct nfsctl_arg;
struct sched_param;
struct swap_info_struct;

/**
 * struct security_operations - main security structure
 *
 * Security hooks for program execution operations.
 *
 * @bprm_alloc_security:
 *	Allocate and attach a security structure to the @bprm->security field.
 *	The security field is initialized to NULL when the bprm structure is
 *	allocated.
 *	@bprm contains the linux_binprm structure to be modified.
 *	Return 0 if operation was successful.
 * @bprm_free_security:
 *	@bprm contains the linux_binprm structure to be modified.
 *	Deallocate and clear the @bprm->security field.
 * @bprm_compute_creds:
 *	Compute and set the security attributes of a process being transformed
 *	by an execve operation based on the old attributes (current->security)
 *	and the information saved in @bprm->security by the set_security hook.
 *	Since this hook function (and its caller) are void, this hook can not
 *	return an error.  However, it can leave the security attributes of the
 *	process unchanged if an access failure occurs at this point. It can
 *	also perform other state changes on the process (e.g.  closing open
 *	file descriptors to which access is no longer granted if the attributes
 *	were changed). 
 *	@bprm contains the linux_binprm structure.
 * @bprm_set_security:
 *	Save security information in the bprm->security field, typically based
 *	on information about the bprm->file, for later use by the compute_creds
 *	hook.  This hook may also optionally check permissions (e.g. for
 *	transitions between security domains).
 *	This hook may be called multiple times during a single execve, e.g. for
 *	interpreters.  The hook can tell whether it has already been called by
 *	checking to see if @bprm->security is non-NULL.  If so, then the hook
 *	may decide either to retain the security information saved earlier or
 *	to replace it.
 *	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 * @bprm_check_security:
 * 	This hook mediates the point when a search for a binary handler	will
 * 	begin.  It allows a check the @bprm->security value which is set in
 * 	the preceding set_security call.  The primary difference from
 * 	set_security is that the argv list and envp list are reliably
 * 	available in @bprm.  This hook may be called multiple times
 * 	during a single execve; and in each pass set_security is called
 * 	first.
 * 	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 *
 * Security hooks for filesystem operations.
 *
 * @sb_alloc_security:
 *	Allocate and attach a security structure to the sb->s_security field.
 *	The s_security field is initialized to NULL when the structure is
 *	allocated.
 *	@sb contains the super_block structure to be modified.
 *	Return 0 if operation was successful.
 * @sb_free_security:
 *	Deallocate and clear the sb->s_security field.
 *	@sb contains the super_block structure to be modified.
 * @sb_statfs:
 *	Check permission before obtaining filesystem statistics for the @sb
 *	filesystem.
 *	@sb contains the super_block structure for the filesystem.
 *	Return 0 if permission is granted.  
 * @sb_mount:
 *	Check permission before an object specified by @dev_name is mounted on
 *	the mount point named by @nd.  For an ordinary mount, @dev_name
 *	identifies a device if the file system type requires a device.  For a
 *	remount (@flags & MS_REMOUNT), @dev_name is irrelevant.  For a
 *	loopback/bind mount (@flags & MS_BIND), @dev_name identifies the
 *	pathname of the object being mounted.
 *	@dev_name contains the name for object being mounted.
 *	@nd contains the nameidata structure for mount point object.
 *	@type contains the filesystem type.
 *	@flags contains the mount flags.
 *	@data contains the filesystem-specific data.
 *	Return 0 if permission is granted.
 * @sb_check_sb:
 *	Check permission before the device with superblock @mnt->sb is mounted
 *	on the mount point named by @nd.
 *	@mnt contains the vfsmount for device being mounted.
 *	@nd contains the nameidata object for the mount point.
 *	Return 0 if permission is granted.
 * @sb_umount:
 *	Check permission before the @mnt file system is unmounted.
 *	@mnt contains the mounted file system.
 *	@flags contains the unmount flags, e.g. MNT_FORCE.
 *	Return 0 if permission is granted.
 * @sb_umount_close:
 *	Close any files in the @mnt mounted filesystem that are held open by
 *	the security module.  This hook is called during an umount operation
 *	prior to checking whether the filesystem is still busy.
 *	@mnt contains the mounted filesystem.
 * @sb_umount_busy:
 *	Handle a failed umount of the @mnt mounted filesystem, e.g.  re-opening
 *	any files that were closed by umount_close.  This hook is called during
 *	an umount operation if the umount fails after a call to the
 *	umount_close hook.
 *	@mnt contains the mounted filesystem.
 * @sb_post_remount:
 *	Update the security module's state when a filesystem is remounted.
 *	This hook is only called if the remount was successful.
 *	@mnt contains the mounted file system.
 *	@flags contains the new filesystem flags.
 *	@data contains the filesystem-specific data.
 * @sb_post_mountroot:
 *	Update the security module's state when the root filesystem is mounted.
 *	This hook is only called if the mount was successful.
 * @sb_post_addmount:
 *	Update the security module's state when a filesystem is mounted.
 *	This hook is called any time a mount is successfully grafetd to
 *	the tree.
 *	@mnt contains the mounted filesystem.
 *	@mountpoint_nd contains the nameidata structure for the mount point.
 * @sb_pivotroot:
 *	Check permission before pivoting the root filesystem.
 *	@old_nd contains the nameidata structure for the new location of the current root (put_old).
 *      @new_nd contains the nameidata structure for the new root (new_root).
 *	Return 0 if permission is granted.
 * @sb_post_pivotroot:
 *	Update module state after a successful pivot.
 *	@old_nd contains the nameidata structure for the old root.
 *      @new_nd contains the nameidata structure for the new root.
 *
 * Security hooks for inode operations.
 *
 * @inode_alloc_security:
 *	Allocate and attach a security structure to @inode->i_security.  The
 *	i_security field is initialized to NULL when the inode structure is
 *	allocated.
 *	@inode contains the inode structure.
 *	Return 0 if operation was successful.
 * @inode_free_security:
 *	@inode contains the inode structure.
 *	Deallocate the inode security structure and set @inode->i_security to
 *	NULL. 
 * @inode_create:
 *	Check permission to create a regular file.
 *	@dir contains inode structure of the parent of the new file.
 *	@dentry contains the dentry structure for the file to be created.
 *	@mode contains the file mode of the file to be created.
 *	Return 0 if permission is granted.
 * @inode_post_create:
 *	Set the security attributes on a newly created regular file.  This hook
 *	is called after a file has been successfully created.
 *	@dir contains the inode structure of the parent directory of the new file.
 *	@dentry contains the the dentry structure for the newly created file.
 *	@mode contains the file mode.
 * @inode_link:
 *	Check permission before creating a new hard link to a file.
 *	@old_dentry contains the dentry structure for an existing link to the file.
 *	@dir contains the inode structure of the parent directory of the new link.
 *	@new_dentry contains the dentry structure for the new link.
 *	Return 0 if permission is granted.
 * @inode_post_link:
 *	Set security attributes for a new hard link to a file.
 *	@old_dentry contains the dentry structure for the existing link.
 *	@dir contains the inode structure of the parent directory of the new file.
 *	@new_dentry contains the dentry structure for the new file link.
 * @inode_unlink:
 *	Check the permission to remove a hard link to a file. 
 *	@dir contains the inode structure of parent directory of the file.
 *	@dentry contains the dentry structure for file to be unlinked.
 *	Return 0 if permission is granted.
 * @inode_symlink:
 *	Check the permission to create a symbolic link to a file.
 *	@dir contains the inode structure of parent directory of the symbolic link.
 *	@dentry contains the dentry structure of the symbolic link.
 *	@old_name contains the pathname of file.
 *	Return 0 if permission is granted.
 * @inode_post_symlink:
 *	@dir contains the inode structure of the parent directory of the new link.
 *	@dentry contains the dentry structure of new symbolic link.
 *	@old_name contains the pathname of file.
 *	Set security attributes for a newly created symbolic link.  Note that
 *	@dentry->d_inode may be NULL, since the filesystem might not
 *	instantiate the dentry (e.g. NFS).
 * @inode_mkdir:
 *	Check permissions to create a new directory in the existing directory
 *	associated with inode strcture @dir. 
 *	@dir containst the inode structure of parent of the directory to be created.
 *	@dentry contains the dentry structure of new directory.
 *	@mode contains the mode of new directory.
 *	Return 0 if permission is granted.
 * @inode_post_mkdir:
 *	Set security attributes on a newly created directory.
 *	@dir contains the inode structure of parent of the directory to be created.
 *	@dentry contains the dentry structure of new directory.
 *	@mode contains the mode of new directory.
 * @inode_rmdir:
 *	Check the permission to remove a directory.
 *	@dir contains the inode structure of parent of the directory to be removed.
 *	@dentry contains the dentry structure of directory to be removed.
 *	Return 0 if permission is granted.
 * @inode_mknod:
 *	Check permissions when creating a special file (or a socket or a fifo
 *	file created via the mknod system call).  Note that if mknod operation
 *	is being done for a regular file, then the create hook will be called
 *	and not this hook.
 *	@dir contains the inode structure of parent of the new file.
 *	@dentry contains the dentry structure of the new file.
 *	@mode contains the mode of the new file.
 *	@dev contains the the device number.
 *	Return 0 if permission is granted.
 * @inode_post_mknod:
 *	Set security attributes on a newly created special file (or socket or
 *	fifo file created via the mknod system call).
 *	@dir contains the inode structure of parent of the new node.
 *	@dentry contains the dentry structure of the new node.
 *	@mode contains the mode of the new node.
 *	@dev contains the the device number.
 * @inode_rename:
 *	Check for permission to rename a file or directory.
 *	@old_dir contains the inode structure for parent of the old link.
 *	@old_dentry contains the dentry structure of the old link.
 *	@new_dir contains the inode structure for parent of the new link.
 *	@new_dentry contains the dentry structure of the new link.
 *	Return 0 if permission is granted.
 * @inode_post_rename:
 *	Set security attributes on a renamed file or directory.
 *	@old_dir contains the inode structure for parent of the old link.
 *	@old_dentry contains the dentry structure of the old link.
 *	@new_dir contains the inode structure for parent of the new link.
 *	@new_dentry contains the dentry structure of the new link.
 * @inode_readlink:
 *	Check the permission to read the symbolic link.
 *	@dentry contains the dentry structure for the file link.
 *	Return 0 if permission is granted.
 * @inode_follow_link:
 *	Check permission to follow a symbolic link when looking up a pathname.
 *	@dentry contains the dentry structure for the link.
 *	@nd contains the nameidata structure for the parent directory.
 *	Return 0 if permission is granted.
 * @inode_permission:
 *	Check permission before accessing an inode.  This hook is called by the
 *	existing Linux permission function, so a security module can use it to
 *	provide additional checking for existing Linux permission checks.
 *	Notice that this hook is called when a file is opened (as well as many
 *	other operations), whereas the file_security_ops permission hook is
 *	called when the actual read/write operations are performed.
 *	@inode contains the inode structure to check.
 *	@mask contains the permission mask.
 *	Return 0 if permission is granted.
 * @inode_permission_lite:
 * 	Check permission before accessing an inode.  This hook is
 * 	currently only called when checking MAY_EXEC access during
 * 	pathname resolution.  The dcache lock is held and thus modules
 * 	that could sleep or contend the lock should return -EAGAIN to
 * 	inform the kernel to drop the lock and try again calling the
 * 	full permission hook.
 * 	@inode contains the inode structure to check.
 * 	@mask contains the permission mask.
 * 	Return 0 if permission is granted.
 * @inode_setattr:
 *	Check permission before setting file attributes.  Note that the kernel
 *	call to notify_change is performed from several locations, whenever
 *	file attributes change (such as when a file is truncated, chown/chmod
 *	operations, transferring disk quotas, etc).
 *	@dentry contains the dentry structure for the file.
 *	@attr is the iattr structure containing the new file attributes.
 *	Return 0 if permission is granted.
 * @inode_getattr:
 *	Check permission before obtaining file attributes.
 *	@mnt is the vfsmount where the dentry was looked up
 *	@dentry contains the dentry structure for the file.
 *	Return 0 if permission is granted.
 * @inode_post_lookup:
 *	Set the security attributes for a file after it has been looked up.
 *	@inode contains the inode structure for parent directory.
 *	@d contains the dentry structure for the file.
 * @inode_delete:
 *	@inode contains the inode structure for deleted inode.
 *	This hook is called when a deleted inode is released (i.e. an inode
 *	with no hard links has its use count drop to zero).  A security module
 *	can use this hook to release any persistent label associated with the
 *	inode.
 * @inode_setxattr:
 * 	Check permission before setting the extended attributes
 * 	@value identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_getxattr:
 * 	Check permission before obtaining the extended attributes
 * 	identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_listxattr:
 * 	Check permission before obtaining the list of extended attribute 
 * 	names for @dentry.
 * 	Return 0 if permission is granted.
 * @inode_removexattr:
 * 	Check permission before removing the extended attribute
 * 	identified by @name for @dentry.
 * 	Return 0 if permission is granted.
 *
 * Security hooks for file operations
 *
 * @file_permission:
 *	Check file permissions before accessing an open file.  This hook is
 *	called by various operations that read or write files.  A security
 *	module can use this hook to perform additional checking on these
 *	operations, e.g.  to revalidate permissions on use to support privilege
 *	bracketing or policy changes.  Notice that this hook is used when the
 *	actual read/write operations are performed, whereas the
 *	inode_security_ops hook is called when a file is opened (as well as
 *	many other operations).
 *	Caveat:  Although this hook can be used to revalidate permissions for
 *	various system call operations that read or write files, it does not
 *	address the revalidation of permissions for memory-mapped files.
 *	Security modules must handle this separately if they need such
 *	revalidation.
 *	@file contains the file structure being accessed.
 *	@mask contains the requested permissions.
 *	Return 0 if permission is granted.
 * @file_alloc_security:
 *	Allocate and attach a security structure to the file->f_security field.
 *	The security field is initialized to NULL when the structure is first
 *	created.
 *	@file contains the file structure to secure.
 *	Return 0 if the hook is successful and permission is granted.
 * @file_free_security:
 *	Deallocate and free any security structures stored in file->f_security.
 *	@file contains the file structure being modified.
 * @file_llseek:
 *	Check permission before re-positioning the file offset in @file.
 *	@file contains the file structure being modified.
 *	Return 0 if permission is granted.
 * @file_ioctl:
 *	@file contains the file structure.
 *	@cmd contains the operation to perform.
 *	@arg contains the operational arguments.
 *	Check permission for an ioctl operation on @file.  Note that @arg can
 *	sometimes represents a user space pointer; in other cases, it may be a
 *	simple integer value.  When @arg represents a user space pointer, it
 *	should never be used by the security module.
 *	Return 0 if permission is granted.
 * @file_mmap :
 *	Check permissions for a mmap operation.  The @file may be NULL, e.g.
 *	if mapping anonymous memory.
 *	@file contains the file structure for file to map (may be NULL).
 *	@prot contains the requested permissions.
 *	@flags contains the operational flags.
 *	Return 0 if permission is granted.
 * @file_mprotect:
 *	Check permissions before changing memory access permissions.
 *	@vma contains the memory region to modify.
 *	@prot contains the requested permissions.
 *	Return 0 if permission is granted.
 * @file_lock:
 *	Check permission before performing file locking operations.
 *	Note: this hook mediates both flock and fcntl style locks.
 *	@file contains the file structure.
 *	@cmd contains the posix-translated lock operation to perform
 *	(e.g. F_RDLCK, F_WRLCK).
 *	@blocking indicates if the request is for a blocking lock.
 *	Return 0 if permission is granted.
 * @file_fcntl:
 *	Check permission before allowing the file operation specified by @cmd
 *	from being performed on the file @file.  Note that @arg can sometimes
 *	represents a user space pointer; in other cases, it may be a simple
 *	integer value.  When @arg represents a user space pointer, it should
 *	never be used by the security module.
 *	@file contains the file structure.
 *	@cmd contains the operation to be performed.
 *	@arg contains the operational arguments.
 *	Return 0 if permission is granted.
 * @file_set_fowner:
 *	Save owner security information (typically from current->security) in
 *	file->f_security for later use by the send_sigiotask hook.
 *	@file contains the file structure to update.
 *	Return 0 on success.
 * @file_send_sigiotask:
 *	Check permission for the file owner @fown to send SIGIO to the process
 *	@tsk.  Note that this hook is always called from interrupt.  Note that
 *	the fown_struct, @fown, is never outside the context of a struct file,
 *	so the file structure (and associated security information) can always
 *	be obtained:
 *		(struct file *)((long)fown - offsetof(struct file,f_owner));
 * 	@tsk contains the structure of task receiving signal.
 *	@fown contains the file owner information.
 *	@fd contains the file descriptor.
 *	@reason contains the operational flags.
 *	Return 0 if permission is granted.
 * @file_receive:
 *	This hook allows security modules to control the ability of a process
 *	to receive an open file descriptor via socket IPC.
 *	@file contains the file structure being received.
 *	Return 0 if permission is granted.
 *
 * Security hooks for task operations.
 *
 * @task_create:
 *	Check permission before creating a child process.  See the clone(2)
 *	manual page for definitions of the @clone_flags.
 *	@clone_flags contains the flags indicating what should be shared.
 *	Return 0 if permission is granted.
 * @task_alloc_security:
 *	@p contains the task_struct for child process.
 *	Allocate and attach a security structure to the p->security field. The
 *	security field is initialized to NULL when the task structure is
 *	allocated.
 *	Return 0 if operation was successful.
 * @task_free_security:
 *	@p contains the task_struct for process.
 *	Deallocate and clear the p->security field.
 * @task_setuid:
 *	Check permission before setting one or more of the user identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*uid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a uid.
 *	@id1 contains a uid.
 *	@id2 contains a uid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_post_setuid:
 *	Update the module's state after setting one or more of the user
 *	identity attributes of the current process.  The @flags parameter
 *	indicates which of the set*uid system calls invoked this hook.  If
 *	@flags is LSM_SETID_FS, then @old_ruid is the old fs uid and the other
 *	parameters are not used.
 *	@old_ruid contains the old real uid (or fs uid if LSM_SETID_FS).
 *	@old_euid contains the old effective uid (or -1 if LSM_SETID_FS).
 *	@old_suid contains the old saved uid (or -1 if LSM_SETID_FS).
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 on success.
 * @task_setgid:
 *	Check permission before setting one or more of the group identity
 *	attributes of the current process.  The @flags parameter indicates
 *	which of the set*gid system calls invoked this hook and how to
 *	interpret the @id0, @id1, and @id2 parameters.  See the LSM_SETID
 *	definitions at the beginning of this file for the @flags values and
 *	their meanings.
 *	@id0 contains a gid.
 *	@id1 contains a gid.
 *	@id2 contains a gid.
 *	@flags contains one of the LSM_SETID_* values.
 *	Return 0 if permission is granted.
 * @task_setpgid:
 *	Check permission before setting the process group identifier of the
 *	process @p to @pgid.
 *	@p contains the task_struct for process being modified.
 *	@pgid contains the new pgid.
 *	Return 0 if permission is granted.
 * @task_getpgid:
 *	Check permission before getting the process group identifier of the
 *	process @p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_getsid:
 *	Check permission before getting the session identifier of the process
 *	@p.
 *	@p contains the task_struct for the process.
 *	Return 0 if permission is granted.
 * @task_setgroups:
 *	Check permission before setting the supplementary group set of the
 *	current process to @grouplist.
 *	@gidsetsize contains the number of elements in @grouplist.
 *	@grouplist contains the array of gids.
 *	Return 0 if permission is granted.
 * @task_setnice:
 *	Check permission before setting the nice value of @p to @nice.
 *	@p contains the task_struct of process.
 *	@nice contains the new nice value.
 *	Return 0 if permission is granted.
 * @task_setrlimit:
 *	Check permission before setting the resource limits of the current
 *	process for @resource to @new_rlim.  The old resource limit values can
 *	be examined by dereferencing (current->rlim + resource).
 *	@resource contains the resource whose limit is being set.
 *	@new_rlim contains the new limits for @resource.
 *	Return 0 if permission is granted.
 * @task_setscheduler:
 *	Check permission before setting scheduling policy and/or parameters of
 *	process @p based on @policy and @lp.
 *	@p contains the task_struct for process.
 *	@policy contains the scheduling policy.
 *	@lp contains the scheduling parameters.
 *	Return 0 if permission is granted.
 * @task_getscheduler:
 *	Check permission before obtaining scheduling information for process
 *	@p.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_kill:
 *	Check permission before sending signal @sig to @p.  @info can be NULL,
 *	the constant 1, or a pointer to a siginfo structure.  If @info is 1 or
 *	SI_FROMKERNEL(info) is true, then the signal should be viewed as coming
 *	from the kernel and should typically be permitted.
 *	SIGIO signals are handled separately by the send_sigiotask hook in
 *	file_security_ops.
 *	@p contains the task_struct for process.
 *	@info contains the signal information.
 *	@sig contains the signal value.
 *	Return 0 if permission is granted.
 * @task_wait:
 *	Check permission before allowing a process to reap a child process @p
 *	and collect its status information.
 *	@p contains the task_struct for process.
 *	Return 0 if permission is granted.
 * @task_prctl:
 *	Check permission before performing a process control operation on the
 *	current process.
 *	@option contains the operation.
 *	@arg2 contains a argument.
 *	@arg3 contains a argument.
 *	@arg4 contains a argument.
 *	@arg5 contains a argument.
 *	Return 0 if permission is granted.
 * @task_kmod_set_label:
 *	Set the security attributes in current->security for the kernel module
 *	loader thread, so that it has the permissions needed to perform its
 *	function.
 * @task_reparent_to_init:
 * 	Set the security attributes in @p->security for a kernel thread that
 * 	is being reparented to the init task.
 *	@p contains the task_struct for the kernel thread.
 *
 * @ptrace:
 *	Check permission before allowing the @parent process to trace the
 *	@child process.
 *	Security modules may also want to perform a process tracing check
 *	during an execve in the set_security or compute_creds hooks of
 *	binprm_security_ops if the process is being traced and its security
 *	attributes would be changed by the execve.
 *	@parent contains the task_struct structure for parent process.
 *	@child contains the task_struct structure for child process.
 *	Return 0 if permission is granted.
 * @capget:
 *	Get the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  The hook may also perform permission checking to
 *	determine if the current process is allowed to see the capability sets
 *	of the @target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if the capability sets were successfully obtained.
 * @capset_check:
 *	Check permission before setting the @effective, @inheritable, and
 *	@permitted capability sets for the @target process.
 *	Caveat:  @target is also set to current if a set of processes is
 *	specified (i.e. all processes other than current and init or a
 *	particular process group).  Hence, the capset_set hook may need to
 *	revalidate permission to the actual target process.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 *	Return 0 if permission is granted.
 * @capset_set:
 *	Set the @effective, @inheritable, and @permitted capability sets for
 *	the @target process.  Since capset_check cannot always check permission
 *	to the real @target process, this hook may also perform permission
 *	checking to determine if the current process is allowed to set the
 *	capability sets of the @target process.  However, this hook has no way
 *	of returning an error due to the structure of the sys_capset code.
 *	@target contains the task_struct structure for target process.
 *	@effective contains the effective capability set.
 *	@inheritable contains the inheritable capability set.
 *	@permitted contains the permitted capability set.
 * @acct:
 *	Check permission before enabling or disabling process accounting.  If
 *	accounting is being enabled, then @file refers to the open file used to
 *	store accounting records.  If accounting is being disabled, then @file
 *	is NULL.
 *	@file contains the file structure for the accounting file (may be NULL).
 *	Return 0 if permission is granted.
 * @capable:
 *	Check whether the @tsk process has the @cap capability.
 *	@tsk contains the task_struct for the process.
 *	@cap contains the capability <include/linux/capability.h>.
 *	Return 0 if the capability is granted for @tsk.
 * @sys_security:
 *	Security modules may use this hook to implement new system calls for
 *	security-aware applications.  The interface is similar to socketcall,
 *	but with an @id parameter to help identify the security module whose
 *	call is being invoked.  The module is responsible for interpreting the
 *	parameters, and must copy in the @args array from user space if it is
 *	used.
 *	The recommended convention for creating the hexadecimal @id value is
 *	echo "Name_of_module" | md5sum | cut -c -8; by using this convention,
 *	there is no need for a central registry.
 *	@id contains the security module identifier.
 *	@call contains the call value.
 *	@args contains the call arguments (user space pointer).
 *	The module should return -ENOSYS if it does not implement any new
 *	system calls.
 *
 * @register_security:
 * 	allow module stacking.
 * 	@name contains the name of the security module being stacked.
 * 	@ops contains a pointer to the struct security_operations of the module to stack.
 * @unregister_security:
 *	remove a stacked module.
 *	@name contains the name of the security module being unstacked.
 *	@ops contains a pointer to the struct security_operations of the module to unstack.
 * 
 * This is the main security structure.
 */
struct security_operations {
	int (*ptrace) (struct task_struct * parent, struct task_struct * child);
	int (*capget) (struct task_struct * target,
		       kernel_cap_t * effective,
		       kernel_cap_t * inheritable, kernel_cap_t * permitted);
	int (*capset_check) (struct task_struct * target,
			     kernel_cap_t * effective,
			     kernel_cap_t * inheritable,
			     kernel_cap_t * permitted);
	void (*capset_set) (struct task_struct * target,
			    kernel_cap_t * effective,
			    kernel_cap_t * inheritable,
			    kernel_cap_t * permitted);
	int (*acct) (struct file * file);
	int (*capable) (struct task_struct * tsk, int cap);
	int (*sys_security) (unsigned int id, unsigned call,
			     unsigned long *args);
	int (*quotactl) (int cmds, int type, int id, struct super_block * sb);
	int (*quota_on) (struct file * f);

	int (*bprm_alloc_security) (struct linux_binprm * bprm);
	void (*bprm_free_security) (struct linux_binprm * bprm);
	void (*bprm_compute_creds) (struct linux_binprm * bprm);
	int (*bprm_set_security) (struct linux_binprm * bprm);
	int (*bprm_check_security) (struct linux_binprm * bprm);

	int (*sb_alloc_security) (struct super_block * sb);
	void (*sb_free_security) (struct super_block * sb);
	int (*sb_statfs) (struct super_block * sb);
	int (*sb_mount) (char *dev_name, struct nameidata * nd,
			 char *type, unsigned long flags, void *data);
	int (*sb_check_sb) (struct vfsmount * mnt, struct nameidata * nd);
	int (*sb_umount) (struct vfsmount * mnt, int flags);
	void (*sb_umount_close) (struct vfsmount * mnt);
	void (*sb_umount_busy) (struct vfsmount * mnt);
	void (*sb_post_remount) (struct vfsmount * mnt,
				 unsigned long flags, void *data);
	void (*sb_post_mountroot) (void);
	void (*sb_post_addmount) (struct vfsmount * mnt,
				  struct nameidata * mountpoint_nd);
	int (*sb_pivotroot) (struct nameidata * old_nd,
			     struct nameidata * new_nd);
	void (*sb_post_pivotroot) (struct nameidata * old_nd,
				   struct nameidata * new_nd);

	int (*inode_alloc_security) (struct inode *inode);	
	void (*inode_free_security) (struct inode *inode);
	int (*inode_create) (struct inode *dir,
	                     struct dentry *dentry, int mode);
	void (*inode_post_create) (struct inode *dir,
	                           struct dentry *dentry, int mode);
	int (*inode_link) (struct dentry *old_dentry,
	                   struct inode *dir, struct dentry *new_dentry);
	void (*inode_post_link) (struct dentry *old_dentry,
	                         struct inode *dir, struct dentry *new_dentry);
	int (*inode_unlink) (struct inode *dir, struct dentry *dentry);
	int (*inode_symlink) (struct inode *dir,
	                      struct dentry *dentry, const char *old_name);
	void (*inode_post_symlink) (struct inode *dir,
	                            struct dentry *dentry,
	                            const char *old_name);
	int (*inode_mkdir) (struct inode *dir, struct dentry *dentry, int mode);
	void (*inode_post_mkdir) (struct inode *dir, struct dentry *dentry, 
			    int mode);
	int (*inode_rmdir) (struct inode *dir, struct dentry *dentry);
	int (*inode_mknod) (struct inode *dir, struct dentry *dentry,
	                    int mode, dev_t dev);
	void (*inode_post_mknod) (struct inode *dir, struct dentry *dentry,
	                          int mode, dev_t dev);
	int (*inode_rename) (struct inode *old_dir, struct dentry *old_dentry,
	                     struct inode *new_dir, struct dentry *new_dentry);
	void (*inode_post_rename) (struct inode *old_dir,
	                           struct dentry *old_dentry,
	                           struct inode *new_dir,
	                           struct dentry *new_dentry);
	int (*inode_readlink) (struct dentry *dentry);
	int (*inode_follow_link) (struct dentry *dentry, struct nameidata *nd);
	int (*inode_permission) (struct inode *inode, int mask);
	int (*inode_permission_lite) (struct inode *inode, int mask);
	int (*inode_setattr)	(struct dentry *dentry, struct iattr *attr);
	int (*inode_getattr) (struct vfsmount *mnt, struct dentry *dentry);
	void (*inode_post_lookup) (struct inode *inode, struct dentry *d);
        void (*inode_delete) (struct inode *inode);
	int (*inode_setxattr) (struct dentry *dentry, char *name, void *value,
			       size_t size, int flags);
	int (*inode_getxattr) (struct dentry *dentry, char *name);
	int (*inode_listxattr) (struct dentry *dentry);
	int (*inode_removexattr) (struct dentry *dentry, char *name);

	int (*file_permission) (struct file * file, int mask);
	int (*file_alloc_security) (struct file * file);
	void (*file_free_security) (struct file * file);
	int (*file_llseek) (struct file * file);
	int (*file_ioctl) (struct file * file, unsigned int cmd,
			   unsigned long arg);
	int (*file_mmap) (struct file * file,
			  unsigned long prot, unsigned long flags);
	int (*file_mprotect) (struct vm_area_struct * vma, unsigned long prot);
	int (*file_lock) (struct file * file, unsigned int cmd, int blocking);
	int (*file_fcntl) (struct file * file, unsigned int cmd,
			   unsigned long arg);
	int (*file_set_fowner) (struct file * file);
	int (*file_send_sigiotask) (struct task_struct * tsk,
				    struct fown_struct * fown,
				    int fd, int reason);
	int (*file_receive) (struct file * file);

	int (*task_create) (unsigned long clone_flags);
	int (*task_alloc_security) (struct task_struct * p);
	void (*task_free_security) (struct task_struct * p);
	int (*task_setuid) (uid_t id0, uid_t id1, uid_t id2, int flags);
	int (*task_post_setuid) (uid_t old_ruid /* or fsuid */ ,
				 uid_t old_euid, uid_t old_suid, int flags);
	int (*task_setgid) (gid_t id0, gid_t id1, gid_t id2, int flags);
	int (*task_setpgid) (struct task_struct * p, pid_t pgid);
	int (*task_getpgid) (struct task_struct * p);
	int (*task_getsid) (struct task_struct * p);
	int (*task_setgroups) (int gidsetsize, gid_t * grouplist);
	int (*task_setnice) (struct task_struct * p, int nice);
	int (*task_setrlimit) (unsigned int resource, struct rlimit * new_rlim);
	int (*task_setscheduler) (struct task_struct * p, int policy,
				  struct sched_param * lp);
	int (*task_getscheduler) (struct task_struct * p);
	int (*task_kill) (struct task_struct * p,
			  struct siginfo * info, int sig);
	int (*task_wait) (struct task_struct * p);
	int (*task_prctl) (int option, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4,
			   unsigned long arg5);
	void (*task_kmod_set_label) (void);
	void (*task_reparent_to_init) (struct task_struct * p);

	/* allow module stacking */
	int (*register_security) (const char *name,
	                          struct security_operations *ops);
	int (*unregister_security) (const char *name,
	                            struct security_operations *ops);
};


/* prototypes */
extern int security_scaffolding_startup	(void);
extern int register_security	(struct security_operations *ops);
extern int unregister_security	(struct security_operations *ops);
extern int mod_reg_security	(const char *name, struct security_operations *ops);
extern int mod_unreg_security	(const char *name, struct security_operations *ops);
extern int capable		(int cap);

/* global variables */
extern struct security_operations *security_ops;


#endif /* __KERNEL__ */

#endif /* ! __LINUX_SECURITY_H */

