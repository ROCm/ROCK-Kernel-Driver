/*
 * NetWare Redirector for Linux
 * Author: James Turner/Richard Williams
 *
 * This file contains all defined commands.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __NOVFS_COMMANDS_H
#define __NOVFS_COMMANDS_H

#define VFS_COMMAND_GET_CONNECTED_SERVER_LIST   0
#define VFS_COMMAND_GET_SERVER_VOLUME_LIST      1
#define VFS_COMMAND_VERIFY_FILE                 2
#define VFS_COMMAND_OPEN_CONNECTION_BY_ADDR     3
#define VFS_COMMAND_LOGIN_IDENTITY              4
#define VFS_COMMAND_ENUMERATE_DIRECTORY         5
#define VFS_COMMAND_OPEN_FILE                   6
#define VFS_COMMAND_CREATE_FILE                 7
#define VFS_COMMAND_CLOSE_FILE                  8
#define VFS_COMMAND_READ_FILE                   9
#define VFS_COMMAND_WRITE_FILE                  10
#define VFS_COMMAND_DELETE_FILE                 11
#define VFS_COMMAND_CREATE_DIRECOTRY            12
#define VFS_COMMAND_START_ENUMERATE             13
#define VFS_COMMAND_END_ENUMERATE               14
#define VFS_COMMAND_LOGIN_USER                  15
#define VFS_COMMAND_LOGOUT_USER                 16
#define VFS_COMMAND_CREATE_CONTEXT              17
#define VFS_COMMAND_DESTROY_CONTEXT             18
#define VFS_COMMAND_SET_FILE_INFO               19
#define VFS_COMMAND_TRUNCATE_FILE               20
#define VFS_COMMAND_OPEN_CONNECTION_BY_NAME     21
#define VFS_COMMAND_XPLAT_CALL                  22
#define VFS_COMMAND_RENAME_FILE                 23
#define VFS_COMMAND_ENUMERATE_DIRECTORY_EX      24
#define VFS_COMMAND_GETPWUD                     25
#define VFS_COMMAND_ENUM_XCONN                  26
#define VFS_COMMAND_READ_STREAM                 27
#define VFS_COMMAND_WRITE_STREAM                28
#define VFS_COMMAND_CLOSE_STREAM                29
#define VFS_COMMAND_GET_VERSION                 30
#define VFS_COMMAND_SET_MOUNT_PATH              31
#define VFS_COMMAND_GET_USER_SPACE              32
#define VFS_COMMAND_DBG                         33
#define VFS_COMMAND_GET_CACHE_FLAG              34
#define VFS_COMMAND_GET_EXTENDED_ATTRIBUTE		35
#define VFS_COMMAND_LIST_EXTENDED_ATTRIBUTES	36
#define VFS_COMMAND_SET_EXTENDED_ATTRIBUTE		37
#define VFS_COMMAND_SET_FILE_LOCK				38

#define  NWD_ACCESS_QUERY                        0x00000001
#define  NWD_ACCESS_READ                         0x00000002
#define  NWD_ACCESS_WRITE                        0x00000004
#define  NWD_ACCESS_EXECUTE                      0x00000008
#define  NWD_ACCESS_VALID                        0x0000000F

/*
   Share Mode

   A value of zero in a shared mode field specifies the caller
   desires exclusive access to the object.
*/

#define  NWD_SHARE_READ                          0x00000001
#define  NWD_SHARE_WRITE                         0x00000002
#define  NWD_SHARE_DELETE                        0x00000004
#define  NWD_SHARE_VALID                         0x00000007

/*
   Creates a new file.  The create API will fail if the specified
   file already exists.
*/
#define  NWD_DISP_CREATE_NEW                     0x00000001

/*
   Creates a new file.  If the specified file already exists,
   the create API will overwrite the old file and clear the
   existing attributes.
*/
#define  NWD_DISP_CREATE_ALWAYS                  0x00000002

/*
   Opens the file.  The API will fail if the file does not exist.
*/
#define  NWD_DISP_OPEN_EXISTING                  0x00000003

/*
   Opens the file.  If the file does not exist, the API will
   create the file.
*/
#define  NWD_DISP_OPEN_ALWAYS                    0x00000004

/*
   Opens the file.  When the file is opened the API will truncate
   the stream to zero bytes.  The API will fail if the file
   does not exist.
*/
#define  NWD_DISP_TRUNCATE_EXISTING              0x00000005
#define  NWD_DISP_MAXIMUM                        0x00000005

/*
   Open/Create returned information values

   The bottom two bytes of NWD_ACTION are returned
   as a value.  All values are mutually exclusive.
*/

#define  NWD_ACTION_OPENED                       0x00000001
#define  NWD_ACTION_CREATED                      0x00000002

#define  MAX_IO_SIZE							(1024 * 32)

#define  MAX_XATTR_NAME_LEN			255
#define	 MAX_PATH_LENGTH			255
#define  ENOATTR				ENODATA
/*===[ Type definitions ]=================================================*/

/*===[ Function prototypes ]==============================================*/

#pragma pack(push, 1)

/*struct _ncl_string
{
	unsigned int  	type;
	unsigned char 	*buffer;
	unsigned int	len;

} NclString, *PNclString;
*/
struct ncl_string {
	unsigned int type;
	unsigned char *buffer;
	u32 len;
};

struct nwd_string {
	unsigned int type;
	unsigned int len;
	unsigned int boffset;
};

struct novfs_command_request_header {
	unsigned int CommandType;
	unsigned long SequenceNumber;
	struct novfs_schandle SessionId;

};

struct novfs_command_reply_header {
	unsigned long Sequence_Number;
	unsigned int ErrorCode;

};


struct novfs_delete_file_request {
	struct novfs_command_request_header Command;
	unsigned int isDirectory;
	unsigned int pathlength;
	unsigned char path[1];
};

struct novfs_delete_file_reply {
	struct novfs_command_reply_header Reply;
};

struct novfs_get_connected_server_list {
	struct novfs_command_request_header Command;
};

struct novfs_get_connected_server_list_reply {
	struct novfs_command_reply_header Reply;
	unsigned char List[1];
};

struct novfs_get_connected_server_list_request_ex {
	struct novfs_command_request_header Command;
};

struct novfs_get_connected_server_list_reply_ex {

	struct novfs_command_reply_header Reply;
	unsigned int bufferLen;
	unsigned char List[1];

};

struct novfs_get_server_volume_list {
	struct novfs_command_request_header Command;
	unsigned int Length;
	unsigned char Name[1];
};

struct novfs_get_server_volume_list_reply {
	struct novfs_command_reply_header Reply;
	unsigned char List[1];
};

struct novfs_verify_file_request {
	struct novfs_command_request_header Command;
	unsigned int pathLen;
	unsigned char path[1];

};

struct novfs_verify_file_reply {
	struct novfs_command_reply_header Reply;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long fileSize;
	unsigned int fileMode;

};

struct novfs_begin_enumerate_directory_request {
	struct novfs_command_request_header Command;
	unsigned int pathLen;
	unsigned char path[1];

};

struct novfs_begin_enumerate_directory_reply {
	struct novfs_command_reply_header Reply;
	void *enumerateHandle;

};

struct novfs_end_enumerate_directory_request {
	struct novfs_command_request_header Command;
	void *enumerateHandle;

};

struct novfs_end_enumerate_directory_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_enumerate_directory_request {
	struct novfs_command_request_header Command;
	void *enumerateHandle;
	unsigned int pathLen;
	unsigned char path[1];

};

struct novfs_enumerate_directory_reply {
	struct novfs_command_reply_header Reply;
	void *enumerateHandle;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long size;
	unsigned int mode;
	unsigned int nameLen;
	unsigned char name[1];

};

struct novfs_enumerate_directory_ex_request {
	struct novfs_command_request_header Command;
	void *enumerateHandle;
	unsigned int pathLen;
	unsigned char path[1];

};

struct novfs_enumerate_directory_ex_data {
	unsigned int length;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long size;
	unsigned int mode;
	unsigned int nameLen;
	unsigned char name[1];

};

struct novfs_enumerate_directory_ex_reply {
	struct novfs_command_reply_header Reply;
	void *enumerateHandle;
	unsigned int enumCount;

};

struct novfs_open_file_request {
	struct novfs_command_request_header Command;
	unsigned int access;	/* File Access */
	unsigned int mode;	/* Sharing Mode */
	unsigned int disp;	/* Create Disposition */
	unsigned int pathLen;
	unsigned char path[1];

};

struct novfs_open_file_reply {
	struct novfs_command_reply_header Reply;
	void *handle;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned int attributes;
	loff_t size;

};

struct novfs_create_file_request {

	struct novfs_command_request_header Command;
	unsigned int pathlength;
	unsigned char path[1];

};

struct novfs_create_file_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_close_file_request {
	struct novfs_command_request_header Command;
	void *handle;

};

struct novfs_close_file_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_read_file_request {
	struct novfs_command_request_header Command;
	void *handle;
	loff_t offset;
	size_t len;

};

struct novfs_read_file_reply {
	struct novfs_command_reply_header Reply;
	unsigned long long bytesRead;
	unsigned char data[1];

};

struct novfs_write_file_request {
	struct novfs_command_request_header Command;
	void *handle;
	loff_t offset;
	size_t len;
	unsigned char data[1];

};

struct novfs_write_file_reply {
	struct novfs_command_reply_header Reply;
	unsigned long long bytesWritten;
};

struct novfs_read_stream_request {
	struct novfs_command_request_header Command;
	void *connection;
	unsigned char handle[6];
	loff_t offset;
	size_t len;
};

struct novfs_read_stream_reply {
	struct novfs_command_reply_header Reply;
	size_t bytesRead;
	unsigned char data[1];
};

struct novfs_write_stream_request {
	struct novfs_command_request_header Command;
	void *connection;
	unsigned char handle[6];
	loff_t offset;
	size_t len;
	unsigned char data[1];
};

struct novfs_write_stream_reply {
	struct novfs_command_reply_header Reply;
	size_t bytesWritten;
};

struct novfs_close_stream_request {
	struct novfs_command_request_header Command;
	void *connection;
	unsigned char handle[6];
};

struct novfs_close_stream_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_login_user_request {
	struct novfs_command_request_header Command;
	unsigned int srvNameType;
	unsigned int serverLength;
	unsigned int serverOffset;
	unsigned int usrNameType;
	unsigned int userNameLength;
	unsigned int userNameOffset;
	unsigned int pwdNameType;
	unsigned int passwordLength;
	unsigned int passwordOffset;

};

struct novfs_login_user_reply {
	struct novfs_command_reply_header Reply;
	unsigned int connectionHandle;
	void *loginIdentity;

};

struct novfs_logout_request {
	struct novfs_command_request_header Command;
	unsigned int length;
	unsigned char Name[1];

};

struct novfs_logout_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_create_context_request {
	struct novfs_command_request_header Command;

};

struct novfs_create_context_reply {
	struct novfs_command_reply_header Reply;
	struct novfs_schandle SessionId;

};

struct novfs_destroy_context_request {
	struct novfs_command_request_header Command;

};

struct novfs_destroy_context_reply {
	struct novfs_command_reply_header Reply;

};

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */
#ifndef ATTR_MODE
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512	/* Not a change, but a change it */
#define ATTR_ATTR_FLAG	1024
#endif

struct novfs_lnx_file_info {
	unsigned int ia_valid;
	unsigned int ia_mode;
	uid_t ia_uid;
	gid_t ia_gid;
	loff_t ia_size;
	time_t ia_atime;
	time_t ia_mtime;
	time_t ia_ctime;
	unsigned int ia_attr_flags;
};

struct novfs_set_file_info_request {
	struct novfs_command_request_header Command;
	struct novfs_lnx_file_info fileInfo;
	unsigned int pathlength;
	char path[1];
};

struct novfs_set_file_info_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_truncate_file_request {
	struct novfs_command_request_header Command;
	unsigned int pathLen;
	char path[1];

};

struct novfs_truncate_file_reply {
	struct novfs_command_reply_header Reply;

};

struct novfs_getpwuid_request {
	struct novfs_command_request_header Command;
	unsigned int uid;
};

struct novfs_getpwuid_reply {
	struct novfs_command_reply_header Reply;
	unsigned char UserName[1];
};

struct novfs_get_version_request {
	struct novfs_command_request_header Command;
};

struct novfs_get_version_reply {
	struct novfs_command_reply_header Reply;
	unsigned char Version[1];
};

struct novfs_set_mount_path {
	struct novfs_command_request_header Command;
	unsigned int PathLength;
	unsigned char Path[1];
};

struct novfs_set_mount_path_reply {
	struct novfs_command_reply_header Reply;
};

struct novfs_get_user_space {
	struct novfs_command_request_header Command;
};

struct novfs_get_user_space_reply {
	struct novfs_command_reply_header Reply;
	uint64_t TotalSpace;
	uint64_t FreeSpace;
	uint64_t TotalEnties;
	uint64_t FreeEnties;
};

struct novfs_xplat_call_request {
	struct novfs_command_request_header Command;
	unsigned int NwcCommand;
	unsigned long dataLen;
	unsigned char data[1];

};

struct novfs_xplat_call_reply {
	struct novfs_command_reply_header Reply;
	unsigned long dataLen;
	unsigned char data[1];

};

/* XPlat NWC structures used by the daemon */

struct nwd_open_conn_by_name {
	void *ConnHandle;
	unsigned int nameLen;
	unsigned int oName;	/* Ofset to the Name */
	unsigned int serviceLen;
	unsigned int oServiceType;	/* Offset to service Type; */
	unsigned int uConnFlags;
	unsigned int uTranType;
	void *newConnHandle;

};

struct nwd_tran_addr {
	unsigned int uTransportType;
	unsigned int uAddressLength;
	unsigned int oAddress;

};

struct nwd_open_conn_by_addr {
	void *ConnHandle;
	unsigned int oServiceType;
	unsigned int uConnFlags;
	struct nwd_tran_addr TranAddr;

};

struct nwd_close_conn {
	void *ConnHandle;

};

struct nwd_ncp_req {
	void *ConnHandle;
	unsigned int replyLen;
	unsigned int requestLen;
	unsigned int function;
/*	unsigned int 	subFunction; */
/*	unsigned int 	verb; */
	unsigned int flags;
	unsigned char data[1];

};

struct nwd_ncp_rep {
	unsigned int replyLen;
	unsigned char data[1];

};

struct nwc_auth_wid {
	void *ConnHandle;
	u32 AuthenticationId;

};

struct nwc_unauthenticate {
	void *ConnHandle;
	unsigned int AuthenticationId;

};

struct nwc_lisc_id {
	void *ConnHandle;

};

struct nwc_unlic_conn {
	void *ConnHandle;

};

struct nwd_get_id_info {
	u32 AuthenticationId;
	unsigned int AuthType;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int IdentityFlags;
	unsigned int domainLen;
	unsigned int pDomainNameOffset;
	unsigned int objectLen;
	unsigned int pObjectNameOffset;

};

struct nwc_lo_id {
	u32 AuthenticationId;

};

struct novfs_rename_file_request {
	struct novfs_command_request_header Command;
	int directoryFlag;
	unsigned int newnameLen;
	unsigned char newname[256];
	unsigned int oldnameLen;
	unsigned char oldname[256];
};

struct novfs_rename_file_reply {
	struct novfs_command_reply_header Reply;

};

struct nwd_server_version {
	unsigned int uMajorVersion;
	unsigned short int uMinorVersion;
	unsigned short int uRevision;
};


#define	MAX_ADDRESS_LENGTH	32

struct tagNwdTranAddrEx {
	unsigned int uTransportType;
	unsigned int uAddressLength;
	unsigned char Buffer[MAX_ADDRESS_LENGTH];

};

struct __NWD_CONN_INFO {
	unsigned int uInfoVersion;
	unsigned int uAuthenticationState;
	unsigned int uBroadcastState;
	u32 uConnectionReference;
	unsigned int pTreeNameOffset;
/*	unsigned int		pWorkGroupIdOffset;  Not used */
	unsigned int uSecurityState;
	unsigned int uConnectionNumber;
	unsigned int uUserId;
	unsigned int pServerNameOffset;
	unsigned int uNdsState;
	unsigned int uMaxPacketSize;
	unsigned int uLicenseState;
	unsigned int uPublicState;
	unsigned int bcastState;
	unsigned int pServiceTypeOffset;
	unsigned int uDistance;
	u32 uAuthId;
	unsigned int uDisconnected;
	struct nwd_server_version ServerVersion;
	struct nwd_tran_addr TranAddress;
};

struct nwd_conn_info {
	void *ConnHandle;
	unsigned int uInfoLevel;
	unsigned int uInfoLength;
};

struct nwd_open_conn_by_ref {
	void *uConnReference;
	unsigned int uConnFlags;
	void *ConnHandle;

};

struct nwd_get_reqversion {
	unsigned int uMajorVersion;
	unsigned int uMinorVersion;
	unsigned int uRevision;

};

struct nwd_scan_conn_info {
	unsigned int uScanIndex;
	unsigned int uScanInfoLevel;
	unsigned int uScanInfoLen;
	unsigned int uScanConnInfoOffset;
	unsigned int uScanFlags;
	unsigned int uReturnInfoLevel;
	unsigned int uReturnInfoLength;
	unsigned int uConnectionReference;
	unsigned int uReturnConnInfoOffset;

};

struct nwd_get_pref_ds_tree {
	unsigned int uTreeLength;
	unsigned int DsTreeNameOffset;

};

struct nwd_set_pref_ds_tree {
	unsigned int uTreeLength;
	unsigned int DsTreeNameOffset;

};

struct nwd_set_def_name_ctx {
	unsigned int uTreeLength;
	unsigned int TreeOffset;
	unsigned int uNameLength;
	unsigned int NameContextOffset;

};

struct nwd_get_def_name_ctx {
	unsigned int uTreeLength;
	unsigned int TreeOffset;
	unsigned int uNameLength;
	unsigned int NameContextOffset;

};

struct nwd_get_tree_monitored_conn_ref {
	struct nwd_string TreeName;
	void *uConnReference;

};

struct nwd_enum_ids {
	unsigned int Iterator;
	unsigned int domainNameLen;
	unsigned int domainNameOffset;
	unsigned int AuthType;
	unsigned int objectNameLen;
	unsigned int objectNameOffset;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int IdentityFlags;
	u32 AuthenticationId;

};

struct nwd_change_key {
	unsigned int domainNameOffset;
	unsigned int domainNameLen;
	unsigned int AuthType;
	unsigned int objectNameOffset;
	unsigned int objectNameLen;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int verifyPasswordOffset;
	unsigned int verifyPasswordLen;
	unsigned int newPasswordOffset;
	unsigned int newPasswordLen;

};

struct nwd_set_primary_conn {
	void *ConnHandle;

};

struct nwd_get_bcast_notification {
	unsigned int uMessageFlags;
	void *uConnReference;
	unsigned int messageLen;
	char message[1];

};

struct nwd_set_conn_info {
	void *ConnHandle;
	unsigned int uInfoLevel;
	unsigned int uInfoLength;
	unsigned int offsetConnInfo;

};

struct novfs_debug_request {
	struct novfs_command_request_header Command;
	int cmdlen;
	char dbgcmd[1];

};

struct novfs_debug_reply {
	struct novfs_command_reply_header Reply;

};

struct nwd_set_key {
	void *ConnHandle;
	unsigned int AuthenticationId;
	unsigned int objectNameLen;
	unsigned int objectNameOffset;
	unsigned short int ObjectType;
	unsigned int newPasswordLen;
	unsigned int newPasswordOffset;

};

struct nwd_verify_key {
	unsigned int AuthType;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int domainNameLen;
	unsigned int domainNameOffset;
	unsigned int objectNameLen;
	unsigned int objectNameOffset;
	unsigned int verifyPasswordLen;
	unsigned int verifyPasswordOffset;

};

struct novfs_get_cache_flag {
	struct novfs_command_request_header Command;
	int pathLen;
	unsigned char path[0];

};

struct novfs_get_cache_flag_reply {
	struct novfs_command_reply_header Reply;
	int CacheFlag;

};

struct novfs_xa_list_reply {
	struct novfs_command_reply_header Reply;
	unsigned char *pData;

};

struct novfs_xa_get_request {
	struct novfs_command_request_header Command;
	unsigned int pathLen;
	unsigned int nameLen;
	unsigned char data[1];	//hold path, attribute name

};

struct novfs_xa_get_reply {
	struct novfs_command_reply_header Reply;
	unsigned char *pData;

};

struct novfs_xa_set_request {
	struct novfs_command_request_header Command;
	unsigned int TtlWriteDataSize;
	unsigned int WritePosition;
	int flags;
	unsigned int pathLen;
	unsigned int nameLen;
	unsigned int valueLen;
	unsigned char data[1];	//hold path, attribute name, value data

};

struct novfs_xa_set_reply {
	struct novfs_command_reply_header Reply;
	unsigned char *pData;

};

struct novfs_set_file_lock_request {
	struct novfs_command_request_header Command;
	void *handle;
	unsigned char fl_type;
	loff_t fl_start;
	loff_t fl_len;

};

struct novfs_set_file_lock_reply {
	struct novfs_command_reply_header Reply;

};


struct novfs_scope_list{
	struct list_head ScopeList;
	struct novfs_schandle ScopeId;
	struct novfs_schandle SessionId;
	pid_t ScopePid;
	struct task_struct *ScopeTask;
	unsigned int ScopeHash;
	uid_t ScopeUid;
	uint64_t ScopeUSize;
	uint64_t ScopeUFree;
	uint64_t ScopeUTEnties;
	uint64_t ScopeUAEnties;
	int ScopeUserNameLength;
	unsigned char ScopeUserName[32];
};

#pragma pack(pop)

#endif	/* __NOVFS_COMMANDS_H */
