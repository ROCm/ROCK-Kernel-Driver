/*
 *   fs/cifs/transport.c
 *
 *   Copyright (c) International Business Machines  Corp., 2002
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
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

extern kmem_cache_t *cifs_mid_cachep;
extern kmem_cache_t *cifs_oplock_cachep;

struct mid_q_entry *
AllocMidQEntry(struct smb_hdr *smb_buffer, struct cifsSesInfo *ses)
{
	struct mid_q_entry *temp;
	int timeout = 10 * HZ;

/* BB add spinlock to protect midq for each session BB */
	if (ses == NULL) {
		cERROR(1, ("Null session passed in to AllocMidQEntry "));
		return NULL;
	}
	temp = (struct mid_q_entry *) kmem_cache_alloc(cifs_mid_cachep,
						       SLAB_KERNEL);
	if (temp == NULL)
		return temp;
	else {
		memset(temp, 0, sizeof (struct mid_q_entry));
		temp->mid = smb_buffer->Mid;	/* always LE */
		temp->pid = current->pid;
		temp->command = smb_buffer->Command;
		cFYI(1, ("For smb_command %d", temp->command));
		do_gettimeofday(&temp->when_sent);
		temp->ses = ses;
		temp->tsk = current;
	}

	while ((ses->server->tcpStatus != CifsGood) && (timeout > 0)){ 
		/* Give the tcp thread up to 10 seconds to reconnect */
		/* Should we wake up tcp thread first? BB  */
		timeout = wait_event_interruptible_timeout(ses->server->response_q,
			(ses->server->tcpStatus == CifsGood), timeout);
        cFYI(1,("timeout (after reconnection wait) %d",timeout));
	}

	if (ses->server->tcpStatus == CifsGood) {
		write_lock(&GlobalMid_Lock);
		list_add_tail(&temp->qhead, &ses->server->pending_mid_q);
		atomic_inc(&midCount);
		temp->midState = MID_REQUEST_ALLOCATED;
		write_unlock(&GlobalMid_Lock);
	} else { 
		cERROR(1,("Need to reconnect after session died to server"));
		if (temp)
			kmem_cache_free(cifs_mid_cachep, temp);
		return NULL;
	}
	return temp;
}

void
DeleteMidQEntry(struct mid_q_entry *midEntry)
{
	/* BB add spinlock to protect midq for each session BB */
	write_lock(&GlobalMid_Lock);
	midEntry->midState = MID_FREE;
	list_del(&midEntry->qhead);
	atomic_dec(&midCount);
	write_unlock(&GlobalMid_Lock);
	buf_release(midEntry->resp_buf);
	kmem_cache_free(cifs_mid_cachep, midEntry);
}

struct oplock_q_entry *
AllocOplockQEntry(struct file * file, struct cifsTconInfo * tcon)
{
	struct oplock_q_entry *temp;
	if ((file == NULL) || (tcon == NULL)) {
		cERROR(1, ("Null parms passed to AllocOplockQEntry"));
		return NULL;
	}
	temp = (struct oplock_q_entry *) kmem_cache_alloc(cifs_oplock_cachep,
						       SLAB_KERNEL);
	if (temp == NULL)
		return temp;
	else {
		temp->file_to_flush = file;
		temp->tcon = tcon;
		write_lock(&GlobalMid_Lock);
		list_add_tail(&temp->qhead, &GlobalOplock_Q);
		write_unlock(&GlobalMid_Lock);
	}
    return temp;

}

void DeleteOplockQEntry(struct oplock_q_entry * oplockEntry)
{
	/* BB add spinlock to protect midq for each session BB */
	write_lock(&GlobalMid_Lock); 
    /* should we check if list empty first? */
	list_del(&oplockEntry->qhead);
	write_unlock(&GlobalMid_Lock);
	kmem_cache_free(cifs_oplock_cachep, oplockEntry);
}

int
smb_send(struct socket *ssocket, struct smb_hdr *smb_buffer,
	 unsigned int smb_buf_length, struct sockaddr *sin)
{
	int rc = 0;
	struct msghdr smb_msg;
	struct iovec iov;
	mm_segment_t temp_fs;

	if(ssocket == NULL)
		return -ENOTSOCK; /* BB eventually add reconnect code here */
/*  ssocket->sk->allocation = GFP_BUFFER; *//* BB is this spurious? */
	iov.iov_base = smb_buffer;
	iov.iov_len = smb_buf_length + 4;

	smb_msg.msg_name = sin;
	smb_msg.msg_namelen = sizeof (struct sockaddr);
	smb_msg.msg_iov = &iov;
	smb_msg.msg_iovlen = 1;
	smb_msg.msg_control = NULL;
	smb_msg.msg_controllen = 0;
	smb_msg.msg_flags = MSG_DONTWAIT + MSG_NOSIGNAL; /* BB add more flags?*/

	/* smb header is converted in header_assemble. bcc and rest of SMB word
	   area, and byte area if necessary, is converted to littleendian in 
	   cifssmb.c and RFC1001 len is converted to bigendian in smb_send */
	if (smb_buf_length > 12)
		smb_buffer->Flags2 = cpu_to_le16(smb_buffer->Flags2);

	/* if(smb_buffer->Flags2 & SMBFLG2_SECURITY_SIGNATURE)
		sign_smb(smb_buffer); */ /* BB enable when signing tested more */

	smb_buffer->smb_buf_length = cpu_to_be32(smb_buffer->smb_buf_length);
	cFYI(1, ("Sending smb of length %d ", smb_buf_length));
	dump_smb(smb_buffer, smb_buf_length + 4);

	temp_fs = get_fs();	/* we must turn off socket api parm checking */
	set_fs(get_ds());
	rc = sock_sendmsg(ssocket, &smb_msg, smb_buf_length + 4);

	set_fs(temp_fs);

	if (rc < 0) {
		cERROR(1,
		       ("Error %d sending data on socket to server.", rc));
	} else
		rc = 0;

	return rc;
}

int
SendReceive(const unsigned int xid, struct cifsSesInfo *ses,
	    struct smb_hdr *in_buf, struct smb_hdr *out_buf,
	    int *pbytes_returned, const int long_op)
{
	int rc = 0;
	int receive_len;
	long timeout;
	struct mid_q_entry *midQ;

	midQ = AllocMidQEntry(in_buf, ses);
	if (midQ == NULL)
		return -EIO;
	if (in_buf->smb_buf_length > CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE - 4) {
		cERROR(1,
		       ("Illegal length, greater than maximum frame, %d ",
			in_buf->smb_buf_length));
		DeleteMidQEntry(midQ);
		return -EIO;
	}
	midQ->midState = MID_REQUEST_SUBMITTED;
	rc = smb_send(ses->server->ssocket, in_buf, in_buf->smb_buf_length,
		      (struct sockaddr *) &(ses->server->sockAddr));

	if (long_op > 1) /* writes past end of file can take looooong time */
		timeout = 300 * HZ;
	else if (long_op == 1)
		timeout = 60 * HZ;
	else
		timeout = 15 * HZ;
	/* wait for 15 seconds or until woken up due to response arriving or 
	   due to last connection to this server being unmounted */

	timeout = wait_event_interruptible_timeout(ses->server->response_q,
				midQ->
				midState & MID_RESPONSE_RECEIVED,
				timeout);
	if (signal_pending(current)) {
		cERROR(1, ("CIFS: caught signal"));
		DeleteMidQEntry(midQ);
		return -EINTR;
	} else {
		if (midQ->resp_buf)
			receive_len =
			    be32_to_cpu(midQ->resp_buf->smb_buf_length);
		else {
			DeleteMidQEntry(midQ);
			return -EIO;
		}
	}

	if (timeout == 0) {
		cFYI(1,
		     ("Timeout on receive. Assume response SMB is invalid."));
		rc = -ETIMEDOUT;
	} else if (receive_len > CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE) {
		cERROR(1,
		       ("Frame too large received.  Length: %d  Xid: %d",
			receive_len, xid));
		rc = -EIO;
	} else {		/* rcvd frame is ok */

		if (midQ->resp_buf && out_buf
		    && (midQ->midState == MID_RESPONSE_RECEIVED)) {
			memcpy(out_buf, midQ->resp_buf,
			       receive_len +
			       4 /* include 4 byte RFC1001 header */ );
			/* convert the length back to a form that we can use */

			dump_smb(out_buf, 92);
			out_buf->smb_buf_length =
			    be32_to_cpu(out_buf->smb_buf_length);
			if (out_buf->smb_buf_length > 12)
				out_buf->Flags2 = le16_to_cpu(out_buf->Flags2);
			if (out_buf->smb_buf_length > 28)
				out_buf->Pid = le16_to_cpu(out_buf->Pid);
			if (out_buf->smb_buf_length > 28)
				out_buf->PidHigh =
				    le16_to_cpu(out_buf->PidHigh);

			*pbytes_returned = out_buf->smb_buf_length;

			/* BB special case reconnect tid and reconnect uid here? */
			rc = map_smb_to_linux_error(out_buf);

			/* convert ByteCount if necessary */
			if (receive_len >=
			    sizeof (struct smb_hdr) -
			    4 /* do not count RFC1001 header */  +
			    (2 * out_buf->WordCount) + 2 /* bcc */ )
				BCC(out_buf) = le16_to_cpu(BCC(out_buf));
		} else
			rc = -EIO;
	}

	DeleteMidQEntry(midQ);	/* BB what if process is killed?
			 - BB add background daemon to clean up Mid entries from
			 killed processes & test killing process with active mid */
	return rc;
}
