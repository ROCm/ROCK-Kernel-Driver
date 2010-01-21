/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2006-2007 Emulex.  All rights reserved.           *
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

#define N_DH_GROUP            4
#define ELS_CMD_AUTH_BYTE     0x90

#define AUTH_REJECT           0xA
#define AUTH_NEGOTIATE        0xB
#define AUTH_DONE             0xC

#define DHCHAP_CHALLENGE 0x10
#define DHCHAP_REPLY     0x11
#define DHCHAP_SUCCESS   0x12

#define FCAP_REQUEST	0x13
#define FCAP_ACK        0x14
#define FCAP_CONFIRM    0x15

#define PROTS_NUM	0x01

#define NAME_TAG	0x01
#define NAME_LEN	0x08

#define HASH_LIST_TAG   0x01

#define DHGID_LIST_TAG  0x02

#define HBA_SECURITY       0x20

#define AUTH_ERR                 0x1
#define LOGIC_ERR                0x2

#define BAD_DHGROUP              0x2
#define BAD_ALGORITHM            0x3
#define AUTHENTICATION_FAILED    0x5
#define BAD_PAYLOAD              0x6
#define BAD_PROTOCOL             0x7
#define RESTART		         0x8

#define AUTH_VERSION	0x1

#define MAX_AUTH_MESSAGE_SIZE 1024

struct lpfc_auth_reject {
   uint8_t reason;
   uint8_t explanation;
   uint8_t reserved[2];
}  __attribute__ ((packed));

struct lpfc_auth_message {		/* Structure is in Big Endian format */
	uint8_t command_code;
	uint8_t flags;
	uint8_t message_code;
	uint8_t protocol_ver;
	uint32_t message_len;
	uint32_t trans_id;
	uint8_t data[0];
}  __attribute__ ((packed));

int lpfc_build_auth_neg(struct lpfc_vport *vport, uint8_t *message);
int lpfc_build_dhchap_challenge(struct lpfc_vport *vport, uint8_t *message,
				struct fc_auth_rsp *fc_rsp);
int lpfc_build_dhchap_reply(struct lpfc_vport *vport, uint8_t *message,
			    struct fc_auth_rsp *fc_rsp);
int lpfc_build_dhchap_success(struct lpfc_vport *vport, uint8_t *message,
			      struct fc_auth_rsp *fc_rsp);

int lpfc_unpack_auth_negotiate(struct lpfc_vport *vport, uint8_t *message,
				 uint8_t *reason, uint8_t *explanation);
int lpfc_unpack_dhchap_challenge(struct lpfc_vport *vport, uint8_t *message,
				 uint8_t *reason, uint8_t *explanation);
int lpfc_unpack_dhchap_reply(struct lpfc_vport *vport, uint8_t *message,
			     struct fc_auth_req *fc_req);
int lpfc_unpack_dhchap_success(struct lpfc_vport *vport, uint8_t *message,
			       struct fc_auth_req *fc_req);
