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
#include <linux/string.h>

#include "nwcapi.h"

typedef void *HANDLE;

struct schandle {
	void *hTypeId;
	void *hId;
};

static inline void copy_schandle(struct schandle *dest, struct schandle *source)
{
	memcpy(dest, source, sizeof(struct schandle));
}
#define copy_session_id	copy_schandle

typedef struct schandle session_t;

#include "commands.h"

#define SC_PRESENT(X)		((X.hTypeId != NULL) || (X.hId != NULL)) ? 1 : 0
#define SC_EQUAL(X, Y)		((X->hTypeId == Y->hTypeId) && (X->hId == Y->hId)) ? 1 : 0
#define SC_INITIALIZE(X)	{X.hTypeId = X.hId = NULL;}

#define UID_TO_SCHANDLE(hSC, uid)	\
		{ \
			hSC.hTypeId = NULL; \
			hSC.hId = (HANDLE)(unsigned long)(uid); \
		}



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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
#define D_CHILD d_u.d_child
#define AS_TREE_LOCK(l)   read_lock_irq(l)
#define AS_TREE_UNLOCK(l) read_unlock_irq(l)
#else
#define D_CHILD d_child
#define AS_TREE_LOCK(l)   spin_lock_irq(l)
#define AS_TREE_UNLOCK(l) spin_unlock_irq(l)
#endif

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
 * Define READ/WRITE flag for struct data_list
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

struct entry_info {
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

struct login {
	struct novfs_string Server;
	struct novfs_string UserName;
	struct novfs_string Password;
};

struct logout {
	struct novfs_string Server;
};

struct dir_cache {
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

struct data_list {
	void *page;
	void *offset;
	int len;
	int rwflag;
};


extern char *ctime_r(time_t * clock, char *buf);

static inline u32 HandletoUint32(HANDLE h)
/*
 *
 *  Arguments:   HANDLE h - handle value
 *
 *  Returns:     u32 - u32 value
 *
 *  Abstract:    Converts a HANDLE to a u32 type.
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	return (u32) ((unsigned long) h);
}

/*++======================================================================*/
static inline HANDLE Uint32toHandle(u32 ui32)
/*
 *
 *  Arguments:   u32 ui32
 *
 *  Returns:     HANDLE - Handle type.
 *
 *  Abstract:    Converts a u32 to a HANDLE type.
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	return ((HANDLE) (unsigned long) ui32);
}

/* Global variables */

extern int Novfs_Version_Major;
extern int Novfs_Version_Minor;
extern int Novfs_Version_Sub;
extern int Novfs_Version_Release;
extern struct dentry *Novfs_root;
extern struct proc_dir_entry *Novfs_Procfs_dir;
extern unsigned long File_update_timeout;
extern int PageCache;
extern char *Novfs_CurrentMount;
extern struct dentry_operations Novfs_dentry_operations;
extern int MaxIoSize;


/* Global functions */
extern int Novfs_Remove_from_Root(char *);
extern void Novfs_dump_inode(void *pf);

extern void mydump(int size, void *dumpptr);

extern int Queue_Daemon_Command(void *request, unsigned long reqlen, void *data,
				int dlen, void **reply, unsigned long * replen,
				int interruptible);

extern int Init_Procfs_Interface(void);
extern void Uninit_Procfs_Interface(void);

/*
 * daemon.c functions
 */
extern void Init_Daemon_Queue(void);
extern void Uninit_Daemon_Queue(void);
extern int do_login(NclString * Server, NclString * Username, NclString * Password, HANDLE * lgnId, struct schandle *Session);
extern int do_logout(struct qstr *Server, struct schandle *Session);
extern int Daemon_SetMountPoint(char *Path);
extern int Daemon_CreateSessionId(struct schandle *SessionId);
extern int Daemon_DestroySessionId(struct schandle *SessionId);
extern int Daemon_getpwuid(uid_t uid, int unamelen, char *uname);
extern int Daemon_Get_UserSpace(struct schandle *session_id, u64 *TotalSize,
				u64 *TotalFree, u64 *TotalDirectoryEnties,
				u64 *FreeDirectoryEnties);
extern int Daemon_SendDebugCmd(char *Command);
extern ssize_t Daemon_Receive_Reply(struct file *file, const char __user *buf, size_t nbytes, loff_t *ppos);
extern ssize_t Daemon_Send_Command(struct file *file, char __user *buf, size_t len, loff_t *off);
extern int Daemon_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
extern int Daemon_Library_close(struct inode *inode, struct file *file);
extern int Daemon_Library_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
extern int Daemon_Library_open(struct inode *inode, struct file *file);
extern ssize_t Daemon_Library_write(struct file *file, const char __user *buf, size_t len, loff_t * off);
extern ssize_t Daemon_Library_read(struct file *file, char __user *buf, size_t len, loff_t * off);
extern loff_t Daemon_Library_llseek(struct file *file, loff_t offset, int origin);
extern int Daemon_Open_Control(struct inode *Inode, struct file *File);
extern int Daemon_Close_Control(struct inode *Inode, struct file *File);
extern int Daemon_getversion(char *Buf, int Length);


/*
 * file.c functions
 */
extern int Novfs_get_alltrees(struct dentry *parent);
extern int Novfs_Get_Connected_Server_List(unsigned char **ServerList, struct schandle *SessionId);
extern int Novfs_Get_Server_Volume_List(struct qstr *Server, unsigned char **VolumeList, struct schandle *SessionId);
extern int Novfs_Get_File_Info(unsigned char *Path, struct entry_info *Info, struct schandle *SessionId);
extern int Novfs_GetX_File_Info(char *Path, const char *Name, char *buffer, ssize_t buffer_size, ssize_t *dataLen, struct schandle *SessionId);
extern int Novfs_ListX_File_Info(char *Path, char *buffer, ssize_t buffer_size, ssize_t * dataLen, struct schandle *SessionId);
extern int Novfs_SetX_File_Info(char *Path, const char *Name, const void *Value,
				unsigned long valueLen,
				unsigned long *bytesWritten, int flags,
				struct schandle *SessionId);

extern int Novfs_Get_Directory_ListEx(unsigned char *Path, HANDLE *EnumHandle,
				      int *Count, struct entry_info **Info,
				      struct schandle *SessionId);
extern int Novfs_Open_File(unsigned char *Path, int Flags, struct entry_info *info,
			   HANDLE * Handle, session_t SessionId);
extern int Novfs_Create(unsigned char *Path, int DirectoryFlag,
			session_t SessionId);
extern int Novfs_Close_File(HANDLE Handle, session_t SessionId);
extern int Novfs_Read_File(HANDLE Handle, unsigned char *Buffer, size_t * Bytes,
			   loff_t * Offset, session_t SessionId);
extern int Novfs_Read_Pages(HANDLE Handle, struct data_list *dlist, int DList_Cnt,
			    size_t * Bytes, loff_t * Offset,
			    session_t SessionId);
extern int Novfs_Write_File(HANDLE Handle, unsigned char *Buffer,
			    size_t * Bytes, loff_t * Offset,
			    session_t SessionId);
extern int Novfs_Write_Page(HANDLE Handle, struct page *Page,
			    session_t SessionId);
extern int Novfs_Write_Pages(HANDLE Handle, struct data_list *dlist, int DList_Cnt,
			     size_t Bytes, loff_t Offset, session_t SessionId);
extern int Novfs_Delete(unsigned char *Path, int DirectoryFlag,
			session_t SessionId);
extern int Novfs_Truncate_File(unsigned char *Path, int PathLen,
			       session_t SessionId);
extern int Novfs_Truncate_File_Ex(HANDLE Handle, loff_t Offset,
				  session_t SessionId);
extern int Novfs_Rename_File(int DirectoryFlag, unsigned char *OldName,
			     int OldLen, unsigned char *NewName, int NewLen,
			     session_t SessionId);
extern int Novfs_Set_Attr(unsigned char *Path, struct iattr *Attr,
			  session_t SessionId);
extern int Novfs_Get_File_Cache_Flag(unsigned char * Path, session_t SessionId);
extern int Novfs_Set_File_Lock(session_t SessionId, HANDLE fhandle,
			       unsigned char fl_type, loff_t fl_start,
			       loff_t len);

extern struct inode *Novfs_get_inode(struct super_block *sb, int mode, int dev, uid_t uid, ino_t ino, struct qstr *name);
extern int Novfs_Read_Stream(HANDLE ConnHandle, unsigned char * Handle,
			     unsigned char * Buffer, size_t * Bytes, loff_t * Offset,
			     int User, session_t SessionId);
extern int Novfs_Write_Stream(HANDLE ConnHandle, unsigned char * Handle,
			      unsigned char * Buffer, size_t * Bytes, loff_t * Offset,
			      session_t SessionId);
extern int Novfs_Close_Stream(HANDLE ConnHandle, unsigned char * Handle,
			      session_t SessionId);

extern int Novfs_Add_to_Root(char *);


/*
 * scope.c functions
 */
extern void Scope_Init(void);
extern void Scope_Uninit(void);
extern void *Scope_Lookup(void);
extern uid_t Scope_Get_Uid(void *);
extern session_t Scope_Get_SessionId(void *Scope);
//extern session_t Scope_Get_SessionId(PSCOPE_LIST Scope);
extern char *Scope_Get_ScopeUsers(void);
extern int Scope_Set_UserSpace(u64 *TotalSize, u64 *Free,
			       u64 *TotalEnties, u64 *FreeEnties);
extern int Scope_Get_UserSpace(u64 *TotalSize, u64 *Free,
			       u64 *TotalEnties, u64 *FreeEnties);
extern char *Scope_dget_path(struct dentry *Dentry, char *Buf,
			     unsigned int Buflen, int Flags);
extern char *Scope_Get_UserName(void);
extern void Scope_Cleanup(void);

/*
 * profile.c functions
 */
extern u64 get_nanosecond_time(void);
static inline void *Novfs_Malloc(size_t size, int flags) { return kmalloc(size, flags); }
extern int DbgPrint(char *Fmt, ...);
extern int init_profile(void);
extern void uninit_profile(void);

/*
 * nwcapi.c functions
 */
extern int NwAuthConnWithId(PXPLAT pdata, session_t Session);
extern int NwConnClose(PXPLAT pdata, HANDLE * Handle, session_t Session);
extern int NwGetConnInfo(PXPLAT pdata, session_t Session);
extern int NwSetConnInfo(PXPLAT pdata, session_t Session);
extern int NwGetDaemonVersion(PXPLAT pdata, session_t Session);
extern int NwGetIdentityInfo(PXPLAT pdata, session_t Session);
extern int NwLicenseConn(PXPLAT pdata, session_t Session);
extern int NwLoginIdentity(PXPLAT pdata, struct schandle *Session);
extern int NwLogoutIdentity(PXPLAT pdata, session_t Session);
extern int NwOpenConnByAddr(PXPLAT pdata, HANDLE * Handle, session_t Session);
extern int NwOpenConnByName(PXPLAT pdata, HANDLE * Handle, session_t Session);
extern int NwOpenConnByRef(PXPLAT pdata, HANDLE * Handle, session_t Session);
extern int NwQueryFeature(PXPLAT pdata, session_t Session);
extern int NwRawSend(PXPLAT pdata, session_t Session);
extern int NwScanConnInfo(PXPLAT pdata, session_t Session);
extern int NwSysConnClose(PXPLAT pdata, unsigned long * Handle, session_t Session);
extern int NwUnAuthenticate(PXPLAT pdata, session_t Session);
extern int NwUnlicenseConn(PXPLAT pdata, session_t Session);
extern int NwcChangeAuthKey(PXPLAT pdata, session_t Session);
extern int NwcEnumIdentities(PXPLAT pdata, session_t Session);
extern int NwcGetDefaultNameCtx(PXPLAT pdata, session_t Session);
extern int NwcGetPreferredDSTree(PXPLAT pdata, session_t Session);
extern int NwcGetTreeMonitoredConn(PXPLAT pdata, session_t Session);
extern int NwcSetDefaultNameCtx(PXPLAT pdata, session_t Session);
extern int NwcSetPreferredDSTree(PXPLAT pdata, session_t Session);
extern int NwcSetPrimaryConn(PXPLAT pdata, session_t Session);
extern int NwcGetPrimaryConn(PXPLAT pdata, session_t Session);
extern int NwcSetMapDrive(PXPLAT pdata, session_t Session);
extern int NwcUnMapDrive(PXPLAT pdata, session_t Session);
extern int NwcEnumerateDrives(PXPLAT pdata, session_t Session);
extern int NwcGetBroadcastMessage(PXPLAT pdata, session_t Session);
extern int NwdSetKeyValue(PXPLAT pdata, session_t Session);
extern int NwdVerifyKeyValue(PXPLAT pdata, session_t Session);


#endif	/* __NOVFS_H */

