/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * Include file for novfs.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#ifndef __NOVFS_H
#define __NOVFS_H

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0L
#endif

#include <linux/version.h>
#include <linux/namei.h>

#include "nwcapi.h"


#ifndef  XTIER_SCHANDLE
struct novfs_schandle {
	void * hTypeId;
	void * hId;

};

#include "commands.h"

#define SC_PRESENT(X)		((X.hTypeId != NULL) || (X.hId != NULL)) ? 1 : 0
#define SC_EQUAL(X, Y)		((X.hTypeId == Y.hTypeId) && (X.hId == Y.hId)) ? 1 : 0
#define SC_INITIALIZE(X)	{X.hTypeId = X.hId = NULL;}

#define UID_TO_SCHANDLE(hSC, uid)	\
		{ \
			hSC.hTypeId = NULL; \
			hSC.hId = (void *)(unsigned long)(uid); \
		}

#define XTIER_SCHANDLE
#endif


/*===[ Manifest constants ]===============================================*/
#define NOVFS_MAGIC		0x4e574653
#define MODULE_NAME		"novfs"

#define TREE_DIRECTORY_NAME	".Trees"
#define SERVER_DIRECTORY_NAME	".Servers"

#define PATH_LENGTH_BUFFER	PATH_MAX
#define NW_MAX_PATH_LENGTH	255

#define XA_BUFFER		(8 * 1024)

#define IOC_LOGIN		0x4a540000
#define IOC_LOGOUT		0x4a540001
#define IOC_XPLAT		0x4a540002
#define IOC_SESSION		0x4a540003
#define IOC_DEBUGPRINT		0x4a540004

/*
 * NetWare file attributes
 */

#define NW_ATTRIBUTE_NORMAL		0x00
#define NW_ATTRIBUTE_READ_ONLY		0x01
#define NW_ATTRIBUTE_HIDDEN		0x02
#define NW_ATTRIBUTE_SYSTEM		0x04
#define NW_ATTRIBUTE_EXECUTE_ONLY	0x08
#define NW_ATTRIBUTE_DIRECTORY		0x10
#define NW_ATTRIBUTE_ARCHIVE		0x20
#define NW_ATTRIBUTE_EXECUTE		0x40
#define NW_ATTRIBUTE_SHAREABLE		0x80

/*
 * Define READ/WRITE flag for DATA_LIST
 */
#define DLREAD		0
#define DLWRITE		1

/*
 * Define list type
 */
#define USER_LIST	1
#define SERVER_LIST	2
#define VOLUME_LIST	3

/*
 * Define flags used in for inodes
 */
#define USER_INODE	1
#define UPDATE_INODE	2

/*
 * Define flags for directory cache flags
 */
#define ENTRY_VALID	0x00000001

#ifdef INTENT_MAGIC
#define NDOPENFLAGS intent.it_flags
#else
#define NDOPENFLAGS intent.open.flags
#endif

/*
 * daemon_command_t flags values
 */
#define INTERRUPTIBLE	1

#ifndef NOVFS_VFS_MAJOR
#define NOVFS_VFS_MAJOR		0
#endif

#ifndef NOVFS_VFS_MINOR
#define NOVFS_VFS_MINOR		0
#endif

#ifndef NOVFS_VFS_SUB
#define NOVFS_VFS_SUB		0
#endif

#ifndef NOVFS_VFS_RELEASE
#define NOVFS_VFS_RELEASE	0
#endif

#define VALUE_TO_STR( value ) #value
#define DEFINE_TO_STR(value) VALUE_TO_STR(value)

#define NOVFS_VERSION_STRING \
         DEFINE_TO_STR(NOVFS_VFS_MAJOR)"." \
         DEFINE_TO_STR(NOVFS_VFS_MINOR)"." \
         DEFINE_TO_STR(NOVFS_VFS_SUB)"-" \
         DEFINE_TO_STR(NOVFS_VFS_RELEASE) \
         "\0"

/*===[ Type definitions ]=================================================*/
struct novfs_entry_info {
	int type;
	umode_t mode;
	uid_t uid;
	gid_t gid;
	loff_t size;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	int namelength;
	unsigned char name[1];
};

struct novfs_string {
	int length;
	unsigned char *data;
};

struct novfs_login {
	struct novfs_string Server;
	struct novfs_string UserName;
	struct novfs_string Password;
};

struct novfs_logout {
	struct novfs_string Server;
};

struct novfs_dir_cache {
	struct list_head list;
	int flags;
	u64 jiffies;
	ino_t ino;
	loff_t size;
	umode_t mode;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	unsigned long hash;
	int nameLen;
	char name[1];
};

struct novfs_data_list {
	void *page;
	void *offset;
	int len;
	int rwflag;
};


extern char *ctime_r(time_t * clock, char *buf);

/*
 *  Converts a HANDLE to a u32 type.
 */
static inline u32 HandletoUint32(void * h)
{
	return (u32) ((unsigned long) h);
}

/*
 * Converts a u32 to a HANDLE type.
 */
static inline void *Uint32toHandle(u32 ui32)
{
	return ((void *) (unsigned long) ui32);
}

/* Global variables */

extern struct dentry *novfs_root;
extern struct proc_dir_entry *novfs_procfs_dir;
extern unsigned long novfs_update_timeout;
extern int novfs_page_cache;
extern char *novfs_current_mnt;
extern int novfs_max_iosize;


/* Global functions */
extern int novfs_remove_from_root(char *);
extern void novfs_dump_inode(void *pf);

extern void novfs_dump(int size, void *dumpptr);

extern int Queue_Daemon_Command(void *request, unsigned long reqlen, void *data,
				int dlen, void **reply, unsigned long * replen,
				int interruptible);
extern int novfs_do_login(struct ncl_string * Server, struct ncl_string* Username, struct ncl_string * Password, void **lgnId, struct novfs_schandle *Session);

extern int novfs_proc_init(void);
extern void novfs_proc_exit(void);

/*
 * daemon.c functions
 */
extern void novfs_daemon_queue_init(void);
extern void novfs_daemon_queue_exit(void);
extern int novfs_daemon_logout(struct qstr *Server, struct novfs_schandle *Session);
extern int novfs_daemon_set_mnt_point(char *Path);
extern int novfs_daemon_create_sessionId(struct novfs_schandle * SessionId);
extern int novfs_daemon_destroy_sessionId(struct novfs_schandle SessionId);
extern int novfs_daemon_getpwuid(uid_t uid, int unamelen, char *uname);
extern int novfs_daemon_get_userspace(struct novfs_schandle SessionId,
		uint64_t * TotalSize, uint64_t * TotalFree,
		uint64_t * TotalDirectoryEnties,
		uint64_t * FreeDirectoryEnties);
extern int novfs_daemon_debug_cmd_send(char *Command);
extern ssize_t novfs_daemon_recv_reply(struct file *file,
		const char *buf, size_t nbytes, loff_t * ppos);
extern ssize_t novfs_daemon_cmd_send(struct file *file, char *buf,
		size_t len, loff_t * off);
extern int novfs_daemon_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg);
extern int novfs_daemon_lib_close(struct inode *inode, struct file *file);
extern int novfs_daemon_lib_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg);
extern int novfs_daemon_lib_open(struct inode *inode, struct file *file);
extern ssize_t novfs_daemon_lib_read(struct file *file, char *buf,
		size_t len, loff_t * off);
extern ssize_t novfs_daemon_lib_write(struct file *file, const char *buf,
		size_t len, loff_t * off);
extern loff_t novfs_daemon_lib_llseek(struct file *file, loff_t offset,
		int origin);
extern int novfs_daemon_open_control(struct inode *Inode, struct file *File);
extern int novfs_daemon_close_control(struct inode *Inode, struct file *File);
extern int novfs_daemon_getversion(char *Buf, int Length);


/*
 * file.c functions
 */
extern int novfs_verify_file(struct qstr *Path, struct novfs_schandle SessionId);
extern int novfs_get_alltrees(struct dentry *parent);
extern int novfs_get_servers(unsigned char **ServerList,
		struct novfs_schandle SessionId);
extern int novfs_get_vols(struct qstr *Server,
		unsigned char **VolumeList, struct novfs_schandle SessionId);
extern int novfs_get_file_info(unsigned char *Path,
		struct novfs_entry_info *Info, struct novfs_schandle SessionId);
extern int novfs_getx_file_info(char *Path, const char *Name,
		char *buffer, ssize_t buffer_size, ssize_t *dataLen,
		struct novfs_schandle SessionId);
extern int novfs_listx_file_info(char *Path, char *buffer,
		ssize_t buffer_size, ssize_t *dataLen,
		struct novfs_schandle SessionId);
extern int novfs_setx_file_info(char *Path, const char *Name, const void *Value,
				unsigned long valueLen,
				unsigned long *bytesWritten, int flags,
				struct novfs_schandle SessionId);

extern int novfs_get_dir_listex(unsigned char *Path, void **EnumHandle,
	int *Count, struct novfs_entry_info **Info,
	struct novfs_schandle SessionId);
extern int novfs_open_file(unsigned char *Path, int Flags,
		struct novfs_entry_info * Info, void **Handle,
		struct novfs_schandle SessionId);
extern int novfs_create(unsigned char *Path, int DirectoryFlag,
			struct novfs_schandle SessionId);
extern int novfs_close_file(void * Handle, struct novfs_schandle SessionId);
extern int novfs_read_file(void * Handle, unsigned char *Buffer,
		size_t * Bytes, loff_t * Offset,
		struct novfs_schandle SessionId);
extern int novfs_read_pages(void * Handle, struct novfs_data_list *DList,
		int DList_Cnt, size_t * Bytes, loff_t * Offset,
			    struct novfs_schandle SessionId);
extern int novfs_write_file(void * Handle, unsigned char *Buffer,
			    size_t * Bytes, loff_t * Offset,
			    struct novfs_schandle SessionId);
extern int novfs_write_page(void * Handle, struct page *Page,
			    struct novfs_schandle SessionId);
extern int novfs_write_pages(void * Handle, struct novfs_data_list *DList,
		int DList_Cnt, size_t Bytes, loff_t Offset,
		struct novfs_schandle SessionId);
extern int novfs_delete(unsigned char *Path, int DirectoryFlag,
			struct novfs_schandle SessionId);
extern int novfs_trunc(unsigned char *Path, int PathLen,
		       struct novfs_schandle SessionId);
extern int novfs_trunc_ex(void * Handle, loff_t Offset,
				  struct novfs_schandle SessionId);
extern int novfs_rename_file(int DirectoryFlag, unsigned char *OldName,
			     int OldLen, unsigned char *NewName, int NewLen,
			     struct novfs_schandle SessionId);
extern int novfs_set_attr(unsigned char *Path, struct iattr *Attr,
			  struct novfs_schandle SessionId);
extern int novfs_get_file_cache_flag(unsigned char * Path,
		struct novfs_schandle SessionId);
extern int novfs_set_file_lock(struct novfs_schandle SessionId, void * fhandle,
			       unsigned char fl_type, loff_t fl_start,
			       loff_t len);

extern struct inode *novfs_get_inode(struct super_block *sb, int mode,
		int dev, uid_t uid, ino_t ino, struct qstr *name);
extern int novfs_read_stream(void * ConnHandle, unsigned char * Handle,
			     unsigned char * Buffer, size_t * Bytes, loff_t * Offset,
			     int User, struct novfs_schandle SessionId);
extern int novfs_write_stream(void * ConnHandle, unsigned char * Handle,
			      unsigned char * Buffer, size_t * Bytes, loff_t * Offset,
			      struct novfs_schandle SessionId);
extern int novfs_close_stream(void * ConnHandle, unsigned char * Handle,
			      struct novfs_schandle SessionId);

extern int novfs_add_to_root(char *);


/*
 * scope.c functions
 */
extern void novfs_scope_init(void);
extern void novfs_scope_exit(void);
extern void *novfs_scope_lookup(void);
extern uid_t novfs_scope_get_uid(struct novfs_scope_list *);
extern struct novfs_schandle novfs_scope_get_sessionId(struct
		novfs_scope_list *);
extern char *novfs_get_scopeusers(void);
extern int novfs_scope_set_userspace(uint64_t * TotalSize, uint64_t * Free,
			       uint64_t * TotalEnties, uint64_t * FreeEnties);
extern int novfs_scope_get_userspace(uint64_t * TotalSize, uint64_t * Free,
			       uint64_t * TotalEnties, uint64_t * FreeEnties);
extern char *novfs_scope_dget_path(struct dentry *Dentry, char *Buf,
			     unsigned int Buflen, int Flags);
extern void novfs_scope_cleanup(void);
extern struct novfs_scope_list *novfs_get_scope_from_name(struct qstr *);
extern struct novfs_scope_list *novfs_get_scope(struct dentry *);
extern char *novfs_scope_get_username(void);

/*
 * profile.c functions
 */
extern u64 get_nanosecond_time(void);
extern int DbgPrint(char *Fmt, ...);
extern void novfs_profile_init(void);
extern void novfs_profile_exit(void);

/*
 * nwcapi.c functions
 */
extern int novfs_auth_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_conn_close(struct novfs_xplat *pdata,
		void **Handle, struct novfs_schandle Session);
extern int novfs_get_conn_info(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_conn_info(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_daemon_ver(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_id_info(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_license_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_login_id(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_logout_id(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_open_conn_by_addr(struct novfs_xplat *pdata,
		void **Handle, struct novfs_schandle Session);
extern int novfs_open_conn_by_name(struct novfs_xplat *pdata,
		void **Handle, struct novfs_schandle Session);
extern int novfs_open_conn_by_ref(struct novfs_xplat *pdata,
		void **Handle, struct novfs_schandle Session);
extern int novfs_query_feature(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_raw_send(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_scan_conn_info(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_sys_conn_close(struct novfs_xplat *pdata,
		unsigned long *Handle, struct novfs_schandle Session);
extern int novfs_unauthenticate(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_unlicense_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_change_auth_key(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_enum_ids(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_default_ctx(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_preferred_DS_tree(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_tree_monitored_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_default_ctx(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_preferred_DS_tree(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_pri_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_pri_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_map_drive(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_unmap_drive(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_enum_drives(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_get_bcast_msg(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_set_key_value(struct novfs_xplat *pdata,
		struct novfs_schandle Session);
extern int novfs_verify_key_value(struct novfs_xplat *pdata,
		struct novfs_schandle Session);


#endif	/* __NOVFS_H */

