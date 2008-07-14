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

#ifndef strlen_user
#define strlen_user(str) strnlen_user(str, ~0UL >> 1)
#endif

static void GetUserData(struct nwc_scan_conn_info * connInfo, struct novfs_xplat_call_request *cmd, struct novfs_xplat_call_reply *reply);
static void GetConnData(struct nwc_get_conn_info * connInfo, struct novfs_xplat_call_request *cmd, struct novfs_xplat_call_reply *reply);

/*++======================================================================*/
int novfs_open_conn_by_name(struct novfs_xplat *pdata, void ** Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_open_conn_by_name *openConn, *connReply;
	struct nwc_open_conn_by_name ocbn;
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
	openConn = (struct nwd_open_conn_by_name *) cmd->data;

	openConn->nameLen = strlen_user(ocbn.pName->pString);
	openConn->serviceLen = strlen_user(ocbn.pServiceType);
	openConn->uConnFlags = ocbn.uConnFlags;
	openConn->ConnHandle = Uint32toHandle(ocbn.ConnHandle);
	data = (char *)openConn;
	data += sizeof(*openConn);
	openConn->oName = sizeof(*openConn);

	openConn->oServiceType = openConn->oName + openConn->nameLen;
	cpylen =
		copy_from_user(data, ocbn.pName->pString,
				openConn->nameLen);
	data += openConn->nameLen;
	cpylen =
		copy_from_user(data, ocbn.pServiceType,
				openConn->serviceLen);

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (struct nwd_open_conn_by_name *) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (struct nwd_open_conn_by_name *) reply->data;
			ocbn.RetConnHandle = HandletoUint32(connReply->newConnHandle);
			*Handle = connReply->newConnHandle;

			cpylen = copy_to_user(pdata->reqData, &ocbn, sizeof(ocbn));
			DbgPrint("New Conn Handle = %X\n", connReply->newConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);
	return ((int)retCode);

}

int novfs_open_conn_by_addr(struct novfs_xplat *pdata, void ** Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_open_conn_by_addr *openConn, *connReply;
	struct nwc_open_conn_by_addr ocba;
	struct nwc_tran_addr tranAddr;
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
	openConn = (struct nwd_open_conn_by_addr *) cmd->data;

	cpylen =
		copy_from_user(&tranAddr, ocba.pTranAddr, sizeof(tranAddr));

	DbgPrint("NwOpenConnByAddr: tranAddr\n");
	novfs_dump(sizeof(tranAddr), &tranAddr);

	openConn->TranAddr.uTransportType = tranAddr.uTransportType;
	openConn->TranAddr.uAddressLength = tranAddr.uAddressLength;
	memset(addr, 0xcc, sizeof(addr) - 1);

	cpylen =
		copy_from_user(addr, tranAddr.puAddress,
				tranAddr.uAddressLength);

	DbgPrint("NwOpenConnByAddr: addr\n");
	novfs_dump(sizeof(addr), addr);

	openConn->TranAddr.oAddress = *(unsigned int *) (&addr[2]);

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (struct nwd_open_conn_by_addr *) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (struct nwd_open_conn_by_addr *) reply->data;
			ocba.ConnHandle =
				HandletoUint32(connReply->ConnHandle);
			*Handle = connReply->ConnHandle;
			cpylen =
				copy_to_user(pdata->reqData, &ocba,
						sizeof(ocba));
			DbgPrint("New Conn Handle = %X\n",
					connReply->ConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);

	return (retCode);

}

int novfs_open_conn_by_ref(struct novfs_xplat *pdata, void ** Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_open_conn_by_ref *openConn;
	struct nwc_open_conn_by_ref ocbr;
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
	openConn = (struct nwd_open_conn_by_ref *) cmd->data;

	openConn->uConnReference =
		(void *) (unsigned long) ocbr.uConnReference;
	openConn->uConnFlags = ocbr.uConnFlags;

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		openConn = (struct nwd_open_conn_by_ref *) reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			ocbr.ConnHandle =
				HandletoUint32(openConn->ConnHandle);
			*Handle = openConn->ConnHandle;

			cpylen =
				copy_to_user(pdata->reqData, &ocbr,
						sizeof(ocbr));
			DbgPrint("New Conn Handle = %X\n",
					openConn->ConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);
	return (retCode);

}

int novfs_raw_send(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_request xRequest;
	struct nwc_frag *frag, *cFrag, *reqFrag;
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen, totalLen;
	unsigned int x;
	struct nwd_ncp_req *ncpData;
	struct nwd_ncp_rep *ncpReply;
	unsigned char *reqData;
	unsigned long actualReplyLength = 0;

	DbgPrint("[XPLAT] Process Raw NCP Send\n");
	cpylen = copy_from_user(&xRequest, pdata->reqData, sizeof(xRequest));

	/*
	 * Figure out the length of the request
	 */
	frag =
	    kmalloc(xRequest.uNumReplyFrags * sizeof(struct nwc_frag), GFP_KERNEL);

	DbgPrint("[XPLAT RawNCP] - Reply Frag Count 0x%X\n",
		 xRequest.uNumReplyFrags);

	if (!frag)
		return (retCode);

	cpylen =
	    copy_from_user(frag, xRequest.pReplyFrags,
			   xRequest.uNumReplyFrags * sizeof(struct nwc_frag));
	totalLen = 0;

	cFrag = frag;
	for (x = 0; x < xRequest.uNumReplyFrags; x++) {
		DbgPrint("[XPLAT - RawNCP] - Frag Len = %d\n", cFrag->uLength);
		totalLen += cFrag->uLength;
		cFrag++;
	}

	DbgPrint("[XPLAT - RawNCP] - totalLen = %d\n", totalLen);
	datalen = 0;
	reqFrag =
	    kmalloc(xRequest.uNumRequestFrags * sizeof(struct nwc_frag),
			 GFP_KERNEL);
	if (!reqFrag) {
		kfree(frag);
		return (retCode);
	}

	cpylen =
	    copy_from_user(reqFrag, xRequest.pRequestFrags,
			   xRequest.uNumRequestFrags * sizeof(struct nwc_frag));
	cFrag = reqFrag;
	for (x = 0; x < xRequest.uNumRequestFrags; x++) {
		datalen += cFrag->uLength;
		cFrag++;
	}

	/*
	 * Allocate the cmd Request
	 */
	cmdlen = datalen + sizeof(*cmd) + sizeof(*ncpData);
	DbgPrint("[XPLAT RawNCP] - Frag Count 0x%X\n",
		 xRequest.uNumRequestFrags);
	DbgPrint("[XPLAT RawNCP] - Total Command Data Len = %x\n", cmdlen);

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_RAW_NCP_REQUEST;

	/*
	 * build the NCP Request
	 */
	cmd->dataLen = cmdlen - sizeof(*cmd);
	ncpData = (struct nwd_ncp_req *) cmd->data;
	ncpData->replyLen = totalLen;
	ncpData->requestLen = datalen;
	ncpData->ConnHandle = (void *) (unsigned long) xRequest.ConnHandle;
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

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	DbgPrint("RawNCP - reply = %x\n", reply);
	DbgPrint("RawNCP - retCode = %x\n", retCode);

	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		ncpReply = (struct nwd_ncp_rep *) reply->data;
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

			datalen =
				min((unsigned long) cFrag->uLength, totalLen);

			cpylen =
				copy_to_user(cFrag->pData, reqData,
						datalen);
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

	kfree(reqFrag);
	kfree(frag);

	return (retCode);
}

int novfs_conn_close(struct novfs_xplat *pdata, void ** Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_close_conn cc;
	struct nwd_close_conn *nwdClose;
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

	nwdClose = (struct nwd_close_conn *) cmd->data;
	cmd->dataLen = sizeof(*nwdClose);
	*Handle = nwdClose->ConnHandle = Uint32toHandle(cc.ConnHandle);

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
	return (retCode);

}

int novfs_sys_conn_close(struct novfs_xplat *pdata, unsigned long *Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_close_conn cc;
	struct nwd_close_conn *nwdClose;
	unsigned int retCode = 0;
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
	cmd->NwcCommand = NWC_SYS_CLOSE_CONN;

	nwdClose = (struct nwd_close_conn *) cmd->data;
	cmd->dataLen = sizeof(*nwdClose);
	nwdClose->ConnHandle = (void *) (unsigned long) cc.ConnHandle;
	*Handle = (unsigned long) cc.ConnHandle;

	/*
	 * send the request
	 */
	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, 0);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_login_id(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_login_id lgn, *plgn;
	int retCode = -ENOMEM;
	struct ncl_string server;
	struct ncl_string username;
	struct ncl_string password;
	unsigned long cpylen;
	struct nwc_string nwcStr;

	cpylen = copy_from_user(&lgn, pdata->reqData, sizeof(lgn));

	DbgPrint("NwLoginIdentity:\n");
	novfs_dump(sizeof(lgn), &lgn);

	cpylen = copy_from_user(&nwcStr, lgn.pDomainName, sizeof(nwcStr));
	DbgPrint("NwLoginIdentity: DomainName\n");
	novfs_dump(sizeof(nwcStr), &nwcStr);

	if ((server.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
		server.type = nwcStr.DataType;
		server.len = nwcStr.DataLen;
		if (!copy_from_user((void *)server.buffer, nwcStr.pBuffer, server.len)) {
			DbgPrint("NwLoginIdentity: Server\n");
			novfs_dump(server.len, server.buffer);

			cpylen = copy_from_user(&nwcStr, lgn.pObjectName, sizeof(nwcStr));
			DbgPrint("NwLoginIdentity: ObjectName\n");
			novfs_dump(sizeof(nwcStr), &nwcStr);

			if ((username.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
				username.type = nwcStr.DataType;
				username.len = nwcStr.DataLen;
				if (!copy_from_user((void *)username.buffer, nwcStr.pBuffer, username.len)) {
					DbgPrint("NwLoginIdentity: User\n");
					novfs_dump(username.len, username.buffer);

					cpylen = copy_from_user(&nwcStr, lgn.pPassword, sizeof(nwcStr));
					DbgPrint("NwLoginIdentity: Password\n");
					novfs_dump(sizeof(nwcStr), &nwcStr);

					if ((password.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
						password.type = nwcStr.DataType;
						password.len = nwcStr.DataLen;
						if (!copy_from_user((void *)password.buffer, nwcStr.pBuffer, password.len)) {
							retCode =  novfs_do_login(&server, &username, &password, (void **)&lgn.AuthenticationId, &Session);
							if (retCode) {
								lgn.AuthenticationId = 0;
							}

							plgn = (struct nwc_login_id *)pdata->reqData;
							cpylen = copy_to_user(&plgn->AuthenticationId, &lgn.AuthenticationId, sizeof(plgn->AuthenticationId));
						}
						memset(password.buffer, 0, password.len);
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

int novfs_auth_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_auth_with_id pauth;
	struct nwc_auth_wid *pDauth;
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDauth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_AUTHENTICATE_CONN_WITH_ID;

	cpylen = copy_from_user(&pauth, pdata->reqData, sizeof(pauth));

	pDauth = (struct nwc_auth_wid *) cmd->data;
	cmd->dataLen = datalen;
	pDauth->AuthenticationId = pauth.AuthenticationId;
	pDauth->ConnHandle = (void *) (unsigned long) pauth.ConnHandle;

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	return (retCode);
}

int novfs_license_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_license_conn lisc;
	struct nwc_lisc_id * pDLisc;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDLisc);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_LICENSE_CONN;

	cpylen = copy_from_user(&lisc, pdata->reqData, sizeof(lisc));

	pDLisc = (struct nwc_lisc_id *) cmd->data;
	cmd->dataLen = datalen;
	pDLisc->ConnHandle = (void *) (unsigned long) lisc.ConnHandle;

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);
}

int novfs_logout_id(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_lo_id logout, *pDLogout;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDLogout);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_LOGOUT_IDENTITY;

	cpylen =
		copy_from_user(&logout, pdata->reqData, sizeof(logout));

	pDLogout = (struct nwc_lo_id *) cmd->data;
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
	return (retCode);
}

int novfs_unlicense_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_unlic_conn *pUconn, ulc;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&ulc, pdata->reqData, sizeof(ulc));
	datalen = sizeof(*pUconn);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_UNLICENSE_CONN;
	cmd->dataLen = datalen;
	pUconn = (struct nwc_unlic_conn *) cmd->data;

	pUconn->ConnHandle = (void *) (unsigned long) ulc.ConnHandle;
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
	return (retCode);

}

int novfs_unauthenticate(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_unauthenticate auth, *pDAuth;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDAuth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = (struct novfs_xplat_call_request *)kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_UNAUTHENTICATE_CONN;

	cpylen = copy_from_user(&auth, pdata->reqData, sizeof(auth));

	pDAuth = (struct nwc_unauthenticate *) cmd->data;
	cmd->dataLen = datalen;
	pDAuth->AuthenticationId = auth.AuthenticationId;
	pDAuth->ConnHandle = (void *) (unsigned long) auth.ConnHandle;

	retCode =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_get_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_get_conn_info connInfo;
	struct nwd_conn_info *pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_get_conn_info));

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_CONN_INFO;

	pDConnInfo = (struct nwd_conn_info *) cmd->data;

	pDConnInfo->ConnHandle = (void *) (unsigned long) connInfo.ConnHandle;
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
	return (retCode);

}

int novfs_set_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_set_conn_info connInfo;
	struct nwd_set_conn_info *pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_set_conn_info));

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_CONN_INFO;

	pDConnInfo = (struct nwd_set_conn_info *) cmd->data;

	pDConnInfo->ConnHandle = (void *) (unsigned long) connInfo.ConnHandle;
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
	return (retCode);

}

int novfs_get_id_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_get_id_info qidInfo, *gId;
	struct nwd_get_id_info *idInfo;
	struct nwc_string xferStr;
	char *str;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cmdlen = sizeof(*cmd) + sizeof(*idInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	cpylen = copy_from_user(&qidInfo, pdata->reqData, sizeof(qidInfo));

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_IDENTITY_INFO;

	idInfo = (struct nwd_get_id_info *) cmd->data;

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
			idInfo = (struct nwd_get_id_info *) reply->data;
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
						sizeof(struct nwc_string));
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
						sizeof(struct nwc_string));

			cpylen =
				copy_from_user(&xferStr, gId->pObjectName,
						sizeof(struct nwc_string));
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
						sizeof(struct nwc_string));
		}

		kfree(reply);
	}
	kfree(cmd);
	return (retCode);
}

int novfs_scan_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_scan_conn_info connInfo, *rInfo;
	struct nwd_scan_conn_info *pDConnInfo;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;
	unsigned char *localData;

	cpylen =
	    copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_scan_conn_info));

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo) + connInfo.uScanInfoLen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SCAN_CONN_INFO;

	pDConnInfo = (struct nwd_scan_conn_info *) cmd->data;

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

		pDConnInfo = (struct nwd_scan_conn_info *) reply->data;
		retCode = (unsigned long) reply->Reply.ErrorCode;
		if (!retCode) {
			GetUserData(&connInfo, cmd, reply);
			rInfo = (struct nwc_scan_conn_info *) pdata->repData;
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
			rInfo = (struct nwc_scan_conn_info *) pdata->reqData;
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
	return (retCode);
}

/*
 *  Copies the user data out of the scan conn info call.
 */
static void GetUserData(struct nwc_scan_conn_info * connInfo, struct novfs_xplat_call_request *cmd, struct novfs_xplat_call_reply *reply)
{
	unsigned long uLevel;
	struct nwd_scan_conn_info *pDConnInfo;

	unsigned char *srcData = NULL;
	unsigned long dataLen = 0, cpylen;

	pDConnInfo = (struct nwd_scan_conn_info *) reply->data;
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
			struct nwc_tran_addr tranAddr;

			srcData = (unsigned char *) reply->data;
			dataLen = reply->dataLen;

			DbgPrint
			    ("GetUserData NWC_CONN_INFO_TRAN_ADDR 0x%p -> 0x%p :: 0x%X\n",
			     srcData, connInfo->pReturnConnInfo, dataLen);

			cpylen =
			    copy_from_user(&tranAddr, dstData,
					   sizeof(tranAddr));

			srcData +=
			    ((struct nwd_scan_conn_info *) srcData)->
			    uReturnConnInfoOffset;

			tranAddr.uTransportType =
			    ((struct nwd_tran_addr *)  srcData)->uTransportType;
			tranAddr.uAddressLength =
			    ((struct tagNwdTranAddrEx *) srcData)->uAddressLength;

			cpylen =
			    copy_to_user(dstData, &tranAddr, sizeof(tranAddr));
			cpylen =
			    copy_to_user(tranAddr.puAddress,
					 ((struct tagNwdTranAddrEx *) srcData)->Buffer,
					 ((struct tagNwdTranAddrEx *) srcData)->
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

/*
 *  Copies the user data out of the scan conn info call.
 */
static void GetConnData(struct nwc_get_conn_info * connInfo, struct novfs_xplat_call_request *cmd, struct novfs_xplat_call_reply *reply)
{
	unsigned long uLevel;
	struct nwd_conn_info * pDConnInfo;

	unsigned char *srcData = NULL;
	unsigned long dataLen = 0, cpylen;

	pDConnInfo = (struct nwd_conn_info *) cmd->data;
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
			struct nwc_tran_addr tranAddr;

			srcData = (unsigned char *) reply->data;

			cpylen =
			    copy_from_user(&tranAddr, dstData,
					   sizeof(tranAddr));
			tranAddr.uTransportType =
			    ((struct tagNwdTranAddrEx *) srcData)->uTransportType;
			tranAddr.uAddressLength =
			    ((struct tagNwdTranAddrEx *) srcData)->uAddressLength;

			cpylen =
			    copy_to_user(dstData, &tranAddr, sizeof(tranAddr));
			cpylen =
			    copy_to_user(tranAddr.puAddress,
					 ((struct tagNwdTranAddrEx *) srcData)->Buffer,
					 ((struct tagNwdTranAddrEx *) srcData)->
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

int novfs_get_daemon_ver(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_get_reqversion *pDVersion;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;

	datalen = sizeof(*pDVersion);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

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
		pDVersion = (struct nwd_get_reqversion *) reply->data;
		cpylen =
			copy_to_user(pDVersion, pdata->reqData,
					sizeof(*pDVersion));
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_get_preferred_DS_tree(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_get_pref_ds_tree *pDGetTree;
	struct nwc_get_pref_ds_tree xplatCall, *p;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_get_pref_ds_tree));
	datalen = sizeof(*pDGetTree) + xplatCall.uTreeLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

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
				(struct nwd_get_pref_ds_tree *) reply->data;
			dPtr =
				reply->data + pDGetTree->DsTreeNameOffset;
			p = (struct nwc_get_pref_ds_tree *) pdata->reqData;

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
	return (retCode);

}

int novfs_set_preferred_DS_tree(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwd_set_pref_ds_tree *pDSetTree;
	struct nwc_set_pref_ds_tree xplatCall;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_set_pref_ds_tree));
	datalen = sizeof(*pDSetTree) + xplatCall.uTreeLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_PREFERRED_DS_TREE;

	pDSetTree = (struct nwd_set_pref_ds_tree *) cmd->data;
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
	return (retCode);

}

int novfs_set_default_ctx(struct novfs_xplat *pdata,
		struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_set_def_name_ctx xplatCall;
	struct nwd_set_def_name_ctx * pDSet;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen, cpylen;
	unsigned char *dPtr;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_set_def_name_ctx));
	datalen =
	    sizeof(*pDSet) + xplatCall.uTreeLength + xplatCall.uNameLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_DEFAULT_NAME_CONTEXT;
	cmd->dataLen =
		sizeof(struct nwd_set_def_name_ctx) +
		xplatCall.uTreeLength + xplatCall.uNameLength;

	pDSet = (struct nwd_set_def_name_ctx *) cmd->data;
	dPtr = cmd->data;

	pDSet->TreeOffset = sizeof(struct nwd_set_def_name_ctx);
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
	return (retCode);

}

int novfs_get_default_ctx(struct novfs_xplat *pdata,
		struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_get_def_name_ctx xplatCall;
	struct nwd_get_def_name_ctx * pGet;
	char *dPtr;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_get_def_name_ctx));
	cmdlen =
	    sizeof(*cmd) + sizeof(struct nwd_get_def_name_ctx ) +
	    xplatCall.uTreeLength;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_DEFAULT_NAME_CONTEXT;
	cmd->dataLen =
		sizeof(struct nwd_get_def_name_ctx) + xplatCall.uTreeLength;

	pGet = (struct nwd_get_def_name_ctx *) cmd->data;
	dPtr = cmd->data;

	pGet->TreeOffset = sizeof(struct nwd_get_def_name_ctx );
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
			pGet = (struct nwd_get_def_name_ctx *) reply->data;

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
	return (retCode);

}

int novfs_query_feature(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_query_feature xpCall;
	int status = 0;
	unsigned long cpylen;

	cpylen =
	    copy_from_user(&xpCall, pdata->reqData, sizeof(struct nwc_query_feature));
	switch (xpCall.Feature) {
	case NWC_FEAT_NDS:
	case NWC_FEAT_NDS_MTREE:
	case NWC_FEAT_PRN_CAPTURE:
	case NWC_FEAT_NDS_RESOLVE:

		status = NWE_REQUESTER_FAILURE;

	}
	return (status);
}

int novfs_get_tree_monitored_conn(struct novfs_xplat *pdata,
		struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_get_tree_monitored_conn_ref xplatCall, *p;
	struct nwd_get_tree_monitored_conn_ref *pDConnRef;
	char *dPtr;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_get_tree_monitored_conn_ref));
	datalen = sizeof(*pDConnRef) + xplatCall.pTreeName->DataLen;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_TREE_MONITORED_CONN_REF;

	pDConnRef = (struct nwd_get_tree_monitored_conn_ref *) cmd->data;
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
		pDConnRef = (struct nwd_get_tree_monitored_conn_ref *) reply->data;
		dPtr = reply->data + pDConnRef->TreeName.boffset;
		p = (struct nwc_get_tree_monitored_conn_ref *) pdata->reqData;
		cpylen =
			copy_to_user(&p->uConnReference,
					&pDConnRef->uConnReference, 4);

		status = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (status);
}

int novfs_enum_ids(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_enum_ids xplatCall, *eId;
	struct nwd_enum_ids *pEnum;
	struct nwc_string xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_enum_ids));
	datalen = sizeof(*pEnum);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_ENUMERATE_IDENTITIES;

	DbgPrint("NwcEnumIdentities: Send Request\n");
	DbgPrint("   iterator = %x\n", xplatCall.Iterator);
	DbgPrint("   cmdlen = %d\n", cmdlen);

	pEnum = (struct nwd_enum_ids *) cmd->data;
	pEnum->Iterator = xplatCall.Iterator;
	status =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);
	if (reply) {
		status = reply->Reply.ErrorCode;

		eId = pdata->repData;
		pEnum = (struct nwd_enum_ids *) reply->data;
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
						sizeof(struct nwc_string));
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
						sizeof(struct nwc_string));

			cpylen =
				copy_from_user(&xferStr, eId->pObjectName,
						sizeof(struct nwc_string));
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
						sizeof(struct nwc_string));
		}

		kfree(reply);

	}
	kfree(cmd);
	return (status);
}

int novfs_change_auth_key(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_change_key xplatCall;
	struct nwd_change_key *pNewKey;
	struct nwc_string xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_change_key));

	datalen =
	    sizeof(struct nwd_change_key) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pNewPassword->DataLen +
	    xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_change_key *) cmd->data;
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
				sizeof(struct nwc_string));
	pNewKey->domainNameOffset = sizeof(*pNewKey);
	cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
	pNewKey->domainNameLen = xferStr.DataLen;

	/*
	 * Get the User Name
	 */
	str += pNewKey->domainNameLen;
	cpylen =
		copy_from_user(&xferStr, xplatCall.pObjectName,
				sizeof(struct nwc_string));
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
				sizeof(struct nwc_string));
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
				sizeof(struct nwc_string));
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
	return (status);
}

int novfs_set_pri_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_set_primary_conn xplatCall;
	struct nwd_set_primary_conn *pConn;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData,
			   sizeof(struct nwc_set_primary_conn));

	datalen = sizeof(struct nwd_set_primary_conn);
	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	pConn = (struct nwd_set_primary_conn *) cmd->data;
	cmd->dataLen = datalen;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_PRIMARY_CONN;
	pConn->ConnHandle = (void *) (unsigned long) xplatCall.ConnHandle;
	status =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (status);
}

int novfs_get_pri_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request cmd;
	struct novfs_xplat_call_reply *reply;
	unsigned long status = -ENOMEM, cmdlen, replylen, cpylen;

	cmdlen = (unsigned long) (&((struct novfs_xplat_call_request *) 0)->data);

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

int novfs_set_map_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{

	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	unsigned long status = 0, datalen, cmdlen, replylen, cpylen;
	struct nwc_map_drive_ex symInfo;

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

	novfs_dump(sizeof(symInfo), &symInfo);

	cmdlen += datalen;

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

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
	return (status);

}

int novfs_unmap_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	unsigned long status = 0, datalen, cmdlen, replylen, cpylen;
	struct nwc_unmap_drive_ex symInfo;

	DbgPrint("Call to NwcUnMapDrive\n");

	cpylen = copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo));
	cmdlen = sizeof(*cmd);
	datalen = sizeof(symInfo) + symInfo.linkLen;

	cmdlen += datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

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
	return (status);
}

int novfs_enum_drives(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	unsigned long status = 0, cmdlen, replylen, cpylen;
	unsigned long offset;
	char *cp;

	DbgPrint("Call to NwcEnumerateDrives\n");

	cmdlen = sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

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
				sizeof(((struct nwc_get_mapped_drives *) pdata->
							repData)->MapBuffLen);
			cp = reply->data;
			replylen =
				((struct nwc_get_mapped_drives *) pdata->repData)->
				MapBuffLen;
			cpylen =
				copy_to_user(pdata->repData, cp, offset);
			cp += offset;
			cpylen =
				copy_to_user(((struct nwc_get_mapped_drives *) pdata->
							repData)->MapBuffer, cp,
						min(replylen - offset,
							reply->dataLen - offset));
		}

		kfree(reply);
	}
	kfree(cmd);
	return (status);
}

int novfs_get_bcast_msg(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	unsigned long cmdlen, replylen;
	int status = 0x8866, cpylen;
	struct nwc_get_bcast_notification msg;
	struct nwd_get_bcast_notification *dmsg;

	cmdlen = sizeof(*cmd) + sizeof(*dmsg);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cpylen = copy_from_user(&msg, pdata->reqData, sizeof(msg));
	cmd->dataLen = sizeof(*dmsg);
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;

	cmd->NwcCommand = NWC_GET_BROADCAST_MESSAGE;
	dmsg = (struct nwd_get_bcast_notification *) cmd->data;
	dmsg->uConnReference = (void *) (unsigned long) msg.uConnReference;

	status =
		Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0,
				(void **)&reply, &replylen,
				INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;

		if (!status) {
			char *cp = pdata->repData;

			dmsg =
				(struct nwd_get_bcast_notification *) reply->data;
			if (pdata->repLen < dmsg->messageLen) {
				dmsg->messageLen = pdata->repLen;
			}
			msg.messageLen = dmsg->messageLen;
			cpylen =
				offsetof(struct
						nwc_get_bcast_notification,
						message);
			cp += cpylen;
			cpylen =
				copy_to_user(pdata->repData, &msg, cpylen);
			cpylen =
				copy_to_user(cp, dmsg->message,
						msg.messageLen);
		} else {
			msg.messageLen = 0;
			msg.message[0] = 0;
			cpylen = offsetof(struct
					nwc_get_bcast_notification,
					message);
			cpylen =
				copy_to_user(pdata->repData, &msg,
						sizeof(msg));
		}

		kfree(reply);
	}
	kfree(cmd);
	return (status);
}

int novfs_set_key_value(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_set_key xplatCall;
	struct nwd_set_key *pNewKey;
	struct nwc_string cstrObjectName, cstrPassword;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen = copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_set_key));
	cpylen =
	    copy_from_user(&cstrObjectName, xplatCall.pObjectName,
			   sizeof(struct nwc_string));
	cpylen =
	    copy_from_user(&cstrPassword, xplatCall.pNewPassword,
			   sizeof(struct nwc_string));

	datalen =
	    sizeof(struct nwd_set_key ) + cstrObjectName.DataLen + cstrPassword.DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_set_key *) cmd->data;
	cmd->dataLen = datalen;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_KEY;

	pNewKey->ObjectType = xplatCall.ObjectType;
	pNewKey->AuthenticationId = xplatCall.AuthenticationId;
	pNewKey->ConnHandle = (void *) (unsigned long) xplatCall.ConnHandle;
	str = (char *)pNewKey;

	/*
	 * Get the User Name
	 */
	str += sizeof(struct nwd_set_key );
	cpylen =
		copy_from_user(str, cstrObjectName.pBuffer,
				cstrObjectName.DataLen);

	str += pNewKey->objectNameLen = cstrObjectName.DataLen;
	pNewKey->objectNameOffset = sizeof(struct nwd_set_key );

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
	kfree(cmd);
	return (status);
}

int novfs_verify_key_value(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd;
	struct novfs_xplat_call_reply *reply;
	struct nwc_verify_key xplatCall;
	struct nwd_verify_key *pNewKey;
	struct nwc_string xferStr;
	char *str;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen, cpylen;

	cpylen =
	    copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_verify_key));

	datalen =
	    sizeof(struct nwd_verify_key) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = (struct novfs_xplat_call_request *)kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_verify_key *) cmd->data;
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
				sizeof(struct nwc_string));
	pNewKey->domainNameOffset = sizeof(*pNewKey);
	cpylen = copy_from_user(str, xferStr.pBuffer, xferStr.DataLen);
	pNewKey->domainNameLen = xferStr.DataLen;

	/*
	 * Get the User Name
	 */
	str += pNewKey->domainNameLen;
	cpylen =
		copy_from_user(&xferStr, xplatCall.pObjectName,
				sizeof(struct nwc_string));
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
				sizeof(struct nwc_string));
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
	kfree(cmd);
	return (status);
}
