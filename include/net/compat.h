#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#include <linux/config.h>

#if defined(CONFIG_COMPAT)

#include <linux/compat.h>

struct compat_msghdr {
	compat_uptr_t	msg_name;
	s32		msg_namelen;
	compat_uptr_t	msg_iov;
	compat_size_t	msg_iovlen;
	compat_uptr_t	msg_control;
	compat_size_t	msg_controllen;
	u32		msg_flags;
};

struct compat_cmsghdr {
	compat_size_t	cmsg_len;
	s32		cmsg_level;
	s32		cmsg_type;
};

#else /* defined(CONFIG_COMPAT) */
#define compat_msghdr	msghdr		/* to avoid compiler warnings */
#endif /* defined(CONFIG_COMPAT) */

extern int get_compat_msghdr(struct msghdr *, struct compat_msghdr *);
extern int verify_compat_iovec(struct msghdr *, struct iovec *, char *, int);
extern asmlinkage long compat_sys_sendmsg(int,struct compat_msghdr *,unsigned);
extern asmlinkage long compat_sys_recvmsg(int,struct compat_msghdr *,unsigned);
extern asmlinkage long compat_sys_getsockopt(int, int, int, char *, int *);
extern int put_cmsg_compat(struct msghdr*, int, int, int, void *);
extern int put_compat_msg_controllen(struct msghdr *, struct compat_msghdr *,
		unsigned long);
extern int cmsghdr_from_user_compat_to_kern(struct msghdr *, unsigned char *,
		int);

#endif /* NET_COMPAT_H */
