#ifndef _FIB_LOOKUP_H
#define _FIB_LOOKUP_H

#include <linux/types.h>
#include <linux/list.h>
#include <net/ip_fib.h>

struct fib_alias {
	struct list_head	fa_list;
	struct fib_info		*fa_info;
	u8			fa_tos;
	u8			fa_type;
	u8			fa_scope;
	u8			fa_state;
};

#define FA_S_ACCESSED	0x01

/* Exported by fib_semantics.c */
extern int fib_semantic_match(struct list_head *head,
			      const struct flowi *flp,
			      struct fib_result *res, int prefixlen);
extern void fib_release_info(struct fib_info *);
extern struct fib_info *fib_create_info(const struct rtmsg *r,
					struct kern_rta *rta,
					const struct nlmsghdr *,
					int *err);
extern int fib_nh_match(struct rtmsg *r, struct nlmsghdr *,
			struct kern_rta *rta, struct fib_info *fi);
extern int fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event,
			 u8 tb_id, u8 type, u8 scope, void *dst,
			 int dst_len, u8 tos, struct fib_info *fi);

#endif /* _FIB_LOOKUP_H */
