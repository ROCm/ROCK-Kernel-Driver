#ifndef _LINUX_IP_FW_COMPAT_H
#define _LINUX_IP_FW_COMPAT_H

/* From ip_fw_compat_redir.c */
extern unsigned int
do_redirect(struct sk_buff *skb,
	    const struct net_device *dev,
	    u_int16_t redirpt);

extern void
check_for_redirect(struct sk_buff *skb);

extern void
check_for_unredirect(struct sk_buff *skb);

/* From ip_fw_compat_masq.c */
extern unsigned int
do_masquerade(struct sk_buff **pskb, const struct net_device *dev);

extern void check_for_masq_error(struct sk_buff **pskb);

extern unsigned int
check_for_demasq(struct sk_buff **pskb);

extern int __init masq_init(void);
extern void masq_cleanup(void);

#endif /* _LINUX_IP_FW_COMPAT_H */
