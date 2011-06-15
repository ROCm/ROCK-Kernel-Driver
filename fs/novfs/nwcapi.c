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
#include <linux/semaphore.h>
#include <asm/uaccess.h>

#include "nwcapi.h"
#include "nwerror.h"
#include "vfs.h"
#include "commands.h"

#ifndef strlen_user
#define strlen_user(str) strnlen_user(str, ~0UL >> 1)
#endif

static void GetUserData(struct nwc_scan_conn_info *connInfo, struct novfs_xplat_call_request *cmd,
			struct novfs_xplat_call_reply *reply);
static void GetConnData(struct nwc_get_conn_info *connInfo, struct novfs_xplat_call_request *cmd,
			struct novfs_xplat_call_reply *reply);

/*++======================================================================*/
int novfs_open_conn_by_name(struct novfs_xplat *pdata, void **Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_open_conn_by_name *openConn = NULL, *connReply = NULL;
	struct nwc_open_conn_by_name ocbn;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen, pnamelen, stypelen;
	char *data = NULL;

	if(copy_from_user(&ocbn, pdata->reqData, sizeof(ocbn)))
		return -EFAULT;
	pnamelen = strlen_user(ocbn.pName->pString);
	stypelen = strlen_user(ocbn.pServiceType);
	if (pnamelen > MAX_NAME_LEN || stypelen > NW_MAX_SERVICE_TYPE_LEN)
		return -EINVAL;
	datalen = sizeof(*openConn) + pnamelen + stypelen;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_OPEN_CONN_BY_NAME;

	cmd->dataLen = datalen;
	openConn = (struct nwd_open_conn_by_name *)cmd->data;

	openConn->nameLen = pnamelen;
	openConn->serviceLen = stypelen;
	openConn->uConnFlags = ocbn.uConnFlags;
	openConn->ConnHandle = Uint32toHandle(ocbn.ConnHandle);
	data = (char *)openConn;
	data += sizeof(*openConn);
	openConn->oName = sizeof(*openConn);

	openConn->oServiceType = openConn->oName + openConn->nameLen;
	if(copy_from_user(data, ocbn.pName->pString, openConn->nameLen)) {
		retCode = -EFAULT;
		goto exit;
	}
	data += openConn->nameLen;
	if(copy_from_user(data, ocbn.pServiceType, openConn->serviceLen)) {
		retCode = -EFAULT;
		goto exit;
	}

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (struct nwd_open_conn_by_name *)reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (struct nwd_open_conn_by_name *)reply->data;
			ocbn.RetConnHandle = HandletoUint32(connReply->newConnHandle);
			*Handle = connReply->newConnHandle;

			if(copy_to_user(pdata->reqData, &ocbn, sizeof(ocbn)))
				retCode = -EFAULT;
			else
				DbgPrint("New Conn Handle = %X", connReply->newConnHandle);
		}
		kfree(reply);
	}

exit:
	kfree(cmd);
	return ((int)retCode);
}

int novfs_open_conn_by_addr(struct novfs_xplat *pdata, void **Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_open_conn_by_addr *openConn = NULL, *connReply = NULL;
	struct nwc_open_conn_by_addr ocba;
	struct nwc_tran_addr tranAddr;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen;
	char addr[MAX_ADDRESS_LENGTH];

	if(copy_from_user(&ocba, pdata->reqData, sizeof(ocba)))
		return -EFAULT;
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
	openConn = (struct nwd_open_conn_by_addr *)cmd->data;

	if(copy_from_user(&tranAddr, ocba.pTranAddr, sizeof(tranAddr))) {
		retCode = -EFAULT;
		goto out;
	}
	if (tranAddr.uAddressLength > sizeof(addr)) {
		retCode = -EINVAL;
		goto out;
	}

	DbgPrint("tranAddr");
	novfs_dump(sizeof(tranAddr), &tranAddr);

	openConn->TranAddr.uTransportType = tranAddr.uTransportType;
	openConn->TranAddr.uAddressLength = tranAddr.uAddressLength;
	memset(addr, 0xcc, sizeof(addr) - 1);

	if(copy_from_user(addr, tranAddr.puAddress, tranAddr.uAddressLength)) {
		retCode = -EFAULT;
		goto out;
	}

	DbgPrint("addr");
	novfs_dump(sizeof(addr), addr);

	openConn->TranAddr.oAddress = *(unsigned int *)(&addr[2]);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		connReply = (struct nwd_open_conn_by_addr *)reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			connReply = (struct nwd_open_conn_by_addr *)reply->data;
			ocba.ConnHandle = HandletoUint32(connReply->ConnHandle);
			*Handle = connReply->ConnHandle;
			if(copy_to_user(pdata->reqData, &ocba, sizeof(ocba)))
				retCode = -EFAULT;
			else
				DbgPrint("New Conn Handle = %X", connReply->ConnHandle);
		}
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_open_conn_by_ref(struct novfs_xplat *pdata, void **Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_open_conn_by_ref *openConn = NULL;
	struct nwc_open_conn_by_ref ocbr;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	if(copy_from_user(&ocbr, pdata->reqData, sizeof(ocbr)))
		return -EFAULT;
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
	openConn = (struct nwd_open_conn_by_ref *)cmd->data;

	openConn->uConnReference = (void *)(unsigned long)ocbr.uConnReference;
	openConn->uConnFlags = ocbr.uConnFlags;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		openConn = (struct nwd_open_conn_by_ref *)reply->data;
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			/*
			 * we got valid data.
			 */
			ocbr.ConnHandle = HandletoUint32(openConn->ConnHandle);
			*Handle = openConn->ConnHandle;

			if(copy_to_user(pdata->reqData, &ocbr, sizeof(ocbr)))
				retCode = -EFAULT;
			else
				DbgPrint("New Conn Handle = %X", openConn->ConnHandle);
		}
		kfree(reply);
	}

	kfree(cmd);
	return (retCode);

}

int novfs_raw_send(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_request xRequest;
	struct nwc_frag *frag = NULL, *cFrag = NULL, *reqFrag = NULL;
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen, totalLen;
	unsigned int x;
	struct nwd_ncp_req *ncpData = NULL;
	struct nwd_ncp_rep *ncpReply = NULL;
	unsigned char *reqData = NULL;
	unsigned long actualReplyLength = 0;

	DbgPrint("[XPLAT] Process Raw NCP Send");
	if(copy_from_user(&xRequest, pdata->reqData, sizeof(xRequest)))
		return -EFAULT;

	if (xRequest.uNumReplyFrags > MAX_NUM_REPLIES || xRequest.uNumReplyFrags < MIN_NUM_REPLIES ||
	    xRequest.uNumRequestFrags > MAX_NUM_REQUESTS || xRequest.uNumRequestFrags < MIN_NUM_REQUESTS)
		return -EINVAL;

	/*
	 * Figure out the length of the request
	 */
	frag = kmalloc(xRequest.uNumReplyFrags * sizeof(struct nwc_frag), GFP_KERNEL);

	DbgPrint("[XPLAT RawNCP] - Reply Frag Count 0x%X", xRequest.uNumReplyFrags);

	if (!frag)
		return -ENOMEM;

	if(copy_from_user(frag, xRequest.pReplyFrags, xRequest.uNumReplyFrags * sizeof(struct nwc_frag))) {
		retCode = -EFAULT;
		goto out_frag;
	}
	totalLen = 0;

	cFrag = frag;
	for (x = 0; x < xRequest.uNumReplyFrags; x++) {
		DbgPrint("[XPLAT - RawNCP] - Frag Len = %d", cFrag->uLength);
		if (cFrag->uLength > MAX_FRAG_SIZE || cFrag->uLength < MIN_FRAG_SIZE) {
			retCode = -EINVAL;
			goto out;
		}
		totalLen += cFrag->uLength;
		cFrag++;
	}

	DbgPrint("[XPLAT - RawNCP] - totalLen = %d", totalLen);
	datalen = 0;
	reqFrag = kmalloc(xRequest.uNumRequestFrags * sizeof(struct nwc_frag), GFP_KERNEL);
	if (!reqFrag) {
		retCode = -ENOMEM;
		goto out;
	}

	if(copy_from_user(reqFrag, xRequest.pRequestFrags, xRequest.uNumRequestFrags * sizeof(struct nwc_frag))) {
		retCode = -EFAULT;
		goto out_reqfrag;
	}
	cFrag = reqFrag;
	for (x = 0; x < xRequest.uNumRequestFrags; x++) {
		if (cFrag->uLength > MAX_FRAG_SIZE || cFrag->uLength < MIN_FRAG_SIZE) {
			retCode = -EINVAL;
			goto out;
		}
		datalen += cFrag->uLength;
		cFrag++;
	}

	/*
	 * Allocate the cmd Request
	 */
	cmdlen = datalen + sizeof(*cmd) + sizeof(*ncpData);
	DbgPrint("[XPLAT RawNCP] - Frag Count 0x%X", xRequest.uNumRequestFrags);
	DbgPrint("[XPLAT RawNCP] - Total Command Data Len = %x", cmdlen);

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd) {
		retCode = -ENOMEM;
		goto out;
	}

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_RAW_NCP_REQUEST;

	/*
	 * build the NCP Request
	 */
	cmd->dataLen = cmdlen - sizeof(*cmd);
	ncpData = (struct nwd_ncp_req *)cmd->data;
	ncpData->replyLen = totalLen;
	ncpData->requestLen = datalen;
	ncpData->ConnHandle = (void *)(unsigned long)xRequest.ConnHandle;
	ncpData->function = xRequest.uFunction;

	reqData = ncpData->data;
	cFrag = reqFrag;

	for (x = 0; x < xRequest.uNumRequestFrags; x++) {
		if(copy_from_user(reqData, cFrag->pData, cFrag->uLength)) {
			retCode = -EFAULT;
			goto out;
		}
		reqData += cFrag->uLength;
		cFrag++;
	}

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	DbgPrint("RawNCP - reply = %x", reply);
	DbgPrint("RawNCP - retCode = %x", retCode);

	if (reply) {
		/*
		 * we got reply data from the daemon
		 */
		ncpReply = (struct nwd_ncp_rep *)reply->data;
		retCode = reply->Reply.ErrorCode;

		DbgPrint("RawNCP - Reply Frag Count 0x%X", xRequest.uNumReplyFrags);

		/*
		 * We need to copy the reply frags to the packet.
		 */
		reqData = ncpReply->data;
		cFrag = frag;

		totalLen = ncpReply->replyLen;
		for (x = 0; x < xRequest.uNumReplyFrags; x++) {

			DbgPrint("RawNCP - Copy Frag %d: 0x%X", x, cFrag->uLength);

			datalen = min((unsigned long)cFrag->uLength, totalLen);

			if(copy_to_user(cFrag->pData, reqData, datalen)) {
				kfree(reply);
				retCode = -EFAULT;
				goto out;
			}
			totalLen -= datalen;
			reqData += datalen;
			actualReplyLength += datalen;

			cFrag++;
		}

		kfree(reply);
	} else {
		retCode = -EIO;
	}

	xRequest.uActualReplyLength = actualReplyLength;
	if (copy_to_user(pdata->reqData, &xRequest, sizeof(xRequest)))
		retCode = -EFAULT;

out:
	kfree(cmd);
out_reqfrag:
	kfree(reqFrag);
out_frag:
	kfree(frag);

	return (retCode);
}

int novfs_conn_close(struct novfs_xplat *pdata, void **Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_close_conn cc;
	struct nwd_close_conn *nwdClose = NULL;
	int retCode = 0;
	unsigned long cmdlen, datalen, replylen;

	if(copy_from_user(&cc, pdata->reqData, sizeof(cc)))
		return -EFAULT;

	datalen = sizeof(*nwdClose);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_CLOSE_CONN;

	nwdClose = (struct nwd_close_conn *)cmd->data;
	cmd->dataLen = sizeof(*nwdClose);
	*Handle = nwdClose->ConnHandle = Uint32toHandle(cc.ConnHandle);

	/*
	 * send the request
	 */
	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, 0);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_sys_conn_close(struct novfs_xplat *pdata, unsigned long *Handle, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_close_conn cc;
	struct nwd_close_conn *nwdClose = NULL;
	unsigned int retCode = 0;
	unsigned long cmdlen, datalen, replylen;

	if(copy_from_user(&cc, pdata->reqData, sizeof(cc)))
		return -EFAULT;

	datalen = sizeof(*nwdClose);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SYS_CLOSE_CONN;

	nwdClose = (struct nwd_close_conn *)cmd->data;
	cmd->dataLen = sizeof(*nwdClose);
	nwdClose->ConnHandle = (void *)(unsigned long)cc.ConnHandle;
	*Handle = (unsigned long)cc.ConnHandle;

	/*
	 * send the request
	 */
	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, 0);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

int novfs_login_id(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_login_id lgn, *plgn = NULL;
	int retCode = -ENOMEM;
	struct ncl_string server;
	struct ncl_string username;
	struct ncl_string password;
	struct nwc_string nwcStr;

	memset(&server, 0, sizeof(server));
	memset(&username, 0, sizeof(username));
	memset(&password, 0, sizeof(password));

	if(copy_from_user(&lgn, pdata->reqData, sizeof(lgn)))
		return -EFAULT;

	DbgPrint("");
	novfs_dump(sizeof(lgn), &lgn);

	if(copy_from_user(&nwcStr, lgn.pDomainName, sizeof(nwcStr)))
		return -EFAULT;
	DbgPrint("DomainName\n");
	novfs_dump(sizeof(nwcStr), &nwcStr);

	if (nwcStr.DataLen > MAX_DOMAIN_LEN)
		return -EINVAL;

	if ((server.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
		server.type = nwcStr.DataType;
		server.len = nwcStr.DataLen;
		if (!copy_from_user((void *)server.buffer, nwcStr.pBuffer, server.len)) {
			DbgPrint("Server");
			novfs_dump(server.len, server.buffer);

			if(copy_from_user(&nwcStr, lgn.pObjectName, sizeof(nwcStr))) {
				retCode = -EFAULT;
				goto out_server;
			}
			DbgPrint("ObjectName");
			if (nwcStr.DataLen > NW_MAX_DN_BYTES) {
				retCode = -EINVAL;
				goto out;
			}
			novfs_dump(sizeof(nwcStr), &nwcStr);
			if ((username.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
				username.type = nwcStr.DataType;
				username.len = nwcStr.DataLen;
				if (!copy_from_user((void *)username.buffer, nwcStr.pBuffer, username.len)) {
					DbgPrint("User");
					novfs_dump(username.len, username.buffer);

					if(copy_from_user(&nwcStr, lgn.pPassword, sizeof(nwcStr))) {
						retCode = -EFAULT;
						goto out_username;
					}
					DbgPrint("Password");
					if (nwcStr.DataLen > MAX_PASSWORD_LENGTH) {
						retCode = -EINVAL;
						goto out_username;
					}
					novfs_dump(sizeof(nwcStr), &nwcStr);

					if ((password.buffer = kmalloc(nwcStr.DataLen, GFP_KERNEL))) {
						password.type = nwcStr.DataType;
						password.len = nwcStr.DataLen;
						if (!copy_from_user((void *)password.buffer, nwcStr.pBuffer, password.len)) {
							retCode =
							    novfs_do_login(&server, &username, &password,
									   (void **)&lgn.AuthenticationId, &Session);
							if (retCode) {
								lgn.AuthenticationId = 0;
							}

							plgn = (struct nwc_login_id *)pdata->reqData;
							if(copy_to_user(&plgn->AuthenticationId, &lgn.AuthenticationId, sizeof(plgn->AuthenticationId)))
								retCode = -EFAULT;
						}
						memset(password.buffer, 0, password.len);

					}
				}
				memset(username.buffer, 0, username.len);
			}
		}
	}
out:
	kfree(password.buffer);
out_username:
	kfree(username.buffer);
out_server:
	kfree(server.buffer);
	return (retCode);
}

int novfs_auth_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_auth_with_id pauth;
	struct nwc_auth_wid *pDauth = NULL;
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	datalen = sizeof(*pDauth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_AUTHENTICATE_CONN_WITH_ID;

	if(copy_from_user(&pauth, pdata->reqData, sizeof(pauth))) {
		retCode = -EFAULT;
		goto out;
	}

	pDauth = (struct nwc_auth_wid *)cmd->data;
	cmd->dataLen = datalen;
	pDauth->AuthenticationId = pauth.AuthenticationId;
	pDauth->ConnHandle = (void *)(unsigned long)pauth.ConnHandle;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_license_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_license_conn lisc;
	struct nwc_lisc_id *pDLisc = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	datalen = sizeof(*pDLisc);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_LICENSE_CONN;

	if(copy_from_user(&lisc, pdata->reqData, sizeof(lisc))) {
		retCode = -EFAULT;
		goto out;
	}

	pDLisc = (struct nwc_lisc_id *)cmd->data;
	cmd->dataLen = datalen;
	pDLisc->ConnHandle = (void *)(unsigned long)lisc.ConnHandle;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_logout_id(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_lo_id logout, *pDLogout = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	datalen = sizeof(*pDLogout);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_LOGOUT_IDENTITY;

	if(copy_from_user(&logout, pdata->reqData, sizeof(logout))) {
		retCode = -EFAULT;
		goto out;
	}

	pDLogout = (struct nwc_lo_id *)cmd->data;
	cmd->dataLen = datalen;
	pDLogout->AuthenticationId = logout.AuthenticationId;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_unlicense_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_unlic_conn *pUconn = NULL, ulc;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	if(copy_from_user(&ulc, pdata->reqData, sizeof(ulc)))
		return -EFAULT;
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
	pUconn = (struct nwc_unlic_conn *)cmd->data;

	pUconn->ConnHandle = (void *)(unsigned long)ulc.ConnHandle;
	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
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
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_unauthenticate auth, *pDAuth = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

	datalen = sizeof(*pDAuth);
	cmdlen = datalen + sizeof(*cmd);
	cmd = (struct novfs_xplat_call_request *)kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_UNAUTHENTICATE_CONN;

	if(copy_from_user(&auth, pdata->reqData, sizeof(auth))) {
		retCode = -EFAULT;
		goto out;
	}

	pDAuth = (struct nwc_unauthenticate *)cmd->data;
	cmd->dataLen = datalen;
	pDAuth->AuthenticationId = auth.AuthenticationId;
	pDAuth->ConnHandle = (void *)(unsigned long)auth.ConnHandle;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);

}

int novfs_get_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_get_conn_info connInfo;
	struct nwd_conn_info *pDConnInfo = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	if(copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_get_conn_info))) {
		retCode = -EFAULT;
		goto out;
	}

	if (connInfo.uInfoLength > MAX_INFO_LEN) {
		retCode = -EINVAL;
		goto out;
	}

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_CONN_INFO;

	pDConnInfo = (struct nwd_conn_info *)cmd->data;

	pDConnInfo->ConnHandle = (void *)(unsigned long)connInfo.ConnHandle;
	pDConnInfo->uInfoLevel = connInfo.uInfoLevel;
	pDConnInfo->uInfoLength = connInfo.uInfoLength;
	cmd->dataLen = sizeof(*pDConnInfo);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			GetConnData(&connInfo, cmd, reply);
		}

		kfree(reply);
	}
out:
	kfree(cmd);
	return (retCode);

}

int novfs_set_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_set_conn_info connInfo;
	struct nwd_set_conn_info *pDConnInfo = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if(copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_set_conn_info))) {
		retCode = -EFAULT;
		goto out;
	}

	if (connInfo.uInfoLength > MAX_INFO_LEN) {
		retCode = -EINVAL;
		goto out;
	}

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_CONN_INFO;

	pDConnInfo = (struct nwd_set_conn_info *)cmd->data;

	pDConnInfo->ConnHandle = (void *)(unsigned long)connInfo.ConnHandle;
	pDConnInfo->uInfoLevel = connInfo.uInfoLevel;
	pDConnInfo->uInfoLength = connInfo.uInfoLength;
	cmd->dataLen = sizeof(*pDConnInfo);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);

}

int novfs_get_id_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_get_id_info qidInfo, *gId = NULL;
	struct nwd_get_id_info *idInfo = NULL;
	struct nwc_string xferStr;
	char *str = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen;

	cmdlen = sizeof(*cmd) + sizeof(*idInfo);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if(copy_from_user(&qidInfo, pdata->reqData, sizeof(qidInfo))) {
		retCode = -EFAULT;
		goto out_cmd;
	}

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_IDENTITY_INFO;

	idInfo = (struct nwd_get_id_info *)cmd->data;
	idInfo->AuthenticationId = qidInfo.AuthenticationId;
	cmd->dataLen = sizeof(*idInfo);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;

		if (!reply->Reply.ErrorCode) {
			/*
			 * Save the return info to the user structure.
			 */
			gId = pdata->reqData;
			idInfo = (struct nwd_get_id_info *)reply->data;
			if(copy_to_user(&gId->AuthenticationId, &idInfo->AuthenticationId, sizeof(idInfo->AuthenticationId))) {
				retCode = -EFAULT;
				goto out;
			}
			if(copy_to_user(&gId->AuthType, &idInfo->AuthType, sizeof(idInfo->AuthType))) {
				retCode = -EFAULT;
				goto out;
			}
			if(copy_to_user(&gId->IdentityFlags, &idInfo->IdentityFlags, sizeof(idInfo->IdentityFlags))) {
				retCode = -EFAULT;
				goto out;
			}
			if(copy_to_user(&gId->NameType, &idInfo->NameType, sizeof(idInfo->NameType))) {
				retCode = -EFAULT;
				goto out;
			}
			if(copy_to_user(&gId->ObjectType, &idInfo->ObjectType, sizeof(idInfo->ObjectType))) {
				retCode = -EFAULT;
				goto out;
			}

			if(copy_from_user(&xferStr, gId->pDomainName, sizeof(struct nwc_string))) {
				retCode = -EFAULT;
				goto out;
			}
			if (idInfo->pDomainNameOffset >= reply->dataLen) {
				retCode = -EINVAL;
				goto out;
			}
			str = (char *)((char *)reply->data + idInfo->pDomainNameOffset);
			if (idInfo->domainLen > reply->dataLen - idInfo->pDomainNameOffset) {
				retCode = -EINVAL;
				goto out;
			}

			if(copy_to_user(xferStr.pBuffer, str, idInfo->domainLen)) {
				retCode = -EFAULT;
				goto out;
			}
			xferStr.DataType = NWC_STRING_TYPE_ASCII;
			xferStr.DataLen = idInfo->domainLen;
			if(copy_to_user(gId->pDomainName, &xferStr, sizeof(struct nwc_string))) {
				retCode = -EFAULT;
				goto out;
			}
			if(copy_from_user(&xferStr, gId->pObjectName, sizeof(struct nwc_string))) {
				retCode = -EFAULT;
				goto out;
			}

			if (idInfo->pObjectNameOffset >= reply->dataLen) {
				retCode = -EINVAL;
				goto out;
			}
			str = (char *)((char *)reply->data + idInfo->pObjectNameOffset);
			if (idInfo->objectLen > reply->dataLen - idInfo->pObjectNameOffset) {
				retCode = -EINVAL;
				goto out;
			}
			if(copy_to_user(xferStr.pBuffer, str, idInfo->objectLen)) {
				retCode = -EFAULT;
				goto out;
			}

			xferStr.DataLen = idInfo->objectLen - 1;
			xferStr.DataType = NWC_STRING_TYPE_ASCII;
			if(copy_to_user(gId->pObjectName, &xferStr, sizeof(struct nwc_string)))
				retCode = -EFAULT;
		}
	}

out:
	kfree(reply);
out_cmd:
	kfree(cmd);
	return (retCode);
}

int novfs_scan_conn_info(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_scan_conn_info connInfo, *rInfo = NULL;
	struct nwd_scan_conn_info *pDConnInfo = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen;
	unsigned char *localData = NULL;

	if(copy_from_user(&connInfo, pdata->reqData, sizeof(struct nwc_scan_conn_info)))
		return -EFAULT;

	if (connInfo.uReturnInfoLength > MAX_INFO_LEN || connInfo.uScanInfoLen > MAX_INFO_LEN)
		return -EINVAL;

	cmdlen = sizeof(*cmd) + sizeof(*pDConnInfo) + connInfo.uScanInfoLen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SCAN_CONN_INFO;

	pDConnInfo = (struct nwd_scan_conn_info *)cmd->data;

	DbgPrint("Input Data");
	__DbgPrint("    connInfo.uScanIndex = 0x%X\n", connInfo.uScanIndex);
	__DbgPrint("    connInfo.uConnectionReference = 0x%X\n", connInfo.uConnectionReference);
	__DbgPrint("    connInfo.uScanInfoLevel = 0x%X\n", connInfo.uScanInfoLevel);
	__DbgPrint("    connInfo.uScanInfoLen = 0x%X\n", connInfo.uScanInfoLen);
	__DbgPrint("    connInfo.uReturnInfoLength = 0x%X\n", connInfo.uReturnInfoLength);
	__DbgPrint("    connInfo.uReturnInfoLevel = 0x%X\n", connInfo.uReturnInfoLevel);
	__DbgPrint("    connInfo.uScanFlags = 0x%X\n", connInfo.uScanFlags);

	pDConnInfo->uScanIndex = connInfo.uScanIndex;
	pDConnInfo->uConnectionReference = connInfo.uConnectionReference;
	pDConnInfo->uScanInfoLevel = connInfo.uScanInfoLevel;
	pDConnInfo->uScanInfoLen = connInfo.uScanInfoLen;
	pDConnInfo->uReturnInfoLength = connInfo.uReturnInfoLength;
	pDConnInfo->uReturnInfoLevel = connInfo.uReturnInfoLevel;
	pDConnInfo->uScanFlags = connInfo.uScanFlags;

	if (pDConnInfo->uScanInfoLen) {
		localData = (unsigned char *)pDConnInfo;
		pDConnInfo->uScanConnInfoOffset = sizeof(*pDConnInfo);
		localData += pDConnInfo->uScanConnInfoOffset;
		if(copy_from_user(localData, connInfo.pScanConnInfo, connInfo.uScanInfoLen)) {
			retCode = -EFAULT;
			goto out;
		}
	} else {
		pDConnInfo->uScanConnInfoOffset = 0;
	}

	cmd->dataLen = sizeof(*pDConnInfo);

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		DbgPrint("Reply recieved");
		__DbgPrint("   NextIndex = %x\n", connInfo.uScanIndex);
		__DbgPrint("   ErrorCode = %x\n", reply->Reply.ErrorCode);
		__DbgPrint("   data = %p\n", reply->data);

		pDConnInfo = (struct nwd_scan_conn_info *)reply->data;
		retCode = (unsigned long)reply->Reply.ErrorCode;
		if (!retCode) {
			GetUserData(&connInfo, cmd, reply);
			rInfo = (struct nwc_scan_conn_info *)pdata->repData;
			if(copy_to_user(pdata->repData, &pDConnInfo->uScanIndex, sizeof(pDConnInfo->uScanIndex))) {
				kfree(reply);
				retCode = -EFAULT;
				goto out;
			}
			if(copy_to_user(&rInfo->uConnectionReference, &pDConnInfo->uConnectionReference, sizeof(pDConnInfo->uConnectionReference))) {
				kfree(reply);
				retCode = -EFAULT;
				goto out;
			}
		} else {
			unsigned long x;

			x = 0;
			rInfo = (struct nwc_scan_conn_info *)pdata->reqData;
			if(copy_to_user(&rInfo->uConnectionReference, &x, sizeof(rInfo->uConnectionReference)))
				retCode = -EFAULT;
		}

		kfree(reply);
	} else {
		retCode = -EIO;
	}

out:
	kfree(cmd);
	return (retCode);
}

/*
 *
 * Copies the user data out of the scan conn info call.
 *
 * FIXME: This function is very badly designed. The return parameter should not be void
 * and should be an integer. Based on the return value, the OUT parameters should be
 * used. This function and the callers should be improved and written again. */
static void GetUserData(struct nwc_scan_conn_info *connInfo, struct novfs_xplat_call_request *cmd,
			struct novfs_xplat_call_reply *reply)
{
	unsigned long uLevel;
	struct nwd_scan_conn_info *pDConnInfo = NULL;
	unsigned char *srcData = NULL;
	unsigned long dataLen = 0;

	pDConnInfo = (struct nwd_scan_conn_info *)reply->data;
	uLevel = pDConnInfo->uReturnInfoLevel;
	DbgPrint("uLevel = %d, reply = 0x%p, reply->data = 0x%X", uLevel, reply, reply->data);

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
		srcData = (unsigned char *)pDConnInfo;
		srcData += pDConnInfo->uReturnConnInfoOffset;
		dataLen = pDConnInfo->uReturnInfoLength;
		break;

	case NWC_CONN_INFO_TRAN_ADDR:
		{
			unsigned char *dstData = connInfo->pReturnConnInfo;
			struct nwc_tran_addr tranAddr;

			srcData = (unsigned char *)reply->data;
			dataLen = reply->dataLen;

			DbgPrint("NWC_CONN_INFO_TRAN_ADDR 0x%p -> 0x%p :: 0x%X", srcData, connInfo->pReturnConnInfo, dataLen);

			if(copy_from_user(&tranAddr, dstData, sizeof(tranAddr)))
				goto out_memerr;
			if (((struct nwd_scan_conn_info *)srcData)->uReturnConnInfoOffset >= reply->dataLen)
				goto out;
			srcData += ((struct nwd_scan_conn_info *)srcData)->uReturnConnInfoOffset;
			tranAddr.uTransportType = ((struct nwd_tran_addr *)srcData)->uTransportType;
			tranAddr.uAddressLength = ((struct tagNwdTranAddrEx *)srcData)->uAddressLength;
			if (tranAddr.uAddressLength > MAX_ADDRESS_LENGTH)
				goto out;
			if(copy_to_user(dstData, &tranAddr, sizeof(tranAddr)))
				goto out_memerr;
			if(copy_to_user(tranAddr.puAddress,
					      ((struct tagNwdTranAddrEx *)srcData)->Buffer, tranAddr.uAddressLength))
				goto out_memerr;
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

	if (srcData && dataLen && dataLen <= reply->dataLen) {
		DbgPrint("Copy Data 0x%p -> 0x%p :: 0x%X", srcData, connInfo->pReturnConnInfo, dataLen);
		if(copy_to_user(connInfo->pReturnConnInfo, srcData, dataLen))
			goto out_memerr;
	}

out:
	return;

out_memerr:
	/* Having this separate label for memory error handling,
	 * helps in improving code readability. Ideally a -EFAULT
	 * should be returned if the function would return an int. */
	DbgPrint("EFAULT while trying to copy memory between user and kernel space");
	return;
}

/*
 *  Copies the user data out of the scan conn info call.
 */
static void GetConnData(struct nwc_get_conn_info *connInfo, struct novfs_xplat_call_request *cmd,
			struct novfs_xplat_call_reply *reply)
{
	unsigned long uLevel;
	struct nwd_conn_info *pDConnInfo = NULL;

	unsigned char *srcData = NULL;
	unsigned long dataLen = 0;

	pDConnInfo = (struct nwd_conn_info *)cmd->data;
	uLevel = pDConnInfo->uInfoLevel;

	switch (uLevel) {
	case NWC_CONN_INFO_RETURN_ALL:
		srcData = (unsigned char *)reply->data;
		dataLen = reply->dataLen;
		break;

	case NWC_CONN_INFO_RETURN_NONE:
		dataLen = 0;
		break;

	case NWC_CONN_INFO_TRAN_ADDR:
		{
			unsigned char *dstData = connInfo->pConnInfo;
			struct nwc_tran_addr tranAddr;

			srcData = (unsigned char *)reply->data;

			if(copy_from_user(&tranAddr, dstData, sizeof(tranAddr)))
				goto out_memerr;
			tranAddr.uTransportType = ((struct tagNwdTranAddrEx *)srcData)->uTransportType;
			tranAddr.uAddressLength = ((struct tagNwdTranAddrEx *)srcData)->uAddressLength;
			if (tranAddr.uAddressLength > MAX_ADDRESS_LENGTH)
				goto out;
			if(copy_to_user(dstData, &tranAddr, sizeof(tranAddr)))
				goto out_memerr;
			if(copy_to_user(tranAddr.puAddress,
					      ((struct tagNwdTranAddrEx *)srcData)->Buffer, tranAddr.uAddressLength))
				goto out_memerr;
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
		srcData = (unsigned char *)reply->data;
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

	if (srcData && dataLen && dataLen <= reply->dataLen) {
		if(copy_to_user(connInfo->pConnInfo, srcData, connInfo->uInfoLength))
			goto out_memerr;
	}

out:
	return;

out_memerr:
	/* Having this separate label for memory error handling, 
	 * helps in improving code readability. Ideally a -EFAULT 
	 * should be returned if the function would return an int. */
	DbgPrint("EFAULT while trying to copy memory between user and kernel space");
	return;
}

int novfs_get_daemon_ver(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_get_reqversion *pDVersion = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;

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
	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		pDVersion = (struct nwd_get_reqversion *)reply->data;
		if(copy_to_user(pDVersion, pdata->reqData, sizeof(*pDVersion)))
			retCode = -EFAULT;
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);
}

int novfs_get_preferred_DS_tree(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_get_pref_ds_tree *pDGetTree = NULL;
	struct nwc_get_pref_ds_tree xplatCall, *p = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;
	unsigned char *dPtr = NULL;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_get_pref_ds_tree)))
		return -EFAULT;
	if (xplatCall.uTreeLength > NW_MAX_TREE_NAME_LEN)
		return -EINVAL;
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

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			pDGetTree = (struct nwd_get_pref_ds_tree *)reply->data;
			if (pDGetTree->DsTreeNameOffset >= reply->dataLen) {
				retCode = -EINVAL;
				goto out;
			}
			dPtr = reply->data + pDGetTree->DsTreeNameOffset;
			p = (struct nwc_get_pref_ds_tree *)pdata->reqData;

			DbgPrint("Reply recieved");
			__DbgPrint("   TreeLen = %x\n", pDGetTree->uTreeLength);
			__DbgPrint("   TreeName = %s\n", dPtr);

			if (pDGetTree->uTreeLength > reply->dataLen - pDGetTree->DsTreeNameOffset) {
				retCode = -EINVAL;
				goto out;
			}
			if(copy_to_user(p, &pDGetTree->uTreeLength, 4))
				retCode = -EFAULT;
			else if(copy_to_user(xplatCall.pDsTreeName, dPtr, pDGetTree->uTreeLength))
				retCode = -EFAULT;
		}
	}

out:
	kfree(reply);
	kfree(cmd);
	return (retCode);

}

int novfs_set_preferred_DS_tree(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwd_set_pref_ds_tree *pDSetTree = NULL;
	struct nwc_set_pref_ds_tree xplatCall;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;
	unsigned char *dPtr = NULL;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_set_pref_ds_tree)))
		return -EFAULT;
	if (xplatCall.uTreeLength > NW_MAX_TREE_NAME_LEN)
		return -EINVAL;
	datalen = sizeof(*pDSetTree) + xplatCall.uTreeLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_PREFERRED_DS_TREE;

	pDSetTree = (struct nwd_set_pref_ds_tree *)cmd->data;
	pDSetTree->DsTreeNameOffset = sizeof(*pDSetTree);
	pDSetTree->uTreeLength = xplatCall.uTreeLength;

	dPtr = cmd->data + sizeof(*pDSetTree);
	if(copy_from_user(dPtr, xplatCall.pDsTreeName, xplatCall.uTreeLength))
		goto out;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}
out:
	kfree(cmd);
	return (retCode);
}

int novfs_set_default_ctx(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_set_def_name_ctx xplatCall;
	struct nwd_set_def_name_ctx *pDSet = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, datalen, replylen;
	unsigned char *dPtr = NULL;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_set_def_name_ctx)))
		return -EFAULT;
	if (xplatCall.uNameLength > MAX_NAME_LEN || xplatCall.uTreeLength > NW_MAX_TREE_NAME_LEN)
		return -EINVAL;
	datalen = sizeof(*pDSet) + xplatCall.uTreeLength + xplatCall.uNameLength;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_DEFAULT_NAME_CONTEXT;
	cmd->dataLen = sizeof(struct nwd_set_def_name_ctx) + xplatCall.uTreeLength + xplatCall.uNameLength;

	pDSet = (struct nwd_set_def_name_ctx *)cmd->data;
	dPtr = cmd->data;

	pDSet->TreeOffset = sizeof(struct nwd_set_def_name_ctx);
	pDSet->uTreeLength = xplatCall.uTreeLength;
	pDSet->NameContextOffset = pDSet->TreeOffset + xplatCall.uTreeLength;
	pDSet->uNameLength = xplatCall.uNameLength;

	if(copy_from_user(dPtr + pDSet->TreeOffset, xplatCall.pDsTreeName, xplatCall.uTreeLength)) {
		retCode = -EFAULT;
		goto out;
	}

	if(copy_from_user(dPtr + pDSet->NameContextOffset, xplatCall.pNameContext, xplatCall.uNameLength)) {
		retCode = -EFAULT;
		goto out;
	}

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_get_default_ctx(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_get_def_name_ctx xplatCall;
	struct nwd_get_def_name_ctx *pGet = NULL;
	char *dPtr = NULL;
	int retCode = -ENOMEM;
	unsigned long cmdlen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_get_def_name_ctx)))
		return -EFAULT;
	if (xplatCall.uTreeLength > NW_MAX_TREE_NAME_LEN)
		return -EINVAL;

	cmdlen = sizeof(*cmd) + sizeof(struct nwd_get_def_name_ctx) + xplatCall.uTreeLength;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_DEFAULT_NAME_CONTEXT;
	cmd->dataLen = sizeof(struct nwd_get_def_name_ctx) + xplatCall.uTreeLength;

	pGet = (struct nwd_get_def_name_ctx *)cmd->data;
	dPtr = cmd->data;

	pGet->TreeOffset = sizeof(struct nwd_get_def_name_ctx);
	pGet->uTreeLength = xplatCall.uTreeLength;

	if(copy_from_user(dPtr + pGet->TreeOffset, xplatCall.pDsTreeName, xplatCall.uTreeLength)) {
		retCode = -EFAULT;
		goto out;
	}
	dPtr[pGet->TreeOffset + pGet->uTreeLength] = 0;

	retCode = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		retCode = reply->Reply.ErrorCode;
		if (!retCode) {
			pGet = (struct nwd_get_def_name_ctx *)reply->data;

			DbgPrint("retCode=0x%x uNameLength1=%d uNameLength2=%d", retCode, pGet->uNameLength, xplatCall.uNameLength);
			if (xplatCall.uNameLength < pGet->uNameLength) {
				pGet->uNameLength = xplatCall.uNameLength;
				retCode = NWE_BUFFER_OVERFLOW;
			}
			dPtr = (char *)pGet + pGet->NameContextOffset;
			if(copy_to_user(xplatCall.pNameContext, dPtr, pGet->uNameLength))
				retCode = -EFAULT;
		}

		kfree(reply);
	}

out:
	kfree(cmd);
	return (retCode);
}

int novfs_query_feature(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct nwc_query_feature xpCall;
	int status = 0;

	if(copy_from_user(&xpCall, pdata->reqData, sizeof(struct nwc_query_feature)))
		return -EFAULT;
	switch (xpCall.Feature) {
	case NWC_FEAT_NDS:
	case NWC_FEAT_NDS_MTREE:
	case NWC_FEAT_PRN_CAPTURE:
	case NWC_FEAT_NDS_RESOLVE:

		status = NWE_REQUESTER_FAILURE;

	}
	return (status);
}

int novfs_get_tree_monitored_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_get_tree_monitored_conn_ref xplatCall, *p = NULL;
	struct nwd_get_tree_monitored_conn_ref *pDConnRef = NULL;
	char *dPtr = NULL;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_get_tree_monitored_conn_ref)))
		return -EFAULT;
	if (!access_ok(VERIFY_READ, xplatCall.pTreeName, sizeof(struct nwc_string)))
		return -EINVAL;
	if (xplatCall.pTreeName->DataLen > NW_MAX_TREE_NAME_LEN)
		return -EINVAL;
	datalen = sizeof(*pDConnRef) + xplatCall.pTreeName->DataLen;
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_GET_TREE_MONITORED_CONN_REF;

	pDConnRef = (struct nwd_get_tree_monitored_conn_ref *)cmd->data;
	pDConnRef->TreeName.boffset = sizeof(*pDConnRef);
	pDConnRef->TreeName.len = xplatCall.pTreeName->DataLen;
	pDConnRef->TreeName.type = xplatCall.pTreeName->DataType;

	dPtr = cmd->data + sizeof(*pDConnRef);
	if(copy_from_user(dPtr, xplatCall.pTreeName->pBuffer, pDConnRef->TreeName.len)) {
		status = -EFAULT;
		goto out;
	}
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		pDConnRef = (struct nwd_get_tree_monitored_conn_ref *)reply->data;
		dPtr = reply->data + pDConnRef->TreeName.boffset;
		p = (struct nwc_get_tree_monitored_conn_ref *)pdata->reqData;
		if(copy_to_user(&p->uConnReference, &pDConnRef->uConnReference, 4))
			status = -EFAULT;
		else
			status = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (status);
}

int novfs_enum_ids(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_enum_ids xplatCall, *eId = NULL;
	struct nwd_enum_ids *pEnum = NULL;
	struct nwc_string xferStr;
	char *str = NULL;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_enum_ids)))
		return -EFAULT;
	datalen = sizeof(*pEnum);
	cmdlen = datalen + sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_ENUMERATE_IDENTITIES;

	DbgPrint("Send Request");
	__DbgPrint("   iterator = %x\n", xplatCall.Iterator);
	__DbgPrint("   cmdlen = %d\n", cmdlen);

	pEnum = (struct nwd_enum_ids *)cmd->data;
	pEnum->Iterator = xplatCall.Iterator;
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		status = reply->Reply.ErrorCode;

		eId = pdata->repData;
		pEnum = (struct nwd_enum_ids *)reply->data;
		if(copy_to_user(&eId->Iterator, &pEnum->Iterator, sizeof(pEnum->Iterator))) {
			status = -EFAULT;
			goto out;
		}
		DbgPrint("[XPLAT NWCAPI] Found AuthId 0x%X", pEnum->AuthenticationId);
		if(copy_to_user(&eId->AuthenticationId, &pEnum->AuthenticationId, sizeof(pEnum->AuthenticationId))) {
			status = -EFAULT;
			goto out;
		}
		if(copy_to_user(&eId->AuthType, &pEnum->AuthType, sizeof(pEnum->AuthType))) {
			status = -EFAULT;
			goto out;
		}
		if(copy_to_user(&eId->IdentityFlags, &pEnum->IdentityFlags, sizeof(pEnum->IdentityFlags))) {
			status = -EFAULT;
			goto out;
		}
		if(copy_to_user(&eId->NameType, &pEnum->NameType, sizeof(pEnum->NameType))) {
			status = -EFAULT;
			goto out;
		}
		if(copy_to_user(&eId->ObjectType, &pEnum->ObjectType, sizeof(pEnum->ObjectType))) {
			status = -EFAULT;
			goto out;
		}

		if (!status) {
			if(copy_from_user(&xferStr, eId->pDomainName, sizeof(struct nwc_string))) {
				status = -EFAULT;
				goto out;
			}
			if (pEnum->domainNameOffset >= reply->dataLen) {
				status = -EINVAL;
				goto out;
			}
			str = (char *)((char *)reply->data + pEnum->domainNameOffset);
			DbgPrint("[XPLAT NWCAPI] Found Domain %s", str);
			if (pEnum->domainNameLen > reply->dataLen - pEnum->domainNameOffset) {
				status = -EINVAL;
				goto out;
			}
			if(copy_to_user(xferStr.pBuffer, str, pEnum->domainNameLen)) {
				status = -EFAULT;
				goto out;
			}
			xferStr.DataType = NWC_STRING_TYPE_ASCII;
			xferStr.DataLen = pEnum->domainNameLen - 1;
			if(copy_to_user(eId->pDomainName, &xferStr, sizeof(struct nwc_string))) {
				status = -EFAULT;
				goto out;
			}

			if(copy_from_user(&xferStr, eId->pObjectName, sizeof(struct nwc_string))) {
				status = -EFAULT;
				goto out;
			}
			if (pEnum->objectNameOffset >= reply->dataLen) {
				status = -EINVAL;
				goto out;
			}
			str = (char *)((char *)reply->data + pEnum->objectNameOffset);
			DbgPrint("[XPLAT NWCAPI] Found User %s", str);
			if (pEnum->objectNameLen > reply->dataLen - pEnum->objectNameOffset) {
				status = -EINVAL;
				goto out;
			}
			if(copy_to_user(xferStr.pBuffer, str, pEnum->objectNameLen)) {
				status = -EFAULT;
				goto out;
			}
			xferStr.DataType = NWC_STRING_TYPE_ASCII;
			xferStr.DataLen = pEnum->objectNameLen - 1;
			if(copy_to_user(eId->pObjectName, &xferStr, sizeof(struct nwc_string)))
				status = -EFAULT;
		}
	}
out:
	kfree(reply);
	kfree(cmd);
	return (status);
}

int novfs_change_auth_key(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_change_key xplatCall;
	struct nwd_change_key *pNewKey = NULL;
	struct nwc_string xferStr;
	char *str = NULL;
	unsigned long status = -ENOMEM, cmdlen = 0, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_change_key)))
		return -EFAULT;
	if (!access_ok(VERIFY_READ, xplatCall.pDomainName, sizeof(struct nwc_string)) ||
	    !access_ok(VERIFY_READ, xplatCall.pObjectName, sizeof(struct nwc_string)) ||
	    !access_ok(VERIFY_READ, xplatCall.pNewPassword, sizeof(struct nwc_string)) ||
	    !access_ok(VERIFY_READ, xplatCall.pVerifyPassword, sizeof(struct nwc_string)))
		return -EINVAL;
	if (xplatCall.pDomainName->DataLen > MAX_DOMAIN_LEN ||
	    xplatCall.pObjectName->DataLen > MAX_OBJECT_NAME_LENGTH ||
	    xplatCall.pNewPassword->DataLen > MAX_PASSWORD_LENGTH || xplatCall.pVerifyPassword->DataLen > MAX_PASSWORD_LENGTH)
		return -EINVAL;

	datalen =
	    sizeof(struct nwd_change_key) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pNewPassword->DataLen + xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_change_key *)cmd->data;
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
	if(copy_from_user(&xferStr, xplatCall.pDomainName, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->domainNameOffset = sizeof(*pNewKey);
	if (xferStr.DataLen > MAX_DOMAIN_LEN) {
		status = -EINVAL;
		goto out;
	}
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->domainNameLen = xferStr.DataLen;

	/*
	 * Get the User Name
	 */
	str += pNewKey->domainNameLen;
	if(copy_from_user(&xferStr, xplatCall.pObjectName, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->objectNameOffset = pNewKey->domainNameOffset + pNewKey->domainNameLen;
	if (xferStr.DataLen > MAX_OBJECT_NAME_LENGTH) {
		status = -EINVAL;
		goto out;
	}
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->objectNameLen = xferStr.DataLen;

	/*
	 * Get the New Password
	 */
	str += pNewKey->objectNameLen;
	if(copy_from_user(&xferStr, xplatCall.pNewPassword, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->newPasswordOffset = pNewKey->objectNameOffset + pNewKey->objectNameLen;
	if (xferStr.DataLen > MAX_PASSWORD_LENGTH) {
		status = -EINVAL;
		goto out;
	}
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->newPasswordLen = xferStr.DataLen;

	/*
	 * Get the Verify Password
	 */
	str += pNewKey->newPasswordLen;
	if(copy_from_user(&xferStr, xplatCall.pVerifyPassword, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->verifyPasswordOffset = pNewKey->newPasswordOffset + pNewKey->newPasswordLen;
	if (xferStr.DataLen > MAX_PASSWORD_LENGTH) {
		status = -EINVAL;
		goto out;
	}
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->verifyPasswordLen = xferStr.DataLen;

	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		status = reply->Reply.ErrorCode;

	}
out:
	memset(cmd, 0, cmdlen);
	kfree(reply);
	kfree(cmd);
	return (status);
}

int novfs_set_pri_conn(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_set_primary_conn xplatCall;
	struct nwd_set_primary_conn *pConn = NULL;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_set_primary_conn)))
		return -EFAULT;

	datalen = sizeof(struct nwd_set_primary_conn);
	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	pConn = (struct nwd_set_primary_conn *)cmd->data;
	cmd->dataLen = datalen;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_PRIMARY_CONN;
	pConn->ConnHandle = (void *)(unsigned long)xplatCall.ConnHandle;
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

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
	struct novfs_xplat_call_reply *reply = NULL;
	unsigned long status = -ENOMEM, cmdlen, replylen;

	cmdlen = (unsigned long)(&((struct novfs_xplat_call_request *)0)->data);

	cmd.dataLen = 0;
	cmd.Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd.Command.SequenceNumber = 0;
	cmd.Command.SessionId = Session;
	cmd.NwcCommand = NWC_GET_PRIMARY_CONN;

	status = Queue_Daemon_Command((void *)&cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		if (!status) {
			if(copy_to_user(pdata->repData, reply->data, sizeof(unsigned long)))
				status = -EFAULT;
		}

		kfree(reply);
	}

	return (status);
}

int novfs_set_map_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{

	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	unsigned long status = 0, datalen, cmdlen, replylen;
	struct nwc_map_drive_ex symInfo;

	DbgPrint("");
	cmdlen = sizeof(*cmd);
	if (copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;
	if (symInfo.dirPathOffsetLength > MAX_OFFSET_LEN || symInfo.linkOffsetLength > MAX_OFFSET_LEN)
		return -EINVAL;
	datalen = sizeof(symInfo) + symInfo.dirPathOffsetLength + symInfo.linkOffsetLength;

	__DbgPrint(" cmdlen = %d\n", cmdlen);
	__DbgPrint(" dataLen = %d\n", datalen);
	__DbgPrint(" symInfo.dirPathOffsetLength = %d\n", symInfo.dirPathOffsetLength);
	__DbgPrint(" symInfo.linkOffsetLength = %d\n", symInfo.linkOffsetLength);
	__DbgPrint(" pdata->datalen = %d\n", pdata->reqLen);

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

	if (copy_from_user(cmd->data, pdata->reqData, datalen)) {
		kfree(cmd);
		return -EFAULT;
	}
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		kfree(reply);
	}
	kfree(cmd);
	return (status);

}

int novfs_unmap_drive(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	unsigned long status = 0, datalen, cmdlen, replylen;
	struct nwc_unmap_drive_ex symInfo;

	DbgPrint("");

	if(copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;
	if (symInfo.linkLen > MAX_NAME_LEN)
		return -EINVAL;
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

	if(copy_from_user(cmd->data, pdata->reqData, datalen)) {
		status = -EFAULT;
		goto out;
	}
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (status);
}

int novfs_enum_drives(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	unsigned long status = 0, cmdlen, replylen;
	unsigned long offset;
	char *cp = NULL;

	DbgPrint("");

	cmdlen = sizeof(*cmd);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->dataLen = 0;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_ENUMERATE_DRIVES;
	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;
		DbgPrint("Status Code = 0x%X", status);
		if (!status) {
			offset = sizeof(((struct nwc_get_mapped_drives *) pdata->repData)->MapBuffLen);
			cp = reply->data;
			replylen = ((struct nwc_get_mapped_drives *)pdata->repData)->MapBuffLen;
			if (offset > reply->dataLen) {
				status = -EINVAL;
				goto out;
			}
			if(copy_to_user(pdata->repData, cp, offset)) {
				status = -EFAULT;
				goto out;
			}
			cp += offset;
			if(copy_to_user(((struct nwc_get_mapped_drives *)pdata->repData)->MapBuffer, cp,
					      min(replylen - offset, reply->dataLen - offset)))
				status = -EFAULT;
		}
	}
out:
	kfree(reply);
	kfree(cmd);
	return (status);
}

int novfs_get_bcast_msg(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	unsigned long cmdlen, replylen;
	int status = 0x8866, cpylen;
	struct nwc_get_bcast_notification msg;
	struct nwd_get_bcast_notification *dmsg = NULL;

	cmdlen = sizeof(*cmd) + sizeof(*dmsg);
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	if(copy_from_user(&msg, pdata->reqData, sizeof(msg))) {
		status = -EFAULT;
		goto out;
	}
	cmd->dataLen = sizeof(*dmsg);
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;

	cmd->NwcCommand = NWC_GET_BROADCAST_MESSAGE;
	dmsg = (struct nwd_get_bcast_notification *)cmd->data;
	dmsg->uConnReference = (void *)(unsigned long)msg.uConnReference;

	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);

	if (reply) {
		status = reply->Reply.ErrorCode;

		if (!status) {
			char *cp = pdata->repData;

			dmsg = (struct nwd_get_bcast_notification *)reply->data;
			if (pdata->repLen < dmsg->messageLen) {
				dmsg->messageLen = pdata->repLen;
			}
			msg.messageLen = dmsg->messageLen;
			cpylen = offsetof(struct nwc_get_bcast_notification, message);
			cp += cpylen;
			if(copy_to_user(pdata->repData, &msg, cpylen)) {
				status = -EFAULT;
				kfree(reply);
				goto out;
			}
			if(copy_to_user(cp, dmsg->message, msg.messageLen)) {
				status = -EFAULT;
				kfree(reply);
				goto out;
			}
		} else {
			msg.messageLen = 0;
			msg.message[0] = 0;
			cpylen = offsetof(struct nwc_get_bcast_notification, message);
			if(copy_to_user(pdata->repData, &msg, sizeof(msg)))
				status = -EFAULT;
		}
		kfree(reply);
	}

out:
	kfree(cmd);
	return (status);
}

int novfs_set_key_value(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_set_key xplatCall;
	struct nwd_set_key *pNewKey = NULL;
	struct nwc_string cstrObjectName, cstrPassword;
	char *str = NULL;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_set_key)))
		return -EFAULT;
	if(copy_from_user(&cstrObjectName, xplatCall.pObjectName, sizeof(struct nwc_string)))
		return -EFAULT;
	if(copy_from_user(&cstrPassword, xplatCall.pNewPassword, sizeof(struct nwc_string)))
		return -EFAULT;

	if (cstrObjectName.DataLen > MAX_OBJECT_NAME_LENGTH || cstrPassword.DataLen > MAX_PASSWORD_LENGTH)
		return -EINVAL;
	datalen = sizeof(struct nwd_set_key) + cstrObjectName.DataLen + cstrPassword.DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_set_key *)cmd->data;
	cmd->dataLen = datalen;
	cmd->Command.CommandType = VFS_COMMAND_XPLAT_CALL;
	cmd->Command.SequenceNumber = 0;
	cmd->Command.SessionId = Session;
	cmd->NwcCommand = NWC_SET_KEY;

	pNewKey->ObjectType = xplatCall.ObjectType;
	pNewKey->AuthenticationId = xplatCall.AuthenticationId;
	pNewKey->ConnHandle = (void *)(unsigned long)xplatCall.ConnHandle;
	str = (char *)pNewKey;

	/*
	 * Get the User Name
	 */
	str += sizeof(struct nwd_set_key);
	if(copy_from_user(str, cstrObjectName.pBuffer, cstrObjectName.DataLen)) {
		status = -EFAULT;
		goto out;
	}

	str += pNewKey->objectNameLen = cstrObjectName.DataLen;
	pNewKey->objectNameOffset = sizeof(struct nwd_set_key);

	/*
	 * Get the Verify Password
	 */
	if(copy_from_user(str, cstrPassword.pBuffer, cstrPassword.DataLen)) {
		status = -EFAULT;
		goto out;
	}

	pNewKey->newPasswordLen = cstrPassword.DataLen;
	pNewKey->newPasswordOffset = pNewKey->objectNameOffset + pNewKey->objectNameLen;

	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		status = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (status);
}

int novfs_verify_key_value(struct novfs_xplat *pdata, struct novfs_schandle Session)
{
	struct novfs_xplat_call_request *cmd = NULL;
	struct novfs_xplat_call_reply *reply = NULL;
	struct nwc_verify_key xplatCall;
	struct nwd_verify_key *pNewKey = NULL;
	struct nwc_string xferStr;
	char *str = NULL;
	unsigned long status = -ENOMEM, cmdlen, datalen, replylen;

	if(copy_from_user(&xplatCall, pdata->reqData, sizeof(struct nwc_verify_key))) {
		status = -EFAULT;
		goto out;
	}

	if (!access_ok(VERIFY_READ, xplatCall.pDomainName, sizeof(struct nwc_string)) ||
	    !access_ok(VERIFY_READ, xplatCall.pVerifyPassword, sizeof(struct nwc_string)))
		return -EINVAL;
	if (xplatCall.pDomainName->DataLen > MAX_NAME_LEN || xplatCall.pObjectName->DataLen > MAX_OBJECT_NAME_LENGTH ||
	    xplatCall.pVerifyPassword->DataLen > MAX_PASSWORD_LENGTH)
		return -EINVAL;

	datalen =
	    sizeof(struct nwd_verify_key) + xplatCall.pDomainName->DataLen +
	    xplatCall.pObjectName->DataLen + xplatCall.pVerifyPassword->DataLen;

	cmdlen = sizeof(*cmd) + datalen;
	cmd = (struct novfs_xplat_call_request *)kmalloc(cmdlen, GFP_KERNEL);

	if (!cmd)
		return -ENOMEM;

	pNewKey = (struct nwd_verify_key *)cmd->data;
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
	if(copy_from_user(&xferStr, xplatCall.pDomainName, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->domainNameOffset = sizeof(*pNewKey);
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->domainNameLen = xferStr.DataLen;

	/*
	 * Get the User Name
	 */
	str += pNewKey->domainNameLen;
	if(copy_from_user(&xferStr, xplatCall.pObjectName, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->objectNameOffset = pNewKey->domainNameOffset + pNewKey->domainNameLen;
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->objectNameLen = xferStr.DataLen;

	/*
	 * Get the Verify Password
	 */
	str += pNewKey->objectNameLen;
	if(copy_from_user(&xferStr, xplatCall.pVerifyPassword, sizeof(struct nwc_string))) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->verifyPasswordOffset = pNewKey->objectNameOffset + pNewKey->objectNameLen;
	if(copy_from_user(str, xferStr.pBuffer, xferStr.DataLen)) {
		status = -EFAULT;
		goto out;
	}
	pNewKey->verifyPasswordLen = xferStr.DataLen;

	status = Queue_Daemon_Command((void *)cmd, cmdlen, NULL, 0, (void **)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		status = reply->Reply.ErrorCode;
		kfree(reply);
	}

out:
	kfree(cmd);
	return (status);
}
