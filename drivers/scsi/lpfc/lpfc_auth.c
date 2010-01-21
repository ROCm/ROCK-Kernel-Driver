/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006-2008 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/
/* See Fibre Channel protocol T11 FC-SP for details */
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_crtn.h"
#include "lpfc_logmsg.h"
#include "lpfc_auth_access.h"
#include "lpfc_auth.h"

void
lpfc_start_authentication(struct lpfc_vport *vport,
		       struct lpfc_nodelist *ndlp)
{
	uint32_t nego_payload_len;
	uint8_t *nego_payload;

	nego_payload = kmalloc(MAX_AUTH_REQ_SIZE, GFP_KERNEL);
	if (!nego_payload)
		return;
	vport->auth.trans_id++;
	vport->auth.auth_msg_state = LPFC_AUTH_NEGOTIATE;
	nego_payload_len = lpfc_build_auth_neg(vport, nego_payload);
	lpfc_issue_els_auth(vport, ndlp, AUTH_NEGOTIATE,
			    nego_payload, nego_payload_len);
	kfree(nego_payload);
}

void
lpfc_dhchap_make_challenge(struct Scsi_Host *shost, int status,
			void *rsp, uint32_t rsp_len)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_nodelist *ndlp;
	uint32_t chal_payload_len;
	uint8_t *chal_payload;
	struct fc_auth_rsp *auth_rsp = rsp;

	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		kfree(rsp);
		return;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_SECURITY,
			 "1003 Send dhchap challenge local_wwpn "
			 "%llX remote_wwpn %llX \n",
			 (unsigned long long)auth_rsp->local_wwpn,
			 (unsigned long long)auth_rsp->remote_wwpn);

	chal_payload = kmalloc(MAX_AUTH_REQ_SIZE, GFP_KERNEL);
	if (!chal_payload) {
		kfree(rsp);
		return;
	}
	vport->auth.auth_msg_state = LPFC_DHCHAP_CHALLENGE;
	chal_payload_len = lpfc_build_dhchap_challenge(vport,
				chal_payload, rsp);
	lpfc_issue_els_auth(vport, ndlp, DHCHAP_CHALLENGE,
			    chal_payload, chal_payload_len);
	kfree(chal_payload);
	kfree(rsp);
}


void
lpfc_dhchap_make_response(struct Scsi_Host *shost, int status,
			void *rsp, uint32_t rsp_len)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_nodelist *ndlp;
	uint32_t reply_payload_len;
	uint8_t *reply_payload;
	struct fc_auth_rsp *auth_rsp = rsp;

	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		kfree(rsp);
		return;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_SECURITY,
			 "1004 Send dhchap reply local_wwpn "
			 "%llX remote_wwpn %llX \n",
			 (unsigned long long)auth_rsp->local_wwpn,
			 (unsigned long long)auth_rsp->remote_wwpn);

	reply_payload = kmalloc(MAX_AUTH_REQ_SIZE, GFP_KERNEL);
	if (!reply_payload) {
		kfree(rsp);
		return;
	}

	vport->auth.auth_msg_state = LPFC_DHCHAP_REPLY;
	reply_payload_len = lpfc_build_dhchap_reply(vport, reply_payload, rsp);
	lpfc_issue_els_auth(vport, ndlp, DHCHAP_REPLY,
			    reply_payload, reply_payload_len);
	kfree(reply_payload);
	kfree(rsp);

}


void
lpfc_dhchap_authenticate(struct Scsi_Host *shost,
			int status, void *rsp,
			uint32_t rsp_len)
{
	struct fc_auth_rsp *auth_rsp = (struct fc_auth_rsp *)rsp;
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_nodelist *ndlp;

	ndlp = lpfc_findnode_did(vport, Fabric_DID);
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		kfree(rsp);
		return;
	}
	if (status != 0) {
		lpfc_issue_els_auth_reject(vport, ndlp,
			AUTH_ERR, AUTHENTICATION_FAILED);
		kfree(rsp);
		return;
	}

	if (auth_rsp->u.dhchap_success.authenticated) {
		uint32_t suc_payload_len;
		uint8_t *suc_payload;

		suc_payload = kmalloc(MAX_AUTH_REQ_SIZE, GFP_KERNEL);
		if (!suc_payload) {
			lpfc_issue_els_auth_reject(vport, ndlp,
				AUTH_ERR, AUTHENTICATION_FAILED);
			kfree(rsp);
			return;
		}
		suc_payload_len = lpfc_build_dhchap_success(vport,
				suc_payload, rsp);
		if (suc_payload_len == sizeof(uint32_t)) {
			/* Auth is complete after sending this SUCCESS */
			vport->auth.auth_msg_state = LPFC_DHCHAP_SUCCESS;
		} else {
			/* Need to wait for SUCCESS from Auth Initiator */
			vport->auth.auth_msg_state = LPFC_DHCHAP_SUCCESS_REPLY;
		}
		lpfc_issue_els_auth(vport, ndlp, DHCHAP_SUCCESS,
				    suc_payload, suc_payload_len);
		kfree(suc_payload);
		vport->auth.direction |= AUTH_DIRECTION_LOCAL;
	} else {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1005 AUTHENTICATION_FAILURE Nport:x%x\n",
				 ndlp->nlp_DID);
		lpfc_issue_els_auth_reject(vport, ndlp,
					   AUTH_ERR, AUTHENTICATION_FAILED);
		if (vport->auth.auth_state == LPFC_AUTH_SUCCESS)
			lpfc_port_auth_failed(ndlp, LPFC_AUTH_FAIL_AUTH_RJT);
	}

	kfree(rsp);
}

int
lpfc_unpack_auth_negotiate(struct lpfc_vport *vport, uint8_t *message,
			   uint8_t *reason, uint8_t *explanation)
{
	uint32_t prot_len;
	uint32_t param_len;
	int i, j = 0;

	/* Following is the format of the message. Name Format.
	 * uint16_t  nameTag;
	 * uint16_t  nameLength;
	 * uint8_t   name[8];
	 * AUTH_Negotiate Message
	 * uint32_t  NumberOfAuthProtocals
	 * uint32_t  AuthProtParameter#1Len
	 * uint32_t  AuthProtID#1  (DH-CHAP = 0x1)
	 * AUTH_Negotiate DH-CHAP
	 * uint16_t  DH-CHAPParameterTag (HashList = 0x1)
	 * uint16_t  DH-CHAPParameterWordCount (number of uint32_t entries)
	 * uint8_t   DH-CHAPParameter[]; (uint32_t entries)
	 * uint16_t  DH-CHAPParameterTag (DHglDList = 0x2)
	 * uint16_t  DH-CHAPParameterWordCount (number of uint32_t entries)
	 * uint8_t   DH-CHAPParameter[]; (uint32_t entries)
	 * DHCHAP_Challenge Message
	 * uint32_t  hashIdentifier;
	 * uint32_t  dhgroupIdentifier;
	 * uint32_t  challengevalueLen;
	 * uint8_t   challengeValue[];
	 * uint32_t  dhvalueLen;
	 * uint8_t   dhvalue[];
	 */

	/* Name Tag */
	if (be16_to_cpu(*(uint16_t *)message) != NAME_TAG) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1006 Bad Name tag in auth message 0x%x\n",
				 be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Name Length */
	if (be16_to_cpu(*(uint16_t *)message) != NAME_LEN) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1007 Bad Name length in auth message 0x%x\n",
				 be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Skip over Remote Port Name */
	message += NAME_LEN;

	 /* Number of Auth Protocols must be 1 DH-CHAP */
	if (be32_to_cpu(*(uint32_t *)message) != 1) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1008 Bad Number of Protocols 0x%x\n",
				 be32_to_cpu(*(uint32_t *)message));
		return 1;
	}
	message += sizeof(uint32_t);

	/* Protocol Parameter Length */
	prot_len = be32_to_cpu(*(uint32_t *)message);
	message += sizeof(uint32_t);

	/* Protocol Parameter type */
	if (be32_to_cpu(*(uint32_t *)message) != FC_DHCHAP) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1009 Bad param type 0x%x\n",
				 be32_to_cpu(*(uint32_t *)message));
		return 1;
	}
	message += sizeof(uint32_t);

	/* Parameter #1 Tag */
	if (be16_to_cpu(*(uint16_t *)message) != HASH_LIST_TAG) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1010 Bad Tag 1 0x%x\n",
				 be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Parameter #1 Length */
	param_len =  be16_to_cpu(*(uint16_t *)message);
	message += sizeof(uint16_t);

	/* Choose a hash function */
	for (i = 0; i < vport->auth.hash_len; i++) {
		for (j = 0; j < param_len; j++) {
			if (vport->auth.hash_priority[i] ==
			    be32_to_cpu(((uint32_t *)message)[j]))
				break;
		}
		if (j != param_len)
			break;
	}
	if (i == vport->auth.hash_len && j == param_len) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1011 Auth_neg no hash function chosen.\n");
		return 1;
	}
	vport->auth.hash_id = vport->auth.hash_priority[i];
	message += sizeof(uint32_t) * param_len;

	/* Parameter #2 Tag */
	if (be16_to_cpu(*(uint16_t *)message) != DHGID_LIST_TAG) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1012 Auth_negotiate Bad Tag 2 0x%x\n",
				 be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Parameter #2 Length */
	param_len =  be16_to_cpu(*(uint16_t *)message);
	message += sizeof(uint16_t);

	/* Choose a DH Group */
	for (i = 0; i < vport->auth.dh_group_len; i++) {
		for (j = 0; j < param_len; j++) {
			if (vport->auth.dh_group_priority[i] ==
			    be32_to_cpu(((uint32_t *)message)[j]))
				break;
		}
		if (j != param_len)
			break;
	}
	if (i == vport->auth.dh_group_len && j == param_len) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1013 Auth_negotiate  no DH_group found. \n");
		return 1;
	}
	vport->auth.group_id = vport->auth.dh_group_priority[i];
	message += sizeof(uint32_t) * param_len;

	return 0;
}

int
lpfc_unpack_dhchap_challenge(struct lpfc_vport *vport, uint8_t *message,
			     uint8_t *reason, uint8_t *explanation)
{
	int i;

	/* Following is the format of the message DHCHAP_Challenge.
	 * uint16_t  nameTag;
	 * uint16_t  nameLength;
	 * uint8_t   name[8];
	 * uint32_t  hashIdentifier;
	 * uint32_t  dhgroupIdentifier;
	 * uint32_t  challengevalueLen;
	 * uint8_t   challengeValue[];
	 * uint32_t  dhvalueLen;
	 * uint8_t   dhvalue[];
	 */

	/* Name Tag */
	if (be16_to_cpu(*(uint16_t *)message) != NAME_TAG) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1014 dhchap challenge bad name tag 0x%x. \n",
				 be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Name Length */
	if (be16_to_cpu(*(uint16_t *)message) != NAME_LEN) {
		*reason = AUTH_ERR;
		*explanation = BAD_PAYLOAD;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1015 dhchap challenge bad name length "
				 "0x%x.\n", be16_to_cpu(*(uint16_t *)message));
		return 1;
	}
	message += sizeof(uint16_t);

	/* Remote Port Name */
	message += NAME_LEN;

	/* Hash ID */
	vport->auth.hash_id = be32_to_cpu(*(uint32_t *)message);  /* Hash id */
	for (i = 0; i < vport->auth.hash_len; i++) {
		if (vport->auth.hash_id == vport->auth.hash_priority[i])
			break;
	}
	if (i == vport->auth.hash_len) {
		*reason = LOGIC_ERR;
		*explanation = BAD_ALGORITHM;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1016 dhchap challenge Hash ID not Supported "
				 "0x%x. \n", vport->auth.hash_id);
		return 1;
	}
	message += sizeof(uint32_t);

	vport->auth.group_id =
		be32_to_cpu(*(uint32_t *)message);  /* DH group id */
	for (i = 0; i < vport->auth.dh_group_len; i++) {
		if (vport->auth.group_id == vport->auth.dh_group_priority[i])
			break;
	}
	if (i == vport->auth.dh_group_len) {
		*reason = LOGIC_ERR;
		*explanation = BAD_DHGROUP;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
				 "1017 dhchap challenge could not find DH "
				 "Group. \n");
		return 1;
	}
	message += sizeof(uint32_t);

	vport->auth.challenge_len =
		be32_to_cpu(*(uint32_t *)message);  /* Challenge Len */
	message += sizeof(uint32_t);

	/* copy challenge to vport */
	if (vport->auth.challenge != NULL)
		kfree(vport->auth.challenge);
	vport->auth.challenge = kmalloc(vport->auth.challenge_len, GFP_KERNEL);
	if (!vport->auth.challenge) {
		*reason = AUTH_ERR;
		return 1;
	}
	memcpy(vport->auth.challenge, message, vport->auth.challenge_len);
	message += vport->auth.challenge_len;

	vport->auth.dh_pub_key_len =
		be32_to_cpu(*(uint32_t *)message);  /* DH Value Len */
	message += sizeof(uint32_t);

	if (vport->auth.dh_pub_key_len != 0) {
		if (vport->auth.group_id == DH_GROUP_NULL) {
			*reason = LOGIC_ERR;
			*explanation = BAD_DHGROUP;
			lpfc_printf_vlog(vport, KERN_ERR, LOG_SECURITY,
					 "1018 dhchap challenge No Public key "
					 "for non-NULL DH Group.\n");
			return 1;
		}

		/* Copy to the vport to save for authentication */
		if (vport->auth.dh_pub_key != NULL)
			kfree(vport->auth.dh_pub_key);
		vport->auth.dh_pub_key = kmalloc(vport->auth.dh_pub_key_len,
				GFP_KERNEL);
		if (!vport->auth.dh_pub_key) {
			*reason = AUTH_ERR;
			return 1;
		}
		memcpy(vport->auth.dh_pub_key, message,
			vport->auth.dh_pub_key_len);
	}
	return 0;
}

int
lpfc_unpack_dhchap_reply(struct lpfc_vport *vport, uint8_t *message,
			 struct fc_auth_req *fc_req)
{
	uint32_t rsp_len;
	uint32_t dh_len;
	uint32_t challenge_len;

	/* Following is the format of the message DHCHAP_Reply.
	 * uint32_t	Response Value Length;
	 * uint8_t	Response Value[];
	 * uint32_t	DH Value Length;
	 * uint8_t	DH Value[];
	 * uint32_t	Challenge Value Length;
	 * uint8_t	Challenge Value[];
	 */

	rsp_len = be32_to_cpu(*(uint32_t *)message);   /* Response Len */
	message += sizeof(uint32_t);
	memcpy(fc_req->u.dhchap_success.data + vport->auth.challenge_len,
	       message, rsp_len);
	fc_req->u.dhchap_success.received_response_len = rsp_len;
	message += rsp_len;

	dh_len = be32_to_cpu(*(uint32_t *)message);   /* DH Len */
	message += sizeof(uint32_t);
	memcpy(fc_req->u.dhchap_success.data + vport->auth.challenge_len +
	       rsp_len, message, dh_len);
	fc_req->u.dhchap_success.received_public_key_len = dh_len;
	message += dh_len;

	challenge_len = be32_to_cpu(*(uint32_t *)message);   /* Challenge Len */
	message += sizeof(uint32_t);
	memcpy(fc_req->u.dhchap_success.data + vport->auth.challenge_len
	       + rsp_len + dh_len, message, challenge_len);
	fc_req->u.dhchap_success.received_challenge_len = challenge_len;
	message += challenge_len;

	return rsp_len + dh_len + challenge_len;
}

int
lpfc_unpack_dhchap_success(struct lpfc_vport *vport, uint8_t *message,
			   struct fc_auth_req *fc_req)
{
	uint32_t rsp_len = 0;

	/* DHCHAP_Success.
	 * uint32_t  responseValueLen;
	 * uint8_t   response[];
	 */

	rsp_len = be32_to_cpu(*(uint32_t *)message);   /* Response Len */
	message += sizeof(uint32_t);
	memcpy(fc_req->u.dhchap_success.data + vport->auth.challenge_len,
	       message, rsp_len);
	fc_req->u.dhchap_success.received_response_len = rsp_len;

	memcpy(fc_req->u.dhchap_success.data +
		vport->auth.challenge_len + rsp_len,
		vport->auth.dh_pub_key, vport->auth.dh_pub_key_len);

	fc_req->u.dhchap_success.received_public_key_len =
		vport->auth.dh_pub_key_len;

	fc_req->u.dhchap_success.received_challenge_len = 0;

	return vport->auth.challenge_len + rsp_len + vport->auth.dh_pub_key_len;
}

int
lpfc_build_auth_neg(struct lpfc_vport *vport, uint8_t *message)
{
	uint8_t *message_start = message;
	uint8_t *params_start;
	uint32_t *params_len;
	uint32_t len;
	int i;

	/* Because some of the fields are not static in length
	 * and number we will pack on the fly.This will be expanded
	 * in the future to optionally offer DHCHAP or FCAP or both.
	 * The packing is done in Big Endian byte order DHCHAP_Reply.
	 *
	 * uint16_t nameTag;
	 * uint16_t nameLength;
	 * uint8_t  name[8];
	 * uint32_t available;		For now we will only offer one
					protocol ( DHCHAP ) for authentication.
	 * uint32_t potocolParamsLenId#1;
	 * uint32_t protocolId#1;	1 : DHCHAP. The protocol list is
	 *					in order of preference.
	 * uint16_t parameter#1Tag	1 : HashList
	 * uint16_t parameter#1Len	2 : Count of how many parameter values
	 *                                  follow in order of preference.
	 * uint16_t parameter#1value#1	5 : MD5 Hash Function
	 * uint16_t parameter#1value#2	6 : SHA-1 Hash Function
	 * uint16_t parameter#2Tag		2 : DHglDList
	 * uint16_t parameter#2Len		1 : Only One is supported now
	 * uint16_t parameter#2value#1	0 : NULL DH-CHAP Algorithm
	 * uint16_t parameter#2value#2 ...
	 * uint32_t protocolParamsLenId#2;
	 * uint32_t protocolId#2;         2 = FCAP
	 * uint16_t parameter#1Tag
	 * uint16_t parameter#1Len
	 * uint16_t parameter#1value#1
	 * uint16_t parameter#1value#2 ...
	 * uint16_t parameter#2Tag
	 * uint16_t parameter#2Len
	 * uint16_t parameter#2value#1
	 * uint16_t parameter#2value#2 ...
	 */


	/* Name Tag */
	*((uint16_t *)message) = cpu_to_be16(NAME_TAG);
	message += sizeof(uint16_t);

	/* Name Len */
	*((uint16_t *)message) = cpu_to_be16(NAME_LEN);
	message += sizeof(uint16_t);

	memcpy(message, vport->fc_portname.u.wwn, sizeof(uint64_t));

	message += sizeof(uint64_t);

	/* Protocols Available */
	*((uint32_t *)message) = cpu_to_be32(PROTS_NUM);
	message += sizeof(uint32_t);

	/* First Protocol Params Len */
	params_len = (uint32_t *)message;
	message += sizeof(uint32_t);

	/* Start of first Param */
	params_start = message;

	 /* Protocol Id */
	*((uint32_t *)message) = cpu_to_be32(FC_DHCHAP);
	message += sizeof(uint32_t);

	/* Hash List Tag */
	*((uint16_t *)message) = cpu_to_be16(HASH_LIST_TAG);
	message += sizeof(uint16_t);

	/* Hash Value Len */
	*((uint16_t *)message) = cpu_to_be16(vport->auth.hash_len);
	message += sizeof(uint16_t);

	/* Hash Value each 4 byte words */
	for (i = 0; i < vport->auth.hash_len; i++) {
		*((uint32_t *)message) =
			cpu_to_be32(vport->auth.hash_priority[i]);
		message += sizeof(uint32_t);
	}

	/* DHgIDList Tag */
	*((uint16_t *)message) = cpu_to_be16(DHGID_LIST_TAG);
	message += sizeof(uint16_t);

	/* DHgIDListValue Len */
	*((uint16_t *)message) = cpu_to_be16(vport->auth.dh_group_len);

	message += sizeof(uint16_t);

	/* DHgIDList each 4 byte words */

	for (i = 0; i < vport->auth.dh_group_len; i++) {
		*((uint32_t *)message) =
			cpu_to_be32(vport->auth.dh_group_priority[i]);
		message += sizeof(uint32_t);
	}

	*params_len = cpu_to_be32(message - params_start);

	len = (uint32_t)(message - message_start);

	return len;
}

int
lpfc_build_dhchap_challenge(struct lpfc_vport *vport, uint8_t *message,
			    struct fc_auth_rsp *fc_rsp)
{
	uint8_t *message_start = message;

	/* Because some of the fields are not static in length and number
	 * we will pack on the fly. The packing is done in Big Endian byte
	 * order DHCHAP_Challenge.
	 *
	 * uint16_t  nameTag;
	 * uint16_t  nameLength;
	 * uint8_t   name[8];
	 * uint32_t  Hash_Identifier;
	 * uint32_t  DH_Group_Identifier;
	 * uint32_t  Challenge_Value_Length;
	 * uint8_t   Challenge_Value[];
	 * uint32_t  DH_Value_Length;
	 * uint8_t   DH_Value[];
	 */

	/* Name Tag */
	*((uint16_t *)message) = cpu_to_be16(NAME_TAG);
	message += sizeof(uint16_t);

	/* Name Len */
	*((uint16_t *)message) = cpu_to_be16(NAME_LEN);
	message += sizeof(uint16_t);

	memcpy(message, vport->fc_portname.u.wwn, NAME_LEN);
	message += NAME_LEN;

	/* Hash Value each 4 byte words */
	*((uint32_t *)message) = cpu_to_be32(vport->auth.hash_id);
	message += sizeof(uint32_t);

	/* DH group id each 4 byte words */
	*((uint32_t *)message) = cpu_to_be32(vport->auth.group_id);
	message += sizeof(uint32_t);

	/* Challenge Length */
	*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.
		dhchap_challenge.our_challenge_len);
	message += sizeof(uint32_t);

	/* copy challenge to vport to save */
	kfree(vport->auth.challenge);
	vport->auth.challenge_len = fc_rsp->u.
		dhchap_challenge.our_challenge_len;
	vport->auth.challenge = kmalloc(vport->auth.challenge_len, GFP_KERNEL);

	if (!vport->auth.challenge)
		return 0;

	memcpy(vport->auth.challenge, fc_rsp->u.dhchap_challenge.data,
	       fc_rsp->u.dhchap_challenge.our_challenge_len);

	/* Challenge */
	memcpy(message, fc_rsp->u.dhchap_challenge.data,
	       fc_rsp->u.dhchap_challenge.our_challenge_len);
	message += fc_rsp->u.dhchap_challenge.our_challenge_len;

	/* Public Key length */
	*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.
		dhchap_challenge.our_public_key_len);
	message += sizeof(uint32_t);

	/* Public Key */
	memcpy(message, fc_rsp->u.dhchap_challenge.data +
	       fc_rsp->u.dhchap_challenge.our_challenge_len,
	       fc_rsp->u.dhchap_challenge.our_public_key_len);
	message += fc_rsp->u.dhchap_challenge.our_public_key_len;

	return (uint32_t)(message - message_start);

}

int
lpfc_build_dhchap_reply(struct lpfc_vport *vport, uint8_t *message,
				struct fc_auth_rsp *fc_rsp)

{
	uint8_t *message_start = message;

	/*
	 * Because some of the fields are not static in length and
	 * number we will pack on the fly. The packing is done in
	 * Big Endian byte order DHCHAP_Reply.
	 *
	 * uint32_t  ResonseLength;
	 * uint8_t   ResponseValue[];
	 * uint32_t  DHLength;
	 * uint8_t   DHValue[];          Our Public key
	 * uint32_t  ChallengeLength;    Used for bi-directional authentication
	 * uint8_t   ChallengeValue[];
	 *
	 * The combined key ( g^x mod p )^y mod p is used as the last
	 * hash of the password.
	 *
	 * g is the base 2 or 5.
	 * y is our private key.
	 * ( g^y mod p ) is our public key which we send.
	 * ( g^x mod p ) is their public key which we received.
	 */
	/* Response Value Length */
	*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.dhchap_reply.
		our_challenge_rsp_len);

	message += sizeof(uint32_t);
	/* Response Value */
	memcpy(message, fc_rsp->u.dhchap_reply.data,
		fc_rsp->u.dhchap_reply.our_challenge_rsp_len);

	message += fc_rsp->u.dhchap_reply.our_challenge_rsp_len;
	/* DH Value Length */
	*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.dhchap_reply.
			our_public_key_len);

	message += sizeof(uint32_t);
	/* DH Value */
	memcpy(message, fc_rsp->u.dhchap_reply.data +
				fc_rsp->u.dhchap_reply.our_challenge_rsp_len,
				fc_rsp->u.dhchap_reply.our_public_key_len);

	message += fc_rsp->u.dhchap_reply.our_public_key_len;

	if (vport->auth.bidirectional) {

		/* copy to vport to save */
		kfree(vport->auth.challenge);
		vport->auth.challenge_len = fc_rsp->u.dhchap_reply.
			our_challenge_len;
		vport->auth.challenge = kmalloc(vport->auth.challenge_len,
			GFP_KERNEL);
		if (!vport->auth.challenge)
			return 0;

		memcpy(vport->auth.challenge, fc_rsp->u.dhchap_reply.data +
		       fc_rsp->u.dhchap_reply.our_challenge_rsp_len +
		       fc_rsp->u.dhchap_reply.our_public_key_len,
		       fc_rsp->u.dhchap_reply.our_challenge_len);
		/* Challenge Value Length */
		*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.
			dhchap_reply.our_challenge_len);
		message += sizeof(uint32_t);
		/* Challenge Value */
		memcpy(message, fc_rsp->u.dhchap_reply.data +
			fc_rsp->u.dhchap_reply.our_challenge_rsp_len +
			fc_rsp->u.dhchap_reply.our_public_key_len,
			fc_rsp->u.dhchap_reply.our_challenge_len);

		message += fc_rsp->u.dhchap_reply.our_challenge_len;

	} else {
		*((uint32_t *)message) = 0;      /* Challenge Len for No
						bidirectional authentication */
		message += sizeof(uint32_t); /* Challenge Value Not Present */
	}

	return (uint32_t)(message - message_start);

}

int
lpfc_build_dhchap_success(struct lpfc_vport *vport, uint8_t *message,
			  struct fc_auth_rsp *fc_rsp)
{
	uint8_t *message_start = message;

	/*
	 * Because some of the fields are not static in length and number
	 * we will pack on the fly. The packing is done in Big Endian byte
	 * order DHCHAP_Success.
	 * uint32_t  responseValueLen;
	 * uint8_t   response[];.
	 */

	*((uint32_t *)message) = cpu_to_be32(fc_rsp->u.
			dhchap_success.response_len);
	message += sizeof(uint32_t);

	memcpy(message, fc_rsp->u.dhchap_success.data,
	       fc_rsp->u.dhchap_success.response_len);
	message += fc_rsp->u.dhchap_success.response_len;

	return (uint32_t)(message - message_start);
}

