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
#include "md5.h"

/* Calculate and return the CIFS signature based on the mac key and the smb pdu */
/* the 16 byte signature must be allocated by the caller  */
/* Note we only use the 1st eight bytes */
/* Note that the smb header signature field on input contains the  
	sequence number before this function is called */
	
static int cifs_calculate_signature(const struct smb_hdr * cifs_pdu, const char * key, char * signature)
{
	struct	MD5Context context;

	if((cifs_pdu == NULL) || (signature == NULL))
		return -EINVAL;

	MD5Init(&context);
	MD5Update(&context,key,CIFS_SESSION_KEY_SIZE);
	MD5Update(&context,cifs_pdu->Protocol,cifs_pdu->smb_buf_length);
	MD5Final(signature,&context);
	cifs_dump_mem("signature: ",signature,16); /* BB remove BB */
	return 0;
}

int cifs_sign_smb(struct smb_hdr * cifs_pdu, struct cifsSesInfo * ses,
	__u32 * pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];

	/* BB remember to initialize sequence number elsewhere and initialize mac_signing key elsewhere BB */
	/* BB remember to add code to save expected sequence number in midQ entry BB */

	if((cifs_pdu == NULL) || (ses == NULL))
		return -EINVAL;

	if((le32_to_cpu(cifs_pdu->Flags2) & SMBFLG2_SECURITY_SIGNATURE) == 0) 
		return rc;

	write_lock(&GlobalMid_Lock);
	cifs_pdu->Signature.Sequence.SequenceNumber = ses->sequence_number;
	cifs_pdu->Signature.Sequence.Reserved = 0;
	
	*pexpected_response_sequence_number = ses->sequence_number++;
	ses->sequence_number++;
	write_unlock(&GlobalMid_Lock);

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
	
	/* Do not need to verify session setups with signature "BSRSPYL "  */
	if(memcmp(cifs_pdu->Signature.SecuritySignature,"BSRSPYL ",8)==0)
		cFYI(1,("dummy signature received for smb command 0x%x",cifs_pdu->Command));

	return rc;
} 
