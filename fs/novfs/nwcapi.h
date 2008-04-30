/*
 * NetWare Redirector for Linux
 * Author: Sheffer Clark
 *
 * This file contains all typedefs and constants for the NetWare Client APIs.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#ifndef __NWCLNX_H__
#define __NWCLNX_H__

#if 0                          //sgled hack
#else //sgled hack (up to endif)

#define NW_MAX_TREE_NAME_LEN              33
#define NW_MAX_SERVICE_TYPE_LEN           49
/* Transport Type - (nuint32 value) */
#define NWC_TRAN_TYPE_IPX                 0x0001
#define NWC_TRAN_TYPE_DDP                 0x0003
#define NWC_TRAN_TYPE_ASP                 0x0004
#define NWC_TRAN_TYPE_UDP                 0x0008
#define NWC_TRAN_TYPE_TCP                 0x0009
#define NWC_TRAN_TYPE_UDP6                0x000A
#define NWC_TRAN_TYPE_TCP6                0x000B
#define NWC_TRAN_TYPE_WILD                0x8000

//
// DeviceIoControl requests for the NetWare Redirector
//
// Macro definition for defining DeviceIoControl function control codes.
// The function codes 0 - 2047 are reserved for Microsoft.
// Function codes 2048 - 4096 are reserved for customers.
// The NetWare Redirector will use codes beginning at 3600.
//
// METHOD_NEITHER User buffers will be passed directly from the application
// to the file system.  The redirector is responsible for either probing
// and locking the buffers or using a try - except around access of the
// buffers.

#define  BASE_REQ_NUM                  0x4a541000

// Connection functions
#define  NWC_OPEN_CONN_BY_NAME         (BASE_REQ_NUM + 0)
#define  NWC_OPEN_CONN_BY_ADDRESS      (BASE_REQ_NUM + 1)
#define  NWC_OPEN_CONN_BY_REFERENCE    (BASE_REQ_NUM + 2)
#define  NWC_CLOSE_CONN                (BASE_REQ_NUM + 3)
#define  NWC_SYS_CLOSE_CONN            (BASE_REQ_NUM + 4)
#define  NWC_GET_CONN_INFO             (BASE_REQ_NUM + 5)
#define  NWC_SET_CONN_INFO             (BASE_REQ_NUM + 6)
#define  NWC_SCAN_CONN_INFO            (BASE_REQ_NUM + 7)
#define  NWC_MAKE_CONN_PERMANENT       (BASE_REQ_NUM + 8)
#define  NWC_LICENSE_CONN              (BASE_REQ_NUM + 9)
#define  NWC_UNLICENSE_CONN            (BASE_REQ_NUM + 10)
#define  NWC_GET_NUM_CONNS             (BASE_REQ_NUM + 11)
#define  NWC_GET_PREFERRED_SERVER      (BASE_REQ_NUM + 12)
#define  NWC_SET_PREFERRED_SERVER      (BASE_REQ_NUM + 13)
#define  NWC_GET_PRIMARY_CONN          (BASE_REQ_NUM + 14)
#define  NWC_SET_PRIMARY_CONN          (BASE_REQ_NUM + 15)

// Authentication functions
#define  NWC_CHANGE_KEY                (BASE_REQ_NUM + 20)
#define  NWC_ENUMERATE_IDENTITIES      (BASE_REQ_NUM + 21)
#define  NWC_GET_IDENTITY_INFO         (BASE_REQ_NUM + 22)
#define  NWC_LOGIN_IDENTITY            (BASE_REQ_NUM + 23)
#define  NWC_LOGOUT_IDENTITY           (BASE_REQ_NUM + 24)
#define  NWC_SET_KEY                   (BASE_REQ_NUM + 25)
#define  NWC_VERIFY_KEY                (BASE_REQ_NUM + 26)
#define  NWC_AUTHENTICATE_CONN_WITH_ID (BASE_REQ_NUM + 27)
#define  NWC_UNAUTHENTICATE_CONN       (BASE_REQ_NUM + 28)

// Directory Services functions
#define  NWC_GET_DEFAULT_NAME_CONTEXT  (BASE_REQ_NUM + 30)
#define  NWC_SET_DEFAULT_NAME_CONTEXT  (BASE_REQ_NUM + 31)
#define  NWC_GET_PREFERRED_DS_TREE     (BASE_REQ_NUM + 32)
#define  NWC_SET_PREFERRED_DS_TREE     (BASE_REQ_NUM + 33)
#define  NWC_GET_TREE_MONITORED_CONN_REF  (BASE_REQ_NUM + 34)
#define  NWC_NDS_RESOLVE_NAME_TO_ID    (BASE_REQ_NUM + 35)

// NCP Request functions
#define  NWC_FRAGMENT_REQUEST          (BASE_REQ_NUM + 40)
#define  NWC_NCP_ORDERED_REQUEST_ALL   (BASE_REQ_NUM + 41)
#define  NWC_RAW_NCP_REQUEST           (BASE_REQ_NUM + 42)
#define  NWC_RAW_NCP_REQUEST_ALL       (BASE_REQ_NUM + 43)

// File Handle Conversion functions
#define  NWC_CONVERT_LOCAL_HANDLE      (BASE_REQ_NUM + 50)
#define  NWC_CONVERT_NETWARE_HANDLE    (BASE_REQ_NUM + 51)

// Misc. functions
#define  NWC_MAP_DRIVE                 (BASE_REQ_NUM + 60)
#define  NWC_UNMAP_DRIVE               (BASE_REQ_NUM + 61)
#define  NWC_ENUMERATE_DRIVES          (BASE_REQ_NUM + 62)

#define  NWC_GET_REQUESTER_VERSION     (BASE_REQ_NUM + 63)
#define  NWC_QUERY_FEATURE             (BASE_REQ_NUM + 64)

#define  NWC_GET_CONFIGURED_NSPS       (BASE_REQ_NUM + 65)

#define  NWC_GET_MOUNT_PATH            (BASE_REQ_NUM + 66)

#define  NWC_GET_BROADCAST_MESSAGE     (BASE_REQ_NUM + 67)

#endif //sgled hack -------------------------------

#define IOC_XPLAT    0x4a540002

typedef struct _XPLAT_ {
	int xfunction;
	unsigned long reqLen;
	void *reqData;
	unsigned long repLen;
	void *repData;

} XPLAT, *PXPLAT;

#if 0
N_EXTERN_LIBRARY(NWRCODE)
    NWCLnxReq
    (nuint32 request, nptr pInBuf, nuint32 inLen, nptr pOutBuf, nuint32 outLen);
#endif
//
// Network Name Format Type
//

#define  NWC_NAME_FORMAT_NDS              0x0001
#define  NWC_NAME_FORMAT_BIND             0x0002
#define  NWC_NAME_FORMAT_BDP              0x0004
#define  NWC_NAME_FORMAT_NDS_TREE         0x0008
#define  NWC_NAME_FORMAT_WILD             0x8000

//
// API String Types
//

#define  NWC_STRING_TYPE_ASCII            0x0001	// multi-byte, not really ascii
#define  NWC_STRING_TYPE_UNICODE          0x0002
#define  NWC_STRING_TYPE_UTF8             0x0003

//
// Open Connection Flags
//

#define  NWC_OPEN_LICENSED                0x0001
#define  NWC_OPEN_UNLICENSED              0x0002
#define  NWC_OPEN_PRIVATE                 0x0004
#define  NWC_OPEN_PUBLIC                  0x0008
#define  NWC_OPEN_EXISTING_HANDLE         0x0010
#define  NWC_OPEN_NO_HANDLE               0x0020
#define  NWC_OPEN_PERMANENT               0x0040
#define  NWC_OPEN_DISCONNECTED            0x0080
#define  NWC_OPEN_NEAREST                 0x0100
#define  NWC_OPEN_IGNORE_CACHE            0x0200

//
// Close Connection Flags
//

#define  NWC_CLOSE_TEMPORARY              0x0000
#define  NWC_CLOSE_PERMANENT              0x0001

//
// Connection Information Levels
//

#define  NWC_CONN_INFO_RETURN_ALL            0xFFFF
#define  NWC_CONN_INFO_RETURN_NONE           0x0000
#define  NWC_CONN_INFO_VERSION               0x0001
#define  NWC_CONN_INFO_AUTH_STATE            0x0002
#define  NWC_CONN_INFO_BCAST_STATE           0x0003
#define  NWC_CONN_INFO_CONN_REF              0x0004
#define  NWC_CONN_INFO_TREE_NAME             0x0005
#define  NWC_CONN_INFO_WORKGROUP_ID          0x0006
#define  NWC_CONN_INFO_SECURITY_STATE        0x0007
#define  NWC_CONN_INFO_CONN_NUMBER           0x0008
#define  NWC_CONN_INFO_USER_ID               0x0009
#define  NWC_CONN_INFO_SERVER_NAME           0x000A
#define  NWC_CONN_INFO_TRAN_ADDR             0x000B
#define  NWC_CONN_INFO_NDS_STATE             0x000C
#define  NWC_CONN_INFO_MAX_PACKET_SIZE       0x000D
#define  NWC_CONN_INFO_LICENSE_STATE         0x000E
#define  NWC_CONN_INFO_PUBLIC_STATE          0x000F
#define  NWC_CONN_INFO_SERVICE_TYPE          0x0010
#define  NWC_CONN_INFO_DISTANCE              0x0011
#define  NWC_CONN_INFO_SERVER_VERSION        0x0012
#define  NWC_CONN_INFO_AUTH_ID               0x0013
#define  NWC_CONN_INFO_SUSPENDED             0x0014
#define  NWC_CONN_INFO_TREE_NAME_UNICODE     0x0015
#define  NWC_CONN_INFO_SERVER_NAME_UNICODE   0x0016
#define  NWC_CONN_INFO_LOCAL_TRAN_ADDR       0x0017
#define  NWC_CONN_INFO_ALTERNATE_ADDR        0x0018
#define  NWC_CONN_INFO_SERVER_GUID           0x0019

#define  NWC_CONN_INFO_MAX_LEVEL             0x0014

//
// Information Versions
//

#define  NWC_INFO_VERSION_1               0x0001
#define  NWC_INFO_VERSION_2               0x0002

//
// Authentication State
//

#define  NWC_AUTH_TYPE_NONE               0x0000
#define  NWC_AUTH_TYPE_BINDERY            0x0001
#define  NWC_AUTH_TYPE_NDS                0x0002
#define  NWC_AUTH_TYPE_PNW                0x0003

#define  NWC_AUTH_STATE_NONE              0x0000
#define  NWC_AUTH_STATE_BINDERY           0x0001
#define  NWC_AUTH_STATE_NDS               0x0002
#define  NWC_AUTH_STATE_PNW               0x0003

//
// Authentication Flags
//

#define  NWC_AUTH_PRIVATE                 0x00000004
#define  NWC_AUTH_PUBLIC                  0x00000008

//
// Broadcast State
//

#define  NWC_BCAST_PERMIT_ALL             0x0000
#define  NWC_BCAST_PERMIT_SYSTEM          0x0001
#define  NWC_BCAST_PERMIT_NONE            0x0002
#define  NWC_BCAST_PERMIT_SYSTEM_POLLED   0x0003
#define  NWC_BCAST_PERMIT_ALL_POLLED      0x0004

//
// Broadcast State
//

#define  NWC_NDS_NOT_CAPABLE              0x0000
#define  NWC_NDS_CAPABLE                  0x0001

//
// License State
//

#define  NWC_NOT_LICENSED                 0x0000
#define  NWC_CONNECTION_LICENSED          0x0001
#define  NWC_HANDLE_LICENSED              0x0002

//
// Public State
//

#define  NWC_CONN_PUBLIC                  0x0000
#define  NWC_CONN_PRIVATE                 0x0001

//
// Scan Connection Information Flags used
// for finding connections by specific criteria
//

#define  NWC_MATCH_NOT_EQUALS             0x0000
#define  NWC_MATCH_EQUALS                 0x0001
#define  NWC_RETURN_PUBLIC                0x0002
#define  NWC_RETURN_PRIVATE               0x0004
#define  NWC_RETURN_LICENSED              0x0008
#define  NWC_RETURN_UNLICENSED            0x0010

//
// Authentication Types
//

#define  NWC_AUTHENT_BIND                 0x0001
#define  NWC_AUTHENT_NDS                  0x0002
#define  NWC_AUTHENT_PNW                  0x0003

//
// Disconnected info
//

#define  NWC_SUSPENDED                    0x0001

//
// Maximum object lengths
//

#define  MAX_DEVICE_LENGTH                16
#define  MAX_NETWORK_NAME_LENGTH          1024
#define  MAX_OBJECT_NAME_LENGTH           48
#define  MAX_PASSWORD_LENGTH              128
#define  MAX_SERVER_NAME_LENGTH           48
#define  MAX_SERVICE_TYPE_LENGTH          48
#define  MAX_TREE_NAME_LENGTH             32
#define  MAX_ADDRESS_LENGTH               32
#define  MAX_NAME_SERVICE_PROVIDERS       10

//
// Flags for the GetBroadcastMessage API
//

#define  MESSAGE_GET_NEXT_MESSAGE         1
#define  MESSAGE_RECEIVED_FOR_CONNECTION  2

//
// This constant must always be equal to the last device
//

#define  DEVICE_LAST_DEVICE               0x00000003

//
// Defined feature set provided by requester
//

#ifndef  NWC_FEAT_PRIV_CONN
#define  NWC_FEAT_PRIV_CONN               1
#define  NWC_FEAT_REQ_AUTH                2
#define  NWC_FEAT_SECURITY                3
#define  NWC_FEAT_NDS                     4
#define  NWC_FEAT_NDS_MTREE               5
#define  NWC_FEAT_PRN_CAPTURE             6
#define  NWC_FEAT_NDS_RESOLVE             7
#endif

//===[ Type definitions ]==================================================

//
// Connection Handle returned from all OpenConnByXXXX calls
//

typedef u32 NW_CONN_HANDLE, *PNW_CONN_HANDLE;

//
// Authentication Id returned from the NwcCreateAuthenticationId call
//

typedef u32 AUTHEN_ID, *PAUTHEN_ID;

//
// Structure for defining what a transport
// address looks like
//

typedef struct tagNwcTranAddr {
	u32 uTransportType;
	u32 uAddressLength;
	unsigned char *puAddress;

} NwcTranAddr, *PNwcTranAddr;

//
// Structure for defining what a new transport
// address looks like
//

typedef struct tagNwcTranAddrEx {
	u32 uTransportType;
	u32 uAddressLength;
	unsigned char buBuffer[MAX_ADDRESS_LENGTH];

} NwcTranAddrEx, *PNwcTranAddrEx;

typedef struct tagNwcReferral {
	u32 uAddrCnt;
	PNwcTranAddrEx pAddrs;

} NwcReferral, *PNwcReferral;

typedef struct tagNwcServerVersion {
	u32 uMajorVersion;
	u16 uMinorVersion;
	u16 uRevision;

} NwcServerVersion, *PNwcServerVersion;

typedef struct tagNwcConnString {
	char *pString;
	u32 uStringType;
	u32 uNameFormatType;

} NwcConnString, *PNwcConnString;

//#if defined(NTYPES_H)
//typedef NWCString    NwcString, *PNwcString;
//#else
typedef struct tagNwcString {
	u32 DataType;
	u32 BuffSize;
	u32 DataLen;
	void *pBuffer;
	u32 CodePage;
	u32 CountryCode;

} NwcString, *PNwcString;
//#endif

//
// Structure used in NDS Resolve name
//

#define  RESOLVE_INFO_SVC_V1_00     0x00FE0001

typedef struct tagNwcResolveInfo {
	u32 uResolveInfoVersion;
	u32 luFlags;
	u32 luReqFlags;
	u32 luReqScope;
	u32 luResolveType;
	u32 luRepFlags;
	u32 luResolvedOffset;
	u32 luDerefNameLen;
	u16 *pDerefName;
} NwcResolveInfo, *PNwcResolveInfo;

//
// Definition of a fragment for the Raw NCP requests
//

typedef struct tagNwcFrag {
	void *pData;
	u32 uLength;

} NwcFrag, *PNwcFrag;

//
// Current connection information available for
// enumeration using GetConnInfo and ScanConnInfo
//

#define NW_INFO_BUFFER_SIZE   NW_MAX_TREE_NAME_LEN + \
                              NW_MAX_TREE_NAME_LEN + \
                              NW_MAX_SERVICE_TYPE_LEN

typedef struct tagNwcConnInfo {
	u32 uInfoVersion;
	u32 uAuthenticationState;
	u32 uBroadcastState;
	u32 uConnectionReference;
	u32 TreeNameOffset;
	u32 uSecurityState;
	u32 uConnectionNumber;
	u32 uUserId;
	u32 ServerNameOffset;
	u32 uNdsState;
	u32 uMaxPacketSize;
	u32 uLicenseState;
	u32 uPublicState;
	u32 bcastState;
	u32 ServiceTypeOffset;
	u32 uDistance;
	u32 uAuthId;
	u32 uDisconnected;
	NwcServerVersion serverVersion;
	NwcTranAddrEx tranAddress;
	unsigned char buBuffer[NW_INFO_BUFFER_SIZE];

} NwcConnInfo, *PNwcConnInfo;

//
// Get Browse Connection References
//

typedef struct _GetBrowseConnectionsRec {

	u32 recordSize;
	u32 numConnectionsReturned;
	u32 numConnectionsAvailable;
	u32 connReferences[1];

} GetBrowseConnectionRec, *PGetBrowseConnectionRec;

//++=======================================================================
//  API Name:        NwcClearBroadcastMessage
//
//  Arguments In:    NONE
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//
//  Abstract:        This API is clears the broadcast message buffer.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

//++=======================================================================
//  API Name:        NwcCloseConn
//
//  Arguments In:    ConnHandle - The handle to a connection that is
//                   no longer needed.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_INVALID_OWNER
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API is used by an application that opened the
//                   connection using one of the open connection calls
//                   is finished using the connection.  After it is closed,
//                   the handle may no longer be used to access the
//                   connection.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcCloseConn {
	NW_CONN_HANDLE ConnHandle;

} NwcCloseConn, *PNwcCloseConn;

//++=======================================================================
//  API Name:        NwcConvertLocalFileHandle
//
//  Arguments In:    NONE
//
//  Arguments Out:   uConnReference - The connection reference associated
//                   with the returned NetWare file handle.
//
//                   pNetWareFileHandle - The six byte NetWare file handle
//                   associated with the given local file handle.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_NOT_OWNED
//
//  Abstract:        This API is used to return the NetWare handle that
//                   has been associated to a local file handle.
//                   In addition to returning the NetWare file handle,
//                   this API also returns the connection reference to
//                   the connection that owns the file.
//
//  Notes:           This API does not create a new NetWare handle, it
//                   only returns the existing handle associated to the
//                   local handle.
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcConvertLocalHandle {
	u32 uConnReference;
	unsigned char NetWareHandle[6];

} NwcConvertLocalHandle, *PNwcConvertLocalHandle;

//++=======================================================================
//  API Name:        NwcConvertNetWareHandle
//
//  Arguments In:    ConnHandle - The connection associated with the
//                   NetWare file handle to convert.
//
//                   uAccessMode - The access rights to be used when
//                   allocating the local file handle.
//
//                   pNetWareHandle - The NetWare handle that will be
//                   bound to the new local handle being created.
//
//                   uFileSize - The current file size of the NetWare
//                   file associated with the given NetWare file handle.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_NOT_OWNED
//
//  Abstract:        This API is used to convert a NetWare file handle
//                   to a local file handle.
//
//                   The local handle must have been created previously
//                   by doing a local open to \Special\$Special.net.
//
//                   Then an Ioctl to this function must be issued using the
//                   handle returned from the special net open.
//
//  Notes:           After making this call, the NetWare file handle
//                   should not be closed using the NetWare library
//                   call, instead it should be closed using the local
//                   operating system's close call.
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--
typedef struct tagNwcConvertNetWareHandle {
	NW_CONN_HANDLE ConnHandle;
	u32 uAccessMode;
	unsigned char NetWareHandle[6];
	u32 uFileSize;
} NwcConvertNetWareHandle, *PNwcConvertNetWareHandle;

//++=======================================================================
//  API Name:        NwcFragmentRequest
//
//  Arguments In:    ConnHandle
//                      The connection handle the request is being
//                      directed to.
//
//                   uFunction
//                      The NCP function to be called, should be 104
//                      for NDS fragger/defragger requests.
//
//                   uSubFunction
//                      The NCP subfunction to be called, should be
//                      2 for NDS fragger/defragger requests.
//
//                   uVerb
//                      The actual operation to be completed on the
//                      server backend.
//
//                   flags
//                      Currently not implemented.  Reserved for
//                      future use.
//
//                   uNumRequestFrags
//                      The number of fragments that the request packet
//                      has been broken into.
//
//                   pRequestFrags
//                      List of fragments that make up the request packet.
//                      Each fragment includes the length of the fragment
//                      data and a pointer to the data.
//
//                   uNumReplyFrags
//                      The number of fragments the reply packet has been
//                      broken into.
//
//  Arguments Out:   pReplyFrags
//                      List of fragments that make up the reply packet.
//                      Each fragment includes the length of the fragment
//                      data and a pointer to the data.
//
//                   uActualReplyLength
//                      Total size of the reply packet after any header
//                      and tail information is removed.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        API for sending large NCP/NDS packets that are
//                   larger than the max MTU size for the underlying
//                   network.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--
typedef struct tagNwcFragmentRequest {
	NW_CONN_HANDLE ConnHandle;
	u32 uFunction;
	u32 uSubFunction;
	u32 uVerb;
	u32 flags;
	u32 uNumRequestFrags;
	PNwcFrag pRequestFrags;
	u32 uNumReplyFrags;
	PNwcFrag pReplyFrags;
	u32 uActualReplyLength;
} NwcFragmentRequest, *PNwcFragmentRequest;

//++=======================================================================
//  API Name:        NwcGetBroadcastMessage
//
//  Arguments In:    uMessageFlags - Not currently used.
//
//                   uConnReference - connection reference for
//                   pending message.
//
//                   messageLen - length of message buffer.
//
//                   message - message buffer
//
//  Arguments Out:   messageLen - length of the message
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_NO_MORE_ENTRIES
//
//  Abstract:        This API is used for notifying a caller of pending
//                   broadcast messages on the server.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

/* jlt
typedef  struct   tagNwcGetBroadcastMessage
{
   u32               uMessageFlags;
   u32               uConnReference;
   u32               messageLen;
   unsigned char                message[255];

} NwcGetBroadcastMessage, *PNwcGetBroadcastMessage;
*/

//++=======================================================================
//  API Name:        NwcGetConnInfo
//
//  Arguments In:    ConnHandle - Connection handle for the connection to
//                   get information on.
//                   uInfoLevel - Specifies what information should be
//                   returned.
//                   uInfoLen - Length of the ConnInfo buffer.
//
//  Arguments Out:   pConnInfo - A pointer to a buffer to return connection
//                   information in.  If the caller is requesting all
//                   information the pointer will be to a structure of
//                   type NwcConnInfo.  If the caller is requesting just
//                   a single piece of information, the pointer is the
//                   type of information being requested.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_INVALID_OWNER
//                   NWE_RESOURCE_LOCK
//                   NWE_STRING_TRANSLATION
//
//  Abstract:        This API returns connection information for the specified
//                   connection.  The requester can receive one piece of
//                   information or the whole information structure.
//                   Some of the entries in the NwcConnInfo structure are
//                   pointers.  The requester is responsible for supplying
//                   valid pointers for any info specified to be returned.
//                   If the requester does not want a piece of information
//                   returned, a NULL pointer should be placed in the field.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetConnInfo {
	NW_CONN_HANDLE ConnHandle;
	u32 uInfoLevel;
	u32 uInfoLength;
	void *pConnInfo;

} NwcGetConnInfo, *PNwcGetConnInfo;

//++=======================================================================
//  API Name:        NwcGetDefaultNameContext
//
//  Arguments In::   uTreeLength - Length of tree string.
//
//                   pDsTreeName - Pointer to tree string (multi-byte)
//
//                   pNameLength - On input, this is the length of the
//                   name context buffer. On output, this is the actual
//                   length of the name context string.
//
//  Arguments Out:   pNameContext - The buffer to copy the default name
//                   context into (multi-byte).
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//                   NWE_OBJECT_NOT_FOUND
//                   NWE_PARAM_INVALID
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API returns the default name context that
//                   was previously set either by configuration or
//                   by calling NwcSetDefaultNameContext.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetDefaultNameContext {
	u32 uTreeLength;
	unsigned char *pDsTreeName;
	u32 uNameLength;
// unsigned short *pNameContext;
	unsigned char *pNameContext;

} NwcGetDefaultNameContext, *PNwcGetDefaultNameContext;

//++=======================================================================
//  API Name:        NwcGetTreeMonitoredConnReference
//
//  Arguments In:    NONE
//
//  Arguments Out:   uConnReference - The connection reference associated
//                   with the monitored connection.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_OBJECT_NOT_FOUND
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This call returns a connection reference to a
//                   connection that is monitored.  This connection
//                   reference may be used to open the connection.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetTreeMonitoredConnRef {
	PNwcString pTreeName;
	u32 uConnReference;

} NwcGetTreeMonitoredConnRef, *PNwcGetTreeMonitoredConnRef;

//++=======================================================================
//  API Name:        NwcGetNumberConns
//
//  Arguments In:    NONE
//
//  Arguments Out:   uMaxConns - The maximum number of connections
//                   supported by the redirector.  -1 for dynamic.
//
//                   uPublicConns - The current number of public
//                   connections.
//
//                   uTasksPrivateConns - The current number of private
//                   connections that are owned by the calling process.
//
//                   uOtherPrivateConns - The current number of private
//                   connections that are not owned by the calling
//                   process.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API returns the current number of connections
//                   as well as the maximum number of supported
//                   connections.  If the requester/redirector supports
//                   a dynamic connection table, -1 will be returned
//                   in the uMaxConns field.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetNumberConns {
	u32 uMaxConns;
	u32 uPublicConns;
	u32 uTasksPrivateConns;
	u32 uOtherPrivateConns;

} NwcGetNumberConns, *PNwcGetNumberConns;

//++=======================================================================
//  API Name:        NwcGetPreferredServer
//
//  Arguments In:    uServerNameLength - On input, this is the length
//                   in bytes of the server buffer.  On output, this is
//                   the actual length of the server name string in bytes.
//
//  Arguments Out:   pServerName - The buffer to copy the preferred server
//                   name into.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//                   NWE_OBJECT_NOT_FOUND
//                   NWE_PARAM_INVALID
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API returns the configured preferred bindery
//                   server previously set either by configuration or
//                   by calling NwcSetPreferredServer.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetPreferredServer {
	u32 uServerNameLength;
	char *pServerName;

} NwcGetPreferredServer, *PNwcGetPreferredServer;

//++=======================================================================
//  API Name:        NwcGetPreferredDsTree
//
//  Arguments In:    uTreeLength - On input, this is the length in bytes
//                   of the DS tree name buffer.  On output, this is the
//                   actual length of the DS tree name string in bytes.
//
//  Arguments Out:   pDsTreeName - The buffer to copy the DS tree name into.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//                   NWE_PARAM_INVALID
//                   NWE_DS_PREFERRED_NOT_FOUND
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API returns the preferred DS tree name that was
//                   previously set either by configuration or
//                   by calling NwcSetPreferredDsTree.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--
typedef struct tagNwcGetPreferredDsTree {
	u32 uTreeLength;
	unsigned char *pDsTreeName;
} NwcGetPreferredDsTree, *PNwcGetPreferredDsTree;

//++=======================================================================
//  API Name:        NwcGetPrimaryConnection
//
//  Arguments In:    NONE
//
//  Arguments Out:   uConnReference - Reference to the primary connection.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_PRIMARY_NOT_SET
//
//  Abstract:        This API returns the reference to the current primary
//                   connection in the redirector.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetPrimaryConnection {
	u32 uConnReference;

} NwcGetPrimaryConnection, *PNwcGetPrimaryConnection;

//++=======================================================================
//  API Name:        NwcGetRequesterVersion
//
//  Arguments In:    NONE
//
//  Arguments Out:   uMajorVersion
//                   uMinorVersion
//                   uRevision
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:        This API returns the major version, minor version and
//                   revision of the requester/redirector.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetRequesterVersion {
	u32 uMajorVersion;
	u32 uMinorVersion;
	u32 uRevision;

} NwcGetRequesterVersion, *PNwcGetRequesterVersion;

//++=======================================================================
//  API Name:        NwcLicenseConn
//
//  Arguments In:    ConnHandle - An open connection handle that is in
//                   an unlicensed state.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_HANDLE_ALREADY_LICENSED
//
//
//  Abstract:        This API changes a connections state to licensed.
//                   The licensed count will be incremented, and if
//                   necessary, the license NCP will be sent.
//                   If this handle is already in a licensed state,
//                   an error will be returned.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcLicenseConn {
	NW_CONN_HANDLE ConnHandle;

} NwcLicenseConn, *PNwcLicenseConn;

//++=======================================================================
//  API Name:        NwcMakeConnPermanent
//
//  Arguments In:    ConnHandle - An open connection handle associated
//                   with the connection to be made permanent.
//
//  Arguments Out:   NONE
//
//  Returns:         NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_INVALID_OWNER
//
//  Abstract:        This API is used to keep the connection from being
//                   destroyed until a NwcSysCloseConn request is made
//                   on the connection.  This allows the connection to
//                   remain after all processes that have the
//                   connection open terminate.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcMakeConnPermanent {
	NW_CONN_HANDLE ConnHandle;

} NwcMakeConnPermanent, *PNwcMakeConnPermanent;

//++=======================================================================
//  API Name:        NwcMapDrive
//
//  Arguments In:    ConnHandle - The connection handle of the server
//                   to where the drive is to be mapped.
//
//                   LocalUID - Local user ID
//
//                   LocalPathLen - Length of local/link directory path string,
//                   including nul terminator.
//
//                   LocalPathOffset - Offset of local directory path that will
//                   be mapped to NetWare directory path.
//
//                   NetWarePathLen - Offset of NetWare directory path,
//                   including nul terminator.
//
//                   NetWarePathOffset - Offset of NetWare directory path in
//                   structure.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_INSUFFICIENT_RESOURCES
//                   NWE_STRING_TRANSLATION
//
//  Abstract:        This API maps the target drive to the specified
//                   directory.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcMapDrive {
	NW_CONN_HANDLE ConnHandle;
	u32 LocalUID;
	u32 LinkPathLen;
	u32 LinkPathOffset;
	u32 DestPathLen;
	u32 DestPathOffset;

} NwcMapDrive, *PNwcMapDrive;

//++=======================================================================
//  API Name:        NwcUnmapDrive
//
//  Arguments In:    LinkPathLen - Length of local/link path string,
//                   including nul terminator.
//
//                   LinkPath - Local/link path in structure
//                   to be unmapped
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_PARAM_INVALID
//
//  Abstract:        This API deletes a network drive mapping.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcUnmapDrive {
	u32 LinkPathLen;
	unsigned char LinkPath[1];

} NwcUnmapDrive, *PNwcUnmapDrive;

//++=======================================================================
//  API Name:        NWCGetMappedDrives
//
//  Arguments In:
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//
//  Abstract:        This API returns the NetWare mapped drive info
//                   per user.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcMapDriveElem {
	u32 ElemLen;		// Lenght of drive element
	u32 ConnRefernce;	// Connection reference
	u32 LinkPathLen;	// Local/link dir path, length includes nul
	unsigned char LinkPath[1];	// LinkPath[LinkPathLen]
// u32               DirPathLen;       // NetWare dir path, length includes nul (vol:path)
// unsigned char                DirPath[DirPathLen]; // NetWarePath[DirPathLen]
} NwcMapDriveElem, *PNwcMapDriveElem;

typedef struct tagNwcMapDriveBuff {
	u32 MapCount;		// Number of mapped drives
	NwcMapDriveElem MapDriveElem[1];	// MapDriveElem[MapCount]

} NwcMapDriveBuff, *PNwcMapDriveBuff;

typedef struct tagNwcGetMappedDrives {
	u32 MapBuffLen;		// Buffer length (actual buffer size returned)
	PNwcMapDriveBuff MapBuffer;	// Pointer to map buffer

} NwcGetMappedDrives, *PNwcGetMappedDrives;

//++=======================================================================
//  API Name:        NwcGetMountPath
//
//  Arguments In:    MountPathLen - Length of mount path buffer
//                   including nul terminator.
//
//  Arguments Out:   MountPath - Pointer to mount path buffer
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//
//  Abstract:        This API returns the mount point of the NOVFS file
//                   system.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetMountPath {
	u32 MountPathLen;
	unsigned char *pMountPath;

} NwcGetMountPath, *PNwcGetMountPath;

//++=======================================================================
//  API Name:        NwcMonitorConn
//
//  Arguments In:    ConnHandle - The handle associated with the connection
//                   that is to be marked as the monitored connection.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_LOCK
//                   NWE_CONN_INVALID
//
//
//  Abstract:        This call marks the connection associated with the
//                   connection handle as monitored.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcMonitorConn {
	NW_CONN_HANDLE ConnHandle;

} NwcMonitorConn, *PNwcMonitorConn;

//++=======================================================================
//  API Name:        NwcOpenConnByAddr
//
//  Arguments In:    pServiceType - The type of service required.
//
//                   uConnFlags - Specifies whether this connection
//                   should be public or private.
//
//                   pTranAddress - Specifies the transport address of
//                   the service to open a connection on.
//                   a connection to.
//
//  Arguments Out:   ConnHandle - The new connection handle returned.
//                   This handle may in turn be used for all requests
//                   directed to this connection.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_INSUFFICIENT_RESOURCES
//                   NWE_TRAN_INVALID_TYPE
//                   NWE_RESOURCE_LOCK
//                   NWE_UNSUPPORTED_TRAN_TYPE
//
//  Abstract:        This API will create a service connection to
//                   the service specified by the transport address.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcOpenConnByAddr {
	char *pServiceType;
	u32 uConnFlags;
	PNwcTranAddr pTranAddr;
	NW_CONN_HANDLE ConnHandle;

} NwcOpenConnByAddr, *PNwcOpenConnByAddr;

//++=======================================================================
//  API Name:        NwcOpenConnByName
//
//  Arguments In:    ConnHandle - The connection to use when resolving
//                   a name.  For instance, if the name is a bindery name
//                   the requester will scan the bindery of the given
//                   connection to retrieve the service's address.  This
//                   value can also be NULL if the caller doesn't care
//                   which connection is used to resolve the address.
//
//                   pName - A pointer to the name of the service trying
//                   to be connected to.  This string is NULL terminated,
//                   contains no wild cards, and is a maximum of 512
//                   characters long.
//
//                   pServiceType - The type of service required.
//
//                   uConnFlags - Specifies whether this connection
//                   should be public or private.
//
//                   uTranType - Specifies the preferred or required
//                   transport type to be used.
//                   NWC_TRAN_TYPE_WILD may be ORed with the other values
//                   or used alone.  When ORed with another value, the
//                   wild value indicates an unmarked alternative is
//                   acceptable.  When used alone, the current preferred
//                   transport is used.
//
//  Arguments Out:   ConnHandle - The new connection handle returned.
//                   This handle may in turn be used for all requests
//                   directed to this connection.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_BUFFER_OVERFLOW
//                   NWE_INSUFFICIENT_RESOURCES
//                   NWE_INVALID_STRING_TYPE
//                   NWE_RESOURCE_LOCK
//                   NWE_STRING_TRANSLATION
//                   NWE_TRAN_INVALID_TYPE
//                   NWE_UNSUPPORTED_TRAN_TYPE
//
//  Abstract:        This API will resolve the given name to a network
//                   address then create a service connection to the
//                   specified service.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcOpenConnByName {
	NW_CONN_HANDLE ConnHandle;
	PNwcConnString pName;
	char *pServiceType;
	u32 uConnFlags;
	u32 uTranType;
	NW_CONN_HANDLE RetConnHandle;

} NwcOpenConnByName, *PNwcOpenConnByName;

//++=======================================================================
//  API Name:        NwcOpenConnByReference
//
//  Arguments In:    uConnReference - A reference handle which identifies
//                   a valid connection that the caller wants to obtain
//                   a connection handle to.  A reference handle can be
//                   used to get information about the connection without
//                   actually getting a handle to it.  A connection handle
//                   must be used to make actual requests to that
//                   connection.
//
//                   uConnFlags - Currently unused.
//
//  Arguments Out:   ConnHandle - The new connection handle returned.
//                   This handle may in turn be used for all requests
//                   directed to this connection.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        This API will open the connection associated with
//                   the given connection reference.  The connection
//                   reference can be obtained by calling the
//                   NwcScanConnInfo API.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcOpenConnByReference {
	u32 uConnReference;
	u32 uConnFlags;
	NW_CONN_HANDLE ConnHandle;

} NwcOpenConnByReference, *PNwcOpenConnByReference;

//++=======================================================================
//  API Name:        NwcRawRequest
//
//  Arguments In:    ConnHandle - The connection handle of the connection
//                   that the request is being directed to.
//
//                   uFunction - The NCP function that is being called.
//
//                   uNumRequestFrags - The number of fragments that the
//                   request packet has been broken into.
//
//                   pRequestFrags - List of fragments that make up the
//                   request packet.  Each fragment includes the length
//                   of the fragment data and a pointer to the data.
//
//                   uNumReplyFrags - The number of fragments the reply
//                   packet has been broken into.
//
//  Arguments Out:   pReplyFrags - List of fragments that make up the
//                   request packet.  Each fragment includes the length
//                   of the fragment data and a pointer to the data.
//
//                   uActualReplyLength - Total size of the reply packet
//                   after any header and tail information is removed.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        API for sending raw NCP packets directly to a server.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcRequest {
	NW_CONN_HANDLE ConnHandle;
	u32 uFunction;
	u32 uNumRequestFrags;
	PNwcFrag pRequestFrags;
	u32 uNumReplyFrags;
	PNwcFrag pReplyFrags;
	u32 uActualReplyLength;

} NwcRequest, *PNwcRequest;

//++=======================================================================
//  API Name:        NwcRawRequestAll
//
//  Arguments In:    uFunction - The NCP function that is being called.
//
//                   uNumRequestFrags - The number of fragments that the
//                   request packet has been broken into.
//
//                   pRequestFrags - List of fragments that make up the
//                   request packet.  Each fragment includes the length
//                   of the fragment data and a pointer to the data.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        API for sending the given NCP request to all valid
//                   connections.  If there is a private connection that
//                   is not owned by the caller of this function, that
//                   connection will not be included.  Also, if the
//                   caller has both a private and a public connection
//                   to the same server, only the private connection
//                   will receive the request.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcRequestAll {
	u32 uFunction;
	u32 uNumRequestFrags;
	PNwcFrag pRequestFrags;

} NwcRequestAll, *PNwcRequestAll;

//++=======================================================================
//  API Name:        NwcScanConnInfo
//
//  Arguments In:    uScanIndex - The index to be used on the next
//                   iteration of the scan.  This value should be initially
//                   set to zero.  The output of this parameter will be
//                   used in subsequent calls to this function.
//
//                   uScanInfoLevel - Describes the composition of the
//                   pScanConnInfo pointer.  If this parameter contains
//                   NWC_CONN_INFO_RETURN_ALL, information for all
//                   connections will be returned.
//
//                   uScanInfoLen - Lenght of pScanConnInfo buffer
//
//                   pScanConnInfo - This parameter is a pointer to
//                   data that describes one piece of connection
//                   information.  The type of this data depends on
//                   which level of information is being scanned for.
//                   For instance, if the scan is being used to find all
//                   connections with a particular authentication state,
//                   pScanConnInfo would be a "pnuint" since
//                   authentication state is described as nuint in the
//                   NwcConnInfo structure.
//
//                   uScanFlag - This parameter tells whether to return
//                   connection information for connections that match
//                   the scan criteria or that do not match the scan
//                   criteria.  If the caller wants to find all the
//                   connections that are not in the "NOVELL_INC" DS
//                   tree, he would use the call as described below in
//                   the description except the uScanFlag parameter would
//                   have  the value of NWC_MATCH_NOT_EQUALS.  This flag
//                   is also used to tell the requester whether to
//                   return private or public, licensed or unlicensed
//                   connections.
//
//                   uReturnInfoLevel - Specifies what information
//                   should be returned.
//
//                   uReturnInfoLength - The size in bytes of pConnInfo.
//
//  Arguments Out:   uConnectionReference - Connection reference
//                   associated with the information that is being
//                   returned.
//
//                   pReturnConnInfo - A pointer to the NwcConnInfo
//                   structure defined above.  In some of the
//                   structures within the union, there are pointers to
//                   data to be returned.  It is the responsibility of
//                   the caller to provide pointers to valid memory
//                   to copy this data into.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_LOCK
//                   NWE_CONN_INVALID
//                   NWE_INVALID_LEVEL
//                   NWE_STRING_TRANSLATION
//                   NWE_INVALID_MATCH_DATA
//                   NWE_MATCH_FAILED
//                   NWE_BUFFER_OVERFLOW
//                   NWE_NO_MORE_ENTRIES
//
//  Abstract:        This API is used to return connection information
//                   for multiple connections.  It will return one
//                   piece or the full structure of connection information
//                   for one connection at a time.  This call is designed
//                   to scan for connections based on any piece of
//                   connection information as described in the
//                   NwcConnInfo structure.  For instance, if the caller
//                   wants to scan for all connections in the DS tree
//                   "NOVELL_INC", the call would be made with the
//                   following paramters:
//
//                      uScanLevelInfo = NWC_CONN_INFO_TREE_NAME
//                      pScanConnInfo = "NOVELL_INC"
//                      uScanFlag = NWC_MATCH_EQUALS |
//                                  NWC_RETURN_PUBLIC |
//                                  NWC_RETURN_LICENSED
//
//                   The scan flag is used to tell if the scan is
//                   supposed to return connections that match or don't
//                   match.  This design doesn't allow any other
//                   conditions for this flag (such as greater than or
//                   less than).
//
//                   If the caller specifies the uReturnInfoLevel =
//                   NWC_CONN_INFO_RETURN_ALL, the full NwcConnInfo
//                   structure is returned.  The caller must supply
//                   data for any pointers in the NwcConnInfo structure
//                   (these include tree name, workgroup id, server name
//                   and transport address).  However if the caller
//                   doesn't want to get a particular piece of info
//                   that is expecting a pointer to some data, a NULL
//                   pointer may be used to indicate to the requester
//                   that it should not return that piece of information.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcScanConnInfo {
	u32 uScanIndex;
	u32 uScanInfoLevel;
	u32 uScanInfoLen;
	void *pScanConnInfo;
	u32 uScanFlags;
	u32 uReturnInfoLevel;
	u32 uReturnInfoLength;
	u32 uConnectionReference;
	void *pReturnConnInfo;

} NwcScanConnInfo, *PNwcScanConnInfo;

//++=======================================================================
//  API Name:        NwcSetConnInfo
//
//  Arguments In:    ConnHandle - Connection handle for the connection to
//                   set information on.
//
//                   uInfoLevel - Specifies what information should be set.
//
//                   uInfoLen - Length in bytes of the information being set.
//
//                   pConnInfo - Connection information to set.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_RESOURCE_LOCK
//                   NWE_CONN_INVALID
//                   NWE_INVALID_LEVEL
//
//
//  Abstract:        This API sets information in the connection associated
//                   with the connection handle.
//
//  Notes:           At this time the only setable information levels are:
//                      NWC_CONN_INFO_AUTH_STATE
//                      NWC_CONN_INFO_BCAST_STATE
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetConnInfo {
	NW_CONN_HANDLE ConnHandle;
	u32 uInfoLevel;
	u32 uInfoLength;
	void *pConnInfo;

} NwcSetConnInfo, *PNwcSetConnInfo;

//++=======================================================================
//  API Name:        NwcSetDefaultNameContext
//
//  Arguments In::   uTreeLength - Length of tree string.
//
//                   pDsTreeName - The tree string (multi-byte).
//
//                   uNameLength - The length in bytes of the name
//                   context string.
//
//                   pNameContext - The string to be used as the default
//                   name context (multi-byte).
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_PARAM_INVALID
//                   NWE_RESOURCE_LOCK
//                   NWE_STRING_TRANSLATION
//
//  Abstract:        This API sets the default name context.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetDefaultNameContext {
	u32 uTreeLength;
	unsigned char *pDsTreeName;
	u32 uNameLength;
// unsined short *pNameContext;
	unsigned char *pNameContext;

} NwcSetDefaultNameContext, *PNwcSetDefaultNameContext;

//++=======================================================================
//  API Name:        NwcSetPreferredDsTree
//
//  Arguments In:    uTreeLength - The length in bytes of the DS tree name.
//
//                   pDsTreeName - The string to be used as the preferred
//                   DS tree name.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_INSUFFICIENT_RESOURCES
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API sets the preferred DS tree name.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetPreferredDsTree {
	u32 uTreeLength;
	unsigned char *pDsTreeName;

} NwcSetPreferredDsTree, *PNwcSetPreferredDsTree;

//++=======================================================================
//  API Name:        NwcSetPreferredServer
//
//  Arguments In:    uServerNameLength - The length in bytes of the
//                   preferred server string.
//
//                   pServerName - a pointer to an ASCIIZ string of the
//                   preferred bindery server.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_INSUFFICIENT_RESOURCES
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API sets the preferred server name.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetPreferredServer {
	u32 uServerNameLength;
	char *pServerName;

} NwcSetPreferredServer, *PNwcSetPreferredServer;

//++=======================================================================
//  API Name:        NwcSetPrimaryConnection
//
//  Arguments In:    ConnHandle - Connection handle associated to the
//                   connection reference which the caller wishes to set
//                   as primary.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_PRIMARY_NOT_SET
//
//  Abstract:        This API sets the primary connection according to
//                   the connection handle passed in by the caller.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetPrimaryConnection {
	NW_CONN_HANDLE ConnHandle;

} NwcSetPrimaryConnection, *PNwcSetPrimaryConnection;

//++=======================================================================
//  API Name:        NwcSysCloseConn
//
//  Arguments In:    ConnHandle - The handle to a connection that is
//                   to be destroyed.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        This API is similiar to the NwcCloseConn API, except
//                   that it forces all handles to the connection closed
//                   and destroys the service connection.  This is a system
//                   level request that will cause all processes that are
//                   accessing this connection to lose access to the
//                   resources associated to the connection.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSysCloseConn {
	NW_CONN_HANDLE ConnHandle;

} NwcSysCloseConn, *PNwcSysCloseConn;

//++=======================================================================
//  API Name:        NwcUnlicenseConn
//
//  Arguments In:    ConnHandle - Open connection handle that will be
//                   accessing the connection in an unlicensed manner.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_HANDLE_ALREADY_UNLICENSED
//
//  Abstract:        This API is used to change the state of a connection
//                   handle from licensed to unlicensed.  If all handles
//                   to the connection have been changed to the unlicensed
//                   state, the unlicensed NCP is sent to the server.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcUnlicenseConn {
	NW_CONN_HANDLE ConnHandle;

} NwcUnlicenseConn, *PNwcUnlicenseConn;

//++=======================================================================
//  API Name:        NwcQueryFeature
//
//  Arguments In:    Feature - The number associated with a particular
//                   feature that the caller wants to know if the requester
//                   is supporting
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_REQUESTER_FAILURE
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcQueryFeature {
	u32 Feature;

} NwcQueryFeature, *PNwcQueryFeature;

//++=======================================================================
//  API Name:        NWCChangePassword
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcChangeKey {
	PNwcString pDomainName;
	u32 AuthType;
	PNwcString pObjectName;
	u32 NameType;
	u16 ObjectType;
	PNwcString pVerifyPassword;
	PNwcString pNewPassword;

} NwcChangeKey, *PNwcChangeKey;

//++=======================================================================
//  API Name:        NWCEnumerateIdentities            `
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcEnumerateIdentities {
	u32 Iterator;
	PNwcString pDomainName;
	u32 AuthType;
	PNwcString pObjectName;
	u32 NameType;
	u16 ObjectType;
	u32 IdentityFlags;
	AUTHEN_ID AuthenticationId;

} NwcEnumerateIdentities, *PNwcEnumerateIdentities;

//++=======================================================================
//  API Name:        NWCGetIdentityInfo
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcGetIdentityInfo {
	AUTHEN_ID AuthenticationId;
	PNwcString pDomainName;
	u32 AuthType;
	PNwcString pObjectName;
	u32 NameType;
	u16 ObjectType;
	u32 IdentityFlags;

} NwcGetIdentityInfo, *PNwcGetIdentityInfo;

//++=======================================================================
//  API Name:        NWCLoginIdentity
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcLoginIdentity {
	PNwcString pDomainName;
	u32 AuthType;
	PNwcString pObjectName;
	u32 NameType;
	u16 ObjectType;
	u32 IdentityFlags;
	PNwcString pPassword;
	AUTHEN_ID AuthenticationId;

} NwcLoginIdentity, *PNwcLoginIdentity;

//++=======================================================================
//  API Name:        NWCLogoutIdentity
////

//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcLogoutIdentity {
	AUTHEN_ID AuthenticationId;

} NwcLogoutIdentity, *PNwcLogoutIdentity;

//++=======================================================================
//  API Name:        NWCSetPassword
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcSetKey {
	NW_CONN_HANDLE ConnHandle;
	AUTHEN_ID AuthenticationId;
	PNwcString pObjectName;
	u16 ObjectType;
	PNwcString pNewPassword;

} NwcSetKey, *PNwcSetKey;

//++=======================================================================
//  API Name:        NWCVerifyPassword
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//++=======================================================================

typedef struct tagNwcVerifyKey {
	PNwcString pDomainName;
	u32 AuthType;
	PNwcString pObjectName;
	u32 NameType;
	u16 ObjectType;
	PNwcString pVerifyPassword;

} NwcVerifyKey, *PNwcVerifyKey;

//++=======================================================================
//  API Name:        NwcAuthenticateWithId
//
//  Arguments In:    ConnHandle - The connection to be authenticated
//
//                   AuthenticationId - the authentication Id associated
//                   to the information necessary to authenticate this
//                   connection.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:        This API is used to authenticate a connection using
//                   an authentication ID that has already been created.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcAuthenticateWithId {
	NW_CONN_HANDLE ConnHandle;
	AUTHEN_ID AuthenticationId;

} NwcAuthenticateWithId, *PNwcAuthenticateWithId;

//++=======================================================================
//  API Name:        NwcUnauthenticate
//
//  Arguments In:    ConnHandle - The connection to unauthenticate.
//
//  Arguments Out:   NONE
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//                   NWE_INVALID_OWNER
//                   NWE_RESOURCE_LOCK
//
//  Abstract:        This API removes the authentication for the specified
//                   connection.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcUnauthenticate {
	NW_CONN_HANDLE ConnHandle;
	AUTHEN_ID AuthenticationId;

} NwcUnauthenticate, *PNwcUnauthenticate;

//++=======================================================================
//  API Name:        NwcGetCfgNameServiceProviders
//
//  Arguments In:
//
//  Arguments Out:
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//
//  Abstract:
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct {
	u32 providerCount;
	u32 providers[MAX_NAME_SERVICE_PROVIDERS];

} NwcGetCfgNameServiceProviders, *PNwcGetCfgNameServiceProviders;

//++=======================================================================
//  API Name:        NwcNdsResolveNameToId
//
//  Arguments In:    connHandle
//                      Specifies connection to use to resolve name with.
//
//                   pName
//                      Points to the name of the NDS entry to resolve.
//
//                   uReqTranType
//                      Specifies the preferred or required transport to
//                      be used.
//
//                   pResolveInfo
//                      Points to the NwcNdsResolveInfo structure
//                      containing information on how the entry is to be
//                      resolved.
//
//  Arguments Out:   pResolveInfo
//                      Points to the NwcNdsResolveInfo structure
//                      containing return information on the resolved
//                      entry.
//
//                   pluEntryId
//                      Points to the resolved name's entry ID.
//
//                   pReferral
//                      Points to the NwcReferral structure which describes
//                      network addresses that can be used to locate other
//                      NDS partitions that contain the entry name.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_CONN_INVALID,
//                   NWE_BUFFER_OVERFLOW,
//                   NWE_TRAN_INVALID_TYPE,
//                   NWE_ACCESS_VIOLATION,
//                   NWE_UNSUPPORTED_TRAN_TYPE,
//                   Nds error code
//
//  Abstract:        This API resolves a NDS entry name.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcNdsResolveNameToId {
	NW_CONN_HANDLE connHandle;
	PNwcString pName;
	u32 uReqTranType;
	PNwcResolveInfo pResolveInfo;
	u32 entryId;
	PNwcReferral pReferral;

} NwcNdsResolveNameToId, *PNwcNdsResolveNameToId;

//++=======================================================================
//  API Name:        NwcOrderedRequest
//
//  Arguments In:    uFunction - The NCP function that is being called.
//
//                   uNumRequestFrags - The number of fragments that the
//                   request packet has been broken into.
//
//                   pRequestFrags - List of fragments that make up the
//                   request packet.  Each fragment includes the length
//                   of the fragment data and a pointer to the data.
//
//                   uInverseReqCode - The NCP function that will be called
//                   if the request fails.
//
//                   uNumInverseFrags - The number of fragments the inverse
//                   request packet has been broken into.
//
//                   pReplyFrags - List of fragments that make up the
//                   inverse request packet.  Each fragment includes the length
//                   of the fragment data and a pointer to the data.
//
//  Returns:         STATUS_SUCCESS
//                   NWE_ACCESS_VIOLATION
//                   NWE_CONN_INVALID
//
//  Abstract:        API for sending raw NCP packets directly to a server.
//
//  Notes:
//
//  Environment:     PASSIVE_LEVEL, LINUX
//
//=======================================================================--

typedef struct tagNwcOrderedRequest {
	u32 uReqCode;
	u32 uNumRequestFrags;
	PNwcFrag pRequestFrags;
	u32 uInverseReqCode;
	u32 uNumInverseFrags;
	PNwcFrag pInverseFrags;

} NwcOrderedRequest, *PNwcOrderedRequest;

#if 1				//sgled
typedef struct tagNwcUnmapDriveEx {
//         unsigned long      connHdl;
	unsigned int linkLen;
	char linkData[1];

} NwcUnmapDriveEx, *PNwcUnmapDriveEx;

typedef struct tagNwcMapDriveEx {
	NW_CONN_HANDLE ConnHandle;
	unsigned int localUid;
	unsigned int linkOffsetLength;
	unsigned int linkOffset;
	unsigned int dirPathOffsetLength;
	unsigned int dirPathOffset;
} NwcMapDriveEx, *PNwcMapDriveEx;

typedef struct tagNwcGetBroadcastNotification {
	u32 uMessageFlags;
	u32 uConnReference;
	u32 messageLen;
	char message[1];
} NwcGetBroadcastNotification, *PNwcGetBroadcastNotification;

#endif
#endif /* __NWCLNX_H__ */
