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

static ssize_t novfs_tree_read(struct file * file, char *buf, size_t len, loff_t * off);
extern struct dentry_operations novfs_dentry_operations;

static struct file_operations novfs_tree_operations = {
      read:novfs_tree_read,
};

/*
 * StripTrailingDots was added because some apps will
 * try and create a file name with a trailing dot.  NetWare
 * doesn't like this and will return an error.
 */
static int StripTrailingDots = 1;

int novfs_get_alltrees(struct dentry *parent)
{
	unsigned char *p;
	struct novfs_command_reply_header * reply = NULL;
	unsigned long replylen = 0;
	struct novfs_command_request_header cmd;
	int retCode;
	struct dentry *entry;
	struct qstr name;
	struct inode *inode;

	cmd.CommandType = 0;
	cmd.SequenceNumber = 0;
//sg ???   cmd.SessionId = 0x1234;
	SC_INITIALIZE(cmd.SessionId);

	DbgPrint("novfs_get_alltrees:\n");

	retCode = Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);
	DbgPrint("novfs_get_alltrees: relpy=0x%p replylen=%d\n", reply,
		 replylen);
	if (reply) {
		novfs_dump(replylen, reply);
		if (!reply->ErrorCode
		    && (replylen > sizeof(struct novfs_command_reply_header))) {
			p = (char *)reply + 8;
			while (*p) {
				DbgPrint("novfs_get_alltrees: %s\n", p);
				name.len = strlen(p);
				name.name = p;
				name.hash = full_name_hash(name.name, name.len);
				entry = d_lookup(parent, &name);
				if (NULL == entry) {
					DbgPrint("novfs_get_alltrees: adding %s\n", p);
					entry = d_alloc(parent, &name);
					if (entry) {
						entry->d_op = &novfs_dentry_operations;
						inode = novfs_get_inode(parent->d_sb, S_IFREG | 0400, 0, 0, 0, &name);
						if (inode) {
							inode->i_fop = &novfs_tree_operations;
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

static ssize_t novfs_tree_read(struct file * file, char *buf, size_t len, loff_t * off)
{
	if (file->f_pos != 0) {
		return (0);
	}
	if (copy_to_user(buf, "Tree\n", 5)) {
		return (0);
	}
	return (5);
}

int novfs_get_servers(unsigned char ** ServerList, struct novfs_schandle SessionId)
{
	struct novfs_get_connected_server_list req;
	struct novfs_get_connected_server_list_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;

	*ServerList = NULL;

	req.Command.CommandType = VFS_COMMAND_GET_CONNECTED_SERVER_LIST;
	req.Command.SessionId = SessionId;

	retCode =
	    Queue_Daemon_Command(&req, sizeof(req), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("novfs_Get_Connected_Server_List: reply\n");
		replylen -= sizeof(struct novfs_command_reply_header);
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

int novfs_get_vols(struct qstr *Server, unsigned char ** VolumeList,
				 struct novfs_schandle SessionId)
{
	struct novfs_get_server_volume_list *req;
	struct novfs_get_server_volume_list_reply *reply = NULL;
	unsigned long replylen = 0, reqlen;
	int retCode;

	*VolumeList = NULL;
	reqlen = sizeof(struct novfs_get_server_volume_list) + Server->len;
	req = kmalloc(reqlen, GFP_KERNEL);
	if (!req)
		return -ENOMEM;
	req->Command.CommandType = VFS_COMMAND_GET_SERVER_VOLUME_LIST;
	req->Length = Server->len;
	memcpy(req->Name, Server->name, Server->len);
	req->Command.SessionId = SessionId;

	retCode =
		Queue_Daemon_Command(req, reqlen, NULL, 0, (void *)&reply,
				&replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("novfs_Get_Server_Volume_List: reply\n");
		novfs_dump(replylen, reply);
		replylen -= sizeof(struct novfs_command_reply_header);

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
	return (retCode);
}

int novfs_get_file_info(unsigned char * Path, struct novfs_entry_info * Info, struct novfs_schandle SessionId)
{
	struct novfs_verify_file_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_verify_file_request * cmd;
	int cmdlen;
	int retCode = -ENOENT;
	int pathlen;

	DbgPrint("novfs_Get_File_Info: Path = %s\n", Path);

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
		cmdlen = offsetof(struct novfs_verify_file_request,path) + pathlen;
		cmd = kmalloc(cmdlen, GFP_KERNEL);
		if (cmd) {
			cmd->Command.CommandType = VFS_COMMAND_VERIFY_FILE;
			cmd->Command.SequenceNumber = 0;
			cmd->Command.SessionId = SessionId;
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
					DbgPrint
					    ("novfs_Get_File_Info: replylen=%d sizeof(VERIFY_FILE_REPLY)=%d\n",
					     replylen,
					     sizeof(struct novfs_verify_file_reply));
					if (replylen >
					    sizeof(struct novfs_verify_file_reply)) {
						unsigned int *lp =
						    &reply->fileMode;
						lp++;
						DbgPrint
						    ("novfs_Get_File_Info: extra data 0x%x\n",
						     *lp);
						Info->mtime.tv_nsec = *lp;
					}
					retCode = 0;
				}

				kfree(reply);
			}
			kfree(cmd);
		}
	}

	DbgPrint("novfs_Get_File_Info: return 0x%x\n", retCode);
	return (retCode);
}

int novfs_getx_file_info(char *Path, const char *Name, char *buffer,
			 ssize_t buffer_size, ssize_t * dataLen,
			 struct novfs_schandle SessionId)
{
	struct novfs_xa_get_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_xa_get_request *cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int namelen = strlen(Name);
	int pathlen = strlen(Path);

	DbgPrint
	    ("novfs_GetX_File_Info xattr: Path = %s, pathlen = %i, Name = %s, namelen = %i\n",
	     Path, pathlen, Name, namelen);

	if (namelen > MAX_XATTR_NAME_LEN) {
		return ENOATTR;
	}

	cmdlen = offsetof(struct novfs_xa_get_request, data) + pathlen + 1 + namelen + 1;	// two '\0'
	cmd = (struct novfs_xa_get_request *) kmalloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_GET_EXTENDED_ATTRIBUTE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->pathLen = pathlen;
		memcpy(cmd->data, Path, cmd->pathLen + 1);	//+ '\0'

		cmd->nameLen = namelen;
		memcpy(cmd->data + cmd->pathLen + 1, Name, cmd->nameLen + 1);

		DbgPrint("novfs_GetX_File_Info xattr: PXA_GET_REQUEST BEGIN\n");
		DbgPrint
		    ("novfs_GetX_File_Info xattr: Queue_Daemon_Command %d\n",
		     cmd->Command.CommandType);
		DbgPrint("novfs_GetX_File_Info xattr: Command.SessionId = %d\n",
			 cmd->Command.SessionId);
		DbgPrint("novfs_GetX_File_Info xattr: pathLen = %d\n",
			 cmd->pathLen);
		DbgPrint("novfs_GetX_File_Info xattr: Path = %s\n", cmd->data);
		DbgPrint("novfs_GetX_File_Info xattr: nameLen = %d\n",
			 cmd->nameLen);
		DbgPrint("novfs_GetX_File_Info xattr: name = %s\n",
			 (cmd->data + cmd->pathLen + 1));
		DbgPrint("novfs_GetX_File_Info xattr: PXA_GET_REQUEST END\n");

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint
				    ("novfs_GetX_File_Info xattr: reply->Reply.ErrorCode=%d, %X\n",
				     reply->Reply.ErrorCode,
				     reply->Reply.ErrorCode);
				DbgPrint
				    ("novfs_GetX_File_Info xattr: replylen=%d\n",
				     replylen);

				//0xC9 = EA not found (C9), 0xD1 = EA access denied
				if ((reply->Reply.ErrorCode == 0xC9)
				    || (reply->Reply.ErrorCode == 0xD1)) {
					retCode = -ENOATTR;
				} else {
					retCode = -ENOENT;
				}
			} else {

				*dataLen =
				    replylen - sizeof(struct novfs_command_reply_header);
				DbgPrint
				    ("novfs_GetX_File_Info xattr: replylen=%u, dataLen=%u\n",
				     replylen, *dataLen);

				if (buffer_size >= *dataLen) {
					DbgPrint
					    ("novfs_GetX_File_Info xattr: copying to buffer from &reply->pData\n");
					memcpy(buffer, &reply->pData, *dataLen);

					retCode = 0;
				} else {
					DbgPrint
					    ("novfs_GetX_File_Info xattr: (!!!) buffer is smaller then reply\n");
					retCode = -ERANGE;
				}
				DbgPrint
				    ("novfs_GetX_File_Info xattr: /dumping buffer\n");
				novfs_dump(*dataLen, buffer);
				DbgPrint
				    ("novfs_GetX_File_Info xattr: \\after dumping buffer\n");
			}

			kfree(reply);
		} else {
			DbgPrint("novfs_GetX_File_Info xattr: reply = NULL\n");
		}
		kfree(cmd);

	}

	return retCode;
}

int novfs_setx_file_info(char *Path, const char *Name, const void *Value,
			 unsigned long valueLen, unsigned long *bytesWritten,
			 int flags, struct novfs_schandle SessionId)
{
	struct novfs_xa_set_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_xa_set_request *cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int namelen = strlen(Name);
	int pathlen = strlen(Path);

	DbgPrint
	    ("novfs_SetX_File_Info xattr: Path = %s, pathlen = %i, Name = %s, namelen = %i, value len = %u\n",
	     Path, pathlen, Name, namelen, valueLen);

	if (namelen > MAX_XATTR_NAME_LEN) {
		return ENOATTR;
	}

	cmdlen = offsetof(struct novfs_xa_set_request, data) + pathlen + 1 + namelen + 1 + valueLen;
	cmd = (struct novfs_xa_set_request *) kmalloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_SET_EXTENDED_ATTRIBUTE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

		cmd->flags = flags;
		cmd->pathLen = pathlen;
		memcpy(cmd->data, Path, cmd->pathLen + 1);	//+ '\0'

		cmd->nameLen = namelen;
		memcpy(cmd->data + cmd->pathLen + 1, Name, cmd->nameLen + 1);

		cmd->valueLen = valueLen;
		memcpy(cmd->data + cmd->pathLen + 1 + cmd->nameLen + 1, Value,
		       valueLen);

		DbgPrint("novfs_SetX_File_Info xattr: PXA_SET_REQUEST BEGIN\n");
		DbgPrint
		    ("novfs_SetX_File_Info xattr: Queue_Daemon_Command %d\n",
		     cmd->Command.CommandType);
		DbgPrint("novfs_SetX_File_Info xattr: Command.SessionId = %d\n",
			 cmd->Command.SessionId);
		DbgPrint("novfs_SetX_File_Info xattr: pathLen = %d\n",
			 cmd->pathLen);
		DbgPrint("novfs_SetX_File_Info xattr: Path = %s\n", cmd->data);
		DbgPrint("novfs_SetX_File_Info xattr: nameLen = %d\n",
			 cmd->nameLen);
		DbgPrint("novfs_SetX_File_Info xattr: name = %s\n",
			 (cmd->data + cmd->pathLen + 1));
		novfs_dump(valueLen < 16 ? valueLen : 16, (char *)Value);

		DbgPrint("novfs_SetX_File_Info xattr: PXA_SET_REQUEST END\n");

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint
				    ("novfs_SetX_File_Info xattr: reply->Reply.ErrorCode=%d, %X\n",
				     reply->Reply.ErrorCode,
				     reply->Reply.ErrorCode);
				DbgPrint
				    ("novfs_SetX_File_Info xattr: replylen=%d\n",
				     replylen);

				retCode = -reply->Reply.ErrorCode;	//-ENOENT;
			} else {

				DbgPrint
				    ("novfs_SetX_File_Info xattr: replylen=%u, real len = %u\n",
				     replylen,
				     replylen - sizeof(struct novfs_command_reply_header));
				memcpy(bytesWritten, &reply->pData,
				       replylen - sizeof(struct novfs_command_reply_header));

				retCode = 0;
			}

			kfree(reply);
		} else {
			DbgPrint("novfs_SetX_File_Info xattr: reply = NULL\n");
		}
		kfree(cmd);

	}

	return retCode;
}

int novfs_listx_file_info(char *Path, char *buffer, ssize_t buffer_size,
			  ssize_t * dataLen, struct novfs_schandle SessionId)
{
	struct novfs_xa_list_reply *reply = NULL;
	unsigned long replylen = 0;
	struct novfs_verify_file_request *cmd;
	int cmdlen;
	int retCode = -ENOENT;

	int pathlen = strlen(Path);
	DbgPrint("novfs_ListX_File_Info xattr: Path = %s, pathlen = %i\n", Path,
		 pathlen);

	*dataLen = 0;
	cmdlen = offsetof(struct novfs_verify_file_request, path) + pathlen;
	cmd = (struct novfs_verify_file_request *) kmalloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_LIST_EXTENDED_ATTRIBUTES;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;
		cmd->pathLen = pathlen;
		memcpy(cmd->path, Path, cmd->pathLen + 1);	//+ '\0'
		DbgPrint
		    ("novfs_ListX_File_Info xattr: PVERIFY_FILE_REQUEST BEGIN\n");
		DbgPrint
		    ("novfs_ListX_File_Info xattr: Queue_Daemon_Command %d\n",
		     cmd->Command.CommandType);
		DbgPrint
		    ("novfs_ListX_File_Info xattr: Command.SessionId = %d\n",
		     cmd->Command.SessionId);
		DbgPrint("novfs_ListX_File_Info xattr: pathLen = %d\n",
			 cmd->pathLen);
		DbgPrint("novfs_ListX_File_Info xattr: Path = %s\n", cmd->path);
		DbgPrint
		    ("novfs_ListX_File_Info xattr: PVERIFY_FILE_REQUEST END\n");

		retCode =
		    Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
					 &replylen, INTERRUPTIBLE);

		if (reply) {

			if (reply->Reply.ErrorCode) {
				DbgPrint
				    ("novfs_ListX_File_Info xattr: reply->Reply.ErrorCode=%d, %X\n",
				     reply->Reply.ErrorCode,
				     reply->Reply.ErrorCode);
				DbgPrint
				    ("novfs_ListX_File_Info xattr: replylen=%d\n",
				     replylen);

				retCode = -ENOENT;
			} else {
				*dataLen =
				    replylen - sizeof(struct novfs_command_reply_header);
				DbgPrint
				    ("novfs_ListX_File_Info xattr: replylen=%u, dataLen=%u\n",
				     replylen, *dataLen);

				if (buffer_size >= *dataLen) {
					DbgPrint
					    ("novfs_ListX_File_Info xattr: copying to buffer from &reply->pData\n");
					memcpy(buffer, &reply->pData, *dataLen);
				} else {
					DbgPrint
					    ("novfs_ListX_File_Info xattr: (!!!) buffer is smaller then reply\n");
					retCode = -ERANGE;
				}
				DbgPrint
				    ("novfs_ListX_File_Info xattr: /dumping buffer\n");
				novfs_dump(*dataLen, buffer);
				DbgPrint
				    ("novfs_ListX_File_Info xattr: \\after dumping buffer\n");

				retCode = 0;
			}

			kfree(reply);
		} else {
			DbgPrint("novfs_ListX_File_Info xattr: reply = NULL\n");
		}
		kfree(cmd);

	}

	return retCode;
}

static int begin_directory_enumerate(unsigned char * Path, int PathLen, void ** EnumHandle,
			      struct novfs_schandle SessionId)
{
	struct novfs_begin_enumerate_directory_request *cmd;
	struct novfs_begin_enumerate_directory_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	*EnumHandle = 0;

	cmdlen = offsetof(struct
			novfs_begin_enumerate_directory_request, path) + PathLen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_START_ENUMERATE;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = SessionId;

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

static int end_directory_enumerate(void *EnumHandle, struct novfs_schandle SessionId)
{
	struct novfs_end_enumerate_directory_request cmd;
	struct novfs_end_enumerate_directory_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	cmd.Command.CommandType = VFS_COMMAND_END_ENUMERATE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

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

static int directory_enumerate_ex(void ** EnumHandle, struct novfs_schandle SessionId, int *Count,
			   struct novfs_entry_info **PInfo, int Interrupt)
{
	struct novfs_enumerate_directory_ex_request cmd;
	struct novfs_enumerate_directory_ex_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	struct novfs_entry_info * info;
	struct novfs_enumerate_directory_ex_data *data;
	int isize;

	if (PInfo)
		*PInfo = NULL;
	*Count = 0;

	cmd.Command.CommandType = VFS_COMMAND_ENUMERATE_DIRECTORY_EX;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

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
		    ((replylen > sizeof(struct novfs_command_reply_header)) &&
		     (reply->enumCount > 0))) {
			DbgPrint("directory_enumerate_ex: isize=%d\n",
				 replylen);
			data =
			    (struct novfs_enumerate_directory_ex_data *) ((char *)reply +
							    sizeof
							    (struct novfs_enumerate_directory_ex_reply));
			isize =
			    replylen - sizeof(struct novfs_enumerate_directory_ex_reply *) -
			    reply->enumCount *
			    offsetof(struct
					    novfs_enumerate_directory_ex_data, name);
			isize +=
			    (reply->enumCount *
			     offsetof(struct novfs_entry_info, name));

			if (PInfo) {
				*PInfo = info = kmalloc(isize, GFP_KERNEL);
				if (*PInfo) {
					DbgPrint
					    ("directory_enumerate_ex1: data=0x%p info=0x%p\n",
					     data, info);
					*Count = reply->enumCount;
					do {
						DbgPrint
						    ("directory_enumerate_ex2: data=0x%p length=%d\n",
						     data);

						info->type = 3;
						info->mode = S_IRWXU;

						if (data->
						    mode &
						    NW_ATTRIBUTE_DIRECTORY) {
							info->mode |= S_IFDIR;
							info->mode |= S_IXUSR;
						} else {
							info->mode |= S_IFREG;
						}

						if (data->
						    mode &
						    NW_ATTRIBUTE_READ_ONLY) {
							info->mode &=
							    ~(S_IWUSR);
						}

						if (data->
						    mode & NW_ATTRIBUTE_EXECUTE)
						{
							info->mode |= S_IXUSR;
						}

						info->uid = current->euid;
						info->gid = current->egid;
						info->size = data->size;
						info->atime.tv_sec =
						    data->lastAccessTime;
						info->atime.tv_nsec = 0;
						info->mtime.tv_sec =
						    data->modifyTime;
						info->mtime.tv_nsec = 0;
						info->ctime.tv_sec =
						    data->createTime;
						info->ctime.tv_nsec = 0;
						info->namelength =
						    data->nameLen;
						memcpy(info->name, data->name,
						       data->nameLen);
						data =
						    (struct novfs_enumerate_directory_ex_data *)
						    & data->name[data->nameLen];
						replylen =
						    (int)((char *)&info->
							  name[info->
							       namelength] -
							  (char *)info);
						DbgPrint
						    ("directory_enumerate_ex3: info=0x%p\n",
						     info);
						novfs_dump(replylen, info);

						info =
						    (struct novfs_entry_info *) & info->
						    name[info->namelength];

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

int novfs_get_dir_listex(unsigned char * Path, void ** EnumHandle, int *Count,
			       struct novfs_entry_info **Info,
			       struct novfs_schandle SessionId)
{
	int retCode = -ENOENT;

	if (Count)
		*Count = 0;
	if (Info)
		*Info = NULL;

	if ((void *) - 1 == *EnumHandle) {
		return (-ENODATA);
	}

	if (0 == *EnumHandle) {
		retCode =
		    begin_directory_enumerate(Path, strlen(Path), EnumHandle,
					      SessionId);
	}

	if (*EnumHandle) {
		retCode =
		    directory_enumerate_ex(EnumHandle, SessionId, Count, Info,
					   INTERRUPTIBLE);
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

int novfs_open_file(unsigned char * Path, int Flags, struct novfs_entry_info * Info,
		void ** Handle,
		struct novfs_schandle SessionId)
{
	struct novfs_open_file_request *cmd;
	struct novfs_open_file_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	*Handle = 0;

	cmdlen = offsetof(struct novfs_open_file_request, path) + pathlen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
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

int novfs_create(unsigned char * Path, int DirectoryFlag, struct novfs_schandle SessionId)
{
	struct novfs_create_file_request *cmd;
	struct novfs_create_file_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(struct novfs_create_file_request, path) + pathlen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
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

int novfs_close_file(void *Handle, struct novfs_schandle SessionId)
{
	struct novfs_close_file_request cmd;
	struct novfs_close_file_reply *reply;
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

int novfs_read_file(void *Handle, unsigned char * Buffer, size_t * Bytes,
		    loff_t * Offset, struct novfs_schandle SessionId)
{
	struct novfs_read_file_request cmd;
	struct novfs_read_file_reply * reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	if (offsetof(struct novfs_read_file_reply, data) + len
			> novfs_max_iosize) {
		len = novfs_max_iosize - offsetof(struct
				novfs_read_file_reply, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.Command.CommandType = VFS_COMMAND_READ_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.handle = Handle;
	cmd.len = len;
	cmd.offset = *Offset;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	DbgPrint("novfs_Read_File: Queue_Daemon_Command 0x%x replylen=%d\n",
		 retCode, replylen);

	if (!retCode) {
		if (reply->Reply.ErrorCode) {
			if (NWE_FILE_IO_LOCKED == reply->Reply.ErrorCode) {
				retCode = -EBUSY;
			} else {
				retCode = -EIO;
			}
		} else {
			replylen -= offsetof(struct
					novfs_read_file_reply, data);

			if (replylen > 0) {
				replylen -=
				    copy_to_user(Buffer, reply->data, replylen);
				*Bytes = replylen;
			}
		}
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("novfs_Read_File *Bytes=0x%x retCode=0x%x\n", *Bytes, retCode);

	return (retCode);
}

int novfs_read_pages(void *Handle, struct novfs_data_list *DList,
		int DList_Cnt, size_t * Bytes, loff_t * Offset,
		struct novfs_schandle SessionId)
{
	struct novfs_read_file_request cmd;
	struct novfs_read_file_reply * reply = NULL;
	struct novfs_read_file_reply lreply;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	DbgPrint
	    ("novfs_Read_Pages: Handle=0x%p Dlst=0x%p Dlcnt=%d Bytes=%d Offset=%lld SessionId=0x%p:%p\n",
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
	DList[0].len = offsetof(struct novfs_read_file_reply, data);
	DList[0].rwflag = DLWRITE;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), DList, DList_Cnt,
				 (void *)&reply, &replylen, INTERRUPTIBLE);

	DbgPrint("novfs_Read_Pages: Queue_Daemon_Command 0x%x\n", retCode);

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
		*Bytes = replylen - offsetof(struct
				novfs_read_file_reply, data);
	}

	if (reply) {
		kfree(reply);
	}

	DbgPrint("novfs_Read_Pages: retCode=0x%x\n", retCode);

	return (retCode);
}

int novfs_write_file(void *Handle, unsigned char * Buffer, size_t * Bytes,
		     loff_t * Offset, struct novfs_schandle SessionId)
{
	struct novfs_write_file_request cmd;
	struct novfs_write_file_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	unsigned long boff;
	struct page **pages;
	struct novfs_data_list *dlist;
	int res = 0, npage, i;
	struct novfs_write_file_reply lreply;

	len = *Bytes;
	cmdlen = offsetof(struct novfs_write_file_request, data);

	*Bytes = 0;

	memset(&lreply, 0, sizeof(lreply));

	DbgPrint("novfs_Write_File cmdlen=%ld len=%ld\n", cmdlen, len);

	if ((cmdlen + len) > novfs_max_iosize) {
		len = novfs_max_iosize - cmdlen;
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}
	cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;
	cmd.handle = Handle;
	cmd.len = len;
	cmd.offset = *Offset;

	DbgPrint("novfs_Write_File cmdlen=%ld len=%ld\n", cmdlen, len);

	npage =
	    (((unsigned long)Buffer & ~PAGE_MASK) + len +
	     (PAGE_SIZE - 1)) >> PAGE_SHIFT;

	dlist = kmalloc(sizeof(struct novfs_data_list) * (npage + 1), GFP_KERNEL);
	if (NULL == dlist) {
		return (-ENOMEM);
	}

	pages = kmalloc(sizeof(struct page *) * npage, GFP_KERNEL);

	if (NULL == pages) {
		kfree(dlist);
		return (-ENOMEM);
	}

	down_read(&current->mm->mmap_sem);

	res = get_user_pages(current, current->mm, (unsigned long)Buffer, npage, 0,	/* read type */
			     0,	/* don't force */
			     pages, NULL);

	up_read(&current->mm->mmap_sem);

	DbgPrint("novfs_Write_File res=%d\n", res);

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

		DbgPrint("novfs_Write_File0: page=0x%p offset=0x%p len=%d\n",
			 dlist[0].page, dlist[0].offset, dlist[0].len);

		boff = dlist[0].len;

		DbgPrint("novfs_Write_File len=%d boff=%d\n", len, boff);

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
			    ("novfs_Write_File%d: page=0x%p offset=0x%p len=%d\n",
			     i, dlist[i].page, dlist[i].offset, dlist[i].len);
		}

		dlist[i].page = NULL;
		dlist[i].offset = &lreply;
		dlist[i].len = sizeof(lreply);
		dlist[i].rwflag = DLWRITE;
		res++;

		DbgPrint("novfs_Write_File Buffer=0x%p boff=0x%x len=%d\n",
			 Buffer, boff, len);

		retCode =
		    Queue_Daemon_Command(&cmd, cmdlen, dlist, res,
					 (void *)&reply, &replylen,
					 INTERRUPTIBLE);

	} else {
		char *kdata;

		res = 0;

		kdata = kmalloc(len, GFP_KERNEL);
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

	DbgPrint("novfs_Write_File retCode=0x%x reply=0x%p\n", retCode, reply);

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

	DbgPrint("novfs_Write_File *Bytes=0x%x retCode=0x%x\n", *Bytes,
		 retCode);

	return (retCode);
}

/*
 *  Arguments: HANDLE Handle - novfsd file handle
 *             struct page *Page - Page to be written out
 *             struct novfs_schandle SessionId - novfsd session handle
 *
 *  Returns:   0 - Success
 *             -ENOSPC - Out of space on server
 *             -EACCES - Access denied
 *             -EIO - Any other error
 *
 *  Abstract:  Write page to file.
 */
int novfs_write_page(void *Handle, struct page *Page, struct novfs_schandle SessionId)
{
	struct novfs_write_file_request cmd;
	struct novfs_write_file_reply lreply;
	struct novfs_write_file_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	struct novfs_data_list dlst[2];

	DbgPrint
	    ("novfs_Write_Page: Handle=0x%p Page=0x%p Index=%lu SessionId=0x%llx\n",
	     Handle, Page, Page->index, SessionId);

	dlst[0].page = NULL;
	dlst[0].offset = &lreply;
	dlst[0].len = sizeof(lreply);
	dlst[0].rwflag = DLWRITE;

	dlst[1].page = Page;
	dlst[1].offset = 0;
	dlst[1].len = PAGE_CACHE_SIZE;
	dlst[1].rwflag = DLREAD;

	cmdlen = offsetof(struct novfs_write_file_request, data);

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

	DbgPrint("novfs_Write_Page retCode=0x%x\n", retCode);

	return (retCode);
}

int novfs_write_pages(void *Handle, struct novfs_data_list *DList, int DList_Cnt,
		      size_t Bytes, loff_t Offset, struct novfs_schandle SessionId)
{
	struct novfs_write_file_request cmd;
	struct novfs_write_file_reply lreply;
	struct novfs_write_file_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	DbgPrint
	    ("novfs_Write_Pages: Handle=0x%p Dlst=0x%p Dlcnt=%d Bytes=%d Offset=%lld SessionId=0x%llx\n",
	     Handle, DList, DList_Cnt, Bytes, Offset, SessionId);

	DList[0].page = NULL;
	DList[0].offset = &lreply;
	DList[0].len = sizeof(lreply);
	DList[0].rwflag = DLWRITE;

	len = Bytes;
	cmdlen = offsetof(struct novfs_write_file_request, data);

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
	DbgPrint("novfs_Write_Pages retCode=0x%x\n", retCode);

	return (retCode);
}

int novfs_read_stream(void *ConnHandle, unsigned char * Handle, u_char * Buffer,
		      size_t * Bytes, loff_t * Offset, int User,
		      struct novfs_schandle SessionId)
{
	struct novfs_read_stream_request cmd;
	struct novfs_read_stream_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0;
	size_t len;

	len = *Bytes;
	*Bytes = 0;

	if (offsetof(struct novfs_read_file_reply, data) + len
			> novfs_max_iosize) {
		len = novfs_max_iosize - offsetof(struct
				novfs_read_file_reply, data);
		len = (len / PAGE_SIZE) * PAGE_SIZE;
	}

	cmd.Command.CommandType = VFS_COMMAND_READ_STREAM;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;

	cmd.connection = ConnHandle;
	memcpy(cmd.handle, Handle, sizeof(cmd.handle));
	cmd.len = len;
	cmd.offset = *Offset;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	DbgPrint("novfs_Read_Stream: Queue_Daemon_Command 0x%x replylen=%d\n",
		 retCode, replylen);

	if (reply) {
		retCode = 0;
		if (reply->Reply.ErrorCode) {
			retCode = -EIO;
		} else {
			replylen -= offsetof(struct
					novfs_read_stream_reply, data);
			if (replylen > 0) {
				if (User) {
					replylen -=
					    copy_to_user(Buffer, reply->data,
							 replylen);
				} else {
					memcpy(Buffer, reply->data, replylen);
				}

				*Bytes = replylen;
			}
		}
		kfree(reply);
	}

	DbgPrint("novfs_Read_Stream *Bytes=0x%x retCode=0x%x\n", *Bytes,
		 retCode);

	return (retCode);
}

int novfs_write_stream(void *ConnHandle, unsigned char * Handle, u_char * Buffer,
		       size_t * Bytes, loff_t * Offset, struct novfs_schandle SessionId)
{
	struct novfs_write_stream_request * cmd;
	struct novfs_write_stream_reply * reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;
	size_t len;

	len = *Bytes;
	cmdlen = len + offsetof(struct novfs_write_stream_request, data);
	*Bytes = 0;

	if (cmdlen > novfs_max_iosize) {
		cmdlen = novfs_max_iosize;
		len = cmdlen - offsetof(struct
				novfs_write_stream_request, data);
	}

	DbgPrint("novfs_Write_Stream cmdlen=%d len=%d\n", cmdlen, len);

	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		if (Buffer && len) {
			len -= copy_from_user(cmd->data, Buffer, len);
		}

		DbgPrint("novfs_Write_Stream len=%d\n", len);

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
			    ("novfs_Write_Stream reply->bytesWritten=0x%lx\n",
			     reply->bytesWritten);
			*Bytes = reply->bytesWritten;
			kfree(reply);
		}
		kfree(cmd);
	}
	DbgPrint("novfs_Write_Stream *Bytes=0x%x retCode=0x%x\n", *Bytes,
		 retCode);

	return (retCode);
}

int novfs_close_stream(void *ConnHandle, unsigned char * Handle, struct novfs_schandle SessionId)
{
	struct novfs_close_stream_request cmd;
	struct novfs_close_stream_reply *reply;
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

int novfs_delete(unsigned char * Path, int DirectoryFlag, struct novfs_schandle SessionId)
{
	struct novfs_delete_file_request *cmd;
	struct novfs_delete_file_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(struct novfs_delete_file_request, path) + pathlen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
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

int novfs_trunc(unsigned char * Path, int PathLen,
		struct novfs_schandle SessionId)
{
	struct novfs_truncate_file_request *cmd;
	struct novfs_truncate_file_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	if (StripTrailingDots) {
		if ('.' == Path[PathLen - 1])
			PathLen--;
	}
	cmdlen = offsetof(struct novfs_truncate_file_request, path)
		+ PathLen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
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

int novfs_trunc_ex(void *Handle, loff_t Offset,
		struct novfs_schandle SessionId)
{
	struct novfs_write_file_request cmd;
	struct novfs_write_file_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode = 0, cmdlen;

	DbgPrint("novfs_Truncate_File_Ex Handle=0x%p Offset=%lld\n", Handle,
		 Offset);

	cmdlen = offsetof(struct novfs_write_file_request, data);

	cmd.Command.CommandType = VFS_COMMAND_WRITE_FILE;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = SessionId;
	cmd.handle = Handle;
	cmd.len = 0;
	cmd.offset = Offset;

	retCode =
	    Queue_Daemon_Command(&cmd, cmdlen, NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);

	DbgPrint("novfs_Truncate_File_Ex retCode=0x%x reply=0x%p\n", retCode,
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

	DbgPrint("novfs_Truncate_File_Ex retCode=%d\n", retCode);

	return (retCode);
}

int novfs_rename_file(int DirectoryFlag, unsigned char * OldName, int OldLen,
		      unsigned char * NewName, int NewLen,
		      struct novfs_schandle SessionId)
{
	struct novfs_rename_file_request cmd;
	struct novfs_rename_file_reply *reply;
	unsigned long replylen = 0;
	int retCode;

	DbgPrint("novfs_Rename_File:\n"
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

int novfs_set_attr(unsigned char * Path, struct iattr *Attr,
		struct novfs_schandle SessionId)
{
	struct novfs_set_file_info_request *cmd;
	struct novfs_set_file_info_reply *reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, pathlen;

	pathlen = strlen(Path);

	if (StripTrailingDots) {
		if ('.' == Path[pathlen - 1])
			pathlen--;
	}

	cmdlen = offsetof(struct novfs_set_file_info_request,path) + pathlen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
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

int novfs_get_file_cache_flag(unsigned char * Path,
		struct novfs_schandle SessionId)
{
	struct novfs_get_cache_flag *cmd;
	struct novfs_get_cache_flag_reply *reply = NULL;
	unsigned long replylen = 0;
	int cmdlen;
	int retCode = 0;
	int pathlen;

	DbgPrint("novfs_Get_File_Cache_Flag: Path = %s\n", Path);

	if (Path && *Path) {
		pathlen = strlen(Path);
		if (StripTrailingDots) {
			if ('.' == Path[pathlen - 1])
				pathlen--;
		}
		cmdlen = offsetof(struct novfs_get_cache_flag, path) +
			pathlen;
		cmd = (struct novfs_get_cache_flag *)
			kmalloc(cmdlen, GFP_KERNEL);
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

	DbgPrint("novfs_Get_File_Cache_Flag: return %d\n", retCode);
	return (retCode);
}

/*
 *  Arguments:
 *      SessionId, file handle, type of lock (read/write or unlock),
 *	    start of lock area, length of lock area
 *
 *  Notes: lock type - fcntl
 */
int novfs_set_file_lock(struct novfs_schandle SessionId, void *Handle,
			unsigned char fl_type, loff_t fl_start, loff_t fl_len)
{
	struct novfs_set_file_lock_request *cmd;
	struct novfs_set_file_lock_reply *reply = NULL;
	unsigned long replylen = 0;
	int retCode;

	retCode = -1;

	DbgPrint("novfs_Set_File_Lock:\n"
		 "   SessionId:     0x%llx\n", SessionId);

	cmd =
	    (struct novfs_set_file_lock_request *) kmalloc(sizeof(struct novfs_set_file_lock_request), GFP_KERNEL);

	if (cmd) {
		DbgPrint("novfs_Set_File_Lock 2\n");

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

		DbgPrint("novfs_Set_File_Lock 3\n");

		DbgPrint("novfs_Set_File_Lock: BEGIN dump arguments\n");
		DbgPrint("novfs_Set_File_Lock: Queue_Daemon_Command %d\n",
			 cmd->Command.CommandType);
		DbgPrint("novfs_Set_File_Lock: cmd->handle   = 0x%p\n",
			 cmd->handle);
		DbgPrint("novfs_Set_File_Lock: cmd->fl_type  = %u\n",
			 cmd->fl_type);
		DbgPrint("novfs_Set_File_Lock: cmd->fl_start = 0x%X\n",
			 cmd->fl_start);
		DbgPrint("novfs_Set_File_Lock: cmd->fl_len   = 0x%X\n",
			 cmd->fl_len);
		DbgPrint
		    ("novfs_Set_File_Lock: sizeof(SET_FILE_LOCK_REQUEST) = %u\n",
		     sizeof(struct novfs_set_file_lock_request));
		DbgPrint("novfs_Set_File_Lock: END dump arguments\n");

		retCode =
		    Queue_Daemon_Command(cmd, sizeof(struct novfs_set_file_lock_request),
					 NULL, 0, (void *)&reply, &replylen,
					 INTERRUPTIBLE);
		DbgPrint("novfs_Set_File_Lock 4\n");

		if (reply) {
			DbgPrint("novfs_Set_File_Lock 5, ErrorCode = %X\n",
				 reply->Reply.ErrorCode);

			if (reply->Reply.ErrorCode) {
				retCode = reply->Reply.ErrorCode;
			}
			kfree(reply);
		}
		kfree(cmd);
	}

	DbgPrint("novfs_Set_File_Lock 6\n");

	return (retCode);
}
