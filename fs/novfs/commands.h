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

#ifndef NWHANDLE
typedef void *NWHANDLE;
#endif

/*typedef struct _ncl_string
{
	unsigned int  	type;
	unsigned char 	*buffer;
	unsigned int	len;

} NclString, *PNclString;
*/
typedef struct _ncl_string {
	unsigned int type;
	unsigned char *buffer;
	u32 len;

} NclString, *PNclString;

typedef struct _nwd_string {
	unsigned int type;
	unsigned int len;
	unsigned int boffset;

} NwdString, *PNwdString;

typedef struct _COMMAND_REQUEST_HEADER {
	unsigned int CommandType;
	unsigned long SequenceNumber;
	struct schandle SessionId;
} COMMAND_REQUEST_HEADER, *PCOMMAND_REQUEST_HEADER;

typedef struct _COMMAND_REPLY_HEADER {
	unsigned long Sequence_Number;
	unsigned int ErrorCode;

} COMMAND_REPLY_HEADER, *PCOMMAND_REPLY_HEADER;

typedef struct _CLOSE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	NWHANDLE FileHandle;
} CLOSE_REQUEST, *PCLOSE_REQUEST;

typedef struct _CLOSE_REPLY {
	COMMAND_REPLY_HEADER Reply;
} CLOSE_REPLY, *PCLOSE_REPLY;

typedef struct _DELETE_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int isDirectory;
	unsigned int pathlength;
	unsigned char path[1];
} DELETE_FILE_REQUEST, *PDELETE_FILE_REQUEST;

typedef struct _DELETE_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;
} DELETE_FILE_REPLY, *PDELETE_FILE_REPLY;

typedef struct _FLUSH_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	NWHANDLE FileHandle;
} FLUSH_REQUEST, *PFLUSH_REQUEST;

typedef struct _FLUSH_REPLY {
	COMMAND_REPLY_HEADER Reply;
} FLUSH_REPLY, *PFLUSH_REPLY;

typedef struct _GET_FILEINFO_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	NWHANDLE FileHandle;
} GET_FILEINFO_REQUEST, *PGET_FILEINFO_REQUEST;

typedef struct _GET_FILEINFO_REPLY {
	COMMAND_REPLY_HEADER Reply;
} GET_FILEINFO_REPLY, *PGET_FILEINFO_REPLY;

typedef struct _GET_CONNECTED_SERVER_LIST_REQUEST {
	COMMAND_REQUEST_HEADER Command;
} GET_CONNECTED_SERVER_LIST_REQUEST, *PGET_CONNECTED_SERVER_LIST_REQUEST;

typedef struct _GET_CONNECTED_SERVER_LIST_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char List[1];
} GET_CONNECTED_SERVER_LIST_REPLY, *PGET_CONNECTED_SERVER_LIST_REPLY;

typedef struct _GET_CONNECTED_SERVER_LIST_REQUEST_EX {
	COMMAND_REQUEST_HEADER Command;
} GET_CONNECTED_SERVER_LIST_REQUEST_EX, *PGET_CONNECTED_SERVER_LIST_REQUEST_EX;

typedef struct _GET_CONNECTED_SERVER_LIST_REPLY_EX {
	COMMAND_REPLY_HEADER Reply;
	unsigned int bufferLen;
	unsigned char List[1];

} GET_CONNECTED_SERVER_LIST_REPLY_EX, *PGET_CONNECTED_SERVER_LIST_REPLY_EX;

typedef struct _GET_SERVER_VOLUME_LIST_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int Length;
	unsigned char Name[1];
} GET_SERVER_VOLUME_LIST_REQUEST, *PGET_SERVER_VOLUME_LIST_REQUEST;

typedef struct _GET_SERVER_VOLUME_LIST_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char List[1];
} GET_SERVER_VOLUME_LIST_REPLY, *PGET_SERVER_VOLUME_LIST_REPLY;

typedef struct _OPEN_CONNECTION_BY_ADDR_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int address;

} OPEN_CONNECTION_BY_ADDR_REQUEST, *POPEN_CONNECTION_BY_ADDR_REQUEST;

typedef struct _OPEN_CONNECTION_BY_ADDR_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char serverName[64];
	unsigned char treeName[64];
	NWHANDLE connHandle;

} OPEN_CONNECTION_BY_ADDR_REPLY, *POPEN_CONNECTION_BY_ADDR_REPLY;

typedef struct _OPEN_CONNECTION_BY_NAME_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int NameLen;
	unsigned char Name[1];

} OPEN_CONNECTION_BY_NAME_REQUEST, *POPEN_CONNECTION_BY_NAME_REQUEST;

typedef struct _OPEN_CONNECTION_BY_NAME_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char treeName[64];
	NWHANDLE connHandle;

} OPEN_CONNECTION_BY_NAME_REPLY, *POPEN_CONNECTION_BY_NAME_REPLY;

/*
typedef struct _LOGIN_IDENTITY_REQUEST
{
	COMMAND_REQUEST_HEADER Command;
	unsigned int			treeFlags;
	unsigned char			treeName[64];
	unsigned int			serverFlags;
	unsigned char			serverName[64];
	unsigned int			userFlags;
	unsigned char			userName[512];
	unsigned int			passwordFlags;
	unsigned char			password[128];

} LOGIN_IDENTITY_REQUEST, *PLOGIN_IDENTITY_REQUEST;

typedef struct _LOGIN_IDENTITY_REPLY
{
	COMMAND_REPLY_HEADER Reply;
	unsigned char			serverName[64];
	unsigned char			treeName[64];
	NWHANDLE					connHandle;

} LOGIN_IDENTITY_REPLY, *PLOGIN_IDENTITY_REPLY;
*/

typedef struct _VERIFY_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int pathLen;
	unsigned char path[1];

} VERIFY_FILE_REQUEST, *PVERIFY_FILE_REQUEST;

typedef struct _VERIFY_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long fileSize;
	unsigned int fileMode;

} VERIFY_FILE_REPLY, *PVERIFY_FILE_REPLY;

typedef struct _BEGIN_ENUMERATE_DIRECTORY_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int pathLen;
	unsigned char path[1];

} BEGIN_ENUMERATE_DIRECTORY_REQUEST, *PBEGIN_ENUMERATE_DIRECTORY_REQUEST;

typedef struct _BEGIN_ENUMERATE_DIRECTORY_REPLY {
	COMMAND_REPLY_HEADER Reply;
	HANDLE enumerateHandle;

} BEGIN_ENUMERATE_DIRECTORY_REPLY, *PBEGIN_ENUMERATE_DIRECTORY_REPLY;

typedef struct _END_ENUMERATE_DIRECTORY_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE enumerateHandle;

} END_ENUMERATE_DIRECTORY_REQUEST, *PEND_ENUMERATE_DIRECTORY_REQUEST;

typedef struct _END_ENUMERATE_DIRECTORY_REPLY {
	COMMAND_REPLY_HEADER Reply;

} END_ENUMERATE_DIRECTORY_REPLY, *PEND_ENUMERATE_DIRECTORY_REPLY;

typedef struct _ENUMERATE_DIRECTORY_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE enumerateHandle;
	unsigned int pathLen;
	unsigned char path[1];

} ENUMERATE_DIRECTORY_REQUEST, *PENUMERATE_DIRECTORY_REQUEST;

typedef struct _ENUMERATE_DIRECTORY_REPLY {
	COMMAND_REPLY_HEADER Reply;
	HANDLE enumerateHandle;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long size;
	unsigned int mode;
	unsigned int nameLen;
	unsigned char name[1];

} ENUMERATE_DIRECTORY_REPLY, *PENUMERATE_DIRECTORY_REPLY;

typedef struct _ENUMERATE_DIRECTORY_EX_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE enumerateHandle;
	unsigned int pathLen;
	unsigned char path[1];

} ENUMERATE_DIRECTORY_EX_REQUEST, *PENUMERATE_DIRECTORY_EX_REQUEST;

typedef struct _ENUMERATE_DIRECTORY_EX_DATA {
	unsigned int length;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned long long size;
	unsigned int mode;
	unsigned int nameLen;
	unsigned char name[1];

} ENUMERATE_DIRECTORY_EX_DATA, *PENUMERATE_DIRECTORY_EX_DATA;

typedef struct _ENUMERATE_DIRECTORY_EX_REPLY {
	COMMAND_REPLY_HEADER Reply;
	HANDLE enumerateHandle;
	unsigned int enumCount;

} ENUMERATE_DIRECTORY_EX_REPLY, *PENUMERATE_DIRECTORY_EX_REPLY;

typedef struct _OPEN_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int access;	/* File Access */
	unsigned int mode;	/* Sharing Mode */
	unsigned int disp;	/* Create Disposition */
	unsigned int pathLen;
	unsigned char path[1];

} OPEN_FILE_REQUEST, *POPEN_FILE_REQUEST;

typedef struct _OPEN_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;
	HANDLE handle;
	unsigned int lastAccessTime;
	unsigned int modifyTime;
	unsigned int createTime;
	unsigned int attributes;
	loff_t size;

} OPEN_FILE_REPLY, *POPEN_FILE_REPLY;

typedef struct _CREATE_FILE_REQUEST {

	COMMAND_REQUEST_HEADER Command;
	unsigned int pathlength;
	unsigned char path[1];

} CREATE_FILE_REQUEST, *PCREATE_FILE_REQUEST;

typedef struct _CREATE_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;

} CREATE_FILE_REPLY, *PCREATE_FILE_REPLY;

typedef struct _CLOSE_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE handle;

} CLOSE_FILE_REQUEST, *PCLOSE_FILE_REQUEST;

typedef struct _CLOSE_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;

} CLOSE_FILE_REPLY, *PCLOSE_FILE_REPLY;

typedef struct _READ_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE handle;
	loff_t offset;
	size_t len;

} READ_FILE_REQUEST, *PREAD_FILE_REQUEST;

typedef struct _READ_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned long long bytesRead;
	unsigned char data[1];

} READ_FILE_REPLY, *PREAD_FILE_REPLY;

typedef struct _WRITE_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE handle;
	loff_t offset;
	size_t len;
	unsigned char data[1];

} WRITE_FILE_REQUEST, *PWRITE_FILE_REQUEST;

typedef struct _WRITE_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned long long bytesWritten;
} WRITE_FILE_REPLY, *PWRITE_FILE_REPLY;

typedef struct _READ_STREAM_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE connection;
	unsigned char handle[6];
	loff_t offset;
	size_t len;
} READ_STREAM_REQUEST, *PREAD_STREAM_REQUEST;

typedef struct _READ_STREAM_REPLY {
	COMMAND_REPLY_HEADER Reply;
	size_t bytesRead;
	unsigned char data[1];
} READ_STREAM_REPLY, *PREAD_STREAM_REPLY;

typedef struct _WRITE_STREAM_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE connection;
	unsigned char handle[6];
	loff_t offset;
	size_t len;
	unsigned char data[1];
} WRITE_STREAM_REQUEST, *PWRITE_STREAM_REQUEST;

typedef struct _WRITE_STREAM_REPLY {
	COMMAND_REPLY_HEADER Reply;
	size_t bytesWritten;
} WRITE_STREAM_REPLY, *PWRITE_STREAM_REPLY;

typedef struct _CLOSE_STREAM_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE connection;
	unsigned char handle[6];
} CLOSE_STREAM_REQUEST, *PCLOSE_STREAM_REQUEST;

typedef struct _CLOSE_STREAM_REPLY {
	COMMAND_REPLY_HEADER Reply;

} CLOSE_STREAM_REPLY, *PCLOSE_STREAM_REPLY;

typedef struct _CREATE_DIRECTORY_REQUEST {

	COMMAND_REQUEST_HEADER Command;
	unsigned int pathlength;
	unsigned char path[1];

} CREATE_DIRECTORY_REQUEST, *PCREATE_DIRECTORY_REQUEST;

typedef struct _CREATE_DIRECTORY_REPLY {
	COMMAND_REPLY_HEADER Reply;

} CREATE_DIRECTORY_REPLY, *PCREATE_DIRECTORY_REPLY;

typedef struct _LOGIN_USER_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int srvNameType;
	unsigned int serverLength;
	unsigned int serverOffset;
	unsigned int usrNameType;
	unsigned int userNameLength;
	unsigned int userNameOffset;
	unsigned int pwdNameType;
	unsigned int passwordLength;
	unsigned int passwordOffset;

} LOGIN_USER_REQUEST, *PLOGIN_USER_REQUEST;

typedef struct _LOGIN_USER_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned int connectionHandle;
	HANDLE loginIdentity;

} LOGIN_USER_REPLY, *PLOGIN_USER_REPLY;

typedef struct _LOGOUT_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int length;
	unsigned char Name[1];

} LOGOUT_REQUEST, *PLOGOUT_REQUEST;

typedef struct _LOGOUT_REPLY {
	COMMAND_REPLY_HEADER Reply;

} LOGOUT_REPLY, *PLOGOUT_REPLY;

typedef struct _CREATE_CONTEXT_REQUEST {
	COMMAND_REQUEST_HEADER Command;

} CREATE_CONTEXT_REQUEST, *PCREATE_CONTEXT_REQUEST;

typedef struct _CREATE_CONTEXT_REPLY {
	COMMAND_REPLY_HEADER Reply;
	struct schandle SessionId;
} CREATE_CONTEXT_REPLY, *PCREATE_CONTEXT_REPLY;

typedef struct _DESTROY_CONTEXT_REQUEST {
	COMMAND_REQUEST_HEADER Command;

} DESTROY_CONTEXT_REQUEST, *PDESTROY_CONTEXT_REQUEST;

typedef struct _DESTROY_CONTEXT_REPLY {
	COMMAND_REPLY_HEADER Reply;

} DESTROY_CONTEXT_REPLY, *PDESTROY_CONTEXT_REPLY;

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

typedef struct _LNX_FILE_INFO {
	unsigned int ia_valid;
	unsigned int ia_mode;
	uid_t ia_uid;
	gid_t ia_gid;
	loff_t ia_size;
	time_t ia_atime;
	time_t ia_mtime;
	time_t ia_ctime;
	unsigned int ia_attr_flags;

} LX_FILE_INFO, *PLX_FILE_INFO;

typedef struct _SET_FILE_INFO_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	LX_FILE_INFO fileInfo;
	unsigned int pathlength;
	char path[1];

} SET_FILE_INFO_REQUEST, *PSET_FILE_INFO_REQUEST;

typedef struct _SET_FILE_INFO_REPLY {
	COMMAND_REPLY_HEADER Reply;

} SET_FILE_INFO_REPLY, *PSET_FILE_INFO_REPLY;

typedef struct _TRUNCATE_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int pathLen;
	char path[1];

} TRUNCATE_FILE_REQUEST, *PTRUNCATE_FILE_REQUEST;

typedef struct _TRUNCATE_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;

} TRUNCATE_FILE_REPLY, *PTRUNCATE_FILE_REPLY;

typedef struct _GETPWUID_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int uid;
} GETPWUID_REQUEST, *PGETPWUID_REQUEST;

typedef struct _GETPWUID_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char UserName[1];
} GETPWUID_REPLY, *PGETPWUID_REPLY;

typedef struct _GET_VERSION_REQUEST {
	COMMAND_REQUEST_HEADER Command;
} GET_VERSION_REQUEST, *PGET_VERSION_REQUEST;

typedef struct _GET_VERSION_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char Version[1];
} GET_VERSION_REPLY, *PGET_VERSION_REPLY;

typedef struct _SET_MOUNT_PATH {
	COMMAND_REQUEST_HEADER Command;
	unsigned int PathLength;
	unsigned char Path[1];
} SET_MOUNT_PATH_REQUEST, *PSET_MOUNT_PATH_REQUEST;

typedef struct _SET_MOUNT_PATH_REPLY {
	COMMAND_REPLY_HEADER Reply;
} SET_MOUNT_PATH, *PSET_MOUNT_PATH_REPLY;

typedef struct _GET_USER_SPACE {
	COMMAND_REQUEST_HEADER Command;
} GET_USER_SPACE_REQUEST, *PGET_USER_SPACE_REQUEST;

typedef struct _GET_USER_SPACE_REPLY {
	COMMAND_REPLY_HEADER Reply;
	uint64_t TotalSpace;
	uint64_t FreeSpace;
	uint64_t TotalEnties;
	uint64_t FreeEnties;
} GET_USER_SPACE_REPLY, *PGET_USER_SPACE_REPLY;

typedef struct _XPLAT_CALL_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int NwcCommand;
	unsigned long dataLen;
	unsigned char data[1];

} XPLAT_CALL_REQUEST, *PXPLAT_CALL_REQUEST;

typedef struct _XPLAT_CALL_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned long dataLen;
	unsigned char data[1];

} XPLAT_CALL_REPLY, *PXPLAT_CALL_REPLY;

/* XPlat NWC structures used by the daemon */

typedef struct _NWD_OPEN_CONN_BY_NAME {
	HANDLE ConnHandle;
	unsigned int nameLen;
	unsigned int oName;	/* Ofset to the Name */
	unsigned int serviceLen;
	unsigned int oServiceType;	/* Offset to service Type; */
	unsigned int uConnFlags;
	unsigned int uTranType;
	HANDLE newConnHandle;

} NwdCOpenConnByName, *PNwdCOpenConnByName;

typedef struct _NWD_TRAN_ADDR {
	unsigned int uTransportType;
	unsigned int uAddressLength;
	unsigned int oAddress;

} NwdCTranAddr, *PNwdCTranAddr;

typedef struct _NWD_OPEN_CONN_BY_ADDR {
	HANDLE ConnHandle;
	unsigned int oServiceType;
	unsigned int uConnFlags;
	NwdCTranAddr TranAddr;

} NwdCOpenConnByAddr, *PNwdCOpenConnByAddr;

typedef struct _NWD_CLOSE_CONN {
	HANDLE ConnHandle;

} NwdCCloseConn, *PNwdCCloseConn;

typedef struct _NWD_NCP_REQ {
	HANDLE ConnHandle;
	unsigned int replyLen;
	unsigned int requestLen;
	unsigned int function;
/*	unsigned int 	subFunction; */
/*	unsigned int 	verb; */
	unsigned int flags;
	unsigned char data[1];

} NwdCNCPReq, *PNwdCNCPReq;

typedef struct _NWD_NCP_REP {
	unsigned int replyLen;
	unsigned char data[1];

} NwdCNCPRep, *PNwdCNCPRep;

typedef struct _NWC_AUTH_WID {
	HANDLE ConnHandle;
	u32 AuthenticationId;

} NwdCAuthenticateWithId, *PNwdCAuthenticateWithId;

typedef struct _NWC_AUTHENTICATE {
	HANDLE ConnHandle;
	unsigned int uAuthenticationType;
	unsigned int userNameOffset;
	unsigned int passwordOffset;
	unsigned int MaxInfoLength;
	unsigned int InfoLength;
	unsigned int authenInfoOffset;

} NwdCAuthenticate, *PNwdCAuthenticate;

typedef struct _NWC_UNAUTHENTICATE {
	HANDLE ConnHandle;
	unsigned int AuthenticationId;

} NwdCUnauthenticate, *PNwdCUnauthenticate;

typedef struct _NWC_LISC_ID {
	HANDLE ConnHandle;

} NwdCLicenseConn, *PNwdCLicenseConn;

typedef struct _NWC_UNLIC_CONN {
	HANDLE ConnHandle;

} NwdCUnlicenseConn, *PNwdCUnlicenseConn;

typedef struct _NWC_GET_IDENT_INFO {
	u32 AuthenticationId;
	unsigned int AuthType;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int IdentityFlags;
	unsigned int domainLen;
	unsigned int pDomainNameOffset;
	unsigned int objectLen;
	unsigned int pObjectNameOffset;

} NwdCGetIdentityInfo, *PNwdCGetIdentityInfo;

typedef struct _NWC_LO_ID {
	u32 AuthenticationId;

} NwdCLogoutIdentity, *PNwdCLogoutIdentity;

typedef struct _RENAME_FILE_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	int directoryFlag;
	unsigned int newnameLen;
	unsigned char newname[256];
	unsigned int oldnameLen;
	unsigned char oldname[256];
} RENAME_FILE_REQUEST, *PRENAME_FILE_REQUEST;

typedef struct _RENAME_FILE_REPLY {
	COMMAND_REPLY_HEADER Reply;

} RENAME_FILE_REPLY, *PRENAME_FILE_REPLY;

typedef struct __NwdServerVersion {
	unsigned int uMajorVersion;
	unsigned short int uMinorVersion;
	unsigned short int uRevision;

} NwdServerVersion, *PNwdServerVersion;

#define	MAX_ADDRESS_LENGTH	32

typedef struct tagNwdTranAddrEx {
	unsigned int uTransportType;
	unsigned int uAddressLength;
	unsigned char Buffer[MAX_ADDRESS_LENGTH];

} NwdTranAddr, *PNwdTranAddr;

typedef struct __NWD_CONN_INFO {
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
	NwdServerVersion ServerVersion;
	NwdTranAddr TranAddress;

} NwdConnInfo, *PNwdConnInfo;

typedef struct _nwd_conn_info {
	HANDLE ConnHandle;
	unsigned int uInfoLevel;
	unsigned int uInfoLength;

} NwdCGetConnInfo, *PNwdCGetConnInfo;

typedef struct nwd_open_conn_by_Ref {
	HANDLE uConnReference;
	unsigned int uConnFlags;
	HANDLE ConnHandle;

} NwdCOpenConnByRef, *PNwdCOpenConnByRef;

typedef struct nwd_get_reqversion {
	unsigned int uMajorVersion;
	unsigned int uMinorVersion;
	unsigned int uRevision;

} NwdCGetRequesterVersion, *PNwdCGetRequesterVersion;

typedef struct _nwc_scan_conn_info {
	unsigned int uScanIndex;
	unsigned int uScanInfoLevel;
	unsigned int uScanInfoLen;
	unsigned int uScanConnInfoOffset;
	unsigned int uScanFlags;
	unsigned int uReturnInfoLevel;
	unsigned int uReturnInfoLength;
	unsigned int uConnectionReference;
	unsigned int uReturnConnInfoOffset;

} NwdCScanConnInfo, *PNwdCScanConnInfo;

typedef struct nwc_get_pref_ds_tree {
	unsigned int uTreeLength;
	unsigned int DsTreeNameOffset;

} NwdCGetPreferredDsTree, *PNwdCGetPreferredDsTree;

typedef struct nwc_set_pref_ds_tree {
	unsigned int uTreeLength;
	unsigned int DsTreeNameOffset;

} NwdCSetPreferredDsTree, *PNwdCSetPreferredDsTree;

typedef struct nwc_set_def_name_ctx {
	unsigned int uTreeLength;
	unsigned int TreeOffset;
	unsigned int uNameLength;
	unsigned int NameContextOffset;

} NwdCSetDefaultNameContext, *PNwdCSetDefaultNameContext;

typedef struct nwc_get_def_name_ctx {
	unsigned int uTreeLength;
	unsigned int TreeOffset;
	unsigned int uNameLength;
	unsigned int NameContextOffset;

} NwdCGetDefaultNameContext, *PNwdCGetDefaultNameContext;

typedef struct _nwc_get_treemonitored_connref {
	NwdString TreeName;
	HANDLE uConnReference;

} NwdCGetTreeMonitoredConnRef, *PNwdCGetTreeMonitoredConnRef;

typedef struct _nwc_enumerate_identities {
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

} NwdCDEnumerateIdentities, *PNwdCEnumerateIdentities;

typedef struct nwd_change_key {
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

} NwdCChangeKey, *PNwdCChangeKey;

typedef struct _nwd_get_primary_conn {
	HANDLE uConnReference;

} NwdCGetPrimaryConnection, *PNwdCGetPrimaryConnection;

typedef struct _nwd_set_primary_conn {
	HANDLE ConnHandle;

} NwdCSetPrimaryConnection, *PNwdCSetPrimaryConnection;

typedef struct _nwd_map_drive_ex {
	u32 ConnHandle;
	u32 localUid;
	u32 linkOffsetLength;
	u32 linkOffset;
	u32 dirPathOffsetLength;
	u32 dirPathOffset;

} NwdCMapDriveEx, *PNwdCMapDriveEx;

typedef struct _nwd_unmap_drive_ex {
	unsigned int linkLen;
	char linkPath[1];

} NwdCUnmapDriveEx, *PNwdCUnmapDriveEx;

typedef struct _nwd_enum_links {
	unsigned int totalLen;
	unsigned int linkCount;

} NwdCEnumLinks, *PNwdCEnumLinks;

typedef struct nwd_getbroadcastnotification {
	unsigned int uMessageFlags;
	HANDLE uConnReference;
	unsigned int messageLen;
	char message[1];

} NwdCGetBroadcastNotification, *PNwdCGetBroadcastNotification;

typedef struct _enum_entry {
	unsigned int entryLen;
	u32 connHdl;
	char data[0];
} NwdCEnumEntry, *PNwdCEnumEntry;

typedef struct _nwd_set_conn_info {
	HANDLE ConnHandle;
	unsigned int uInfoLevel;
	unsigned int uInfoLength;
	unsigned int offsetConnInfo;

} NwdCSetConnInfo, *PNwdCSetConnInfo;

typedef struct _len_string {
	u32 stLen;
	char string[1];

} LString, *PLString;

typedef struct _DEBUG_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	int cmdlen;
	char dbgcmd[1];

} DEBUG_REQUEST, *PDEBUG_REQUEST;

typedef struct _DEBUG_REPLY {
	COMMAND_REPLY_HEADER Reply;

} DEBUG_REPLY, *PDEBUG_REPLY;

typedef struct _Nwd_Set_Key {
	HANDLE ConnHandle;
	unsigned int AuthenticationId;
	unsigned int objectNameLen;
	unsigned int objectNameOffset;
	unsigned short int ObjectType;
	unsigned int newPasswordLen;
	unsigned int newPasswordOffset;

} NwdCSetKey, *PNwdCSetKey;

typedef struct _Nwd_Verify_Key {
	unsigned int AuthType;
	unsigned int NameType;
	unsigned short int ObjectType;
	unsigned int domainNameLen;
	unsigned int domainNameOffset;
	unsigned int objectNameLen;
	unsigned int objectNameOffset;
	unsigned int verifyPasswordLen;
	unsigned int verifyPasswordOffset;

} NwdCVerifyKey, *PNwdCVerifyKey;

typedef struct _GET_CACHE_FLAG {
	COMMAND_REQUEST_HEADER Command;
	int pathLen;
	unsigned char path[0];

} GET_CACHE_FLAG_REQUEST, *PGET_CACHE_FLAG_REQUEST;

typedef struct _GET_CACHE_FLAG_REPLY {
	COMMAND_REPLY_HEADER Reply;
	int CacheFlag;

} GET_CACHE_FLAG_REPLY, *PGET_CACHE_FLAG_REPLY;

typedef struct _XA_LIST_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char *pData;

} XA_LIST_REPLY, *PXA_LIST_REPLY;

typedef struct _XA_GET_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int pathLen;
	unsigned int nameLen;
	unsigned char data[1];	//hold path, attribute name

} XA_GET_REQUEST, *PXA_GET_REQUEST;

typedef struct _XA_GET_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char *pData;

} XA_GET_REPLY, *PXA_GET_REPLY;

typedef struct _XA_SET_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	unsigned int TtlWriteDataSize;
	unsigned int WritePosition;
	int flags;
	unsigned int pathLen;
	unsigned int nameLen;
	unsigned int valueLen;
	unsigned char data[1];	//hold path, attribute name, value data

} XA_SET_REQUEST, *PXA_SET_REQUEST;

typedef struct _XA_SET_REPLY {
	COMMAND_REPLY_HEADER Reply;
	unsigned char *pData;

} XA_SET_REPLY, *PXA_SET_REPLY;

typedef struct _SET_FILE_LOCK_REQUEST {
	COMMAND_REQUEST_HEADER Command;
	HANDLE handle;
	unsigned char fl_type;
	loff_t fl_start;
	loff_t fl_len;

} SET_FILE_LOCK_REQUEST, *PSET_FILE_LOCK_REQUEST;

typedef struct _SET_FILE_LOCK_REPLY {
	COMMAND_REPLY_HEADER Reply;

} SET_FILE_LOCK_REPLY, *PSET_FILE_LOCK_REPLY;

#pragma pack(pop)

#endif	/* __NOVFS_COMMANDS_H */
