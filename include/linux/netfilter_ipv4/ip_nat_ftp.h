#ifndef _IP_NAT_FTP_H
#define _IP_NAT_FTP_H
/* FTP extension for TCP NAT alteration. */

#ifndef __KERNEL__
#error Only in kernel.
#endif

/* Protects ftp part of conntracks */
DECLARE_LOCK_EXTERN(ip_ftp_lock);

/* We keep track of where the last SYN correction was, and the SYN
   offsets before and after that correction.  Two of these (indexed by
   direction). */
struct ip_nat_ftp_info
{
	u_int32_t syn_correction_pos;                              
	int32_t syn_offset_before, syn_offset_after; 
};

#endif /* _IP_NAT_FTP_H */
