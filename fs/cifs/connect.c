/*
 *   fs/cifs/connect.c
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
#include <linux/net.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/ipv6.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "ntlmssp.h"
#include "nterr.h"

#define CIFS_PORT 445
#define RFC1001_PORT 139

extern void SMBencrypt(unsigned char *passwd, unsigned char *c8,
		       unsigned char *p24);
extern void SMBNTencrypt(unsigned char *passwd, unsigned char *c8,
			 unsigned char *p24);
extern int inet_addr(char *);

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *UNCip;
	uid_t linux_uid;
	uid_t linux_gid;
	mode_t file_mode;
	mode_t dir_mode;
	int rw;
};

int ipv4_connect(struct sockaddr_in *psin_server, struct socket **csocket);

int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;

	cFYI(1, ("\nReconnecting tcp session "));

	/* lock tcp session */
	/* mark all smb sessions as reconnecting which use this tcp session */
	/* reconnect tcp session */
	/* wake up waiters on reconnection */
	cFYI(1,
	     ("\nState: 0x%x Flags: 0x%lx", server->ssocket->state,
	      server->ssocket->flags));

	ipv4_connect(&server->sockAddr, &server->ssocket);
	return rc;
}

int
cifs_demultiplex_thread(struct TCP_Server_Info *server)
{
	int length, total_read;
	unsigned int pdu_length;
	struct smb_hdr *smb_buffer = NULL;
	struct msghdr smb_msg;
	mm_segment_t temp_fs;
	struct iovec iov;
	struct socket *csocket = server->ssocket;
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;
	char *temp;

	daemonize();

	server->tsk = current;	/* save process info to wake at shutdown */
	cFYI(1, ("\nDemultiplex PID: %d", current->pid));

	temp_fs = get_fs();	/* we must turn off socket api parm checking */
	set_fs(get_ds());

	while (server->tcpStatus != CifsExiting) {
		if (smb_buffer == NULL)
			smb_buffer = buf_get();
		else
			memset(smb_buffer, 0, sizeof (struct smb_hdr));

		if (smb_buffer == NULL) {
			cERROR(1,
			       ("\n Error - can not get mem for SMB response buffer "));
			return -ENOMEM;
		}
		iov.iov_base = smb_buffer;
		iov.iov_len = sizeof (struct smb_hdr) - 1;	
        /* 1 byte less above since wct is not always returned in error cases */
		smb_msg.msg_iov = &iov;
		smb_msg.msg_iovlen = 1;
		smb_msg.msg_control = NULL;
		smb_msg.msg_controllen = 0;

		length =
		    sock_recvmsg(csocket, &smb_msg,
				 sizeof (struct smb_hdr) -
				 1 /* RFC1001 header and SMB header */ ,
				 MSG_PEEK /* flags see socket.h */ );
		if (length < 0) {
			if (length == -ECONNRESET) {
				cERROR(1, ("\nConnection reset by peer "));
				/* BB fix call below */
				/* cifs_reconnect(server);       */
			} else { /* find define for the -512 returned at unmount time */
				cFYI(1,
				       ("\nReceived error on sock_recvmsg( peek) with length = %d\n",
					length)); 
			}
			break;
		}
		if (length == 0) {
			cFYI(1,
			     ("\nZero length peek received - dead session?? "));
			/* schedule_timeout(HZ/4); 
			   continue; */
			break;
		}
		pdu_length = 4 + ntohl(smb_buffer->smb_buf_length);
		cFYI(1, ("\nPeek length rcvd: %d with smb length: %d", length, pdu_length));	/* BB */

		temp = (char *) smb_buffer;
		if (length > 3) {
			if (temp[0] == (char) 0x85) {
				iov.iov_base = smb_buffer;
				iov.iov_len = 4;
				length = sock_recvmsg(csocket, &smb_msg, 4, 0);
				cFYI(0,
				     ("\nReceived 4 byte keep alive packet "));
			} else if ((temp[0] == (char) 0x83)
				   && (length == 5)) {
				/* we get this from Windows 98 instead of error on SMB negprot response */
				cERROR(1,
				       ("\nNegative RFC 1002 Session response. Error = 0x%x",
					temp[4]));
				break;

			} else if (temp[0] != (char) 0) {
				cERROR(1,
				       ("\nUnknown RFC 1001 frame received not 0x00 nor 0x85"));
				dump_mem(" Received Data is: ", temp, length);
				break;
			} else {
				if ((length != sizeof (struct smb_hdr) - 1)
				    || (pdu_length >
					CIFS_MAX_MSGSIZE + MAX_CIFS_HDR_SIZE)
				    || (pdu_length <
					sizeof (struct smb_hdr) - 1)
				    ||
				    (checkSMBhdr
				     (smb_buffer, smb_buffer->Mid))) {
					cERROR(1,
					       (KERN_ERR
						"\nInvalid size or format for SMB found with length %d and pdu_lenght %d\n",
						length, pdu_length));
					/* BB fix by finding next smb signature - and reading off data until next smb ? BB */

					/* BB add reconnect here */

					break;
				} else {	/* length ok */

					length = 0;
					iov.iov_base = smb_buffer;
					iov.iov_len = pdu_length;
					for (total_read = 0; total_read < pdu_length; total_read += length) {	
             /* Should improve check for buffer overflow with bad pdu_length */
						/*  iov.iov_base = smb_buffer+total_read;
						   iov.iov_len =  pdu_length-total_read; */
						length = sock_recvmsg(csocket, &smb_msg, 
                                 pdu_length - total_read, 0);
         /* cERROR(1,("\nFor iovlen %d Length received: %d with total read %d",
						   iov.iov_len, length,total_read));       */
						if (length == 0) {
							cERROR(1,
							       ("\nZero length receive when expecting %d ",
								pdu_length - total_read));
							/* BB add reconnect here */
							break;
						}						
					}
				}

				dump_smb(smb_buffer, length);
				if (checkSMB
				    (smb_buffer, smb_buffer->Mid, total_read)) {
					cERROR(1, ("\n Bad SMB Received "));
					continue;
				}

                task_to_wake = NULL;
				list_for_each(tmp, &server->pending_mid_q) {
					mid_entry = list_entry(tmp, struct
							       mid_q_entry,
							       qhead);

					if (mid_entry->mid == smb_buffer->Mid) {
						cFYI(1,
						     (" Mid 0x%x matched - waking up\n ",mid_entry->mid));
						task_to_wake = mid_entry->tsk;
						mid_entry->resp_buf =
						    smb_buffer;
						mid_entry->midState =
						    MID_RESPONSE_RECEIVED;
					}
				}

				if (task_to_wake) {
					smb_buffer = NULL;	/* will be freed by users thread after he is done */
					wake_up_process(task_to_wake);
				} else if (is_valid_oplock_break(smb_buffer) == FALSE) {                          
                	cERROR(1, ("\n No task to wake, unknown frame rcvd!\n"));
                }
			}
		} else {
			cFYI(0,
			     ("\nFrame less than four bytes received  %d bytes long.",
			      length));
			if (length > 0) {
				length = sock_recvmsg(csocket, &smb_msg, length, 0);	/* throw away junk frame */
				cFYI(1,
				     (" with junk  0x%x in it\n",
				      *(__u32 *) smb_buffer));
			}
		}
	}
	/* BB add code to lock SMB sessions while releasing */
	if(server->ssocket) {
        sock_release(csocket);
	    server->ssocket = NULL;
    }
	set_fs(temp_fs);
	if (smb_buffer) /* buffer usually freed in free_mid - need to free it on error or exit */
		buf_release(smb_buffer);
	if (list_empty(&server->pending_mid_q)) {
		/* loop through server session structures attached to this and mark them dead */
		list_for_each(tmp, &GlobalSMBSessionList) {
			ses =
			    list_entry(tmp, struct cifsSesInfo,
				       cifsSessionList);
			if (ses->server == server) {
				ses->status = CifsExiting;
				ses->server = NULL;
			}
		}
		kfree(server);
	} else	/* BB need to more gracefully handle the rare negative session 
               response case because response will be still outstanding */
		cERROR(1, ("\nThere are still active MIDs in queue and we are exiting but we can not delete mid_q_entries or TCP_Server_Info structure due to pending requests MEMORY LEAK!!\n "));	/* BB wake up waitors, and/or wait and/or free stale mids and try again? BB */
/* BB Need to fix bug in error path above - perhaps wait until smb requests
   time out and then free the tcp per server struct BB */

	cFYI(1, ("\nAbout to exit from demultiplex thread\n"));
	return 0;
}

int
parse_mount_options(char *options, char *devname, struct smb_vol *vol)
{
	char *value;
	char *data;

	vol->username = NULL;
	vol->password = NULL;
	vol->domainname = NULL;
	vol->UNC = NULL;
	vol->UNCip = NULL;
	vol->linux_uid = current->uid;	/* current->euid instead? */
	vol->linux_gid = current->gid;
	vol->rw = TRUE;

	if (!options)
		return 1;

	while ((data = strsep(&options, ",")) != NULL) {
		if (!*data)
			continue;
		if ((value = strchr(data, '=')) != NULL)
			*value++ = '\0';
		if (strnicmp(data, "user", 4) == 0) {
			if (!value || !*value) {
				printk(KERN_ERR
				       "CIFS: invalid or missing username");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 200) < 200) {
				vol->username = value;
			} else {
				printk(KERN_ERR "CIFS: username too long");
				return 1;
			}
		} else if (strnicmp(data, "pass", 4) == 0) {
			if (!value || !*value) {
				vol->password = NULL;
			} else if (strnlen(value, 17) < 17) {
				vol->password = value;
			} else {
				printk(KERN_ERR "CIFS: password too long");
				return 1;
			}
		} else if ((strnicmp(data, "unc", 3) == 0)
			   || (strnicmp(data, "target", 6) == 0)
			   || (strnicmp(data, "path", 4) == 0)) {
			if (!value || !*value) {
				printk(KERN_ERR
				       "CIFS: invalid path to network resource");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 300) < 300) {
				vol->UNC = value;
				if (strncmp(vol->UNC, "//", 2) == 0) {
					vol->UNC[0] = '\\';
					vol->UNC[1] = '\\';
				} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {	                   
					printk(KERN_ERR
					       "CIFS: UNC Path does not begin with // or \\\\");
					return 1;
				}
				vol->UNCip = &vol->UNC[2];
			} else {
				printk(KERN_ERR "CIFS: UNC name too long");
				return 1;
			}
		} else if ((strnicmp(data, "domain", 3) == 0)
			   || (strnicmp(data, "workgroup", 5) == 0)) {
			if (!value || !*value) {
				printk(KERN_ERR "CIFS: invalid domain name");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 65) < 65) {
				vol->domainname = value;
				cFYI(1, ("\nDomain name set"));
			} else {
				printk(KERN_ERR "CIFS: domain name too long");
				return 1;
			}
		} else if (strnicmp(data, "uid", 3) == 0) {
			if (value && *value) {
				vol->linux_uid =
				    simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "gid", 3) == 0) {
			if (value && *value) {
				vol->linux_gid =
				    simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "file_mode", 4) == 0) {
			if (value && *value) {
				vol->file_mode =
				    simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dir_mode", 3) == 0) {
			if (value && *value) {
				vol->dir_mode =
				    simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "version", 3) == 0) {
			/* ignore */
		} else if (strnicmp(data, "rw", 2) == 0) {
			vol->rw = TRUE;
		} else
			printk(KERN_ERR
			       "CIFS: Unrecognized mount option %s = %s",
			       data, value);
	}
	if (vol->UNC == NULL) {
		if (strnlen(devname, 300) < 300) {
			vol->UNC = devname;
			if (strncmp(vol->UNC, "//", 2) == 0) {
				vol->UNC[0] = '\\';
				vol->UNC[1] = '\\';
			} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
				printk(KERN_ERR
				       "CIFS: UNC Path does not begin with // or \\\\");
				return 1;
			}
			vol->UNCip = &vol->UNC[2];
		} else {
			printk(KERN_ERR "CIFS: UNC name too long");
			return 1;
		}
	}
	return 0;
}

struct cifsSesInfo *
find_tcp_session(__u32 new_target_ip_addr,
		 char *userName, struct TCP_Server_Info **psrvTcp)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;

	*psrvTcp = NULL;

	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if (ses->server) {
			if (ses->server->sockAddr.sin_addr.s_addr ==
			    new_target_ip_addr) {
				/* BB lock server and tcp session and increment use count here?? */
				*psrvTcp = ses->server;	/* found a match on the TCP session */
				/* BB check if reconnection needed */
				if (strncmp
				    (ses->userName, userName,
				     MAX_USERNAME_SIZE) == 0)
					return ses;	/* found exact match on both tcp and SMB sessions */
			}
		}
		/* else tcp and smb sessions need reconnection */
	}
	return NULL;
}

struct cifsTconInfo *
find_unc(__u32 new_target_ip_addr, char *uncName, char *userName)
{
	struct list_head *tmp;
	struct cifsTconInfo *tcon;

	list_for_each(tmp, &GlobalTreeConnectionList) {
		cFYI(1, ("\nNext tcon - "));
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		if (tcon->ses) {
			if (tcon->ses->server) {
				cFYI(1,
				     (" old ip addr: %x == new ip %x ?",
				      tcon->ses->server->sockAddr.sin_addr.
				      s_addr, new_target_ip_addr));
				if (tcon->ses->server->sockAddr.sin_addr.
				    s_addr == new_target_ip_addr) {
	/* BB lock tcon and server and tcp session and increment use count here? */
					/* found a match on the TCP session */
					/* BB check if reconnection needed */
					cFYI(1,
					     ("\nMatched ip, old UNC: %s == new: %s ?",
					      tcon->treeName, uncName));
					if (strncmp
					    (tcon->treeName, uncName,
					     MAX_TREE_SIZE) == 0) {
						cFYI(1,
						     ("\nMatched UNC, old user: %s == new: %s ?",
						      tcon->treeName, uncName));
						if (strncmp
						    (tcon->ses->userName,
						     userName,
						     MAX_USERNAME_SIZE) == 0)
							return tcon;/* also matched user (smb session)*/
					}
				}
			}
		}
	}
	return NULL;
}

int
connect_to_dfs_path(int xid, struct cifsSesInfo *pSesInfo,
		    const char *old_path, const struct nls_table *nls_codepage)
{
	char *temp_unc;
	int rc = 0;
	int num_referrals = 0;
	unsigned char *referrals = NULL;

	if (pSesInfo->ipc_tid == 0) {
		temp_unc =
		    kmalloc(2 +
			    strnlen(pSesInfo->serverName,
				    SERVER_NAME_LEN_WITH_NULL * 2) + 1 +
			    4 /* IPC$ */  + 1, GFP_KERNEL);
		if (temp_unc == NULL)
			return -ENOMEM;
		temp_unc[0] = '\\';
		temp_unc[1] = '\\';
		strncpy(temp_unc + 2, pSesInfo->serverName,
			SERVER_NAME_LEN_WITH_NULL * 2);
		strncpy(temp_unc + 2 +
			strnlen(pSesInfo->serverName,
				SERVER_NAME_LEN_WITH_NULL * 2), "\\IPC$", 6);
		rc = CIFSTCon(xid, pSesInfo, temp_unc, NULL, nls_codepage);
		cFYI(1,
		     ("\nCIFS Tcon rc = %d ipc_tid = %d\n", rc,
		      pSesInfo->ipc_tid));
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = CIFSGetDFSRefer(xid, pSesInfo, old_path, &referrals,
				     &num_referrals, nls_codepage);

	return -ENODEV;		/* BB remove and add return rc; */

}

int
ipv4_connect(struct sockaddr_in *psin_server, struct socket **csocket)
{
	int rc = 0;

	rc = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, csocket);
	if (rc < 0) {
		cERROR(1, ("Error creating socket. Aborting operation\n"));
		return rc;
	}

	psin_server->sin_family = AF_INET;
	psin_server->sin_port = htons(CIFS_PORT);

	rc = (*csocket)->ops->connect(*csocket,
				      (struct sockaddr *) psin_server,
				      sizeof (struct sockaddr_in), 0
		/* Is there a way to fix a polling timeout -
         and find out what more of the flags really mean? */
	    );
	if (rc < 0) {
		psin_server->sin_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
					      psin_server,
					      sizeof (struct sockaddr_in), 0);
		if (rc < 0) {
			cFYI(1, ("Error connecting to socket. %d\n", rc));
			sock_release(*csocket);
			return rc;
		}
	}

	return rc;
}

int
ipv6_connect(struct sockaddr_in6 *psin_server, struct socket **csocket)
{
	int rc = 0;

	rc = sock_create(PF_INET6, SOCK_STREAM,
			 IPPROTO_TCP /* IPPROTO_IPV6 ? */ , csocket);
	if (rc < 0) {
		cERROR(1, ("Error creating socket. Aborting operation\n"));
		return rc;
	}

	psin_server->sin6_family = AF_INET6;
	psin_server->sin6_port = htons(CIFS_PORT);

	rc = (*csocket)->ops->connect(*csocket,
				      (struct sockaddr *) psin_server,
				      sizeof (struct sockaddr_in6), 0
/* BB fix the timeout to be shorter - and check flags */
	    );
	if (rc < 0) {
		psin_server->sin6_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
					      psin_server,
					      sizeof (struct sockaddr_in6), 0);
		if (rc < 0) {
			cFYI(1,
			     ("Error connecting to socket (via ipv6). %d\n",
			      rc));
			sock_release(*csocket);
			return rc;
		}
	}

	return rc;
}

int
cifs_mount(struct super_block *sb, struct cifs_sb_info *cifs_sb,
	   char *mount_data, char *devname)
{
	int rc = 0;
	int xid;
    int ntlmv2_flag = FALSE;
	struct socket *csocket;
	struct sockaddr_in sin_server;
/*	struct sockaddr_in6 sin_server6; */
	struct smb_vol volume_info;
	struct cifsSesInfo *pSesInfo = NULL;
	struct cifsSesInfo *existingCifsSes = NULL;
	struct cifsTconInfo *tcon = NULL;
	struct TCP_Server_Info *srvTcp = NULL;
	char cryptKey[CIFS_CRYPTO_KEY_SIZE];
	char session_key[CIFS_SESSION_KEY_SIZE];
	char ntlm_session_key[CIFS_SESSION_KEY_SIZE];
	char password_with_pad[CIFS_ENCPWD_SIZE];

	xid = GetXid();
	cFYI(0, ("\nEntering cifs_mount. Xid: %d with: %s\n", xid, mount_data));

	parse_mount_options(mount_data, devname, &volume_info);

	if (volume_info.username) {
		cFYI(1, ("\nUsername: %s ", volume_info.username));

	} else {
		cERROR(1, ("\nNo username specified "));	
        /* Could add ways to allow getting user name from alternate locations */
	}

	if (volume_info.UNC) {
		sin_server.sin_addr.s_addr = inet_addr(volume_info.UNCip);
		cFYI(1, ("\nUNC: %s  ", volume_info.UNC));
	} else {
		/* BB we could connect to the DFS root? but which server do we ask? */
		cERROR(1,
		       ("\nCIFS mount error: No UNC path (e.g. -o unc=//192.168.1.100/public) specified  "));
		FreeXid(xid);
		return -ENODEV;
	}
	/* BB add support to use the multiuser_mount flag BB */
	existingCifsSes =
	    find_tcp_session(sin_server.sin_addr.s_addr,
			     volume_info.username, &srvTcp);
	if (srvTcp) {
		cFYI(1, ("\nExisting tcp session with server found "));                
	} else {		/* create socket */
		rc = ipv4_connect(&sin_server, &csocket);
		if (rc < 0) {
			cERROR(1,
			       ("Error connecting to IPv4 socket. Aborting operation\n"));
			FreeXid(xid);
			return rc;
		}

		srvTcp = kmalloc(sizeof (struct TCP_Server_Info), GFP_KERNEL);
		if (srvTcp == NULL) {
			rc = -ENOMEM;
			sock_release(csocket);
			FreeXid(xid);
			return rc;
		} else {
			memset(srvTcp, 0, sizeof (struct TCP_Server_Info));
			memcpy(&srvTcp->sockAddr, &sin_server, sizeof (struct sockaddr_in));	
            /* BB Add code for ipv6 case too */
			srvTcp->ssocket = csocket;
			init_waitqueue_head(&srvTcp->response_q);
			INIT_LIST_HEAD(&srvTcp->pending_mid_q);
			kernel_thread((void *) (void *)
				      cifs_demultiplex_thread, srvTcp,
				      CLONE_FS | CLONE_FILES | CLONE_VM);
		}
	}

	if (existingCifsSes) {
		pSesInfo = existingCifsSes;
		cFYI(1, ("\nExisting smb sess found "));
	} else if (!rc) {
		cFYI(1, ("\nExisting smb sess not found "));
		pSesInfo = sesInfoAlloc();
		if (pSesInfo == NULL)
			rc = -ENOMEM;
		else {
			pSesInfo->server = srvTcp;
			pSesInfo->status = CifsGood;
			sprintf(pSesInfo->serverName, "%u.%u.%u.%u",
				NIPQUAD(sin_server.sin_addr.s_addr));
		}

		/* send negotiate protocol smb */
		if (!rc)
			rc = CIFSSMBNegotiate(xid, pSesInfo, cryptKey);
		cFYI(0, ("\nNegotiate rc = %d ", rc));
		if (!rc) {
			cFYI(1, ("\nSecurity Mode: %x", pSesInfo->secMode));
			cFYI(1,
			     (" Server Capabilities: %x",
			      pSesInfo->capabilities));
			cFYI(1,
			     (" Time Zone: 0x%x %d\n", pSesInfo->timeZone,
			      pSesInfo->timeZone));

			memset(password_with_pad, 0, CIFS_ENCPWD_SIZE);
			if (volume_info.password)
				strcpy(password_with_pad, volume_info.password);

			if (extended_security
			    && (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			    && (pSesInfo->secType == NTLMSSP)) {
				cFYI(1, ("\nNew style sesssetup "));
				rc = CIFSSpnegoSessSetup(xid, pSesInfo,
							 volume_info.
							 username,
							 volume_info.
							 domainname, NULL
							 /* security blob */
							 , 0	/* blob length */
							 , cifs_sb->local_nls);
			} else if (extended_security
				   && (pSesInfo->
				       capabilities & CAP_EXTENDED_SECURITY)
				   && (pSesInfo->secType == RawNTLMSSP)) {
				cFYI(1, ("\nNTLMSSP sesssetup "));
				rc = CIFSNTLMSSPNegotiateSessSetup(xid,
								   pSesInfo,
								   cryptKey,
								   volume_info.domainname,
                                   &ntlmv2_flag,
								   cifs_sb->local_nls);
				if (!rc) {
                    if(ntlmv2_flag) {
                        cFYI(1,("\nAble to use the more secure NTLM version 2 password hash"));
                        /* SMBNTv2encrypt( ...);  */ /* BB fix this up - 
                        and note that Samba client equivalent looks wrong */
                    } else
					    SMBNTencrypt(password_with_pad,cryptKey,ntlm_session_key);

					/* for better security the weaker lanman hash not sent 
                       in AuthSessSetup so why bother calculating it */

					/* toUpper(cifs_sb->local_nls,
						password_with_pad);
					SMBencrypt(password_with_pad,
						   cryptKey, session_key); */

					rc = CIFSNTLMSSPAuthSessSetup(xid,
								      pSesInfo,
								      volume_info.
								      username,
								      volume_info.domainname,
								      ntlm_session_key,
								      session_key,
                                      ntlmv2_flag,
								      cifs_sb->local_nls);
				}
			} else {	/* old style NTLM 0.12 session setup */
				SMBNTencrypt(password_with_pad, cryptKey,
					     ntlm_session_key);
               /* Removed following few lines to not send old style password 
                  hash ever - for better security */
			   /* toUpper(cifs_sb->local_nls, password_with_pad);
				   SMBencrypt(password_with_pad, cryptKey,session_key); 
				   dump_mem("\nCIFS (Samba encrypt): ", session_key,CIFS_SESSION_KEY_SIZE); */

				rc = CIFSSessSetup(xid, pSesInfo,
						   volume_info.username,
						   volume_info.
						   domainname,
						   session_key,
						   ntlm_session_key,
						   cifs_sb->local_nls);
			}
			if (rc) {
				cERROR(1,
				       ("\nSend error in SessSetup = %d\n",
					rc));
			} else {
				cFYI(1,
				     ("CIFS Session Established successfully "));
				strncpy(pSesInfo->userName,
					volume_info.username,
					MAX_USERNAME_SIZE);
				atomic_inc(&srvTcp->socketUseCount);
			}
		}
	}

	/* search for existing tcon to this server share */
	if (!rc) {
		tcon =
		    find_unc(sin_server.sin_addr.s_addr, volume_info.UNC,
			     volume_info.username);
		if (tcon) {
			cFYI(1, ("\nFound match on UNC path "));
		} else {
			tcon = tconInfoAlloc();
			if (tcon == NULL)
				rc = -ENOMEM;
			else {
				/* check for null share name ie connect to dfs root */

				/* BB check if this works for exactly length three strings */
				if ((strchr(volume_info.UNC + 3, '\\') == NULL)
				    && (strchr(volume_info.UNC + 3, '/') ==
					NULL)) {
					rc = connect_to_dfs_path(xid,
								 pSesInfo,
								 "",
								 cifs_sb->
								 local_nls);
					return -ENODEV;
				} else {
					rc = CIFSTCon(xid, pSesInfo,
						      volume_info.UNC,
						      tcon, cifs_sb->local_nls);
					cFYI(1, ("\nCIFS Tcon rc = %d\n", rc));
				}
				if (!rc)
					atomic_inc(&pSesInfo->inUse);
			}
		}
	}
	if (pSesInfo->capabilities & CAP_LARGE_FILES) {
		cFYI(0, ("\nLarge files supported "));
		sb->s_maxbytes = (u64) 1 << 63;
	} else
		sb->s_maxbytes = (u64) 1 << 31;	/* 2 GB */

/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		           /* If find_unc succeeded then rc == 0 so we can not end */
        if (tcon)  /* up here accidently freeing someone elses tcon struct */
			tconInfoFree(tcon);
		if (existingCifsSes == 0) {
			if (pSesInfo) {
				if (pSesInfo->server) {
					cFYI(0,
					     ("\nAbout to check if we need to do SMBLogoff "));
					if (pSesInfo->Suid)
						CIFSSMBLogoff(xid, pSesInfo);
					wake_up_process(pSesInfo->server->tsk);
					schedule_timeout(HZ / 4);	/* give captive thread time to exit */
				} else
					cFYI(1, ("\nNo session or bad tcon"));
				sesInfoFree(pSesInfo);
				/* pSesInfo = NULL; */
			}
		}
	} else {
		atomic_inc(&tcon->useCount);
		cifs_sb->tcon = tcon;
		tcon->ses = pSesInfo;

		/* do not care if following two calls succeed - informational only */
		CIFSSMBQFSDeviceInfo(xid, tcon, cifs_sb->local_nls);
		CIFSSMBQFSAttributeInfo(xid, tcon, cifs_sb->local_nls);
		if (tcon->ses->capabilities & CAP_UNIX)
			CIFSSMBQFSUnixInfo(xid, tcon, cifs_sb->local_nls);
	}

	FreeXid(xid);

	return rc;
}

int
CIFSSessSetup(unsigned int xid, struct cifsSesInfo *ses, char *user,
	      char *domain, char session_key[CIFS_SESSION_KEY_SIZE],
	      char session_key2[CIFS_SESSION_KEY_SIZE],
	      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;

	cFYI(1, ("\nIn sesssetup "));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
    smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 13 /* wct */ );

	pSMB->req_no_secext.AndXCommand = 0xFF;
	pSMB->req_no_secext.MaxBufferSize = cpu_to_le16(ses->maxBuf);
	pSMB->req_no_secext.MaxMpxCount = cpu_to_le16(ses->maxReq);

    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req_no_secext.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req_no_secext.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req_no_secext.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req_no_secext.Capabilities |= CAP_DFS;
	}
	pSMB->req_no_secext.Capabilities =
	    cpu_to_le32(pSMB->req_no_secext.Capabilities);
	/* pSMB->req_no_secext.CaseInsensitivePasswordLength =
	   CIFS_SESSION_KEY_SIZE; */
	pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;
	pSMB->req_no_secext.CaseSensitivePasswordLength =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	bcc_ptr = pByteArea(smb_buffer);
	/* memcpy(bcc_ptr, (char *) session_key, CIFS_SESSION_KEY_SIZE);
	   bcc_ptr += CIFS_SESSION_KEY_SIZE; */
	memcpy(bcc_ptr, (char *) session_key2, CIFS_SESSION_KEY_SIZE);
	bcc_ptr += CIFS_SESSION_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if ((int) bcc_ptr % 2) {	/* must be word aligned for Unicode */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
        if(user == NULL)
            bytes_returned = 0; /* skill null user */
        else
		    bytes_returned =
		        cifs_strtoUCS((wchar_t *) bcc_ptr, user, 100, nls_codepage);
		bcc_ptr += 2 * bytes_returned;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
        if(user != NULL) {                
		    strncpy(bcc_ptr, user, 200);
		    bcc_ptr += strnlen(user, 200);
        }
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	BCC(smb_buffer) = (int) bcc_ptr - (int) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	/* dump_mem("\nSessSetup response is: ", smb_buffer_response, 92);*/
	if (rc) {
/* rc = map_smb_to_linux_error(smb_buffer_response); now done in SendReceive */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* do we want to mark SesInfo struct ? */
		if (ses) {
			ses->Suid = smb_buffer_response->Uid;	/* UID left in wire format (le) */
			cFYI(1, ("UID = %d ", ses->Suid));
         /* response can have either 3 or 4 word count - Samba sends 3 */
			bcc_ptr = pByteArea(smb_buffer_response);	
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4)
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((int) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS = kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)bcc_ptr, len,nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =kcalloc(2 * (len + 1),GFP_KERNEL);
						cifs_strfromUCS_le(ses->serverNOS,
								   (wchar_t *)bcc_ptr,len,nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 + (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
          /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2*(len+1),GFP_KERNEL);
							cifs_strfromUCS_le(ses->serverDomain,
							     (wchar_t *)bcc_ptr,len,nls_codepage);
							bcc_ptr += 2 * (len + 1);
							ses->serverDomain[2*len] = 0;
							ses->serverDomain[1+(2*len)] = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,
								    GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain =
						    kcalloc(2, GFP_KERNEL);
						ses->serverNOS =
						    kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */

					len = strnlen(bcc_ptr, 1024);
					if (((int) bcc_ptr + len) - (int)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}
	
	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSSpnegoSessSetup(unsigned int xid, struct cifsSesInfo *ses,
		    char *user, char *domain, char *SecurityBlob,
		    int SecurityBlobLength,
		    const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;

	cFYI(1, ("\nIn v2 sesssetup "));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->maxReq);

    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	bcc_ptr = pByteArea(smb_buffer);
	memcpy(bcc_ptr, SecurityBlob, SecurityBlobLength);
	bcc_ptr += SecurityBlobLength;

	if (ses->capabilities & CAP_UNICODE) {
		if ((int) bcc_ptr % 2) {	/* must be word aligned for Unicode strings */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, user, 100, nls_codepage);
		bcc_ptr += 2 * bytes_returned;	/* convert num of 16 bit words to bytes */
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
		strncpy(bcc_ptr, user, 200);
		bcc_ptr += strnlen(user, 200);
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	BCC(smb_buffer) = (int) bcc_ptr - (int) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	/* dump_mem("\nSessSetup response is: ", smb_buffer_response, 92);  */
	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* BB do we want to set anything in SesInfo struct ? */
		if (ses) {
			ses->Suid = smb_buffer_response->Uid;	/* UID left in wire format (le) */
			cFYI(1, ("UID = %d ", ses->Suid));
			bcc_ptr = pByteArea(smb_buffer_response);	/* response can have either 3 or 4 word count - Samba sends 3 */

			/* BB Fix below to make endian neutral !! */

			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("\nSecurity Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((int) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->serverNOS,
								   (wchar_t *)bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 + (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
                            /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain = kcalloc(2*(len+1),GFP_KERNEL);
							cifs_strfromUCS_le(ses->serverDomain,
							     (wchar_t *)bcc_ptr, 
                                 len,
							     nls_codepage);
							bcc_ptr += 2*(len+1);
							ses->serverDomain[2*len] = 0;
							ses->serverDomain[1+(2*len)] = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain = kcalloc(2, GFP_KERNEL);
						ses->serverNOS = kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */

					len = strnlen(bcc_ptr, 1024);
					if (((int) bcc_ptr + len) - (int)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverOS, bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSNTLMSSPNegotiateSessSetup(unsigned int xid,
			      struct cifsSesInfo *ses,
			      char *challenge_from_server,
			      char *domain, int * pNTLMv2_flag,
			      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof (NEGOTIATE_MESSAGE);
	PNEGOTIATE_MESSAGE SecurityBlob;
	PCHALLENGE_MESSAGE SecurityBlob2;

	cFYI(1, ("\nIn NTLMSSP sesssetup (negotiate) "));
    *pNTLMv2_flag = FALSE;
	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);

	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->maxReq);

    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PNEGOTIATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmNegotiate;
	SecurityBlob->NegotiateFlags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM |
	    NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_NTLM | 0x80000000 |
	    NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_128;
    if(ntlmv2_support)
        SecurityBlob->NegotiateFlags |= NTLMSSP_NEGOTIATE_NTLMV2;
	/* setup pointers to domain name and workstation name */
	bcc_ptr += SecurityBlobLength;

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;

	if (domain == NULL) {
		SecurityBlob->DomainName.Buffer = 0;
		SecurityBlob->DomainName.Length = 0;
		SecurityBlob->DomainName.MaximumLength = 0;
	} else {
		SecurityBlob->NegotiateFlags |=
		    NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
		strncpy(bcc_ptr, domain, 63);
		SecurityBlob->DomainName.Length = strnlen(domain, 64);
		SecurityBlob->DomainName.MaximumLength =
		    cpu_to_le16(SecurityBlob->DomainName.Length);
		SecurityBlob->DomainName.Buffer =
		    cpu_to_le32((unsigned int) &SecurityBlob->
				DomainString -
				(unsigned int) &SecurityBlob->Signature);
		bcc_ptr += SecurityBlob->DomainName.Length;
		SecurityBlobLength += SecurityBlob->DomainName.Length;
		SecurityBlob->DomainName.Length =
		    cpu_to_le16(SecurityBlob->DomainName.Length);
	}
	if (ses->capabilities & CAP_UNICODE) {
		if ((int) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}

		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null terminate Linux version */
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* empty domain field */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags =
	    cpu_to_le32(SecurityBlob->NegotiateFlags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	BCC(smb_buffer) = (int) bcc_ptr - (int) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);

	if (smb_buffer_response->Status.CifsError ==
	    (NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	
        /* Do we want to set anything in SesInfo struct when guest login? */

		bcc_ptr = pByteArea(smb_buffer_response);	
        /* response can have either 3 or 4 word count - Samba sends 3 */

		SecurityBlob2 = (PCHALLENGE_MESSAGE) bcc_ptr;
		if (SecurityBlob2->MessageType != NtLmChallenge) {
			cFYI(1,
			     ("\nUnexpected NTLMSSP message type received %d",
			      SecurityBlob2->MessageType));
		} else if (ses) {
			ses->Suid = smb_buffer_response->Uid; /* UID left in le format */ 
			cFYI(1, ("UID = %d ", ses->Suid));
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("\nSecurity Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				cFYI(1, ("\nNTLMSSP Challenge rcvd "));

				memcpy(challenge_from_server,
				       SecurityBlob2->Challenge,
				       CIFS_CRYPTO_KEY_SIZE);
                if(SecurityBlob2->NegotiateFlags & NTLMSSP_NEGOTIATE_NTLMV2)
                    *pNTLMv2_flag = TRUE;
				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((int) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (wchar_t *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 +
							       (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
           /* last string is not always null terminated (for e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (wchar_t *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain =
							    kcalloc(2,
								    GFP_KERNEL);
					} else {	/* no room so create dummy domain and NOS string */
						ses->serverDomain =
						    kcalloc(2, GFP_KERNEL);
						ses->serverNOS =
						    kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */

					len = strnlen(bcc_ptr, 1024);
					if (((int) bcc_ptr + len) - (int)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						ses->serverOS =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverOS,
							bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain =
						    kcalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);	
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSNTLMSSPAuthSessSetup(unsigned int xid, struct cifsSesInfo *ses,
			 char *user, char *domain,
			 char *ntlm_session_key,
			 char *lanman_session_key, int ntlmv2_flag,
			 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof (AUTHENTICATE_MESSAGE);
	PAUTHENTICATE_MESSAGE SecurityBlob;

	cFYI(1, ("\nIn NTLMSSPSessSetup (Authenticate)"));

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			0 /* no tCon exists yet */ , 12 /* wct */ );
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->maxReq);

	pSMB->req.hdr.Uid = ses->Suid;

    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->req.Capabilities =
	    CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		pSMB->req.Capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		pSMB->req.Capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		pSMB->req.Capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(pSMB->req.Capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PAUTHENTICATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmAuthenticate;
	bcc_ptr += SecurityBlobLength;
	SecurityBlob->NegotiateFlags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET |
	    NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO |
	    0x80000000 | NTLMSSP_NEGOTIATE_ALWAYS_SIGN | NTLMSSP_NEGOTIATE_128;
    if(ntlmv2_flag)
        SecurityBlob->NegotiateFlags |= NTLMSSP_NEGOTIATE_NTLMV2;

/* setup pointers to domain name and workstation name */

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;
	SecurityBlob->SessionKey.Length = 0;
	SecurityBlob->SessionKey.MaximumLength = 0;
	SecurityBlob->SessionKey.Buffer = 0;

	SecurityBlob->LmChallengeResponse.Length = 0;
	SecurityBlob->LmChallengeResponse.MaximumLength = 0;
	SecurityBlob->LmChallengeResponse.Buffer = 0;

	SecurityBlob->NtChallengeResponse.Length =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.MaximumLength =
	    cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	memcpy(bcc_ptr, ntlm_session_key, CIFS_SESSION_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.Buffer =
	    cpu_to_le32(SecurityBlobLength);
	SecurityBlobLength += CIFS_SESSION_KEY_SIZE;
	bcc_ptr += CIFS_SESSION_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			SecurityBlob->DomainName.Length =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, domain, 64,
					  nls_codepage);
			SecurityBlob->DomainName.Length *= 2;
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->DomainName.Length;
			SecurityBlobLength += SecurityBlob->DomainName.Length;
			SecurityBlob->DomainName.Length =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			SecurityBlob->UserName.Length =
			    cifs_strtoUCS((wchar_t *) bcc_ptr, user, 64,
					  nls_codepage);
			SecurityBlob->UserName.Length *= 2;
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(SecurityBlob->UserName.Length);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->UserName.Length;
			SecurityBlobLength += SecurityBlob->UserName.Length;
			SecurityBlob->UserName.Length =
			    cpu_to_le16(SecurityBlob->UserName.Length);
		}

		/* SecurityBlob->WorkstationName.Length = cifs_strtoUCS((wchar_t *) bcc_ptr, "AMACHINE",64, nls_codepage);
		   SecurityBlob->WorkstationName.Length *= 2;
		   SecurityBlob->WorkstationName.MaximumLength = cpu_to_le16(SecurityBlob->WorkstationName.Length);
		   SecurityBlob->WorkstationName.Buffer = cpu_to_le32(SecurityBlobLength);
		   bcc_ptr += SecurityBlob->WorkstationName.Length;
		   SecurityBlobLength += SecurityBlob->WorkstationName.Length;
		   SecurityBlob->WorkstationName.Length = cpu_to_le16(SecurityBlob->WorkstationName.Length);  */

		if ((int) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, UTS_RELEASE, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null term version string */
		bytes_returned =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			SecurityBlob->NegotiateFlags |=
			    NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
			strncpy(bcc_ptr, domain, 63);
			SecurityBlob->DomainName.Length = strnlen(domain, 64);
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->DomainName.Length;
			SecurityBlobLength += SecurityBlob->DomainName.Length;
			SecurityBlob->DomainName.Length =
			    cpu_to_le16(SecurityBlob->DomainName.Length);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			strncpy(bcc_ptr, user, 63);
			SecurityBlob->UserName.Length = strnlen(user, 64);
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(SecurityBlob->UserName.Length);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += SecurityBlob->UserName.Length;
			SecurityBlobLength += SecurityBlob->UserName.Length;
			SecurityBlob->UserName.Length =
			    cpu_to_le16(SecurityBlob->UserName.Length);
		}
		/* BB fill in our workstation name if known BB */

		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, UTS_RELEASE);
		bcc_ptr += strlen(UTS_RELEASE) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* null domain */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags =
	    cpu_to_le32(SecurityBlob->NegotiateFlags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	BCC(smb_buffer) = (int) bcc_ptr - (int) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, 1);
	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		pSMBr->resp.Action = le16_to_cpu(pSMBr->resp.Action);
		pSMBr->resp.SecurityBlobLength =
		    le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (pSMBr->resp.Action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));	/* BB do we want to set anything in SesInfo struct ? */
/*        if(SecurityBlob2->MessageType != NtLm??){                               
                 cFYI("\nUnexpected message type on auth response is %d ")); 
        } */
		if (ses) {
			cFYI(1,
			     ("Does UID on challenge %d match auth response UID %d ",
			      ses->Suid, smb_buffer_response->Uid));
			ses->Suid = smb_buffer_response->Uid; /* UID left in wire format */
			bcc_ptr = pByteArea(smb_buffer_response);	
            /* response can have either 3 or 4 word count - Samba sends 3 */
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (pSMBr->resp.SecurityBlobLength <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    pSMBr->resp.SecurityBlobLength;
					cFYI(1,
					     ("\nSecurity Blob Length %d ",
					      pSMBr->resp.SecurityBlobLength));
				}

				cFYI(1,
				     ("\nNTLMSSP response to Authenticate "));

				if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
					if ((int) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words = BCC(smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
  the end since (at least) WIN2K and Windows XP have a major bug in not null
  terminating last Unicode string in response  */
					ses->serverOS =
					    kcalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (wchar_t *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						ses->serverNOS =
						    kcalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (wchar_t *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1+(2*len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);	
     /* last string not always null terminated (e.g. for Windows XP & 2000) */
							ses->serverDomain =
							    kcalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (wchar_t *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else
							ses->serverDomain = kcalloc(2,GFP_KERNEL);
					} else {  /* no room so create dummy domain and NOS string */
						ses->serverDomain = kcalloc(2, GFP_KERNEL);
						ses->serverNOS = kcalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */

					len = strnlen(bcc_ptr, 1024);
					if (((int) bcc_ptr + len) - 
                        (int) pByteArea(smb_buffer_response) 
                            <= BCC(smb_buffer_response)) {
						ses->serverOS = kcalloc(len + 1,GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverNOS = kcalloc(len+1,GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);	
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						ses->serverDomain = kcalloc(len+1,GFP_KERNEL);
						strncpy(ses->serverDomain, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("Variable field of length %d extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1,
				       (" Security Blob Length extends beyond end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	if (smb_buffer)
		buf_release(smb_buffer);

	return rc;
}

int
CIFSTCon(unsigned int xid, struct cifsSesInfo *ses,
	 const char *tree, struct cifsTconInfo *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	char *bcc_ptr;
	int rc = 0;
	int length;

	if (ses == NULL)
		return -EIO;

	smb_buffer = buf_get();
	if (smb_buffer == 0) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			0 /*no tid */ , 4 /*wct */ );
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
	bcc_ptr = &(pSMB->Password[0]);
	bcc_ptr++;		/* skip password */

    if(ses->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
        smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUCS((wchar_t *) bcc_ptr, tree, 100, nls_codepage);
		bcc_ptr += 2 * length;	/* convert num of 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */

		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	BCC(smb_buffer) = (int) bcc_ptr - (int) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += BCC(smb_buffer);
	BCC(smb_buffer) = cpu_to_le16(BCC(smb_buffer));

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length, 0);

	/* if (rc) rc = map_smb_to_linux_error(smb_buffer_response); */
    /* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		length = strnlen(bcc_ptr, BCC(smb_buffer_response) - 2);
        /* skip service field (NB: this field is always ASCII) */
		bcc_ptr += length + 1;	
        strncpy(tcon->treeName, tree, MAX_TREE_SIZE);
		if (smb_buffer->Flags2 &= SMBFLG2_UNICODE) {
			length = UniStrnlen((wchar_t *) bcc_ptr, 512);
			if (((int) bcc_ptr + (2 * length)) -
			    (int) pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				tcon->nativeFileSystem =
				    kcalloc(length + 2, GFP_KERNEL);
				cifs_strfromUCS_le(tcon->nativeFileSystem,
						   (wchar_t *) bcc_ptr,
						   length, nls_codepage);
				bcc_ptr += 2 * length;
				bcc_ptr[0] = 0;	/* null terminate the string */
				bcc_ptr[1] = 0;
				bcc_ptr += 2;
			}
			/* else do not bother copying these informational fields */
		} else {
			length = strnlen(bcc_ptr, 1024);
			if (((int) bcc_ptr + length) -
			    (int) pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				tcon->nativeFileSystem =
				    kcalloc(length + 1, GFP_KERNEL);
				strncpy(tcon->nativeFileSystem, bcc_ptr,
					length);
			}
			/* else do not bother copying these informational fields */
		}
		tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		cFYI(1, ("\nTcon flags: 0x%x ", tcon->Flags));
	} else if ((rc == 0) && tcon == NULL) {
        /* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	if (smb_buffer)
		buf_release(smb_buffer);
	return rc;
}

int
cifs_umount(struct super_block *sb, struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	int xid;
	struct cifsSesInfo *ses = NULL;

	xid = GetXid();
	if (cifs_sb->tcon) {
		ses = cifs_sb->tcon->ses; /* save ptr to ses before delete tcon!*/
		rc = CIFSSMBTDis(xid, cifs_sb->tcon);
		if (rc == -EBUSY) {
			FreeXid(xid);
			return 0;
		}
		tconInfoFree(cifs_sb->tcon);
		if ((ses) && (ses->server)) {
			cFYI(1, ("\nAbout to do SMBLogoff "));
			rc = CIFSSMBLogoff(xid, ses);
			if (rc == -EBUSY) {
				/* BB this looks wrong - why is this here? */
				FreeXid(xid);
				return 0;
			}
    	 /* wake_up_process(ses->server->tsk);*/ /* was worth a try */
			schedule_timeout(HZ / 4);	/* give captive thread time to exit */
            if((ses->server) && (ses->server->ssocket)) {            
                cFYI(1,("\nWaking up socket by sending it signal "));
                send_sig(SIGINT,ses->server->tsk,1);
                /* No luck figuring out a better way to_close socket */
         /*ses->server->ssocket->sk->prot->close(ses->server->ssocket->sk,0);*/
              /*  ses->server->ssocket = NULL; */  /* serialize better */
                /* sock_wake_async(ses->server->ssocket,3,POLL_HUP); */
            }
		} else
			cFYI(1, ("\nNo session or bad tcon"));
	}
	/* BB future check active count of tcon and then free if needed BB */
	cifs_sb->tcon = NULL;
	if (ses) {
		schedule_timeout(HZ / 2);
		/* if ((ses->server) && (ses->server->ssocket)) {
               cFYI(1,("\nReleasing socket "));        
               sock_release(ses->server->ssocket); 
               kfree(ses->server); 
          } */ 
	}
	if (ses)
		sesInfoFree(ses);

	FreeXid(xid);
	return rc;		/* BB check if we should always return zero here */
} 
