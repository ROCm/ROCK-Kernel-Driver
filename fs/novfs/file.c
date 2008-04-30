/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains functions for accessing files through the daemon.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "vfs.h"
#include "commands.h"
#include "nwerror.h"

/*===[ Function prototypes ]==============================================*/
int Novfs_get_alltrees(struct dentry *parent);
ssize_t Novfs_tree_read(struct file *file, char *buf, size_t len, loff_t * off);

int Novfs_Find_Name_In_List(struct qstr *Name, unsigned char * List);

int Novfs_Create(unsigned char * Path, int DirectoryFlag, session_t SessionId);
int Novfs_Close_File(HANDLE Handle, session_t SessionId);
int Novfs_Read_File(HANDLE Handle, unsigned char * Buffer, size_t * Bytes,
		    loff_t * Offset, session_t SessionId);
int Novfs_Write_File(HANDLE Handle, unsigned char * Buffer, size_t * Bytes,
		     loff_t * Offset, session_t SessionId);
int Novfs_Write_Page(HANDLE Handle, struct page *Page, session_t SessionId);
int Novfs_Read_Stream(HANDLE ConnHandle, unsigned char * Handle, unsigned char * Buffer,
		      size_t * Bytes, loff_t * Offset, int User,
		      session_t SessionId);
int Novfs_Write_Stream(HANDLE ConnHandle, unsigned char * Handle, unsigned char * Buffer,
		       size_t * Bytes, loff_t * Offset, session_t SessionId);
int Novfs_Close_Stream(HANDLE ConnHandle, unsigned char * Handle, session_t SessionId);
int Novfs_Delete(unsigned char * Path, int DirectoryFlag, session_t SessionId);
int Novfs_Truncate_File(unsigned char * Path, int PathLen, session_t SessionId);
int Novfs_Truncate_File_Ex(HANDLE Handle, loff_t Offset, session_t SessionId);
int Novfs_Rename_File(int DirectoryFlag, unsigned char * OldName, int OldLen,
		      unsigned char * NewName, int NewLen, session_t SessionId);
int Novfs_Set_Attr(unsigned char * Path, struct iattr *Attr, session_t SessionId);
int Novfs_Get_File_Cache_Flag(unsigned char * Path, session_t SessionId);

static struct file_operations Novfs_tree_operations = {
      read:Novfs_tree_read,
};

/*
 * StripTrailingDots was added because some apps will
 * try and create a file name with a trailing dot.  NetWare
 * doesn't like this and will return an error.
 */
static int StripTrailingDots = 1;

int Novfs_get_alltrees(struct dentry *parent)
{
	unsigned char *p;
	PCOMMAND_REPLY_HEADER reply = NULL;
	unsigned long replylen = 0;
	COMMAND_REQUEST_HEADER cmd;
	int retCode;
	struct dentry *entry;
	struct qstr name;
	struct inode *inode;

	cmd.CommandType = 0;
	cmd.SequenceNumber = 0;
//sg ???   cmd.SessionId = 0x1234;
	SC_INITIALIZE(cmd.SessionId);

	DbgPrint("Novfs_get_alltrees:\n");

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);
	DbgPrint("Novfs_get_alltrees: relpy=0x%p replylen=%d\n", reply,
		 replylen);
	if (reply) {
		mydump(replylen, reply);
		if (!reply->ErrorCode
		    && (replylen > sizeof(COMMAND_REPLY_HEADER))) {
			p = (char *)reply + 8;
			while (*p) {
				DbgPrint("Novfs_get_alltrees: %s\n", p);
				name.len = strlen(p);
				name.name = p;
				name.hash = full_name_hash(name.name, name.len);
				entry = d_lookup(parent, &name);
				if (NULL == entry) {
					DbgPrint("Novfs_get_alltrees: adding %s\n", p);
					entry = d_alloc(parent, &name);
					if (entry) {
						entry->d_op = &Novfs_dentry_operations;
						inode = Novfs_get_inode(parent->d_sb, S_IFREG | 0400, 0, 0, 0, &name);
						if (inode) {
							inode->i_fop = &Novfs_tree_operations;
							d_add(entry, inode);
						}
					}
				}
				p += (name.len + 1);
			}
		}
		kfree(reply);
	}
	return (retCode);
}

ssize_t Novfs_tree_read(struct file * file, char *buf, size_t len, loff_t * off)
{
	if (file->f_pos != 0) {
		return (0);
	}
	if (copy_to_user(buf, "Tree\n", 5)) {
		return (0);
	}
	return (5);
}

int Novfs_Get_Connected_Server_List(unsigned char ** ServerList, struct schandle *SessionId)
{
	GET_CONNECTED_SERVER_LIST_REQUEST req;
	PGET_CONNECTED_SERVER_LIST_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;

	*ServerList = NULL;

	req.Command.CommandType = VFS_COMMAND_GET_CONNECTED_SERVER_LIST;
	memcpy(&req.Command.SessionId, SessionId, sizeof(*SessionId));

	retCode =
	    Queue_Daemon_Command(&req, sizeof(req), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("Novfs_Get_Connected_Server_List: reply\n");
		replylen -= sizeof(COMMAND_REPLY_HEADER);
		if (!reply->Reply.ErrorCode && replylen) {
			memcpy(reply, reply->List, replylen);
			*ServerList = (unsigned char *) reply;
			retCode = 0;
		} else {
			kfree(reply);
			retCode = -ENOENT;
		}
	}
	return (retCode);
}

int Novfs_Get_Server_Volume_List(struct qstr *Server, unsigned char ** VolumeList,
				 struct schandle *SessionId)
{
	PGET_SERVER_VOLUME_LIST_REQUEST req;
	PGET_SERVER_VOLUME_LIST_REPLY reply = NULL;
	unsigned long replylen = 0, reqlen;
	int retCode;

	*VolumeList = NULL;
	reqlen = sizeof(GET_SERVER_VOLUME_LIST_REQUEST) + Server->len;
	req = kmalloc(reqlen, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->Command.CommandType = VFS_COMMAND_GET_SERVER_VOLUME_LIST;
	req->Length = Server->len;
	memcpy(req->Name, Server->name, Server->len);
	memcpy(&req->Command.SessionId, SessionId, sizeof(*SessionId));

	retCode = Queue_Daemon_Command(req, reqlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("Novfs_Get_Server_Volume_List: reply\n");
		mydump(replylen, reply);
		replylen -= sizeof(COMMAND_REPLY_HEADER);

		if (!reply->Reply.ErrorCode && replylen) {
			memcpy(reply, reply->List, replylen);
			*VolumeList = (unsigned char *) reply;
			retCode = 0;
		} else {
			kfree(reply);
			retCode = -ENOENT;
		}
	}
	kfree(req);
	return retCode;
}

int Novfs_Find_Name_In_List(struct qstr *Name, unsigned char * List)
{
	int len;
	int retCode = 0;

	while (*List) {
		len = strlen(List);
		if ((len == Name->len) && !strncmp(Name->name, List, len)) {
			retCode = 1;
			break;
		}
		List += (len + 1);
	}
	return (retCode);
}

int Novfs_Get_File_Info(unsigned char * Path, struct entry_info *Info, struct schandle *SessionId)
{
	PVERIFY_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	PVERIFY_FILE_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;
	int pathlen;

	DbgPrint("%s: Path = %s\n", __func__, Path);

	Info->mode = S_IFDIR | 0700;
	Info->uid = current->uid;
	Info->gid = current->gid;
	Info->size = 0;
	Info->atime = Info->mtime = Info->ctime = CURRENT_TIME;

	if (Path && *Path) {
		pathlen = strlen(Path);
		if (StripTrailingDots) {
			if ('.' == Path[pathlen - 1])
				pathlen--;
		}
		cmdlen = offsetof(VERIFY_FILE_REQUEST, path) + pathlen;
		cmd = (PVERIFY_FILE_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
		if (cmd) {
			cmd->Command.CommandType = VFS_COMMAND_VERIFY_FILE;
			cmd->Command.SequenceNumber = 0;
			memcpy(&cmd->Command.SessionId, SessionId, sizeof(*SessionId));
			cmd->pathLen = pathlen;
			memcpy(cmd->path, Path, cmd->pathLen);

			retCode =
			    Queue_Daemon_Command(cmd, cmdlen, NULL, 0,
						 (void *)&reply, &replylen,
						 INTERRUPTIBLE);

			if (reply) {

				if (reply->Reply.ErrorCode) {
					retCode = -ENOENT;
				} else {
					Info->type = 3;
					Info->mode = S_IRWXU;

					if (reply->
					    fileMode & NW_ATTRIBUTE_DIRECTORY) {
						Info->mode |= S_IFDIR;
					} else {
						Info->mode |= S_IFREG;
					}

					if (reply->
					    fileMode & NW_ATTRIBUTE_READ_ONLY) {
						Info->mode &= ~(S_IWUSR);
					}

					Info->uid = current->euid;
					Info->gid = current->egid;
					Info->size = reply->fileSize;
					Info->atime.tv_sec =
					    reply->lastAccessTime;
					Info->atime.tv_nsec = 0;
					Info->mtime.tv_sec = reply->modifyTime;
					Info->mtime.tv_nsec = 0;
					Info->ctime.tv_sec = reply->createTime;
					Info->ctime.tv_nsec = 0;
					DbgPrint("%s: replylen=%d sizeof(VERIFY_FILE_REPLY)=%d\n", __func__, replylen, sizeof(VERIFY_FILE_REPLY));
					if (replylen > sizeof(VERIFY_FILE_REPLY)) {
						unsigned int *lp = &reply->fileMode;
						lp++;
						DbgPrint("%s: extra data 0x%x\n", __func__, *lp);
						Info->mtime.tv_nsec = *lp;
					}
					retCode = 0;
				}

				kfree(reply);
			}
			kfree(cmd);
		}
	}

	DbgPrint("%s: return 0x%x\n", __func__, retCode);
	return (retCode);
}

int Novfs_GetX_File_Info(char *Path, const char *Name, char *buffer,
			 ssize_t buffer_size, ssize_t * dataLen,
			 session_t *SessionId)
{
	PXA_GET_REPLY reply = NULL;
	unsigned long replylen = 0;
	PXA_GET_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int namelen = strlen(Name);
	int pathlen = strlen(Path);

	DbgPrint("%s: xattr: Path = %s, pathlen = %i, Name = %s, namelen = %i\n", __func__, Path, pathlen, Name, namelen);

	if (namelen > MAX_XATTR_NAME_LEN) {
		return ENOATTR;
	}

	cmdlen = offsetof(XA_GET_REQUEST, data) + pathlen + 1 + namelen + 1;	// two '\0'
	cmd = (PXA_GET_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_GET_EXTENDED_ATTRIBUTE;
		cmd->Command.SequenceNumber = 0;
		memcpy(&cmd->Command.SessionId, SessionId, sizeof(*SessionId));

		cmd->pathLen = pathlen;
		memcpy(cmd->data, Path, cmd->pathLen + 1);	//+ '\0'

		cmd->nameLen = namelen;
		memcpy(cmd->data + cmd->pathLen + 1, Name, cmd->nameLen + 1);

		DbgPrint("%s xattr: PXA_GET_REQUEST BEGIN\n", __func__);
		DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__, cmd->Command.CommandType);
		DbgPrint("%s xattr: Command.SessionId = %d\n", __func__, cmd->Command.SessionId);
		DbgPrint("%s xattr: pathLen = %d\n", __func__, cmd->pathLen);
		DbgPrint("%s xattr: Path = %s\n", __func__, cmd->data);
		DbgPrint("%s xattr: nameLen = %d\n", __func__, cmd->nameLen);
		DbgPrint("%s xattr: name = %s\n", __func__, (cmd->data + cmd->pathLen + 1));
		DbgPrint("%s xattr: PXA_GET_REQUEST END\n", __func__);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint("%s xattr: reply->Reply.ErrorCode=%d, %X\n", __func__, reply->Reply.ErrorCode, reply->Reply.ErrorCode);
				DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);

				//0xC9 = EA not found (C9), 0xD1 = EA access denied
				if ((reply->Reply.ErrorCode == 0xC9)
				    || (reply->Reply.ErrorCode == 0xD1)) {
					retCode = -ENOATTR;
				} else {
					retCode = -ENOENT;
				}
			} else {

				*dataLen =
				    replylen - sizeof(COMMAND_REPLY_HEADER);
				DbgPrint("%s xattr: replylen=%u, dataLen=%u\n", __func__, replylen, *dataLen);

				if (buffer_size >= *dataLen) {
					DbgPrint("%s xattr: copying to buffer from &reply->pData\n", __func__);
					memcpy(buffer, &reply->pData, *dataLen);

					retCode = 0;
				} else {
					DbgPrint("%s xattr: (!!!) buffer is smaller then reply\n", __func__);
					retCode = -ERANGE;
				}
				DbgPrint("%s xattr: /dumping buffer\n", __func__);
				mydump(*dataLen, buffer);
				DbgPrint("%s xattr: \\after dumping buffer\n", __func__);
			}

			kfree(reply);
		} else {
			DbgPrint("%s xattr: reply = NULL\n", __func__);
		}
		kfree(cmd);

	}

	return retCode;
}

int Novfs_SetX_File_Info(char *Path, const char *Name, const void *Value,
			 unsigned long valueLen, unsigned long *bytesWritten,
			 int flags, struct schandle *SessionId)
{
	PXA_SET_REPLY reply = NULL;
	unsigned long replylen = 0;
	PXA_SET_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int namelen = strlen(Name);
	int pathlen = strlen(Path);

	DbgPrint("%s xattr: Path = %s, pathlen = %i, Name = %s, namelen = %i, value len = %u\n", __func__,
	     Path, pathlen, Name, namelen, valueLen);

	if (namelen > MAX_XATTR_NAME_LEN) {
		return ENOATTR;
	}

	cmdlen = offsetof(XA_SET_REQUEST, data) + pathlen + 1 + namelen + 1 + valueLen;
	cmd = (PXA_SET_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_SET_EXTENDED_ATTRIBUTE;
		cmd->Command.SequenceNumber = 0;
		memcpy(&cmd->Command.SessionId, SessionId, sizeof(*SessionId));

		cmd->flags = flags;
		cmd->pathLen = pathlen;
		memcpy(cmd->data, Path, cmd->pathLen + 1);	//+ '\0'

		cmd->nameLen = namelen;
		memcpy(cmd->data + cmd->pathLen + 1, Name, cmd->nameLen + 1);

		cmd->valueLen = valueLen;
		memcpy(cmd->data + cmd->pathLen + 1 + cmd->nameLen + 1, Value,
		       valueLen);

		DbgPrint("%s xattr: PXA_SET_REQUEST BEGIN\n", __func__);
		DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__, cmd->Command.CommandType);
		DbgPrint("%s xattr: Command.SessionId = %d\n", __func__, cmd->Command.SessionId);
		DbgPrint("%s xattr: pathLen = %d\n", __func__, cmd->pathLen);
		DbgPrint("%s xattr: Path = %s\n", __func__, cmd->data);
		DbgPrint("%s xattr: nameLen = %d\n", __func__, cmd->nameLen);
		DbgPrint("%s xattr: name = %s\n", __func__, (cmd->data + cmd->pathLen + 1));
		mydump(valueLen < 16 ? valueLen : 16, (char *)Value);

		DbgPrint("%s xattr: PXA_SET_REQUEST END\n", __func__);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint("%s xattr: reply->Reply.ErrorCode=%d, %X\n", __func__, reply->Reply.ErrorCode, reply->Reply.ErrorCode);
				DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);

				retCode = -reply->Reply.ErrorCode;	//-ENOENT;
			} else {

				DbgPrint("%s xattr: replylen=%u, real len = %u\n", __func__, replylen, replylen - sizeof(COMMAND_REPLY_HEADER));
				memcpy(bytesWritten, &reply->pData,
				       replylen - sizeof(COMMAND_REPLY_HEADER));

				retCode = 0;
			}

			kfree(reply);
		} else {
			DbgPrint("%s xattr: reply = NULL\n", __func__);
		}
		kfree(cmd);

	}

	return retCode;
}

int Novfs_ListX_File_Info(char *Path, char *buffer, ssize_t buffer_size, ssize_t * dataLen, struct schandle *SessionId)
{
	PXA_LIST_REPLY reply = NULL;
	unsigned long replylen = 0;
	PVERIFY_FILE_REQUEST cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int pathlen = strlen(Path);
	DbgPrint("%s xattr: Path = %s, pathlen = %i\n", __func__, Path, pathlen);

	*dataLen = 0;
	cmdlen = offsetof(VERIFY_FILE_REQUEST, path) + pathlen;
	cmd = (PVERIFY_FILE_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_LIST_EXTENDED_ATTRIBUTES;
		cmd->Command.SequenceNumber = 0;
		memcpy(&cmd->Command.SessionId, SessionId, sizeof(*SessionId));
		cmd->pathLen = pathlen;
		memcpy(cmd->path, Path, cmd->pathLen + 1);	//+ '\0'
		DbgPrint("%s xattr: PVERIFY_FILE_REQUEST BEGIN\n", __func__);
		DbgPrint("%s xattr: Queue_Daemon_Command %d\n", __func__, cmd->Command.CommandType);
		DbgPrint("%s xattr: Command.SessionId = %d\n", __func__, cmd->Command.SessionId);
		DbgPrint("%s xattr: pathLen = %d\n", __func__, cmd->pathLen);
		DbgPrint("%s xattr: Path = %s\n", __func__, cmd->path);
		DbgPrint("%s xattr: PVERIFY_FILE_REQUEST END\n", __func__);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint("%s xattr: reply->Reply.ErrorCode=%d, %X\n", __func__, reply->Reply.ErrorCode, reply->Reply.ErrorCode);
				DbgPrint("%s xattr: replylen=%d\n", __func__, replylen);

				retCode = -ENOENT;
			} else {
				*dataLen = replylen - sizeof(COMMAND_REPLY_HEADER);
				DbgPrint("%s xattr: replylen=%u, dataLen=%u\n", __func__, replylen, *dataLen);

				if (buffer_size >= *dataLen) {
					DbgPrint("%s xattr: copying to buffer from &reply->pData\n", __func__);
					memcpy(buffer, &reply->pData, *dataLen);
				} else {
					DbgPrint("%s xattr: (!!!) buffer is smaller then reply\n", __func__);
					retCode = -ERANGE;
				}
				DbgPrint("%s xattr: /dumping buffer\n", __func__);
				mydump(*dataLen, buffer);
				DbgPrint("%s xattr: \\after dumping buffer\n", __func__);

				retCode = 0;
			}

			kfree(reply);
		} else {
			DbgPrint("%s xattr: reply = NULL\n", __func__);
		}
		kfree(cmd);

	}

	return retCode;
}

static int begin_directory_enumerate(unsigned char *Path, int PathLen, HANDLE *EnumHandle, struct schandle *SessionId)
{
	PBEGIN_ENUMERATE_DIRECTORY_REQUEST cmd;
	PBEGIN_ENUMERATE_DIRECTORY_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	*EnumHandle = 0;

	cmdlen = offsetof(BEGIN_ENUMERATE_DIRECTORY_REQUEST, path) + PathLen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_START_ENUMERATE;
		cmd->Command.SequenceNumber = 0;
		memcpy(&cmd->Command.SessionId, SessionId, sizeof(*SessionId));

		cmd->pathLen = PathLen;
		memcpy(cmd->path, Path, PathLen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
/*
 *      retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, 0);
 */
		if (reply) {
			if (reply->Reply.ErrorCode) {
				retCode = -EIO;
			} else {
				*EnumHandle = reply->enumerateHandle;
				retCode = 0;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

static int end_directory_enumerate(HANDLE EnumHandle, struct schandle *SessionId)
{
	END_ENUMERATE_DIRECTORY_REQUEST cmd;
	PEND_ENUMERATE_DIRECTORY_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_END_ENUMERATE;
	cmd.Command.SequenceNumber = 0;
	copy_session_id(&cmd.Command.SessionId, SessionId);

	cmd.enumerateHandle = EnumHandle;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		}
		kfree(reply);
	}

	return (retCode);
}

int directory_enumerate(HANDLE * EnumHandle, struct entry_info *Info,
			session_t SessionId)
{
	ENUMERATE_DIRECTORY_REQUEST cmd;
	PENUMERATE_DIRECTORY_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_ENUMERATE_DIRECTORY;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.enumerateHandle = *EnumHandle;
	cmd.pathLen = 0;
	cmd.path[0] = '\0';

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	if (reply) {
		/*
		 * The VFS_COMMAND_ENUMERATE_DIRECTORY call can return an
		 * error but there could still be valid data.
		 */
		if (!reply->Reply.ErrorCode ||
		    ((replylen > sizeof(COMMAND_REPLY_HEADER)) &&
		     (reply->nameLen > 0))) {
			Info->type = 3;
			Info->mode = S_IRWXU;

			if (reply->mode & NW_ATTRIBUTE_DIRECTORY) {
				Info->mode |= S_IFDIR;
				Info->mode |= S_IXUSR;
			} else {
				Info->mode |= S_IFREG;
			}

			if (reply->mode & NW_ATTRIBUTE_READ_ONLY) {
				Info->mode &= ~(S_IWUSR);
			}

			if (reply->mode & NW_ATTRIBUTE_EXECUTE) {
				Info->mode |= S_IXUSR;
			}

			Info->uid = current->uid;
			Info->gid = current->gid;
			Info->size = reply->size;
			Info->atime.tv_sec = reply->lastAccessTime;
			Info->atime.tv_nsec = 0;
			Info->mtime.tv_sec = reply->modifyTime;
			Info->mtime.tv_nsec = 0;
			Info->ctime.tv_sec = reply->createTime;
			Info->ctime.tv_nsec = 0;
			Info->namelength = reply->nameLen;
			memcpy(Info->name, reply->name, reply->nameLen);
			retCode = 0;
			if (reply->Reply.ErrorCode) {
				retCode = -1;	/* Eof of data */
			}
			*EnumHandle = reply->enumerateHandle;
		} else {
			retCode = -ENODATA;
		}
		kfree(reply);
	}

	return (retCode);
}

static int directory_enumerate_ex(HANDLE *EnumHandle, struct schandle *SessionId, int *Count, struct entry_info **PInfo, int Interrupt)
{
	ENUMERATE_DIRECTORY_EX_REQUEST cmd;
	PENUMERATE_DIRECTORY_EX_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	struct entry_info *info;
	PENUMERATE_DIRECTORY_EX_DATA data;
	int isize;

	if (PInfo) {
		*PInfo = NULL;
	}
	*Count = 0;

	cmd.Command.CommandType = VFS_COMMAND_ENUMERATE_DIRECTORY_EX;
	cmd.Command.SequenceNumber = 0;
	copy_session_id(&cmd.Command.SessionId, SessionId);

	cmd.enumerateHandle = *EnumHandle;
	cmd.pathLen = 0;
	cmd.path[0] = '\0';

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, Interrupt);

	if (reply) {
		retCode = 0;
		/*
		 * The VFS_COMMAND_ENUMERATE_DIRECTORY call can return an
		 * error but there could still be valid data.
		 */

		if (!reply->Reply.ErrorCode ||
		    ((replylen > sizeof(COMMAND_REPLY_HEADER)) &&
		     (reply->enumCount > 0))) {
			DbgPrint("directory_enumerate_ex: isize=%d\n",
				 replylen);
			data = (PENUMERATE_DIRECTORY_EX_DATA) ((char *)reply + sizeof(ENUMERATE_DIRECTORY_EX_REPLY));
			isize = replylen - sizeof(PENUMERATE_DIRECTORY_EX_REPLY) - reply->enumCount * offsetof(ENUMERATE_DIRECTORY_EX_DATA, name);
			isize += (reply->enumCount * offsetof(struct entry_info, name));

			if (PInfo) {
				*PInfo = info = Novfs_Malloc(isize, GFP_KERNEL);
				if (*PInfo) {
					DbgPrint("directory_enumerate_ex1: data=0x%p info=0x%p\n", data, info);
					*Count = reply->enumCount;
					do {
						DbgPrint("directory_enumerate_ex2: data=0x%p length=%d\n", data);

						info->type = 3;
						info->mode = S_IRWXU;

						if (data->mode & NW_ATTRIBUTE_DIRECTORY) {
							info->mode |= S_IFDIR;
							info->mode |= S_IXUSR;
						} else {
							info->mode |= S_IFREG;
						}

						if (data->mode & NW_ATTRIBUTE_READ_ONLY) {
							info->mode &= ~(S_IWUSR);
						}

						if (data->mode & NW_ATTRIBUTE_EXECUTE) {
							info->mode |= S_IXUSR;
						}

						info->uid = current->euid;
						info->gid = current->egid;
						info->size = data->size;
						info->atime.tv_sec = data->lastAccessTime;
						info->atime.tv_nsec = 0;
						info->mtime.tv_sec = data->modifyTime;
						info->mtime.tv_nsec = 0;
						info->ctime.tv_sec = data->createTime;
						info->ctime.tv_nsec = 0;
						info->namelength = data->nameLen;
						memcpy(info->name, data->name, data->nameLen);
						data = (PENUMERATE_DIRECTORY_EX_DATA)&data->name[data->nameLen];
						replylen = (int)((char *)&info->name[info->namelength] - (char *)info);
						DbgPrint("directory_enumerate_ex3: info=0x%p\n", info);
						mydump(replylen, info);

						info = (struct entry_info *)&info->name[info->namelength];

					} while (--reply->enumCount);
				}
			}

			if (reply->Reply.ErrorCode) {
				retCode = -1;	/* Eof of data */
			}
			*EnumHandle = reply->enumerateHandle;
		} else {
			retCode = -ENODATA;
		}
		kfree(reply);
	}

	return (retCode);
}

int Novfs_Get_Directory_ListEx(unsigned char * Path, HANDLE * EnumHandle, int *Count,
			       struct entry_info **Info, struct schandle *SessionId)
{
	int retCode = -ENOENT;

	if (Count)
		*Count = 0;
	if (Info)
		*Info = NULL;

	if ((HANDLE) - 1 == *EnumHandle) {
		return (-ENODATA);
	}

	if (0 == *EnumHandle)
		retCode = begin_directory_enumerate(Path, strlen(Path), EnumHandle, SessionId);

	if (*EnumHandle) {
		retCode = directory_enumerate_ex(EnumHandle, SessionId, Count, Info, INTERRUPTIBLE);
		if (retCode) {
			end_directory_enumerate(*EnumHandle, SessionId);
			if (-1 == retCode) {
				retCode = 0;
				*EnumHandle = Uint32toHandle(-1);
			}
		}
	}
	return (retCode);
}

int Novfs_Open_File(unsigned char * Path, int Flags, struct entry_info *Info, HANDLE * Handle,
		    session_t SessionId)
{
	POPEN_FILE_REQUEST cmd;
	POPEN_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	*Handle = 0;

	cmdlen = offsetof(OPEN_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_OPEN_FILE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->access = 0;

		if (!(Flags & O_WRONLY) || (Flags & O_RDWR)) {
			cmd->access |= NWD_ACCESS_READ;
		}

		if ((Flags & O_WRONLY) || (Flags & O_RDWR)) {
			cmd->access |= NWD_ACCESS_WRITE;
		}

		switch (Flags & (O_CREAT | O_EXCL | O_TRUNC)) {
		case O_CREAT:
			cmd->disp = NWD_DISP_OPEN_ALWAYS;
			break;

		case O_CREAT | O_EXCL:
			cmd->disp = NWD_DISP_CREATE_NEW;
			break;

		case O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_ALWAYS;
			break;

		case O_CREAT | O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_ALWAYS;
			break;

		case O_CREAT | O_EXCL | O_TRUNC:
			cmd->disp = NWD_DISP_CREATE_NEW;
			break;

		default:
			cmd->disp = NWD_DISP_OPEN_EXISTING;
			break;
		}

		cmd->mode = NWD_SHARE_READ | NWD_SHARE_WRITE | NWD_SHARE_DELETE;

		cmd->pathLen = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {
			if (reply->Reply.ErrorCode) {
				if (NWE_OBJECT_EXISTS == reply->Reply.ErrorCode) {
					retCode = -EEXIST;
				} else if (NWE_ACCESS_DENIED ==
					   reply->Reply.ErrorCode) {
					retCode = -EACCES;
				} else if (NWE_FILE_IN_USE ==
					   reply->Reply.ErrorCode) {
					retCode = -EBUSY;
				} else {
					retCode = -ENOENT;
				}
			} else {
				*Handle = reply->handle;
				retCode = 0;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Create(unsigned char * Path, int DirectoryFlag, session_t SessionId)
{
	PCREATE_FILE_REQUEST cmd;
	PCREATE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(CREATE_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_CREATE_FILE;
		if (DirectoryFlag) {
			cmd->Command.CommandType = VFS_COMMAND_CREATE_DIRECOTRY;
		}
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {
			retCode = 0;
			if (reply->Reply.ErrorCode) {
				retCode = -EIO;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Close_File(HANDLE Handle, session_t SessionId)
{
	CLOSE_FILE_REQUEST cmd;
	PCLOSE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_CLOSE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.handle = Handle;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int Novfs_Read_File(HANDLE Handle, unsigned char * Buffer, size_t * Bytes,
		    loff_t * Offset, session_t SessionId)
{
	READ_FILE_REQUEST cmd;
	PREAD_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	if ((offsetof(READ_FILE_REPLY, data) + len) > MaxIoSize) {
		len = MaxIoSize - offsetof(READ_FILE_REPLY, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.Command.CommandType = VFS_COMMAND_READ_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.handle = Handle;
	cmd.len = len;
	cmd.offset = *Offset;

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("Novfs_Read_File: Queue_Daemon_Command 0x%x replylen=%d\n", retCode, replylen);

	if (!retCode) {
		if (reply->Reply.ErrorCode) {
			if (NWE_FILE_IO_LOCKED == reply->Reply.ErrorCode) {
				retCode = -EBUSY;
			} else {
				retCode = -EIO;
			}
		} else {
			replylen -= offsetof(READ_FILE_REPLY, data);
			if (replylen > 0) {
				replylen -= copy_to_user(Buffer, reply->data, replylen);
				*Bytes = replylen;
			}
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Read_File *Bytes=0x%x retCode=0x%x\n", *Bytes, retCode);

	return (retCode);
}

int Novfs_Read_Pages(HANDLE Handle, struct data_list *DList, int DList_Cnt,
		     size_t * Bytes, loff_t * Offset, session_t SessionId)
{
	READ_FILE_REQUEST cmd;
	PREAD_FILE_REPLY reply = NULL;
	READ_FILE_REPLY lreply;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	DbgPrint
	    ("Novfs_Read_Pages: Handle=0x%p Dlst=0x%p Dlcnt=%d Bytes=%d Offset=%lld SessionId=0x%p:%p\n",
	     Handle, DList, DList_Cnt, len, *Offset, SessionId.hTypeId,
	     SessionId.hId);

	cmd.Command.CommandType = VFS_COMMAND_READ_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.handle = Handle;
	cmd.len = len;
	cmd.offset = *Offset;

	/*
	 * Dlst first entry is reserved for reply header.
	 */
	DList[0].page = NULL;
	DList[0].offset = &lreply;
	DList[0].len = offsetof(READ_FILE_REPLY, data);
	DList[0].rwflag = DLWRITE;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), DList, DList_Cnt,
				 (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("Novfs_Read_Pages: Queue_Daemon_Command 0x%x\n", retCode);

	if (!retCode) {
		if (reply) {
			memcpy(&lreply, reply, sizeof(lreply));
		}

		if (lreply.Reply.ErrorCode) {
			if (NWE_FILE_IO_LOCKED == lreply.Reply.ErrorCode) {
				retCode = -EBUSY;
			} else {
				retCode = -EIO;
			}
		}
		*Bytes = replylen - offsetof(READ_FILE_REPLY, data);
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Read_Pages: retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Write_File(HANDLE Handle, unsigned char * Buffer, size_t * Bytes,
		     loff_t * Offset, session_t SessionId)
{
	WRITE_FILE_REQUEST cmd;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	unsigned long boff;
	struct page **pages;
	struct data_list *dlist;
	int res = 0, npage, i;
	WRITE_FILE_REPLY lreply;

	len = *Bytes;
	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	*Bytes = 0;

	memset(&lreply, 0, sizeof(lreply));

	DbgPrint("Novfs_Write_File cmdlen=%ld len=%ld\n", cmdlen, len);

	if ((cmdlen + len) > MaxIoSize) {
		len = MaxIoSize - cmdlen;
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}
	cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;
	cmd.handle = Handle;
	cmd.len = len;
	cmd.offset = *Offset;

	DbgPrint("Novfs_Write_File cmdlen=%ld len=%ld\n", cmdlen, len);

	npage =
	    (((unsigned long)Buffer & ~PAGE_MASK) + len +
	     (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	dlist = Novfs_Malloc(sizeof(struct data_list) * (npage + 1), GFP_KERNEL);
	if (NULL == dlist) {
		return (-ENOMEM);
	}

	pages = Novfs_Malloc(sizeof(struct page *) * npage, GFP_KERNEL);

	if (NULL == pages) {
		kfree(dlist);
		return (-ENOMEM);
	}

	down_read(&current->mm->mmap_sem);

	res = get_user_pages(current, current->mm, (unsigned long)Buffer, npage, 0,	/* read type */
			     0,	/* don't force */
			     pages, NULL);

	up_read(&current->mm->mmap_sem);

	DbgPrint("Novfs_Write_File res=%d\n", res);

	if (res > 0) {
		boff = (unsigned long)Buffer & ~PAGE_MASK;

		flush_dcache_page(pages[0]);
		dlist[0].page = pages[0];
		dlist[0].offset = (char *)boff;
		dlist[0].len = PAGE_SIZE - boff;
		dlist[0].rwflag = DLREAD;

		if (dlist[0].len > len) {
			dlist[0].len = len;
		}

		DbgPrint("Novfs_Write_File0: page=0x%p offset=0x%p len=%d\n",
			 dlist[0].page, dlist[0].offset, dlist[0].len);

		boff = dlist[0].len;

		DbgPrint("Novfs_Write_File len=%d boff=%d\n", len, boff);

		for (i = 1; (i < res) && (boff < len); i++) {
			flush_dcache_page(pages[i]);

			dlist[i].page = pages[i];
			dlist[i].offset = NULL;
			dlist[i].len = len - boff;
			if (dlist[i].len > PAGE_SIZE) {
				dlist[i].len = PAGE_SIZE;
			}
			dlist[i].rwflag = DLREAD;

			boff += dlist[i].len;
			DbgPrint
			    ("Novfs_Write_File%d: page=0x%p offset=0x%p len=%d\n",
			     i, dlist[i].page, dlist[i].offset, dlist[i].len);
		}

		dlist[i].page = NULL;
		dlist[i].offset = &lreply;
		dlist[i].len = sizeof(lreply);
		dlist[i].rwflag = DLWRITE;
		res++;

		DbgPrint("Novfs_Write_File Buffer=0x%p boff=0x%x len=%d\n",
			 Buffer, boff, len);

		retCode =
		    Queue_Daemon_Command(&cmd, cmdlen, dlist, res,
					 (void *)&reply, &replylen,
					 INTERRUPTIBLE);

	} else {
		char *kdata;

		res = 0;

		kdata = Novfs_Malloc(len, GFP_KERNEL);
		if (kdata) {
			len -= copy_from_user(kdata, Buffer, len);
			dlist[0].page = NULL;
			dlist[0].offset = kdata;
			dlist[0].len = len;
			dlist[0].rwflag = DLREAD;

			dlist[1].page = NULL;
			dlist[1].offset = &lreply;
			dlist[1].len = sizeof(lreply);
			dlist[1].rwflag = DLWRITE;

			retCode =
			    Queue_Daemon_Command(&cmd, cmdlen, dlist, 2,
						 (void *)&reply, &replylen,
						 INTERRUPTIBLE);

			kfree(kdata);
		}
	}

	DbgPrint("Novfs_Write_File retCode=0x%x reply=0x%p\n", retCode, reply);

	if (!retCode) {
		switch (lreply.Reply.ErrorCode) {
		case 0:
			*Bytes = (size_t) lreply.bytesWritten;
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (res) {
		for (i = 0; i < res; i++) {
			if (dlist[i].page) {
				page_cache_release(dlist[i].page);
			}
		}
	}

	kfree(pages);
	kfree(dlist);

	DbgPrint("Novfs_Write_File *Bytes=0x%x retCode=0x%x\n", *Bytes,
		 retCode);

	return (retCode);
}

/*
 *  Arguments: HANDLE Handle - novfsd file handle
 *             struct page *Page - Page to be written out
 *             session_t SessionId - novfsd session handle
 *
 *  Returns:   0 - Success
 *             -ENOSPC - Out of space on server
 *             -EACCES - Access denied
 *             -EIO - Any other error
 *
 *  Abstract:  Write page to file.
 */
int Novfs_Write_Page(HANDLE Handle, struct page *Page, session_t SessionId)
{
	WRITE_FILE_REQUEST cmd;
	WRITE_FILE_REPLY lreply;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	struct data_list dlst[2];

	DbgPrint
	    ("Novfs_Write_Page: Handle=0x%p Page=0x%p Index=%lu SessionId=0x%llx\n",
	     Handle, Page, Page->index, SessionId);

	dlst[0].page = NULL;
	dlst[0].offset = &lreply;
	dlst[0].len = sizeof(lreply);
	dlst[0].rwflag = DLWRITE;

	dlst[1].page = Page;
	dlst[1].offset = 0;
	dlst[1].len = PAGE_CACHE_SIZE;
	dlst[1].rwflag = DLREAD;

	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.handle = Handle;
	cmd.len = PAGE_CACHE_SIZE;
	cmd.offset = (loff_t) Page->index << PAGE_CACHE_SHIFT;;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, &dlst, 2, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (!retCode) {
		if (reply) {
			memcpy(&lreply, reply, sizeof(lreply));
		}
		switch (lreply.Reply.ErrorCode) {
		case 0:
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Write_Page retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Write_Pages(HANDLE Handle, struct data_list *DList, int DList_Cnt,
		      size_t Bytes, loff_t Offset, session_t SessionId)
{
	WRITE_FILE_REQUEST cmd;
	WRITE_FILE_REPLY lreply;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	DbgPrint
	    ("Novfs_Write_Pages: Handle=0x%p Dlst=0x%p Dlcnt=%d Bytes=%d Offset=%lld SessionId=0x%llx\n",
	     Handle, DList, DList_Cnt, Bytes, Offset, SessionId);

	DList[0].page = NULL;
	DList[0].offset = &lreply;
	DList[0].len = sizeof(lreply);
	DList[0].rwflag = DLWRITE;

	len = Bytes;
	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	if (len) {
		cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
		cmd.Command.SequenceNumber = 0;
		cmd.Command.SessionId = SessionId;

		cmd.handle = Handle;
		cmd.len = len;
		cmd.offset = Offset;

		retCode =
		    Queue_Daemon_Command(&cmd, cmdlen, DList, DList_Cnt,
					 (void *)&reply, &replylen,
					 INTERRUPTIBLE);
		if (!retCode) {
			if (reply) {
				memcpy(&lreply, reply, sizeof(lreply));
			}
			switch (lreply.Reply.ErrorCode) {
			case 0:
				retCode = 0;
				break;

			case NWE_INSUFFICIENT_SPACE:
				retCode = -ENOSPC;
				break;

			case NWE_ACCESS_DENIED:
				retCode = -EACCES;
				break;

			default:
				retCode = -EIO;
				break;
			}
		}
		if (reply) {
			kfree(reply);
		}
	}
	DbgPrint("Novfs_Write_Pages retCode=0x%x\n", retCode);

	return (retCode);
}

int Novfs_Read_Stream(HANDLE ConnHandle, unsigned char * Handle, unsigned char * Buffer,
		      size_t * Bytes, loff_t * Offset, int User,
		      session_t SessionId)
{
	READ_STREAM_REQUEST cmd;
	PREAD_STREAM_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	if ((offsetof(READ_FILE_REPLY, data) + len) > MaxIoSize) {
		len = MaxIoSize - offsetof(READ_FILE_REPLY, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.Command.CommandType = VFS_COMMAND_READ_STREAM;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.connection = ConnHandle;
	memcpy(cmd.handle, Handle, sizeof(cmd.handle));
	cmd.len = len;
	cmd.offset = *Offset;

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("Novfs_Read_Stream: Queue_Daemon_Command 0x%x replylen=%d\n", retCode, replylen);

	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		} else {
			replylen -= offsetof(READ_STREAM_REPLY, data);
			if (replylen > 0) {
				if (User)
					replylen -= copy_to_user(Buffer, reply->data, replylen);
				else
					memcpy(Buffer, reply->data, replylen);
				*Bytes = replylen;
			}
		}
		kfree(reply);
	}

	DbgPrint("Novfs_Read_Stream *Bytes=0x%x retCode=0x%x\n", *Bytes, retCode);

	return (retCode);
}

int Novfs_Write_Stream(HANDLE ConnHandle, unsigned char * Handle, unsigned char * Buffer,
		       size_t * Bytes, loff_t * Offset, session_t SessionId)
{
	PWRITE_STREAM_REQUEST cmd;
	PWRITE_STREAM_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	len = *Bytes;
	cmdlen = len + offsetof(WRITE_STREAM_REQUEST, data);
	*Bytes = 0;

	if (cmdlen > MaxIoSize) {
		cmdlen = MaxIoSize;
		len = cmdlen - offsetof(WRITE_STREAM_REQUEST, data);
	}

	DbgPrint("Novfs_Write_Stream cmdlen=%d len=%d\n", cmdlen, len);

	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		if (Buffer && len) {
			len -= copy_from_user(cmd->data, Buffer, len);
		}

		DbgPrint("Novfs_Write_Stream len=%d\n", len);

		cmd->Command.CommandType = VFS_COMMAND_WRITE_STREAM;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->connection = ConnHandle;
		memcpy(cmd->handle, Handle, sizeof(cmd->handle));
		cmd->len = len;
		cmd->offset = *Offset;

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			switch (reply->Reply.ErrorCode) {
			case 0:
				retCode = 0;
				break;

			case NWE_INSUFFICIENT_SPACE:
				retCode = -ENOSPC;
				break;

			case NWE_ACCESS_DENIED:
				retCode = -EACCES;
				break;

			default:
				retCode = -EIO;
				break;
			}
			DbgPrint
			    ("Novfs_Write_Stream reply->bytesWritten=0x%lx\n",
			     reply->bytesWritten);
			*Bytes = reply->bytesWritten;
			kfree(reply);
		}
		kfree(cmd);
	}
	DbgPrint("Novfs_Write_Stream *Bytes=0x%x retCode=0x%x\n", *Bytes,
		 retCode);

	return (retCode);
}

int Novfs_Close_Stream(HANDLE ConnHandle, unsigned char * Handle, session_t SessionId)
{
	CLOSE_STREAM_REQUEST cmd;
	PCLOSE_STREAM_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_CLOSE_STREAM;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.connection = ConnHandle;
	memcpy(cmd.handle, Handle, sizeof(cmd.handle));

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, 0);
	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int Novfs_Delete(unsigned char * Path, int DirectoryFlag, session_t SessionId)
{
	PDELETE_FILE_REQUEST cmd;
	PDELETE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(DELETE_FILE_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_DELETE_FILE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->isDirectory = DirectoryFlag;
		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			retCode = 0;
			if (reply->Reply.ErrorCode) {
				if ((reply->Reply.ErrorCode & 0xFFFF) == 0x0006) {	/* Access Denied Error */
					retCode = -EACCES;
				} else {
					retCode = -EIO;
				}
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Truncate_File(unsigned char * Path, int PathLen, session_t SessionId)
{
	PTRUNCATE_FILE_REQUEST cmd;
	PTRUNCATE_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	if (StripTrailingDots) {
		if ('.' == Path[PathLen - 1])
			PathLen--;
	}
	cmdlen = offsetof(TRUNCATE_FILE_REQUEST, path) + PathLen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_TRUNCATE_FILE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->pathLen = PathLen;
		memcpy(cmd->path, Path, PathLen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			if (reply->Reply.ErrorCode) {
				retCode = -EIO;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Truncate_File_Ex(HANDLE Handle, loff_t Offset, session_t SessionId)
{
	WRITE_FILE_REQUEST cmd;
	PWRITE_FILE_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;

	DbgPrint("Novfs_Truncate_File_Ex Handle=0x%p Offset=%lld\n", Handle,
		 Offset);

	cmdlen = offsetof(WRITE_FILE_REQUEST, data);

	cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;
	cmd.handle = Handle;
	cmd.len = 0;
	cmd.offset = Offset;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	DbgPrint("Novfs_Truncate_File_Ex retCode=0x%x reply=0x%p\n", retCode,
		 reply);

	if (!retCode) {
		switch (reply->Reply.ErrorCode) {
		case 0:
			retCode = 0;
			break;

		case NWE_INSUFFICIENT_SPACE:
			retCode = -ENOSPC;
			break;

		case NWE_ACCESS_DENIED:
			retCode = -EACCES;
			break;

		case NWE_FILE_IO_LOCKED:
			retCode = -EBUSY;
			break;

		default:
			retCode = -EIO;
			break;
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("Novfs_Truncate_File_Ex retCode=%d\n", retCode);

	return (retCode);
}

int Novfs_Rename_File(int DirectoryFlag, unsigned char * OldName, int OldLen,
		      unsigned char * NewName, int NewLen, session_t SessionId)
{
	RENAME_FILE_REQUEST cmd;
	PRENAME_FILE_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	DbgPrint("Novfs_Rename_File:\n"
		 "   DirectoryFlag: %d\n"
		 "   OldName:       %.*s\n"
		 "   NewName:       %.*s\n"
		 "   SessionId:     0x%llx\n",
		 DirectoryFlag, OldLen, OldName, NewLen, NewName, SessionId);

	cmd.Command.CommandType = VFS_COMMAND_RENAME_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.directoryFlag = DirectoryFlag;

	if (StripTrailingDots) {
		if ('.' == OldName[OldLen - 1])
			OldLen--;
		if ('.' == NewName[NewLen - 1])
			NewLen--;
	}

	cmd.newnameLen = NewLen;
	memcpy(cmd.newname, NewName, NewLen);

	cmd.oldnameLen = OldLen;
	memcpy(cmd.oldname, OldName, OldLen);

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -ENOENT;
		}
		kfree(reply);
	}
	return (retCode);
}

int Novfs_Set_Attr(unsigned char * Path, struct iattr *Attr, session_t SessionId)
{
	PSET_FILE_INFO_REQUEST cmd;
	PSET_FILE_INFO_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(SET_FILE_INFO_REQUEST, path) + pathlen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_SET_FILE_INFO;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;
		cmd->fileInfo.ia_valid = Attr->ia_valid;
		cmd->fileInfo.ia_mode = Attr->ia_mode;
		cmd->fileInfo.ia_uid = Attr->ia_uid;
		cmd->fileInfo.ia_gid = Attr->ia_uid;
		cmd->fileInfo.ia_size = Attr->ia_size;
		cmd->fileInfo.ia_atime = Attr->ia_atime.tv_sec;
		cmd->fileInfo.ia_mtime = Attr->ia_mtime.tv_sec;;
		cmd->fileInfo.ia_ctime = Attr->ia_ctime.tv_sec;;
/*
      cmd->fileInfo.ia_attr_flags = Attr->ia_attr_flags;
*/
		cmd->fileInfo.ia_attr_flags = 0;

		cmd->pathlength = pathlen;
		memcpy(cmd->path, Path, pathlen);

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);
		if (reply) {
			switch (reply->Reply.ErrorCode) {
			case 0:
				retCode = 0;
				break;

			case NWE_PARAM_INVALID:
				retCode = -EINVAL;
				break;

			case NWE_FILE_IO_LOCKED:
				retCode = -EBUSY;
				break;

			default:
				retCode = -EIO;
				break;
			}
			kfree(reply);
		}
		kfree(cmd);
	} else {
		retCode = -ENOMEM;
	}
	return (retCode);
}

int Novfs_Get_File_Cache_Flag(unsigned char * Path, session_t SessionId)
{
	PGET_CACHE_FLAG_REQUEST cmd;
	PGET_CACHE_FLAG_REPLY reply = NULL;
	unsigned long replylen = 0;
	int cmdlen;
	int retCode = 0;
	int pathlen;

	DbgPrint("Novfs_Get_File_Cache_Flag: Path = %s\n", Path);

	if (Path && *Path) {
		pathlen = strlen(Path);
		if (StripTrailingDots) {
			if ('.' == Path[pathlen - 1])
				pathlen--;
		}
		cmdlen = offsetof(GET_CACHE_FLAG_REQUEST, path) + pathlen;
		cmd = (PGET_CACHE_FLAG_REQUEST) Novfs_Malloc(cmdlen, GFP_KERNEL);
		if (cmd) {
			cmd->Command.CommandType = VFS_COMMAND_GET_CACHE_FLAG;
			cmd->Command.SequenceNumber = 0;
			cmd->Command.SessionId = SessionId;
			cmd->pathLen = pathlen;
			memcpy(cmd->path, Path, cmd->pathLen);

			Queue_Daemon_Command(cmd, cmdlen, NULL, 0,
					     (void *)&reply, &replylen,
					     INTERRUPTIBLE);

			if (reply) {

				if (!reply->Reply.ErrorCode) {
					retCode = reply->CacheFlag;
				}

				kfree(reply);
			}
			kfree(cmd);
		}
	}

	DbgPrint("Novfs_Get_File_Cache_Flag: return %d\n", retCode);
	return (retCode);
}

/*
 *  Arguments:
 *      SessionId, file handle, type of lock (read/write or unlock),
 *	    start of lock area, length of lock area
 *
 *  Returns:
 *      0 on success
 *      negative value on error
 *
 *  Abstract:
 *
 *  Notes: lock type - fcntl
 */
int Novfs_Set_File_Lock(session_t SessionId, HANDLE Handle,
			unsigned char fl_type, loff_t fl_start, loff_t fl_len)
{
	PSET_FILE_LOCK_REQUEST cmd;
	PSET_FILE_LOCK_REPLY reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	retCode = -1;

	DbgPrint("Novfs_Set_File_Lock:\n"
		 "   SessionId:     0x%llx\n", SessionId);

	cmd =
	    (PSET_FILE_LOCK_REQUEST) Novfs_Malloc(sizeof(SET_FILE_LOCK_REQUEST),
						  GFP_KERNEL);

	if (cmd) {
		DbgPrint("Novfs_Set_File_Lock 2\n");

		cmd->Command.CommandType = VFS_COMMAND_SET_FILE_LOCK;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->handle = Handle;
		if (F_RDLCK == fl_type) {
			fl_type = 1;	// LockRegionExclusive
		} else if (F_WRLCK == fl_type) {
			fl_type = 0;	// LockRegionShared
		}

		cmd->fl_type = fl_type;
		cmd->fl_start = fl_start;
		cmd->fl_len = fl_len;

		DbgPrint("Novfs_Set_File_Lock 3\n");

		DbgPrint("Novfs_Set_File_Lock: BEGIN dump arguments\n");
		DbgPrint("Novfs_Set_File_Lock: Queue_Daemon_Command %d\n",
			 cmd->Command.CommandType);
		DbgPrint("Novfs_Set_File_Lock: cmd->handle   = 0x%p\n",
			 cmd->handle);
		DbgPrint("Novfs_Set_File_Lock: cmd->fl_type  = %u\n",
			 cmd->fl_type);
		DbgPrint("Novfs_Set_File_Lock: cmd->fl_start = 0x%X\n",
			 cmd->fl_start);
		DbgPrint("Novfs_Set_File_Lock: cmd->fl_len   = 0x%X\n",
			 cmd->fl_len);
		DbgPrint
		    ("Novfs_Set_File_Lock: sizeof(SET_FILE_LOCK_REQUEST) = %u\n",
		     sizeof(SET_FILE_LOCK_REQUEST));
		DbgPrint("Novfs_Set_File_Lock: END dump arguments\n");

		retCode =
		    Queue_Daemon_Command(cmd, sizeof(SET_FILE_LOCK_REQUEST),
					 NULL, 0, (void *)&reply, &replylen,
					 INTERRUPTIBLE);
		DbgPrint("Novfs_Set_File_Lock 4\n");

		if (reply) {
			DbgPrint("Novfs_Set_File_Lock 5, ErrorCode = %X\n",
				 reply->Reply.ErrorCode);

			if (reply->Reply.ErrorCode) {
				retCode = reply->Reply.ErrorCode;
			}
			kfree(reply);
		}

		kfree(cmd);
	}

	DbgPrint("Novfs_Set_File_Lock 6\n");

	return (retCode);
}
