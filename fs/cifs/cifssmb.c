/*
 *   fs/cifs/cifssmb.c
 *
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Contains the routines for constructing the SMB PDUs themselves
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

 /* All SMB/CIFS PDU handling routines go here - except for a few leftovers in connect.c */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"

static struct {
	int index;
	char *name;
} protocols[] = {
	{
	CIFS_PROT, "\2NT LM 0.12"}, {
	BAD_PROT, "\2"}
};

int
smb_init(int smb_command, int wct, const struct cifsTconInfo *tcon,
	 void **request_buf /* returned */ ,
	 void **response_buf /* returned */ )
{
	int rc = 0;

	*request_buf = buf_get();
	if (request_buf == 0) {
		return -ENOMEM;
	}
    /* Although the original thought was we needed the response buf for  */
    /* potential retries of smb operations it turns out we can determine */
    /* from the mid flags when the request buffer can be resent without  */
    /* having to use a second distinct buffer for the response */
	*response_buf = *request_buf; 

	header_assemble((struct smb_hdr *) *request_buf, smb_command, tcon,
			wct /*wct */ );
	return rc;
}

int
CIFSSMBNegotiate(unsigned int xid, struct cifsSesInfo *ses,
		 char cryptokey[CIFS_CRYPTO_KEY_SIZE])
{
	NEGOTIATE_REQ *pSMB;
	NEGOTIATE_RSP *pSMBr;
	int rc = 0;
	int bytes_returned;

	rc = smb_init(SMB_COM_NEGOTIATE, 0, 0 /* no tcon yet */ ,
		      (void **) &pSMB, (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->hdr.Flags2 |= SMBFLG2_UNICODE;
	if (extended_security)
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
    if (sign_CIFS_PDUs) {
        pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
    }

	pSMB->ByteCount = strlen(protocols[0].name) + 1;
	strncpy(pSMB->DialectsArray, protocols[0].name, 30);	/* null guaranteed to be at end of source and target buffers anyway */

	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc == 0) {
		ses->dialectIndex = le16_to_cpu(pSMBr->DialectIndex);
		ses->secMode = pSMBr->SecurityMode;	/* one byte - no need to convert this or EncryptionKeyLen field from le,  */
		ses->maxReq = le16_to_cpu(pSMBr->MaxMpxCount);
		/* probably no need to store and check maxvcs */
		ses->maxBuf =
		    min(le32_to_cpu(pSMBr->MaxBufferSize),
			(__u32) CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE);
		ses->maxRw = le32_to_cpu(pSMBr->MaxRawSize);	/* BB le_to_host needed around here and ff */
		cFYI(1, ("\nMax buf = %d ", ses->maxBuf));
		GETU32(ses->sessid) = le32_to_cpu(pSMBr->SessionKey);
		ses->capabilities = le32_to_cpu(pSMBr->Capabilities);
		ses->timeZone = le16_to_cpu(pSMBr->ServerTimeZone);	/* BB with UTC do we ever need to be using srvr timezone? */
		if (pSMBr->EncryptionKeyLength == CIFS_CRYPTO_KEY_SIZE) {
			memcpy(cryptokey, pSMBr->u.EncryptionKey,
			       CIFS_CRYPTO_KEY_SIZE);
		} else if ((pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC)
			   && (pSMBr->EncryptionKeyLength == 0)) {
			/* decode security blob */
		} else
			rc = -EIO;

		/* BB might be helpful to save off the domain of server here */

		if (pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC) {
			if (pSMBr->ByteCount < 16)
				rc = -EIO;
			else if (pSMBr->ByteCount == 16) {
				ses->secType = RawNTLMSSP;
				if (ses->server->socketUseCount.counter > 1) {
					if (memcmp
					    (ses->server->server_GUID,
					     pSMBr->u.extended_response.
					     GUID, 16) != 0) {
						cFYI(1,
						     ("UID of server does not match that of previous connection to same ip address"));
						memcpy(ses->server->
						       server_GUID,
						       pSMBr->u.
						       extended_response.
						       GUID, 16);
					}
				} else
					memcpy(ses->server->server_GUID,
					       pSMBr->u.extended_response.
					       GUID, 16);
			} else {
				rc = decode_negTokenInit(pSMBr->u.
							 extended_response.
							 SecurityBlob,
							 pSMBr->ByteCount -
							 16, &ses->secType);
			}

		} else
			ses->capabilities &= ~CAP_EXTENDED_SECURITY;
        if(sign_CIFS_PDUs == FALSE) {        
            if(ses->secMode & SECMODE_SIGN_REQUIRED)
                cERROR(1,("\nServer required CIFS packet signing - enable /proc/fs/cifs/PacketSigningEnabled"));
            ses->secMode &= ~(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED);
        }
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBTDis(const int xid, struct cifsTconInfo *tcon)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	int rc = 0;
	int length;

	cFYI(1, ("\nIn tree disconnect"));
	/*
	 *  If last user of the connection and
	 *  connection alive - disconnect it
	 *  If this is the last connection on the server session disconnect it
	 *  (and inside session disconnect we should check if tcp socket needs to 
	 *  be freed and kernel thread woken up).
	 */
	if (tcon)
		down(&tcon->tconSem);
	else
		return -EIO;

	atomic_dec(&tcon->useCount);
	if (atomic_read(&tcon->useCount) > 0) {
		up(&tcon->tconSem);
		return -EBUSY;
	}

/* BB remove (from server) list of shares - but with smp safety  BB */
/* BB is ses active - do we need to check here - but how? BB */

	rc = smb_init(SMB_COM_TREE_DISCONNECT, 0, tcon,
		      (void **) &smb_buffer, (void **) &smb_buffer_response);
	if (rc) {
		up(&tcon->tconSem);
		return rc;
	}
	rc = SendReceive(xid, tcon->ses, smb_buffer, smb_buffer_response,
			 &length, 0);
	if (rc)
		cFYI(1, (" Tree disconnect failed %d", rc));

	if (smb_buffer)
		buf_release(smb_buffer);
	up(&tcon->tconSem);
	return rc;
}

int
CIFSSMBLogoff(const int xid, struct cifsSesInfo *ses)
{
	struct smb_hdr *smb_buffer_response;
	LOGOFF_ANDX_REQ *pSMB;
	int rc = 0;
	int length;

	cFYI(1, ("\nIn SMBLogoff for session disconnect"));

	if (ses)
		down(&ses->sesSem);	/* need to add more places where this sem is checked */
	else
		return -EIO;

	atomic_dec(&ses->inUse);
	if (atomic_read(&ses->inUse) > 0) {
		up(&ses->sesSem);
		return -EBUSY;
	}
    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
	rc = smb_init(SMB_COM_LOGOFF_ANDX, 2, 0 /* no tcon anymore */ ,
		      (void **) &pSMB, (void **) &smb_buffer_response);
	if (rc) {
		up(&ses->sesSem);
		return rc;
	}

	pSMB->hdr.Uid = ses->Suid;

	pSMB->AndXCommand = 0xFF;
	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 smb_buffer_response, &length, 0);
	if (ses->server) {
		atomic_dec(&ses->server->socketUseCount);
		if (atomic_read(&ses->server->socketUseCount) == 0)
			ses->server->tcpStatus = CifsExiting;
	}
	if (pSMB)
		buf_release(pSMB);
	up(&ses->sesSem);
	return rc;
}

int
CIFSSMBDelFile(const int xid, const struct cifsTconInfo *tcon,
	       const char *fileName, const struct nls_table *nls_codepage)
{
	DELETE_FILE_REQ *pSMB = NULL;
	DELETE_FILE_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	rc = smb_init(SMB_COM_DELETE, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->fileName, fileName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fileName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM);
	pSMB->ByteCount = name_len + 1;
	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in RMFile = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBRmDir(const int xid, const struct cifsTconInfo *tcon,
	     const char *dirName, const struct nls_table *nls_codepage)
{
	DELETE_DIRECTORY_REQ *pSMB = NULL;
	DELETE_DIRECTORY_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn CIFSSMBRmDir"));

	rc = smb_init(SMB_COM_DELETE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifs_strtoUCS((wchar_t *) pSMB->DirName, dirName, 530
					 /* find define for this maxpathcomponent */
					 , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(dirName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, dirName, name_len);
	}

	pSMB->ByteCount = name_len + 1;
	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in RMDir = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBMkDir(const int xid, const struct cifsTconInfo *tcon,
	     const char *name, const struct nls_table *nls_codepage)
{
	int rc = 0;
	CREATE_DIRECTORY_REQ *pSMB = NULL;
	CREATE_DIRECTORY_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn CIFSSMBMkDir"));

	rc = smb_init(SMB_COM_CREATE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifs_strtoUCS((wchar_t *) pSMB->DirName, name, 530
					 /* find define for this maxpathcomponent */
					 , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(name, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, name, name_len);
	}

	pSMB->ByteCount = name_len + 1 /* for buf format */ ;
	pSMB->BufferFormat = 0x04;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in RMDir = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBOpen(const int xid, const struct cifsTconInfo *tcon,
	    const char *fileName, const int openDisposition,
	    const int access_flags, const int omode, __u16 * netfid,
	    int *pOplock, const struct nls_table *nls_codepage)
{
	int rc = -EACCES;
	OPEN_REQ *pSMB = NULL;
	OPEN_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;

	rc = smb_init(SMB_COM_NT_CREATE_ANDX, 24, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;	/* none */

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		pSMB->ByteCount = 1;	/* account for one byte pad to word boundary */
		name_len =
		    cifs_strtoUCS((wchar_t *) (pSMB->fileName + 1),
				  fileName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->NameLength = cpu_to_le16(name_len);
	} else {		/* BB improve the check for buffer overruns BB */

		pSMB->ByteCount = 0;	/* no pad */
		name_len = strnlen(fileName, 530);
		name_len++;	/* trailing null */
		pSMB->NameLength = cpu_to_le16(name_len);
		strncpy(pSMB->fileName, fileName, name_len);
	}
	if (*pOplock)
		pSMB->OpenFlags = cpu_to_le32(REQ_OPLOCK);
	pSMB->DesiredAccess = cpu_to_le32(access_flags);
	pSMB->AllocationSize = 0;
	pSMB->FileAttributes = ATTR_NORMAL;	/* XP does not handle ATTR_POSIX_SEMANTICS */
	/*if ((omode & S_IWUGO) == 0)
		pSMB->FileAttributes |= ATTR_READONLY;*/	
    /*  Above line causes problems due to problem with vfs splitting create into
        two pieces - need to set mode after file created not while it is being created */
	pSMB->FileAttributes = cpu_to_le32(pSMB->FileAttributes);
	pSMB->ShareAccess = cpu_to_le32(FILE_SHARE_ALL);
	pSMB->CreateDisposition = cpu_to_le32(openDisposition);
	pSMB->CreateOptions = cpu_to_le32(CREATE_NOT_DIR);	/* BB what are these? BB */
	pSMB->ImpersonationLevel = cpu_to_le32(SECURITY_IMPERSONATION);	/* BB ?? BB */
	pSMB->SecurityFlags =
	    cpu_to_le32(SECURITY_CONTEXT_TRACKING | SECURITY_EFFECTIVE_ONLY);

	pSMB->ByteCount += name_len;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;

	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in Open = %d\n", rc));
	} else {
		*pOplock = pSMBr->OplockLevel;	/* one byte no need to le_to_cpu */
		*netfid = pSMBr->Fid;	/* cifs fid stays in le */
		/* Do we care about the CreateAction in any cases? */

		/* BB add code to update inode with file sizes from create response */
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBRead(const int xid, const struct cifsTconInfo *tcon,
	    const int netfid, const unsigned int count,
	    const __u64 lseek, unsigned int *nbytes, char *buf)
{
	int rc = -EACCES;
	READ_REQ *pSMB = NULL;
	READ_RSP *pSMBr = NULL;
	char *pReadData;
	int bytes_returned;

	rc = smb_init(SMB_COM_READ_ANDX, 12, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(lseek & 0xFFFFFFFF);
	pSMB->OffsetHigh = cpu_to_le32(lseek >> 32);
	pSMB->Remaining = 0;
	pSMB->MaxCount = cpu_to_le16(min(count,
					 (tcon->ses->maxBuf -
					  MAX_CIFS_HDR_SIZE) & 0xFFFFFF00));
	pSMB->MaxCountHigh = 0;
	pSMB->ByteCount = 0;	/* no need to do le conversion since it is 0 */

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in read = %d\n", rc));
		*nbytes = 0;
	} else {
		pSMBr->DataLength = le16_to_cpu(pSMBr->DataLength);
		*nbytes = pSMBr->DataLength;
		/* BB check that DataLength would not go beyond end of SMB BB */
/* if(pSMBr->DataOffset < pSMBr->ByteCount + sizeof(READ_RSP) - 1 *//* BB fix this length check */
		if (pSMBr->DataLength > CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE) {
			rc = -EIO;
			*nbytes = 0;
		} else {
			pReadData =
			    (char *) (&pSMBr->hdr.Protocol) +
			    le16_to_cpu(pSMBr->DataOffset);
			copy_to_user(buf, pReadData, pSMBr->DataLength);
		}
	}

	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBWrite(const int xid, const struct cifsTconInfo *tcon,
	     const int netfid, const unsigned int count,
	     const __u64 offset, unsigned int *nbytes, const char *buf,
	     const int long_op)
{
	int rc = -EACCES;
	WRITE_REQ *pSMB = NULL;
	WRITE_RSP *pSMBr = NULL;
	int bytes_returned;

	rc = smb_init(SMB_COM_WRITE_ANDX, 14, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	pSMB->OffsetHigh = cpu_to_le32(offset >> 32);
	pSMB->Remaining = 0;
	if (count > ((tcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFF00))
		pSMB->DataLengthLow =
		    (tcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFF00;
	else
		pSMB->DataLengthLow = count;
	pSMB->DataLengthHigh = 0;
	pSMB->DataOffset =
	    cpu_to_le16((int) &(pSMB->Data) - (int) pSMB->hdr.Protocol);
	copy_from_user(pSMB->Data, buf, pSMB->DataLengthLow);

	pSMB->ByteCount += pSMB->DataLengthLow + 1 /* pad */ ;
	pSMB->DataLengthLow = cpu_to_le16(pSMB->DataLengthLow);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, long_op);
	if (rc) {
		cERROR(1, ("\nSend error in write = %d\n", rc));
		*nbytes = 0;
	} else
		*nbytes = le16_to_cpu(pSMBr->Count);

	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBLock(const int xid, const struct cifsTconInfo *tcon,
	    const __u16 smb_file_id, const __u64 len,
	    const __u64 offset, const __u32 numUnlock,
	    const __u32 numLock, const __u8 lockType, const int waitFlag)
{
	int rc = 0;
	LOCK_REQ *pSMB = NULL;
	LOCK_RSP *pSMBr = NULL;
	int bytes_returned;

	cFYI(1, ("\nIn CIFSSMBLock"));

	rc = smb_init(SMB_COM_LOCKING_ANDX, 8, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->NumberOfLocks = cpu_to_le32(numLock);
	pSMB->NumberOfUnlocks = cpu_to_le32(numUnlock);
	pSMB->LockType = lockType;
	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = smb_file_id; /* netfid stays le */

	pSMB->Locks[0].Pid = cpu_to_le16(current->pid);
	pSMB->Locks[0].Length = cpu_to_le64(len);
	pSMB->Locks[0].Offset = cpu_to_le64(offset);
	pSMB->ByteCount = sizeof (LOCKING_ANDX_RANGE);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);

	if (rc) {
		cERROR(1, ("\nSend error in Lock = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBClose(const int xid, const struct cifsTconInfo *tcon, int smb_file_id)
{
	int rc = 0;
	CLOSE_REQ *pSMB = NULL;
	CLOSE_RSP *pSMBr = NULL;
	int bytes_returned;
	cFYI(1, ("\nIn CIFSSMBClose"));

	rc = smb_init(SMB_COM_CLOSE, 3, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->FileID = (__u16) smb_file_id;
	pSMB->LastWriteTime = 0;
	pSMB->ByteCount = 0;
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in Close = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBRename(const int xid, const struct cifsTconInfo *tcon,
	      const char *fromName, const char *toName,
	      const struct nls_table *nls_codepage)
{
	int rc = 0;
	RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;

	cFYI(1, ("\nIn CIFSSMBRename"));

	rc = smb_init(SMB_COM_RENAME, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->BufferFormat = 0x04;
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->OldFileName, fromName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0;	/* pad */
		pSMB->OldFileName[name_len + 1] = 0x04;	/* strange that protocol requires an ASCII signature byte on Unicode string */
		name_len2 =
		    cifs_strtoUCS((wchar_t *) & pSMB->
				  OldFileName[name_len + 2], toName, 530,
				  nls_codepage);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fromName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, 530);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;	/* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	pSMB->ByteCount = 1 /* 1st signature byte */  + name_len + name_len2;	/* we could also set search attributes but not needed */
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in RMDir = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSUnixCreateSymLink(const int xid, const struct cifsTconInfo *tcon,
		      const char *fromName, const char *toName,
		      const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn Symlink Unix style"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, fromName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fromName, name_len);
	}
	pSMB->ParameterCount = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->InformationLevel) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;

	data_offset = (char *) (&pSMB->hdr.Protocol) + pSMB->DataOffset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifs_strtoUCS((wchar_t *) data_offset, toName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(toName, 530);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, toName, name_len_target);
	}

	pSMB->DataCount = name_len_target;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;
	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1,
		     ("\nSend error in SetPathInfo (create symlink) = %d\n",
		      rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSUnixCreateHardLink(const int xid, const struct cifsTconInfo *tcon,
		       const char *fromName, const char *toName,
		       const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn Create Hard link Unix style"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifs_strtoUCS((wchar_t *) pSMB->FileName, toName, 530
					 /* find define for this maxpathcomponent */
					 , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {		/* BB improve the check for buffer overruns BB */
		name_len = strnlen(toName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, toName, name_len);
	}
	pSMB->ParameterCount = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->InformationLevel) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;

	data_offset = (char *) (&pSMB->hdr.Protocol) + pSMB->DataOffset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifs_strtoUCS((wchar_t *) data_offset, fromName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {		/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(fromName, 530);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, fromName, name_len_target);
	}

	pSMB->DataCount = name_len_target;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_HLINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in SetPathInfo (hard link) = %d\n", rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSCreateHardLink(const int xid, const struct cifsTconInfo *tcon,
		   const char *fromName, const char *toName,
		   const struct nls_table *nls_codepage)
{
	int rc = 0;
	NT_RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;

	cFYI(1, ("\nIn CIFSCreateHardLink"));

	rc = smb_init(SMB_COM_NT_RENAME, 4, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->Flags = cpu_to_le16(CREATE_HARD_LINK);
	pSMB->ClusterCount = 0;

	pSMB->BufferFormat = 0x04;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->OldFileName, fromName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0;	/* pad */
		pSMB->OldFileName[name_len + 1] = 0x04;	/* strange that protocol requires an ASCII signature byte on Unicode string */
		name_len2 =
		    cifs_strtoUCS((wchar_t *) & pSMB->
				  OldFileName[name_len + 2], toName, 530,
				  nls_codepage);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fromName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, 530);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;	/* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	pSMB->ByteCount = 1 /* string type byte */  + name_len + name_len2;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in hard link (NT rename) = %d\n", rc));
	}
	if (pSMB)
		buf_release(pSMB);

	return rc;
}

int
CIFSSMBUnixQuerySymLink(const int xid, const struct cifsTconInfo *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen,
			const struct nls_table *nls_codepage)
{
/* SMB_QUERY_FILE_UNIX_LINK */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn QPathSymLinkInfo (Unix) the path %s", searchName));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	pSMB->TotalParameterCount =
	    2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &(pSMB->InformationLevel) -
			(int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in QuerySymLinkInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		pSMBr->DataCount = le16_to_cpu(pSMBr->DataCount);
		if ((pSMBr->ByteCount < 2) || (pSMBr->DataOffset > 512))	/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
				name_len = UniStrnlen((wchar_t *) ((char *)
								   &pSMBr->hdr.
								   Protocol +
								   pSMBr->
								   DataOffset),
						      min(buflen,
							  (int) pSMBr->
							  DataCount) / 2);
				cifs_strfromUCS_le(symlinkinfo,
						   (wchar_t *) ((char *)
								&pSMBr->
								hdr.
								Protocol +
								pSMBr->
								DataOffset),
						   name_len, nls_codepage);
			} else {
				strncpy(symlinkinfo,
					(char *) &pSMBr->hdr.Protocol +
					pSMBr->DataOffset, min(buflen, (int)
							       pSMBr->
							       DataCount));
			}
			symlinkinfo[buflen] = 0;	/* just in case so the calling code does not go off the end of the buffer */
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBQPathInfo(const int xid, const struct cifsTconInfo *tcon,
		 const unsigned char *searchName,
		 FILE_ALL_INFO * pFindData,
		 const struct nls_table *nls_codepage)
{
/* level 263 SMB_QUERY_FILE_ALL_INFO */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn QPathInfo path %s", searchName));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	pSMB->TotalParameterCount = 2 /* level */  + 4 /* reserved */  +
	    name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16((int) &(pSMB->InformationLevel)
					    - (int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_ALL_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in QPathInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		if ((pSMBr->ByteCount < 40) || (pSMBr->DataOffset > 512))	/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       pSMBr->DataOffset, sizeof (FILE_ALL_INFO));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBUnixQPathInfo(const int xid, const struct cifsTconInfo *tcon,
		     const unsigned char *searchName,
		     FILE_UNIX_BASIC_INFO * pFindData,
		     const struct nls_table *nls_codepage)
{
/* SMB_QUERY_FILE_UNIX_BASIC */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn QPathInfo (Unix) the path %s", searchName));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	pSMB->TotalParameterCount = 2 /* level */  + 4 /* reserved */  +
	    name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &(pSMB->InformationLevel) -
			(int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in QPathInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		if ((pSMBr->ByteCount < 40) || (pSMBr->DataOffset > 512))	/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       pSMBr->DataOffset,
			       sizeof (FILE_UNIX_BASIC_INFO));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSFindSingle(const int xid, const struct cifsTconInfo *tcon,
	       const char *searchName, FILE_ALL_INFO * findData,
	       const struct nls_table *nls_codepage)
{
/* level 257 SMB_ */
	TRANSACTION2_FFIRST_REQ *pSMB = NULL;
	TRANSACTION2_FFIRST_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn FindUnique"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	pSMB->TotalParameterCount = 12 + name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &pSMB->InformationLevel -
			(int) &pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;	/* one byte, no need to le convert */
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_FIRST);
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalDataCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->SearchCount = cpu_to_le16(16);	/* BB increase */
	pSMB->SearchFlags = cpu_to_le16(1);
	pSMB->InformationLevel = cpu_to_le16(SMB_FIND_FILE_DIRECTORY_INFO);
	pSMB->SearchStorageType = 0;	/* BB what should we set this to? BB */
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);

	if (rc) {
		cFYI(1, ("\nSend error in FindFileDirInfo = %d\n", rc));
	} else {		/* decode response */

		/* BB fill in */
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSFindFirst(const int xid, const struct cifsTconInfo *tcon,
	      const char *searchName, FILE_DIRECTORY_INFO * findData,
	      T2_FFIRST_RSP_PARMS * findParms,
	      const struct nls_table *nls_codepage, int *pUnicodeFlag,
	      int *pUnixFlag)
{
/* level 257 SMB_ */
	TRANSACTION2_FFIRST_REQ *pSMB = NULL;
	TRANSACTION2_FFIRST_RSP *pSMBr = NULL;
	char *response_data;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, ("\nIn FindFirst"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	pSMB->TotalParameterCount = 12 + name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(10);
	pSMB->MaxDataCount = cpu_to_le16((tcon->ses->maxBuf -
					  MAX_CIFS_HDR_SIZE) & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &pSMB->SearchAttributes -
			(int) &pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;	/* one byte no need to make endian neutral */
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_FIRST);
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->SearchCount = cpu_to_le16(CIFS_MAX_MSGSIZE / sizeof (FILE_DIRECTORY_INFO));	/* should this be shrunk even more ? */
	pSMB->SearchFlags = cpu_to_le16(CIFS_SEARCH_CLOSE_AT_END);

	/* test for Unix extensions */
	if (tcon->ses->capabilities & CAP_UNIX) {
		pSMB->InformationLevel = cpu_to_le16(SMB_FIND_FILE_UNIX);
		*pUnixFlag = TRUE;
	} else {
		pSMB->InformationLevel =
		    cpu_to_le16(SMB_FIND_FILE_DIRECTORY_INFO);
		*pUnixFlag = FALSE;
	}
	pSMB->SearchStorageType = 0;	/* BB what should we set this to? It is not clear if it matters BB */
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);

	if (rc) {		/* BB add logic to retry regular search if Unix search rejected unexpectedly by server */
		cFYI(1, ("\nError in FindFirst = %d\n", rc));
	} else {		/* decode response */
		/* BB add safety checks for these memcpys */
		if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
			*pUnicodeFlag = TRUE;
		else
			*pUnicodeFlag = FALSE;
		memcpy(findParms,
		       (char *) &pSMBr->hdr.Protocol +
		       le16_to_cpu(pSMBr->ParameterOffset),
		       sizeof (T2_FFIRST_RSP_PARMS));
		/* search handle can stay LE and EAoffset not needed so not converted */
		findParms->EndofSearch = le16_to_cpu(findParms->EndofSearch);
		findParms->LastNameOffset =
		    le16_to_cpu(findParms->LastNameOffset);
		findParms->SearchCount = le16_to_cpu(findParms->SearchCount);
		response_data =
		    (char *) &pSMBr->hdr.Protocol +
		    le16_to_cpu(pSMBr->DataOffset);
		memcpy(findData, response_data, le16_to_cpu(pSMBr->DataCount));
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSFindNext(const int xid, const struct cifsTconInfo *tcon,
	     FILE_DIRECTORY_INFO * findData,
	     T2_FNEXT_RSP_PARMS * findParms, const __u16 searchHandle,
	     __u32 resumeKey, int *pUnicodeFlag, int *pUnixFlag)
{
/* level 257 SMB_ */
	TRANSACTION2_FNEXT_REQ *pSMB = NULL;
	TRANSACTION2_FNEXT_RSP *pSMBr = NULL;
	char *response_data;
	int rc = 0;
	int bytes_returned;

	cFYI(1, ("\nIn FindNext"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 14;	/* includes 2 bytes of null string, converted to LE below */
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(8);
	pSMB->MaxDataCount =
	    cpu_to_le16((tcon->ses->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &pSMB->SearchHandle - (int) &pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_NEXT);
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->SearchHandle = searchHandle;	/* always kept as le */
	findParms->SearchCount = 0;	/* set to zero in case of error */
	pSMB->SearchCount =
	    cpu_to_le16(CIFS_MAX_MSGSIZE / sizeof (FILE_DIRECTORY_INFO));
	/* test for Unix extensions */
	if (tcon->ses->capabilities & CAP_UNIX) {
		pSMB->InformationLevel = cpu_to_le16(SMB_FIND_FILE_UNIX);
		*pUnixFlag = TRUE;
	} else {
		pSMB->InformationLevel =
		    cpu_to_le16(SMB_FIND_FILE_DIRECTORY_INFO);
		*pUnixFlag = FALSE;
	}
	pSMB->ResumeKey = resumeKey;	/* always kept as le */
	pSMB->SearchFlags =
	    cpu_to_le16(CIFS_SEARCH_CLOSE_AT_END |
			CIFS_SEARCH_CONTINUE_FROM_LAST);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);

	if (rc) {
		if (rc == -EBADF)
			rc = 0;	/* search probably was closed at end of search above */
		else
			cFYI(1, ("\nFindNext returned = %d\n", rc));
	} else {		/* decode response */
		/* BB add safety checks for these memcpys */
		if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
			*pUnicodeFlag = TRUE;
		else
			*pUnicodeFlag = FALSE;
		memcpy(findParms,
		       (char *) &pSMBr->hdr.Protocol +
		       le16_to_cpu(pSMBr->ParameterOffset),
		       sizeof (T2_FNEXT_RSP_PARMS));
		findParms->EndofSearch = le16_to_cpu(findParms->EndofSearch);
		findParms->LastNameOffset =
		    le16_to_cpu(findParms->LastNameOffset);
		findParms->SearchCount = le16_to_cpu(findParms->SearchCount);
		response_data =
		    (char *) &pSMBr->hdr.Protocol +
		    le16_to_cpu(pSMBr->DataOffset);
		memcpy(findData, response_data, le16_to_cpu(pSMBr->DataCount));
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSGetDFSRefer(const int xid, struct cifsSesInfo *ses,
		const unsigned char *searchName,
		unsigned char **targetUNCs,
		int *number_of_UNC_in_array,
		const struct nls_table *nls_codepage)
{
/* TRANS2_GET_DFS_REFERRAL */
	TRANSACTION2_GET_DFS_REFER_REQ *pSMB = NULL;
	TRANSACTION2_GET_DFS_REFER_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	*number_of_UNC_in_array = 0;
	*targetUNCs = NULL;

	cFYI(1, ("\nIn GetDFSRefer the path %s", searchName));
	if (ses == NULL)
		return -ENODEV;

	rc = smb_init(SMB_COM_TRANSACTION2, 15, 0, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->hdr.Tid = ses->ipc_tid;
	pSMB->hdr.Uid = ses->Suid;
	if (ses->capabilities & CAP_STATUS32) {
		pSMB->hdr.Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		pSMB->hdr.Flags2 |= SMBFLG2_DFS;
	}
    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
       pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_UNICODE) {
		pSMB->hdr.Flags2 |= SMBFLG2_UNICODE;
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->RequestFileName,
				  searchName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(searchName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->RequestFileName, searchName, name_len);
	}

	pSMB->ParameterCount = 2 /* level */  + name_len /*includes null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = 0;
	pSMB->MaxDataCount = cpu_to_le16(4000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &(pSMB->MaxReferralLevel) -
			(int) pSMB->hdr.Protocol);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_GET_DFS_REFERRAL);
	pSMB->ByteCount = pSMB->ParameterCount + 3 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->MaxReferralLevel = cpu_to_le16(3);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSend error in GetDFSRefer = %d\n", rc));
	} else {		/* decode response */
/* BB Add logic to parse referrals here */
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBQFSInfo(const int xid, const struct cifsTconInfo *tcon,
	       struct statfs *FSData, const struct nls_table *nls_codepage)
{
/* level 0x103 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn QFSInfo"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16((int) &(pSMB->InformationLevel)
					    - (int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_SIZE_INFO);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in QFSInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		cFYI(1,
		     ("\nDecoding qfsinfo response.  BCC: %d  Offset %d\n",
		      pSMBr->ByteCount, pSMBr->DataOffset));
		if ((pSMBr->ByteCount < 24) || (pSMBr->DataOffset > 512))	/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			response_data =
			    (FILE_SYSTEM_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 pSMBr->DataOffset);
			FSData->f_bsize =
			    le32_to_cpu(response_data->BytesPerSector) *
			    le32_to_cpu(response_data->
					SectorsPerAllocationUnit);
			FSData->f_blocks =
			    le64_to_cpu(response_data->TotalAllocationUnits);
			FSData->f_bfree = FSData->f_bavail =
			    le64_to_cpu(response_data->FreeAllocationUnits);
			cFYI(1,
			     ("\nBlocks: %ld  Free: %ld Block size %ld\n",
			      FSData->f_blocks, FSData->f_bfree,
			      FSData->f_bsize));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBQFSAttributeInfo(int xid, struct cifsTconInfo *tcon,
			const struct nls_table *nls_codepage)
{
/* level 0x105  SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_ATTRIBUTE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn QFSAttributeInfo"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16((int) &(pSMB->InformationLevel)
					    - (int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_ATTRIBUTE_INFO);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in QFSAttributeInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		if ((pSMBr->ByteCount < 13) || (pSMBr->DataOffset > 512)) {	/* BB also check enough bytes returned */
			rc = -EIO;	/* bad smb */
		} else {
			response_data =
			    (FILE_SYSTEM_ATTRIBUTE_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 pSMBr->DataOffset);
			memcpy(&tcon->fsAttrInfo, response_data,
			       sizeof (FILE_SYSTEM_ATTRIBUTE_INFO));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBQFSDeviceInfo(int xid, struct cifsTconInfo *tcon,
		     const struct nls_table *nls_codepage)
{
/* level 0x104 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_DEVICE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn QFSDeviceInfo"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ByteCount = pSMB->TotalParameterCount + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(pSMB->TotalParameterCount);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &(pSMB->InformationLevel) -
			(int) pSMB->hdr.Protocol);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_DEVICE_INFO);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in QFSDeviceInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = le16_to_cpu(pSMBr->DataOffset);
		if ((pSMBr->ByteCount < sizeof (FILE_SYSTEM_DEVICE_INFO)) || (pSMBr->DataOffset > 512))	/* BB also check enough bytes returned */
			rc = -EIO;	/* bad smb */
		else {
			response_data =
			    (FILE_SYSTEM_DEVICE_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 pSMBr->DataOffset);
			memcpy(&tcon->fsDevInfo, response_data,
			       sizeof (FILE_SYSTEM_DEVICE_INFO));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBQFSUnixInfo(int xid, struct cifsTconInfo *tcon,
		   const struct nls_table *nls_codepage)
{
/* level 0x200  SMB_QUERY_CIFS_UNIX_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_UNIX_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn QFSUnixInfo"));
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->ParameterCount = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(100);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ByteCount = pSMB->ParameterCount + 1 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset =
	    cpu_to_le16((int) &(pSMB->InformationLevel) -
			(int) pSMB->hdr.Protocol);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_CIFS_UNIX_INFO);
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, ("\nSend error in QFSUnixInfo = %d\n", rc));
	} else {		/* decode response */
		pSMBr->DataOffset = cpu_to_le16(pSMBr->DataOffset);
		if ((pSMBr->ByteCount < 13) || (pSMBr->DataOffset > 512)) {	/* BB also check enough bytes returned */
			rc = -EIO;	/* bad smb */
		} else {
			response_data =
			    (FILE_SYSTEM_UNIX_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 pSMBr->DataOffset);
			memcpy(&tcon->fsUnixInfo, response_data,
			       sizeof (FILE_SYSTEM_UNIX_INFO));
		}
	}
	if (pSMB)
		buf_release(pSMB);
	return rc;
}

/* We can not use write of zero bytes trick to 
   set file size due to need for large file support.  Also note that 
   this SetPathInfo is preferred to SetFileInfo based method in next 
   routine which is only needed to work around a sharing violation bug
   in Samba which this routine can run into */

int
CIFSSMBSetEOF(int xid, struct cifsTconInfo *tcon, char *fileName,
	      __u64 size, int SetAllocation, const struct nls_table *nls_codepage)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	struct file_end_of_file_info *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;

	cFYI(1, ("\nIn SetEOF"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, fileName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fileName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}
	pSMB->ParameterCount = 6 + name_len;
	pSMB->DataCount = sizeof (struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->InformationLevel) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;
    if(SetAllocation) {
        if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
            pSMB->InformationLevel =
                cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
        else
            pSMB->InformationLevel =
                cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
    } else /* Set File Size */  {    
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
    }

	parm_data =
	    (struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol) +
				       pSMB->DataOffset);
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;
	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	parm_data->FileSize = cpu_to_le64(size);
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSetPathInfo (file size) returned %d\n", rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBSetFileSize(const int xid, struct cifsTconInfo *tcon, __u64 size, 
                   __u16 fid, __u32 pid_of_opener, int SetAllocation)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	char *data_offset;
    struct file_end_of_file_info *parm_data;
	int rc = 0;
	int bytes_returned = 0;
    __u32 tmp;

	cFYI(1, ("\nSetFileSize (via SetFileInfo)"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	tmp = cpu_to_le32(pid_of_opener);  /* override pid of current process
                                         so network fid will be valid */
	pSMB->hdr.Pid = tmp & 0xFFFF;
	tmp >>= 16;
	pSMB->hdr.PidHigh = tmp & 0xFFFF;
    
	pSMB->ParameterCount = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->Fid) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;

	data_offset = (char *) (&pSMB->hdr.Protocol) + pSMB->DataOffset;	

	pSMB->DataCount = sizeof(struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;
	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	parm_data =
	    (struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol) +
				       pSMB->DataOffset);
    parm_data->FileSize = size;
    pSMB->Fid = fid;
    if(SetAllocation) {
        if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
            pSMB->InformationLevel =
                cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
        else
            pSMB->InformationLevel =
                cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
    } else /* Set File Size */  {    
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
		        cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
    }
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1,
		     ("\nSend error in SetFileInfo (SetFileSize) = %d\n",
		      rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBSetTimes(int xid, struct cifsTconInfo *tcon, char *fileName,
		FILE_BASIC_INFO * data, const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	char *data_offset;

	cFYI(1, ("\nIn SetTimes"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, fileName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fileName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	pSMB->ParameterCount = 6 + name_len;
	pSMB->DataCount = sizeof (FILE_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->InformationLevel) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;
	data_offset = (char *) (&pSMB->hdr.Protocol) + pSMB->DataOffset;
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;

	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO2);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	memcpy(data_offset, data, sizeof (FILE_BASIC_INFO));
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSetPathInfo (times) returned %d\n", rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}

int
CIFSSMBUnixSetPerms(const int xid, struct cifsTconInfo *tcon,
		    char *fileName, __u64 mode, __u64 uid, __u64 gid,
		    const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	FILE_UNIX_BASIC_INFO *data_offset;

	cFYI(1, ("\nIn SetUID/GID/Mode"));

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUCS((wchar_t *) pSMB->FileName, fileName, 530
				  /* find define for this maxpathcomponent */
				  , nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve the check for buffer overruns BB */

		name_len = strnlen(fileName, 530);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	pSMB->ParameterCount = 6 + name_len;
	pSMB->DataCount = sizeof (FILE_UNIX_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =
	    (int) &(pSMB->InformationLevel) - (int) pSMB->hdr.Protocol;
	pSMB->DataOffset = pSMB->ParameterOffset + pSMB->ParameterCount;
	data_offset =
	    (FILE_UNIX_BASIC_INFO *) ((char *) &pSMB->hdr.Protocol +
				      pSMB->DataOffset);
	pSMB->DataOffset = cpu_to_le16(pSMB->DataOffset);
	pSMB->ParameterOffset = cpu_to_le16(pSMB->ParameterOffset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->ByteCount = 3 /* pad */  + pSMB->ParameterCount + pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(pSMB->ParameterCount);
	pSMB->DataCount = cpu_to_le16(pSMB->DataCount);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	pSMB->hdr.smb_buf_length += pSMB->ByteCount;
	data_offset->Uid = cpu_to_le64(uid);
	cFYI(1, ("\nUid = %lld from %lld ", data_offset->Uid, uid));
	data_offset->Gid = cpu_to_le64(gid);
	cFYI(1, ("\nGid = %lld from %lld ", data_offset->Gid, gid));
	data_offset->Permissions = cpu_to_le64(mode);
	pSMB->ByteCount = cpu_to_le16(pSMB->ByteCount);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, ("\nSetPathInfo (perms) returned %d\n", rc));
	}

	if (pSMB)
		buf_release(pSMB);
	return rc;
}
