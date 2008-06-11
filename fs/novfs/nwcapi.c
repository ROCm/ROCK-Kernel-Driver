/*
 * Novell NCP Redirector for Linux
 * Author: James Turner/Richard Williams
 *
 * This file contains functions used to interface to the library interface of
 * the daemon.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "nwcapi.h"
#include "nwerror.h"
#include "vfs.h"
#include "commands.h"

static void GetUserData(NwcScanConnInfo *connInfo, PXPLAT_CALL_REQUEST cmd, PXPLAT_CALL_REPLY reply);
static void GetConnData(NwcGetConnInfo *connInfo, PXPLAT_CALL_REQUEST cmd, PXPLAT_CALL_REPLY reply);


int NwOpenConnByName(PXPLAT pdata, HANDLE * Handle, session_t Session)
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCOpenConnByName openConn, connReply;
	NwcOpenConnByName ocbn;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen, cpylen;
	char *data;

	cpylen = copy_from_user(&ocbn, pdata->reqData, sizeof(ocbn));
	datalen = sizeof(*openConn) + strlen_user(ocbn.pName->pString) + strlen_user(ocbn.pServiceType);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_OPEN_CONN_BY_NAME;

	cmd->dataLen = datalen;
	openConn = (PNwdCOpenConnByName) cmd->data;

	openConn->nameLen = strlen_user(ocbn.pName->pString);
	openConn->serviceLen = strlen_user(ocbn.pServiceType);
	openConn->uConnFlags = ocbn.uConnFlags;
	openConn->ConnHandle = Uint32toHandle(ocbn.ConnHandle);
	data = (char *)openConn;
	data += sizeof(*openConn);
	openConn->oName = sizeof(*openConn);

	openConn->oServiceType = openConn->oName + openConn->nameLen;
	cpylen = copy_from_user(data, ocbn.pName->pString, openConn->nameLen);
	data += openConn->nameLen;
	cpylen = copy_from_user(data, ocbn.pServiceType, openConn->serviceLen);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					(void **)&reply, &replylen,
					INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (PNwdCOpenConnByName) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (PNwdCOpenConnByName) reply->data;
			ocbn.RetConnHandle = HandletoUint32(connReply->newConnHandle);
			*Handle = connReply->newConnHandle;

			cpylen = copy_to_user(pdata->reqData, &ocbn, sizeof(ocbn));
			DbgPrint("New Conn Handle = %X\n", connReply->newConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);

	return retCode;

}

int NwOpenConnByAddr(PXPLAT pdata, HANDLE * Handle, session_t Session)
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCOpenConnByAddr openConn, connReply;
	NwcOpenConnByAddr ocba;
	NwcTranAddr tranAddr;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen, cpylen;
	char addr[MAX_ADDRESS_LENGTH];

	cpylen = copy_from_user(&ocba, pdata->reqData, sizeof(ocba));
	datalen = sizeof(*openConn);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_OPEN_CONN_BY_ADDRESS;
	cmd->dataLen = datalen;
	openConn = (PNwdCOpenConnByAddr) cmd->data;

	cpylen = copy_from_user(&tranAddr, ocba.pTranAddr, sizeof(tranAddr));

	DbgPrint("NwOpenConnByAddr: tranAddr\n");
	mydump(sizeof(tranAddr), &tranAddr);

	openConn->TranAddr.uTransportType = tranAddr.uTransportType;
	openConn->TranAddr.uAddressLength = tranAddr.uAddressLength;
	memset(addr, 0xcc, sizeof(addr) - 1);

	cpylen = copy_from_user(addr, tranAddr.puAddress, tranAddr.uAddressLength);

	DbgPrint("NwOpenConnByAddr: addr\n");
	mydump(sizeof(addr), addr);

	openConn->TranAddr.oAddress = *(unsigned int*) (&addr[2]);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					(void **)&reply, &replylen,
					INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (PNwdCOpenConnByAddr) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (PNwdCOpenConnByAddr) reply->data;
			ocba.ConnHandle = HandletoUint32(connReply->ConnHandle);
			*Handle = connReply->ConnHandle;
			cpylen = copy_to_user(pdata->reqData, &ocba, sizeof(ocba));
			DbgPrint("New Conn Handle = %X\n", connReply->ConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);
	return retCode;
}

/*++======================================================================*/
int NwOpenConnByRef(PXPLAT pdata, HANDLE * Handle, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCOpenConnByRef openConn;
	NwcOpenConnByReference ocbr;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&ocbr, pdata->reqData, sizeof(ocbr));
	datalen = sizeof(*openConn);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_OPEN_CONN_BY_REFERENCE;
	cmd->dataLen = datalen;
	openConn = (PNwdCOpenConnByRef) cmd->data;

	openConn->uConnReference = (HANDLE) (unsigned long) ocbr.uConnReference;
	openConn->uConnFlags = ocbr.uConnFlags;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					(void **)&reply, &replylen,
					INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		openConn = (PNwdCOpenConnByRef) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			ocbr.ConnHandle = HandletoUint32(openConn->ConnHandle);
			*Handle = openConn->ConnHandle;

			cpylen = copy_to_user(pdata->reqData, &ocbr, sizeof(ocbr));
			DbgPrint("New Conn Handle = %X\n", openConn->ConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);
	return (retCode);

}

int NwRawSend(PXPLAT pdata, session_t Session)
{
	NwcRequest xRequest;
	PNwcFrag frag = NULL;
	PNwcFrag cFrag = NULL;
	PNwcFrag reqFrag = NULL;
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen, totalLen;
	unsigned int x;
	PNwdCNCPReq ncpData;
	PNwdCNCPRep ncpReply;
	unsigned char *reqData;
	unsigned long actualReplyLength = 0;

	DbgPrint("[XPLAT] Process Raw NCP Send\n");
	cpylen = copy_from_user(&xRequest, pdata->reqData, sizeof(xRequest));

	/*
	 * Figure out the length of the request
	 */
	frag = kmalloc(xRequest.uNumReplyFrags * sizeof(NwcFrag), GFP_KERNEL);
	DbgPrint("[XPLAT RawNCP] - Reply Frag Count 0x%X\n", xRequest.uNumReplyFrags);

	if (!frag)
		goto exit;

	cpylen = copy_from_user(frag, xRequest.pReplyFrags, xRequest.uNumReplyFrags * sizeof(NwcFrag));
	totalLen = 0;

	cFrag = frag;
	for (x = 0; x < xRequest.uNumReplyFrags; x++) {
		DbgPrint("[XPLAT - RawNCP] - Frag Len = %d\n", cFrag->uLength);
		totalLen += cFrag->uLength;
		cFrag++;
	}

	DbgPrint("[XPLAT - RawNCP] - totalLen = %d\n", totalLen);
	datalen = 0;
	reqFrag = kmalloc(xRequest.uNumRequestFrags * sizeof(NwcFrag), GFP_KERNEL);
	if (!reqFrag)
		goto exit;

	cpylen = copy_from_user(reqFrag, xRequest.pRequestFrags, xRequest.uNumRequestFrags * sizeof(NwcFrag));
	cFrag = reqFrag;
	for (x = 0; x < xRequest.uNumRequestFrags; x++) {
		datalen += cFrag->uLength;
		cFrag++;
	}

	/*
	 * Allocate the cmd Request
	 */
	cmdlen = datalen + sizeof(*cmd) + sizeof(*ncpData);
	DbgPrint("[XPLAT RawNCP] - Frag Count 0x%X\n", xRequest.uNumRequestFrags);
	DbgPrint("[XPLAT RawNCP] - Total Command Data Len = %x\n", cmdlen);

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		goto exit;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_RAW_NCP_REQUEST;

	/*
	 * build the NCP Request
	 */
	cmd->dataLen = cmdlen - sizeof(*cmd);
	ncpData = (PNwdCNCPReq) cmd->data;
	ncpData->replyLen = totalLen;
	ncpData->requestLen = datalen;
	ncpData->ConnHandle = (HANDLE) (unsigned long) xRequest.ConnHandle;
	ncpData->function = xRequest.uFunction;

	reqData = ncpData->data;
	cFrag = reqFrag;

	for (x = 0; x < xRequest.uNumRequestFrags; x++) {
		cpylen =
		    copy_from_user(reqData, cFrag->pData,
				   cFrag->uLength);
		reqData += cFrag->uLength;
		cFrag++;
	}

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					(void **)&reply, &replylen,
					INTERRUPTIBLE);
	DbgPrint("RawNCP - reply = %x\n", reply);
	DbgPrint("RawNCP - retCode = %x\n", retCode);

	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		ncpReply = (PNwdCNCPRep) reply->data;
		retCode = reply->Reply.ErrorCode;

		DbgPrint("RawNCP - Reply Frag Count 0x%X\n",
			 xRequest.uNumReplyFrags);

		/*
		 * We need to copy the reply frags to the packet.
		 */
		reqData = ncpReply->data;
		cFrag = frag;

		totalLen = ncpReply->replyLen;
		for (x = 0; x < xRequest.uNumReplyFrags; x++) {

			DbgPrint("RawNCP - Copy Frag %d: 0x%X\n", x,
				 cFrag->uLength);

			datalen = min((unsigned long)cFrag->uLength, totalLen);

			cpylen = copy_to_user(cFrag->pData, reqData, datalen);
			totalLen -= datalen;
			reqData += datalen;
			actualReplyLength += datalen;

			cFrag++;
		}

		kfree(reply);
	} else {
		retCode = -EIO;
	}

	kfree(cmd);

	xRequest.uActualReplyLength = actualReplyLength;
	cpylen = copy_to_user(pdata->reqData, &xRequest, sizeof(xRequest));

exit:
	kfree(reqFrag);
	kfree(frag);
	return retCode;
}

int NwConnClose(PXPLAT pdata, HANDLE * Handle, session_t Session)
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcCloseConn cc;
	PNwdCCloseConn nwdClose;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&cc, pdata->reqData, sizeof(cc));

	datalen = sizeof(*nwdClose);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_CLOSE_CONN;

	nwdClose = (PNwdCCloseConn) cmd->data;
	cmd->dataLen = sizeof(*nwdClose);
	*Handle = nwdClose->ConnHandle = Uint32toHandle(cc.ConnHandle);

	/*
	 * send the request
	 */
	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					(void **)&reply, &replylen, 0);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return retCode;
}

int NwSysConnClose(PXPLAT pdata, unsigned long *Handle, session_t Session)
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcCloseConn cc;
	PNwdCCloseConn nwdClose;
	unsigned int retCode = 0;
	unsigned long cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&cc, pdata->reqData, sizeof(cc));

	datalen = sizeof(*nwdClose);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SYS_CLOSE_CONN;

		nwdClose = (PNwdCCloseConn) cmd->data;
		cmd->dataLen = sizeof(*nwdClose);
		nwdClose->ConnHandle = (HANDLE) (unsigned long) cc.ConnHandle;
		*Handle = (unsigned long) cc.ConnHandle;

		/*
		 * send the request
		 */
		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen, 0);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);

	}

	return (retCode);

}

/*++======================================================================*/
int NwLoginIdentity(PXPLAT pdata, struct schandle *Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	NwcLoginIdentity lgn, *plgn;
	int retCode = -ENOMEM;
	NclString server;
	NclString username;
	NclString password;
	unsigned long cpylen;
	NwcString nwcStr;

	cpylen = copy_from_user(&lgn, pdata->reqData, sizeof(lgn));

	DbgPrint("NwLoginIdentity:\n");
	mydump(sizeof(lgn), &lgn);

	cpylen = copy_from_user(&nwcStr, lgn.pDomainName, sizeof(nwcStr));
	DbgPrint("NwLoginIdentity: DomainName\n");
	mydump(sizeof(nwcStr), &nwcStr);

	if ((server.buffer = Novfs_Malloc(nwcStr.DataLen, GFP_KERNEL))) {
		server.type = nwcStr.DataType;
		server.len = nwcStr.DataLen;
		if (!copy_from_user
		    ((void *)server.buffer, nwcStr.pBuffer, server.len)) {
			DbgPrint("NwLoginIdentity: Server\n");
			mydump(server.len, server.buffer);

			cpylen =
			    copy_from_user(&nwcStr, lgn.pObjectName,
					   sizeof(nwcStr));
			DbgPrint("NwLoginIdentity: ObjectName\n");
			mydump(sizeof(nwcStr), &nwcStr);

			if ((username.buffer =
			     Novfs_Malloc(nwcStr.DataLen, GFP_KERNEL))) {
				username.type = nwcStr.DataType;
				username.len = nwcStr.DataLen;
				if (!copy_from_user
				    ((void *)username.buffer, nwcStr.pBuffer,
				     username.len)) {
					DbgPrint("NwLoginIdentity: User\n");
					mydump(username.len, username.buffer);

					cpylen =
					    copy_from_user(&nwcStr,
							   lgn.pPassword,
							   sizeof(nwcStr));
					DbgPrint("NwLoginIdentity: Password\n");
					mydump(sizeof(nwcStr), &nwcStr);

					if ((password.buffer =
					     Novfs_Malloc(nwcStr.DataLen,
							  GFP_KERNEL))) {
						password.type = nwcStr.DataType;
						password.len = nwcStr.DataLen;
						if (!copy_from_user
						    ((void *)password.buffer,
						     nwcStr.pBuffer,
						     password.len)) {
							retCode =
							    do_login(&server,
								     &username,
								     &password,
								     (HANDLE *)&lgn.AuthenticationId,
								     Session);
							if (retCode) {
								lgn.AuthenticationId = 0;
							}

							plgn =
							    (NwcLoginIdentity *)
							    pdata->reqData;
							cpylen =
							    copy_to_user(&plgn->
									 AuthenticationId,
									 &lgn.
									 AuthenticationId,
									 sizeof
									 (plgn->
									  AuthenticationId));

						}
						memset(password.buffer, 0,
						       password.len);
						kfree(password.buffer);
					}
				}
				memset(username.buffer, 0, username.len);
				kfree(username.buffer);
			}
		}
		kfree(server.buffer);
	}
	return (retCode);
}

/*++======================================================================*/
int NwAuthConnWithId(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	NwcAuthenticateWithId pauth;
	PNwdCAuthenticateWithId pDauth;
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDauth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_AUTHENTICATE_CONN_WITH_ID;

		cpylen = copy_from_user(&pauth, pdata->reqData, sizeof(pauth));

		pDauth = (PNwdCAuthenticateWithId) cmd->data;
		cmd->dataLen = datalen;
		pDauth->AuthenticationId = pauth.AuthenticationId;
		pDauth->ConnHandle = (HANDLE) (unsigned long) pauth.ConnHandle;

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);
}

/*++======================================================================*/
int NwLicenseConn(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcLicenseConn lisc;
	PNwdCLicenseConn pDLisc;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDLisc);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_LICENSE_CONN;

		cpylen = copy_from_user(&lisc, pdata->reqData, sizeof(lisc));

		pDLisc = (PNwdCLicenseConn) cmd->data;
		cmd->dataLen = datalen;
		pDLisc->ConnHandle = (HANDLE) (unsigned long) lisc.ConnHandle;

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);
}

/*++======================================================================*/
int NwLogoutIdentity(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcLogoutIdentity logout;
	PNwdCLogoutIdentity pDLogout;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDLogout);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_LOGOUT_IDENTITY;

		cpylen =
		    copy_from_user(&logout, pdata->reqData, sizeof(logout));

		pDLogout = (PNwdCLogoutIdentity) cmd->data;
		cmd->dataLen = datalen;
		pDLogout->AuthenticationId = logout.AuthenticationId;

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);
}

/*++======================================================================*/
int NwUnlicenseConn(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCUnlicenseConn pUconn;
	NwcUnlicenseConn ulc;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&ulc, pdata->reqData, sizeof(ulc));
	datalen = sizeof(*pUconn);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_UNLICENSE_CONN;
		cmd->dataLen = datalen;
		pUconn = (PNwdCUnlicenseConn) cmd->data;

		pUconn->ConnHandle = (HANDLE) (unsigned long) ulc.ConnHandle;
		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			/*
			 * we got reply data from the daemon
			 */
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}

		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwUnAuthenticate(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcUnauthenticate auth;
	PNwdCUnauthenticate pDAuth;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDAuth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_UNAUTHENTICATE_CONN;

		cpylen = copy_from_user(&auth, pdata->reqData, sizeof(auth));

		pDAuth = (PNwdCUnauthenticate) cmd->data;
		cmd->dataLen = datalen;
		pDAuth->AuthenticationId = auth.AuthenticationId;
		pDAuth->ConnHandle = (HANDLE) (unsigned long) auth.ConnHandle;

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwGetConnInfo(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcGetConnInfo connInfo;
	PNwdCGetConnInfo pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(NwcGetConnInfo));

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_CONN_INFO;

		pDConnInfo = (PNwdCGetConnInfo) cmd->data;

		pDConnInfo->ConnHandle = (HANDLE) (unsigned long) connInfo.ConnHandle;
		pDConnInfo->uInfoLevel = connInfo.uInfoLevel;
		pDConnInfo->uInfoLength = connInfo.uInfoLength;
		cmd->dataLen = sizeof(*pDConnInfo);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			if (!retCode) {
				GetConnData(&connInfo, cmd, reply);
			}

			kfree(reply);
		}
		kfree(cmd);

	}

	return (retCode);

}

/*++======================================================================*/
int NwSetConnInfo(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcSetConnInfo connInfo;
	PNwdCSetConnInfo pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(NwcSetConnInfo));

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SET_CONN_INFO;

		pDConnInfo = (PNwdCSetConnInfo) cmd->data;

		pDConnInfo->ConnHandle = (HANDLE) (unsigned long) connInfo.ConnHandle;
		pDConnInfo->uInfoLevel = connInfo.uInfoLevel;
		pDConnInfo->uInfoLength = connInfo.uInfoLength;
		cmd->dataLen = sizeof(*pDConnInfo);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);

	}

	return (retCode);

}

/*++======================================================================*/
int NwGetIdentityInfo(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcGetIdentityInfo qidInfo, *gId;
	PNwdCGetIdentityInfo idInfo;
	NwcString xferStr;
	char *str;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*idInfo);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	cpylen = copy_from_user(&qidInfo, pdata->reqData, sizeof(qidInfo));

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_IDENTITY_INFO;

		idInfo = (PNwdCGetIdentityInfo) cmd->data;

		idInfo->AuthenticationId = qidInfo.AuthenticationId;
		cmd->dataLen = sizeof(*idInfo);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;

			if (!reply->Reply.ErrorCode) {
				/*
				 * Save the return info to the user structure.
				 */
				gId = pdata->reqData;
				idInfo = (PNwdCGetIdentityInfo) reply->data;
				cpylen =
				    copy_to_user(&gId->AuthenticationId,
						 &idInfo->AuthenticationId,
						 sizeof(idInfo->
							AuthenticationId));
				cpylen =
				    copy_to_user(&gId->AuthType,
						 &idInfo->AuthType,
						 sizeof(idInfo->AuthType));
				cpylen =
				    copy_to_user(&gId->IdentityFlags,
						 &idInfo->IdentityFlags,
						 sizeof(idInfo->IdentityFlags));
				cpylen =
				    copy_to_user(&gId->NameType,
						 &idInfo->NameType,
						 sizeof(idInfo->NameType));
				cpylen =
				    copy_to_user(&gId->ObjectType,
						 &idInfo->ObjectType,
						 sizeof(idInfo->ObjectType));

				cpylen =
				    copy_from_user(&xferStr, gId->pDomainName,
						   sizeof(NwcString));
				str =
				    (char *)((char *)reply->data +
					     idInfo->pDomainNameOffset);
				cpylen =
				    copy_to_user(xferStr.pBuffer, str,
						 idInfo->domainLen);
				xferStr.DataType = NWC_STRING_TYPE_ASCII;
				xferStr.DataLen = idInfo->domainLen;
				cpylen =
				    copy_to_user(gId->pDomainName, &xferStr,
						 sizeof(NwcString));

				cpylen =
				    copy_from_user(&xferStr, gId->pObjectName,
						   sizeof(NwcString));
				str =
				    (char *)((char *)reply->data +
					     idInfo->pObjectNameOffset);
				cpylen =
				    copy_to_user(xferStr.pBuffer, str,
						 idInfo->objectLen);
				xferStr.DataLen = idInfo->objectLen - 1;
				xferStr.DataType = NWC_STRING_TYPE_ASCII;
				cpylen =
				    copy_to_user(gId->pObjectName, &xferStr,
						 sizeof(NwcString));
			}

			kfree(reply);
		}
		kfree(cmd);

	}

	return (retCode);
}

/*++======================================================================*/
int NwScanConnInfo(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcScanConnInfo connInfo, *rInfo;
	PNwdCScanConnInfo pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;
	unsigned char *localData;

	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(NwcScanConnInfo));

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo) + connInfo.uScanInfoLen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SCAN_CONN_INFO;

		pDConnInfo = (PNwdCScanConnInfo) cmd->data;

		DbgPrint("NwScanConnInfo: Input Data\n");
		DbgPrint("connInfo.uScanIndex = 0x%X\n", connInfo.uScanIndex);
		DbgPrint("connInfo.uConnectionReference = 0x%X\n",
			 connInfo.uConnectionReference);
		DbgPrint("connInfo.uScanInfoLevel = 0x%X\n",
			 connInfo.uScanInfoLevel);
		DbgPrint("connInfo.uScanInfoLen = 0x%X\n",
			 connInfo.uScanInfoLen);
		DbgPrint("connInfo.uReturnInfoLength = 0x%X\n",
			 connInfo.uReturnInfoLength);
		DbgPrint("connInfo.uReturnInfoLevel = 0x%X\n",
			 connInfo.uReturnInfoLevel);
		DbgPrint("connInfo.uScanFlags = 0x%X\n", connInfo.uScanFlags);

		pDConnInfo->uScanIndex = connInfo.uScanIndex;
		pDConnInfo->uConnectionReference =
		    connInfo.uConnectionReference;
		pDConnInfo->uScanInfoLevel = connInfo.uScanInfoLevel;
		pDConnInfo->uScanInfoLen = connInfo.uScanInfoLen;
		pDConnInfo->uReturnInfoLength = connInfo.uReturnInfoLength;
		pDConnInfo->uReturnInfoLevel = connInfo.uReturnInfoLevel;
		pDConnInfo->uScanFlags = connInfo.uScanFlags;

		if (pDConnInfo->uScanInfoLen) {
			localData = (unsigned char *) pDConnInfo;
			pDConnInfo->uScanConnInfoOffset = sizeof(*pDConnInfo);
			localData += pDConnInfo->uScanConnInfoOffset;
			cpylen =
			    copy_from_user(localData, connInfo.pScanConnInfo,
					   connInfo.uScanInfoLen);
		} else {
			pDConnInfo->uScanConnInfoOffset = 0;
		}

		cmd->dataLen = sizeof(*pDConnInfo);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			DbgPrint("NwScanConnInfo: Reply recieved\n");
			DbgPrint("   NextIndex = %x\n", connInfo.uScanIndex);
			DbgPrint("   ErrorCode = %x\n", reply->Reply.ErrorCode);
			DbgPrint("   data = %x\n", reply->data);

			pDConnInfo = (PNwdCScanConnInfo) reply->data;
			retCode = (unsigned long) reply->Reply.ErrorCode;
			if (!retCode) {
				GetUserData(&connInfo, cmd, reply);
				rInfo = (NwcScanConnInfo *) pdata->repData;
				cpylen =
				    copy_to_user(pdata->repData,
						 &pDConnInfo->uScanIndex,
						 sizeof(pDConnInfo->
							uScanIndex));
				cpylen =
				    copy_to_user(&rInfo->uConnectionReference,
						 &pDConnInfo->
						 uConnectionReference,
						 sizeof(pDConnInfo->
							uConnectionReference));
			} else {
				unsigned long x;

				x = 0;
				rInfo = (NwcScanConnInfo *) pdata->reqData;
				cpylen =
				    copy_to_user(&rInfo->uConnectionReference,
						 &x,
						 sizeof(rInfo->
							uConnectionReference));
			}

			kfree(reply);
		} else {
			retCode = -EIO;
		}
		kfree(cmd);

	}

	return (retCode);
}

/*++======================================================================*/
static void GetUserData(NwcScanConnInfo * connInfo, PXPLAT_CALL_REQUEST cmd, PXPLAT_CALL_REPLY reply)
/*
 *  Abstract:  Copies the user data out of the scan conn info call.
 *
 *========================================================================*/
{
	unsigned long uLevel;
	PNwdCScanConnInfo pDConnInfo;

	unsigned char *srcData = NULL;
	unsigned long dataLen = 0, cpylen;

	pDConnInfo = (PNwdCScanConnInfo) reply->data;
	uLevel = pDConnInfo->uReturnInfoLevel;
	DbgPrint
	    ("[GetUserData] uLevel = %d, reply = 0x%p, reply->data = 0x%X\n",
	     uLevel, reply, reply->data);

	switch (uLevel) {
	case NWC_CONN_INFO_RETURN_ALL:
	case NWC_CONN_INFO_NDS_STATE:
	case NWC_CONN_INFO_MAX_PACKET_SIZE:
	case NWC_CONN_INFO_LICENSE_STATE:
	case NWC_CONN_INFO_PUBLIC_STATE:
	case NWC_CONN_INFO_SERVICE_TYPE:
	case NWC_CONN_INFO_DISTANCE:
	case NWC_CONN_INFO_SERVER_VERSION:
	case NWC_CONN_INFO_AUTH_ID:
	case NWC_CONN_INFO_SUSPENDED:
	case NWC_CONN_INFO_WORKGROUP_ID:
	case NWC_CONN_INFO_SECURITY_STATE:
	case NWC_CONN_INFO_CONN_NUMBER:
	case NWC_CONN_INFO_USER_ID:
	case NWC_CONN_INFO_BCAST_STATE:
	case NWC_CONN_INFO_CONN_REF:
	case NWC_CONN_INFO_AUTH_STATE:
	case NWC_CONN_INFO_TREE_NAME:
	case NWC_CONN_INFO_SERVER_NAME:
	case NWC_CONN_INFO_VERSION:
		srcData = (unsigned char *) pDConnInfo;
		srcData += pDConnInfo->uReturnConnInfoOffset;
		dataLen = pDConnInfo->uReturnInfoLength;
		break;

	case NWC_CONN_INFO_TRAN_ADDR:
		{
			unsigned char *dstData = connInfo->pReturnConnInfo;
			NwcTranAddr tranAddr;

			srcData = (unsigned char *) reply->data;
			dataLen = reply->dataLen;

			DbgPrint
			    ("GetUserData NWC_CONN_INFO_TRAN_ADDR 0x%p -> 0x%p :: 0x%X\n",
			     srcData, connInfo->pReturnConnInfo, dataLen);

			cpylen =
			    copy_from_user(&tranAddr, dstData,
					   sizeof(tranAddr));

			srcData +=
			    ((PNwdCScanConnInfo) srcData)->
			    uReturnConnInfoOffset;

			tranAddr.uTransportType =
			    ((PNwdTranAddr) srcData)->uTransportType;
			tranAddr.uAddressLength =
			    ((PNwdTranAddr) srcData)->uAddressLength;

			cpylen =
			    copy_to_user(dstData, &tranAddr, sizeof(tranAddr));
			cpylen =
			    copy_to_user(tranAddr.puAddress,
					 ((PNwdTranAddr) srcData)->Buffer,
					 ((PNwdTranAddr) srcData)->
					 uAddressLength);
			dataLen = 0;
			break;
		}
	case NWC_CONN_INFO_RETURN_NONE:
	case NWC_CONN_INFO_TREE_NAME_UNICODE:
	case NWC_CONN_INFO_SERVER_NAME_UNICODE:
	case NWC_CONN_INFO_LOCAL_TRAN_ADDR:
	case NWC_CONN_INFO_ALTERNATE_ADDR:
	case NWC_CONN_INFO_SERVER_GUID:
	default:
		break;
	}

	if (srcData && dataLen) {
		DbgPrint("Copy Data in GetUserData 0x%p -> 0x%p :: 0x%X\n",
			 srcData, connInfo->pReturnConnInfo, dataLen);
		cpylen =
		    copy_to_user(connInfo->pReturnConnInfo, srcData, dataLen);
	}

	return;
}

/*++======================================================================*/
static void GetConnData(NwcGetConnInfo * connInfo, PXPLAT_CALL_REQUEST cmd, PXPLAT_CALL_REPLY reply)
/*
 *  Abstract:  Copies the user data out of the scan conn info call.
 *
 *========================================================================*/
{
	unsigned long uLevel;
	PNwdCGetConnInfo pDConnInfo;

	unsigned char *srcData = NULL;
	unsigned long dataLen = 0, cpylen;

	pDConnInfo = (PNwdCGetConnInfo) cmd->data;
	uLevel = pDConnInfo->uInfoLevel;

	switch (uLevel) {
	case NWC_CONN_INFO_RETURN_ALL:
		srcData = (unsigned char *) reply->data;
		dataLen = reply->dataLen;
		break;

	case NWC_CONN_INFO_RETURN_NONE:
		dataLen = 0;
		break;

	case NWC_CONN_INFO_TRAN_ADDR:
		{
			unsigned char *dstData = connInfo->pConnInfo;
			NwcTranAddr tranAddr;

			srcData = (unsigned char *) reply->data;

			cpylen =
			    copy_from_user(&tranAddr, dstData,
					   sizeof(tranAddr));
			tranAddr.uTransportType =
			    ((PNwdTranAddr) srcData)->uTransportType;
			tranAddr.uAddressLength =
			    ((PNwdTranAddr) srcData)->uAddressLength;

			cpylen =
			    copy_to_user(dstData, &tranAddr, sizeof(tranAddr));
			cpylen =
			    copy_to_user(tranAddr.puAddress,
					 ((PNwdTranAddr) srcData)->Buffer,
					 ((PNwdTranAddr) srcData)->
					 uAddressLength);
			dataLen = 0;
			break;
		}
	case NWC_CONN_INFO_NDS_STATE:
	case NWC_CONN_INFO_MAX_PACKET_SIZE:
	case NWC_CONN_INFO_LICENSE_STATE:
	case NWC_CONN_INFO_PUBLIC_STATE:
	case NWC_CONN_INFO_SERVICE_TYPE:
	case NWC_CONN_INFO_DISTANCE:
	case NWC_CONN_INFO_SERVER_VERSION:
	case NWC_CONN_INFO_AUTH_ID:
	case NWC_CONN_INFO_SUSPENDED:
	case NWC_CONN_INFO_WORKGROUP_ID:
	case NWC_CONN_INFO_SECURITY_STATE:
	case NWC_CONN_INFO_CONN_NUMBER:
	case NWC_CONN_INFO_USER_ID:
	case NWC_CONN_INFO_BCAST_STATE:
	case NWC_CONN_INFO_CONN_REF:
	case NWC_CONN_INFO_AUTH_STATE:
	case NWC_CONN_INFO_VERSION:
	case NWC_CONN_INFO_SERVER_NAME:
	case NWC_CONN_INFO_TREE_NAME:
		srcData = (unsigned char *) reply->data;
		dataLen = reply->dataLen;
		break;

	case NWC_CONN_INFO_TREE_NAME_UNICODE:
	case NWC_CONN_INFO_SERVER_NAME_UNICODE:
		break;

	case NWC_CONN_INFO_LOCAL_TRAN_ADDR:
		break;

	case NWC_CONN_INFO_ALTERNATE_ADDR:
		break;

	case NWC_CONN_INFO_SERVER_GUID:
		break;

	default:
		break;
	}

	if (srcData && dataLen) {
		cpylen =
		    copy_to_user(connInfo->pConnInfo, srcData,
				 connInfo->uInfoLength);
	}

	return;
}

/*++======================================================================*/
int NwGetDaemonVersion(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCGetRequesterVersion pDVersion;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDVersion);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_REQUESTER_VERSION;
		cmdlen = sizeof(*cmd);
		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			pDVersion = (PNwdCGetRequesterVersion) reply->data;
			cpylen =
			    copy_to_user(pDVersion, pdata->reqData,
					 sizeof(*pDVersion));
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwcGetPreferredDSTree(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCGetPreferredDsTree pDGetTree;
	NwcGetPreferredDsTree xplatCall, *p;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcGetPreferredDsTree));
	datalen = sizeof(*pDGetTree) + xplatCall.uTreeLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_PREFERRED_DS_TREE;
		cmdlen = sizeof(*cmd);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			if (!retCode) {
				pDGetTree =
				    (PNwdCGetPreferredDsTree) reply->data;
				dPtr =
				    reply->data + pDGetTree->DsTreeNameOffset;
				p = (NwcGetPreferredDsTree *) pdata->reqData;

				DbgPrint
				    ("NwcGetPreferredDSTree: Reply recieved\n");
				DbgPrint("   TreeLen = %x\n",
					 pDGetTree->uTreeLength);
				DbgPrint("   TreeName = %s\n", dPtr);

				cpylen =
				    copy_to_user(p, &pDGetTree->uTreeLength, 4);
				cpylen =
				    copy_to_user(xplatCall.pDsTreeName, dPtr,
						 pDGetTree->uTreeLength);
			}
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwcSetPreferredDSTree(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCSetPreferredDsTree pDSetTree;
	NwcSetPreferredDsTree xplatCall;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcSetPreferredDsTree));
	datalen = sizeof(*pDSetTree) + xplatCall.uTreeLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SET_PREFERRED_DS_TREE;

		pDSetTree = (PNwdCSetPreferredDsTree) cmd->data;
		pDSetTree->DsTreeNameOffset = sizeof(*pDSetTree);
		pDSetTree->uTreeLength = xplatCall.uTreeLength;

		dPtr = cmd->data + sizeof(*pDSetTree);
		cpylen =
		    copy_from_user(dPtr, xplatCall.pDsTreeName,
				   xplatCall.uTreeLength);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwcSetDefaultNameCtx(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcSetDefaultNameContext xplatCall;
	PNwdCSetDefaultNameContext pDSet;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcSetDefaultNameContext));
	datalen =
	    sizeof(*pDSet) + xplatCall.uTreeLength + xplatCall.uNameLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SET_DEFAULT_NAME_CONTEXT;
		cmd->dataLen =
		    sizeof(NwdCSetDefaultNameContext) + xplatCall.uTreeLength +
		    xplatCall.uNameLength;

		pDSet = (PNwdCSetDefaultNameContext) cmd->data;
		dPtr = cmd->data;

		pDSet->TreeOffset = sizeof(NwdCSetDefaultNameContext);
		pDSet->uTreeLength = xplatCall.uTreeLength;
		pDSet->NameContextOffset =
		    pDSet->TreeOffset + xplatCall.uTreeLength;
		pDSet->uNameLength = xplatCall.uNameLength;

//sgled      cpylen = copy_from_user(dPtr+pDSet->TreeOffset, xplatCall.pTreeName, xplatCall.uTreeLength);
		cpylen = copy_from_user(dPtr + pDSet->TreeOffset, xplatCall.pDsTreeName, xplatCall.uTreeLength);	//sgled
		cpylen =
		    copy_from_user(dPtr + pDSet->NameContextOffset,
				   xplatCall.pNameContext,
				   xplatCall.uNameLength);

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

/*++======================================================================*/
int NwcGetDefaultNameCtx(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcGetDefaultNameContext xplatCall;
	PNwdCGetDefaultNameContext pGet;
	char *dPtr;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcGetDefaultNameContext));
	cmdlen =
	    sizeof(*cmd) + sizeof(NwdCGetDefaultNameContext) +
	    xplatCall.uTreeLength;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_DEFAULT_NAME_CONTEXT;
		cmd->dataLen =
		    sizeof(NwdCGetDefaultNameContext) + xplatCall.uTreeLength;

		pGet = (PNwdCGetDefaultNameContext) cmd->data;
		dPtr = cmd->data;

		pGet->TreeOffset = sizeof(NwdCGetDefaultNameContext);
		pGet->uTreeLength = xplatCall.uTreeLength;

//sgled      cpylen = copy_from_user( dPtr + pGet->TreeOffset, xplatCall.pTreeName, xplatCall.uTreeLength);
		cpylen = copy_from_user(dPtr + pGet->TreeOffset, xplatCall.pDsTreeName, xplatCall.uTreeLength);	//sgled
		dPtr[pGet->TreeOffset + pGet->uTreeLength] = 0;

		retCode =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			retCode = reply->Reply.ErrorCode;
			if (!retCode) {
				pGet = (PNwdCGetDefaultNameContext) reply->data;

				DbgPrint
				    ("NwcGetDefaultNameCtx: retCode=0x%x uNameLength1=%d uNameLength2=%d\n",
				     retCode, pGet->uNameLength,
				     xplatCall.uNameLength);
				if (xplatCall.uNameLength < pGet->uNameLength) {
					pGet->uNameLength =
					    xplatCall.uNameLength;
					retCode = NWE_BUFFER_OVERFLOW;
				}
				dPtr = (char *)pGet + pGet->NameContextOffset;
				cpylen =
				    copy_to_user(xplatCall.pNameContext, dPtr,
						 pGet->uNameLength);
			}

			kfree(reply);
		}
		kfree(cmd);
	}
	return (retCode);

}

int NwQueryFeature(PXPLAT pdata, session_t Session)
{
	NwcQueryFeature xpCall;
	int status = 0;
	unsigned long cpylen;

	cpylen =
	    copy_from_user(&xpCall, pdata->reqData, sizeof(NwcQueryFeature));
	switch (xpCall.Feature) {
	case NWC_FEAT_NDS:
	case NWC_FEAT_NDS_MTREE:
	case NWC_FEAT_PRN_CAPTURE:
	case NWC_FEAT_NDS_RESOLVE:

		status = NWE_REQUESTER_FAILURE;

	}
	return (status);
}

/*++======================================================================*/
int NwcGetTreeMonitoredConn(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcGetTreeMonitoredConnRef xplatCall, *p;
	PNwdCGetTreeMonitoredConnRef pDConnRef;
	char *dPtr;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcGetTreeMonitoredConnRef));
	datalen = sizeof(*pDConnRef) + xplatCall.pTreeName->DataLen;
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_GET_TREE_MONITORED_CONN_REF;

		pDConnRef = (PNwdCGetTreeMonitoredConnRef) cmd->data;
		pDConnRef->TreeName.boffset = sizeof(*pDConnRef);
		pDConnRef->TreeName.len = xplatCall.pTreeName->DataLen;
		pDConnRef->TreeName.type = xplatCall.pTreeName->DataType;

		dPtr = cmd->data + sizeof(*pDConnRef);
		cpylen =
		    copy_from_user(dPtr, xplatCall.pTreeName->pBuffer,
				   pDConnRef->TreeName.len);
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			pDConnRef = (PNwdCGetTreeMonitoredConnRef) reply->data;
			dPtr = reply->data + pDConnRef->TreeName.boffset;
			p = (NwcGetTreeMonitoredConnRef *) pdata->reqData;
			cpylen =
			    copy_to_user(&p->uConnReference,
					 &pDConnRef->uConnReference, 4);

			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);

	}

	return (status);
}

/*++======================================================================*/
int NwcEnumIdentities(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcEnumerateIdentities xplatCall, *eId;
	PNwdCEnumerateIdentities pEnum;
	NwcString xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcEnumerateIdentities));
	datalen = sizeof(*pEnum);
	cmdlen = datalen + sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_ENUMERATE_IDENTITIES;

		DbgPrint("NwcEnumIdentities: Send Request\n");
		DbgPrint("   iterator = %x\n", xplatCall.Iterator);
		DbgPrint("   cmdlen = %d\n", cmdlen);

		pEnum = (PNwdCEnumerateIdentities) cmd->data;
		pEnum->Iterator = xplatCall.Iterator;
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			status = reply->Reply.ErrorCode;

			eId = pdata->repData;
			pEnum = (PNwdCEnumerateIdentities) reply->data;
			cpylen =
			    copy_to_user(&eId->Iterator, &pEnum->Iterator,
					 sizeof(pEnum->Iterator));
			DbgPrint("[XPLAT NWCAPI] Found AuthId 0x%X\n",
				 pEnum->AuthenticationId);
			cpylen =
			    copy_to_user(&eId->AuthenticationId,
					 &pEnum->AuthenticationId,
					 sizeof(pEnum->AuthenticationId));
			cpylen =
			    copy_to_user(&eId->AuthType, &pEnum->AuthType,
					 sizeof(pEnum->AuthType));
			cpylen =
			    copy_to_user(&eId->IdentityFlags,
					 &pEnum->IdentityFlags,
					 sizeof(pEnum->IdentityFlags));
			cpylen =
			    copy_to_user(&eId->NameType, &pEnum->NameType,
					 sizeof(pEnum->NameType));
			cpylen =
			    copy_to_user(&eId->ObjectType, &pEnum->ObjectType,
					 sizeof(pEnum->ObjectType));

			if (!status) {
				cpylen =
				    copy_from_user(&xferStr, eId->pDomainName,
						   sizeof(NwcString));
				str =
				    (char *)((char *)reply->data +
					     pEnum->domainNameOffset);
				DbgPrint("[XPLAT NWCAPI] Found Domain %s\n",
					 str);
				cpylen =
				    copy_to_user(xferStr.pBuffer, str,
						 pEnum->domainNameLen);
				xferStr.DataType = NWC_STRING_TYPE_ASCII;
				xferStr.DataLen = pEnum->domainNameLen - 1;
				cpylen =
				    copy_to_user(eId->pDomainName, &xferStr,
						 sizeof(NwcString));

				cpylen =
				    copy_from_user(&xferStr, eId->pObjectName,
						   sizeof(NwcString));
				str =
				    (char *)((char *)reply->data +
					     pEnum->objectNameOffset);
				DbgPrint("[XPLAT NWCAPI] Found User %s\n", str);
				cpylen =
				    copy_to_user(xferStr.pBuffer, str,
						 pEnum->objectNameLen);
				xferStr.DataType = NWC_STRING_TYPE_ASCII;
				xferStr.DataLen = pEnum->objectNameLen - 1;
				cpylen =
				    copy_to_user(eId->pObjectName, &xferStr,
						 sizeof(NwcString));
			}

			kfree(reply);

		}
		kfree(cmd);

	}
	return (status);
}

/*++======================================================================*/
int NwcChangeAuthKey(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Change the password on the server
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcChangeKey xplatCall;
	PNwdCChangeKey pNewKey;
	NwcString xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData, sizeof(NwcChangeKey));

	datalen =
	    sizeof(NwdCChangeKey) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pNewPassword->DataLen +
	    xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		pNewKey = (PNwdCChangeKey) cmd->data;
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_CHANGE_KEY;

		pNewKey->NameType = xplatCall.NameType;
		pNewKey->ObjectType = xplatCall.ObjectType;
		pNewKey->AuthType = xplatCall.AuthType;
		str = (char *)pNewKey;

		/*
		 * Get the tree name
		 */
		str += sizeof(*pNewKey);
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pDomainName,
				   sizeof(NwcString));
		pNewKey->domainNameOffset = sizeof(*pNewKey);
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->domainNameLen = xferStr.DataLen;

		/*
		 * Get the User Name
		 */
		str += pNewKey->domainNameLen;
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pObjectName,
				   sizeof(NwcString));
		pNewKey->objectNameOffset =
		    pNewKey->domainNameOffset + pNewKey->domainNameLen;
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->objectNameLen = xferStr.DataLen;

		/*
		 * Get the New Password
		 */
		str += pNewKey->objectNameLen;
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pNewPassword,
				   sizeof(NwcString));
		pNewKey->newPasswordOffset =
		    pNewKey->objectNameOffset + pNewKey->objectNameLen;
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->newPasswordLen = xferStr.DataLen;

		/*
		 * Get the Verify Password
		 */
		str += pNewKey->newPasswordLen;
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pVerifyPassword,
				   sizeof(NwcString));
		pNewKey->verifyPasswordOffset =
		    pNewKey->newPasswordOffset + pNewKey->newPasswordLen;
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->verifyPasswordLen = xferStr.DataLen;

		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		memset(cmd, 0, cmdlen);

		kfree(cmd);
	}

	return (status);
}

/*++======================================================================*/
int NwcSetPrimaryConn(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Set the primary connection Id
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcSetPrimaryConnection xplatCall;
	PNwdCSetPrimaryConnection pConn;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(NwcSetPrimaryConnection));

	datalen = sizeof(NwdCSetPrimaryConnection);
	cmdlen = sizeof(*cmd) + datalen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		pConn = (PNwdCSetPrimaryConnection) cmd->data;
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SET_PRIMARY_CONN;
		pConn->ConnHandle = (HANDLE) (unsigned long) xplatCall.ConnHandle;
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);

		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}

		kfree(cmd);
	}

	return (status);
}

/*++======================================================================*/
int NwcGetPrimaryConn(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Get the Primary connection
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	XPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	unsigned long status = -ENOMEM, cmdlen, replylen, cpylen;

	cmdlen = (unsigned long) (&((PXPLAT_CALL_REQUEST) 0)->data);

	cmd.dataLen = 0;
	cmd.Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = Session;
	cmd.NwcCommand = NWC_GET_PRIMARY_CONN;

	status =
	    Queue_Daemon_Command((void *)&cmd, cmdlen, NULL, 0, (void **)&reply,
				 &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		if (!status) {
			cpylen =
			    copy_to_user(pdata->repData, reply->data,
					 sizeof(unsigned long));
		}

		kfree(reply);
	}

	return (status);
}

/*++======================================================================*/
int NwcSetMapDrive(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Get the Primary connection
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{

	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	unsigned long status = 0, datalen, cmdlen, replylen, cpylen;
	NwcMapDriveEx symInfo;

	DbgPrint("Call to NwcSetMapDrive\n");
	cpylen = copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo));
	cmdlen = sizeof(*cmd);
	datalen =
	    sizeof(symInfo) + symInfo.dirPathOffsetLength +
	    symInfo.linkOffsetLength;

	DbgPrint(" cmdlen = %d\n", cmdlen);
	DbgPrint(" dataLen = %d\n", datalen);
	DbgPrint(" symInfo.dirPathOffsetLength = %d\n",
		 symInfo.dirPathOffsetLength);
	DbgPrint(" symInfo.linkOffsetLength = %d\n", symInfo.linkOffsetLength);
	DbgPrint(" pdata->datalen = %d\n", pdata->reqLen);

	mydump(sizeof(symInfo), &symInfo);

	cmdlen += datalen;

	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_MAP_DRIVE;

		cpylen = copy_from_user(cmd->data, pdata->reqData, datalen);
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);

		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}
	return (status);

}

/*++======================================================================*/
int NwcUnMapDrive(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Get the Primary connection
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	unsigned long status = 0, datalen, cmdlen, replylen, cpylen;
	NwcUnmapDriveEx symInfo;

	DbgPrint("Call to NwcUnMapDrive\n");

	cpylen = copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo));
	cmdlen = sizeof(*cmd);
	datalen = sizeof(symInfo) + symInfo.linkLen;

	cmdlen += datalen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_UNMAP_DRIVE;

		cpylen = copy_from_user(cmd->data, pdata->reqData, datalen);
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);

		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		kfree(cmd);
	}

	return (status);
}

/*++======================================================================*/
int NwcEnumerateDrives(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Get the Primary connection
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	unsigned long status = 0, cmdlen, replylen, cpylen;
	unsigned long offset;
	char *cp;

	DbgPrint("Call to NwcEnumerateDrives\n");

	cmdlen = sizeof(*cmd);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {
		cmd->dataLen = 0;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_ENUMERATE_DRIVES;
		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);

		if (reply) {
			status = reply->Reply.ErrorCode;
			DbgPrint("Status Code = 0x%X\n", status);
			if (!status) {
				offset =
				    sizeof(((PNwcGetMappedDrives) pdata->
					    repData)->MapBuffLen);
				cp = reply->data;
				replylen =
				    ((PNwcGetMappedDrives) pdata->repData)->
				    MapBuffLen;
				cpylen =
				    copy_to_user(pdata->repData, cp, offset);
				cp += offset;
				cpylen =
				    copy_to_user(((PNwcGetMappedDrives) pdata->
						  repData)->MapBuffer, cp,
						 min(replylen - offset,
						     reply->dataLen - offset));
			}

			kfree(reply);
		}
		kfree(cmd);
	}

	return (status);
}

/*++======================================================================*/
int NwcGetBroadcastMessage(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Get the Primary connection
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	unsigned long cmdlen, replylen;
	int status = 0x8866, cpylen;
	NwcGetBroadcastNotification msg;
	PNwdCGetBroadcastNotification dmsg;

	cmdlen = sizeof(*cmd) + sizeof(*dmsg);
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);
	if (cmd) {

		cpylen = copy_from_user(&msg, pdata->reqData, sizeof(msg));
		cmd->dataLen = sizeof(*dmsg);
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;

		cmd->NwcCommand = NWC_GET_BROADCAST_MESSAGE;
		dmsg = (PNwdCGetBroadcastNotification) cmd->data;
		dmsg->uConnReference = (HANDLE) (unsigned long) msg.uConnReference;

		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);

		if (reply) {
			status = reply->Reply.ErrorCode;

			if (!status) {
				char *cp = pdata->repData;

				dmsg =
				    (PNwdCGetBroadcastNotification) reply->data;
				if (pdata->repLen < dmsg->messageLen) {
					dmsg->messageLen = pdata->repLen;
				}
				msg.messageLen = dmsg->messageLen;
				cpylen = offsetof(NwcGetBroadcastNotification, message);
				cp += cpylen;
				cpylen = copy_to_user(pdata->repData, &msg, cpylen);
				cpylen = copy_to_user(cp, dmsg->message, msg.messageLen);
			} else {
				msg.messageLen = 0;
				msg.message[0] = 0;
				cpylen = offsetof(NwcGetBroadcastNotification, message);
				cpylen = copy_to_user(pdata->repData, &msg, sizeof(msg));
			}

			kfree(reply);
		}
		kfree(cmd);
	}
	return (status);
}

int NwdSetKeyValue(PXPLAT pdata, session_t Session)
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcSetKey xplatCall;
	PNwdCSetKey pNewKey;
	NwcString cstrObjectName, cstrPassword;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&xplatCall, pdata->reqData, sizeof(NwcSetKey));
	cpylen =
	    copy_from_user(&cstrObjectName, xplatCall.pObjectName,
			   sizeof(NwcString));
	cpylen =
	    copy_from_user(&cstrPassword, xplatCall.pNewPassword,
			   sizeof(NwcString));

	datalen =
	    sizeof(NwdCSetKey) + cstrObjectName.DataLen + cstrPassword.DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		pNewKey = (PNwdCSetKey) cmd->data;
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_SET_KEY;

		pNewKey->ObjectType = xplatCall.ObjectType;
		pNewKey->AuthenticationId = xplatCall.AuthenticationId;
		pNewKey->ConnHandle = (HANDLE) (unsigned long) xplatCall.ConnHandle;
		str = (char *)pNewKey;

		/*
		 * Get the User Name
		 */
		str += sizeof(NwdCSetKey);
		cpylen =
		    copy_from_user(str, cstrObjectName.pBuffer,
				   cstrObjectName.DataLen);

		str += pNewKey->objectNameLen = cstrObjectName.DataLen;
		pNewKey->objectNameOffset = sizeof(NwdCSetKey);

		/*
		 * Get the Verify Password
		 */
		cpylen =
		    copy_from_user(str, cstrPassword.pBuffer,
				   cstrPassword.DataLen);

		pNewKey->newPasswordLen = cstrPassword.DataLen;
		pNewKey->newPasswordOffset =
		    pNewKey->objectNameOffset + pNewKey->objectNameLen;

		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		memset(cmd, 0, cmdlen);
		kfree(cmd);
	}

	return (status);
}

/*++======================================================================*/
int NwdVerifyKeyValue(PXPLAT pdata, session_t Session)
/*
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract: Change the password on the server
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	NwcVerifyKey xplatCall;
	PNwdCVerifyKey pNewKey;
	NwcString xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData, sizeof(NwcVerifyKey));

	datalen =
	    sizeof(NwdCVerifyKey) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = Novfs_Malloc(cmdlen, GFP_KERNEL);

	if (cmd) {
		pNewKey = (PNwdCVerifyKey) cmd->data;
		cmd->dataLen = datalen;
		cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
		cmd->Command.SequenceNumber = 0;
		cmd->Command.SessionId = Session;
		cmd->NwcCommand = NWC_VERIFY_KEY;

		pNewKey->NameType = xplatCall.NameType;
		pNewKey->ObjectType = xplatCall.ObjectType;
		pNewKey->AuthType = xplatCall.AuthType;
		str = (char *)pNewKey;

		/*
		 * Get the tree name
		 */
		str += sizeof(*pNewKey);
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pDomainName,
				   sizeof(NwcString));
		pNewKey->domainNameOffset = sizeof(*pNewKey);
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->domainNameLen = xferStr.DataLen;

		/*
		 * Get the User Name
		 */
		str += pNewKey->domainNameLen;
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pObjectName,
				   sizeof(NwcString));
		pNewKey->objectNameOffset =
		    pNewKey->domainNameOffset + pNewKey->domainNameLen;
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->objectNameLen = xferStr.DataLen;

		/*
		 * Get the Verify Password
		 */
		str += pNewKey->objectNameLen;
		cpylen =
		    copy_from_user(&xferStr, xplatCall.pVerifyPassword,
				   sizeof(NwcString));
		pNewKey->verifyPasswordOffset =
		    pNewKey->objectNameOffset + pNewKey->objectNameLen;
		cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
		pNewKey->verifyPasswordLen = xferStr.DataLen;

		status =
		    Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
					 (void **)&reply, &replylen,
					 INTERRUPTIBLE);
		if (reply) {
			status = reply->Reply.ErrorCode;
			kfree(reply);
		}
		memset(cmd, 0, cmdlen);
		kfree(cmd);
	}

	return (status);
}
