/* 
 * fs/rcfs/magic_target.c 
 *
 * Copyright (C) Shailabh Nagar,  IBM Corp. 2004
 * Copyright (C) Vivek Kashyap,  IBM Corp. 2004
 *           
 * 
 * virtual file assisting in reclassification in rcfs. 
 * 
 * Writing a pid to a class's target file reclassifies the corresponding
 * task to the class.
 *
 * Latest version, more details at http://ckrm.sf.net
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Changes
 *
 * 06 Mar 2004
 *        Created.
 *
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <asm/uaccess.h>

#include <linux/rcfs.h>
#include "magic.h"


#define TARGET_MAX_INPUT_SIZE  300

/* The enums for the share types should match the indices expected by
   array parameter to ckrm_set_resshare */

/* Note only the first NUM_SHAREVAL enums correspond to share types,
   the remaining ones are for token matching purposes */

enum target_token_t {
        PID, IPV4, IPV6, RES_TYPE, TARGET_ERR
};

static match_table_t tokens = {
        {PID, "pid=%u"},
	{IPV4, "ipv4=%s"},
	{IPV6, "ipv6=%s"},
        {TARGET_ERR, NULL},
};


struct target_data {
	int flag;
	pid_t mpid;
	char addr[64];
};

static int target_parse(char *options, struct target_data *value)
{
	char *p;
	int option;
	int flag = TARGET_ERR;

	if (!options)
		return 1;
	
	//printk(KERN_ERR "options |%s|\n",options);
	while ((p = strsep(&options, ",")) != NULL) {
		
		substring_t args[MAX_OPT_ARGS];
		int token;
		
		//printk(KERN_ERR "p |%s| options |%s|\n",p,options);

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		//printk(KERN_ERR "Token %d\n",token);
		switch (token) {
			
		case PID:
			if (flag == TARGET_ERR)
				flag = PID;
			else
				break;
			if (match_int(args, &option))
				return 0;
			value->mpid = (pid_t)(option);
			break;

		case IPV4:
			if (flag == TARGET_ERR)
				flag = IPV4;
			else
				break;
			match_strcpy(value->addr,args);
			break;

		case IPV6:
			printk(KERN_INFO "rcfs: IPV6 not supported yet\n");
			return 0;	
		default:
			return 0;
		}

	}
	return 1;
}	

#if 0
static int
magic_aton(char *s)
{
	// TODO
	// Add alpha to IP address conversion.
	return 1;
}
#endif


static ssize_t
target_write(struct file *file, const char __user *buf,
			   size_t count, loff_t *ppos)
{
	struct rcfs_inode_info *ri= RCFS_I(file->f_dentry->d_inode);
	char *optbuf;
	int done;
 	struct target_data value;

	if ((ssize_t) count < 0 || (ssize_t) count > TARGET_MAX_INPUT_SIZE)
		return -EINVAL;
	
	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	
	down(&(ri->vfs_inode.i_sem));
	
	optbuf = kmalloc(TARGET_MAX_INPUT_SIZE, GFP_KERNEL);
	__copy_from_user(optbuf, buf, count);

	/* cat > shares puts in an extra newline */
	if (optbuf[count-1] == '\n')
		optbuf[count-1]='\0';

	done = target_parse(optbuf, &value);

	if (!done) {
		printk(KERN_ERR "Error parsing target \n");
		goto target_out;
	}

	if (value.flag == PID) {
		ckrm_forced_reclassify_pid(value.mpid, ri->core);
	}

#ifdef MAGIC_TARGET_TODO
	else if (value.flag == IPV4) {
		// Get the socket. Find the listening socket's back-pointer
		// to ns (set in lopt when SOCKETAQ option chosen). 
		// Dissociate from old class and join the new one.
		
		u32 daddr;
		u16 dport;

		memset(&saddr, 0, sizeof(saddr));

		daddr = magic_aton(value.addr);
		dport = magic_aton(value.port);

		local_bh_disable();
		sk = find_tcpv4_listener_byaddr(daddr,port);
		sock_hold(sk);
		local_bh_enable();
		lock_sock(sk);
		__sock_put(sk);	
		ns = tcp_sk(sk)->tp->lopt->ns;
		ckrm_forced_reclassify_net(ns, ri->core);
		release_sock(sk);
	}

#endif
	
	

target_out:	

	up(&(ri->vfs_inode.i_sem));
	kfree(optbuf);
	return count;
}



struct file_operations target_fileops = {
	.write          = target_write,
};


