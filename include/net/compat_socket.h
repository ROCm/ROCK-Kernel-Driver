#ifndef NET_COMPAT_SOCKET_H
#define NET_COMPAT_SOCKET_H 1

#include <linux/compat.h>

#if defined(CONFIG_COMPAT)

/* XXX This really belongs in some header file... -DaveM */
#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */

struct compat_msghdr {
	u32		msg_name;
	int		msg_namelen;
	u32		msg_iov;
	compat_size_t	msg_iovlen;
	u32		msg_control;
	compat_size_t	msg_controllen;
	unsigned	msg_flags;
};

struct compat_cmsghdr {
	compat_size_t	cmsg_len;
	int		cmsg_level;
	int		cmsg_type;
};

/* Bleech... */
#define __CMSG_COMPAT_NXTHDR(ctl, len, cmsg, cmsglen) __cmsg_compat_nxthdr((ctl),(len),(cmsg),(cmsglen))
#define CMSG_COMPAT_NXTHDR(mhdr, cmsg, cmsglen) cmsg_compat_nxthdr((mhdr), (cmsg), (cmsglen))

#define CMSG_COMPAT_ALIGN(len) ( ((len)+sizeof(int)-1) & ~(sizeof(int)-1) )

#define CMSG_COMPAT_DATA(cmsg)	((void *)((char *)(cmsg) + CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr))))
#define CMSG_COMPAT_SPACE(len) (CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr)) + CMSG_COMPAT_ALIGN(len))
#define CMSG_COMPAT_LEN(len) (CMSG_COMPAT_ALIGN(sizeof(struct compat_cmsghdr)) + (len))

#define __CMSG_COMPAT_FIRSTHDR(ctl,len) ((len) >= sizeof(struct compat_cmsghdr) ? \
				    (struct compat_cmsghdr *)(ctl) : \
				    (struct compat_cmsghdr *)NULL)
#define CMSG_COMPAT_FIRSTHDR(msg)	__CMSG_COMPAT_FIRSTHDR((msg)->msg_control, (msg)->msg_controllen)

static __inline__ struct compat_cmsghdr *__cmsg_compat_nxthdr(void *__ctl, __kernel_size_t __size,
					      struct compat_cmsghdr *__cmsg, int __cmsg_len)
{
	struct compat_cmsghdr * __ptr;

	__ptr = (struct compat_cmsghdr *)(((unsigned char *) __cmsg) +
				     CMSG_COMPAT_ALIGN(__cmsg_len));
	if ((unsigned long)((char*)(__ptr+1) - (char *) __ctl) > __size)
		return NULL;

	return __ptr;
}

static __inline__ struct compat_cmsghdr *cmsg_compat_nxthdr (struct msghdr *__msg,
					    struct compat_cmsghdr *__cmsg,
					    int __cmsg_len)
{
	return __cmsg_compat_nxthdr(__msg->msg_control, __msg->msg_controllen,
			       __cmsg, __cmsg_len);
}

#endif /* CONFIG_COMPAT */
#endif
