/*
 *   fs/cifs/cifsencrypt.c
 *
 *   Copyright (c) International Business Machines  Corp., 2003
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include "cifspdu.h"
#include "cifsglob.h" 
#include "cifs_debug.h"

/* Calculate and return the CIFS signature based on the mac key and the smb pdu */
/* the eight byte signature must be allocated by the caller. */
/* Note that the smb header signature field on input contains the  
	sequence number before this function is called */
	
static int cifs_calculate_signature(const struct smb_hdr * cifs_pdu, const char * mac_key, char * signature)
{

	if((cifs_pdu == NULL) || (signature == NULL))
		return -EINVAL;

	/* MD5(mac_key, text) */
	/* return 1st eight bytes in signature */

	return 0;
}

int cifs_sign_smb(struct smb_hdr * cifs_pdu, struct cifsSesInfo * ses)
{
	int rc = 0;
	char smb_signature[8];

	/* BB remember to initialize sequence number elsewhere and initialize mac_signing key elsewhere BB */
	/* BB remember to add code to save expected sequence number in midQ entry BB */

	if((cifs_pdu == NULL) || (ses == NULL))
		return -EINVAL;

	if((le32_to_cpu(cifs_pdu->Flags2) & SMBFLG2_SECURITY_SIGNATURE) == 0) 
		return rc;

	cifs_pdu->Signature.Sequence.SequenceNumber = ses->sequence_number;
	cifs_pdu->Signature.Sequence.Reserved = 0;
	rc = cifs_calculate_signature(cifs_pdu, ses->mac_signing_key,smb_signature);
	if(rc)
                memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

int cifs_verify_signature(const struct smb_hdr * cifs_pdu, const char * mac_key,
	__u32 expected_sequence_number)
{
	unsigned int rc = 0;

        if((cifs_pdu == NULL) || (mac_key == NULL))
                return -EINVAL;
	
	/* BB no need to verify negprot or if flag is not on for session (or for frame?? */
	/* BB what if signatures are supposed to be on for session but server does not
		send one? BB */
	/* BB also do not verify oplock breaks for signature */

	return rc;
} 
