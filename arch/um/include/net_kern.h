#ifndef __UM_NET_KERN_H
#define __UM_NET_KERN_H

#include "linux/netdevice.h"
#include "linux/skbuff.h"
#include "linux/socket.h"
#include "linux/list.h"

#define MAX_UML_NETDEV (16)

struct uml_net {
	struct net_device *dev;
	struct net_user_info *user;
	struct net_kern_info *kern;
	int private_size;
	int transport_index;
	unsigned char mac[ETH_ALEN];
	int have_mac;
};

struct uml_net_private {
	struct list_head list;
	spinlock_t lock;
	struct net_device *dev;
	struct timer_list tl;
	struct net_device_stats stats;
	int fd;
	unsigned char mac[ETH_ALEN];
	int have_mac;
	unsigned short (*protocol)(struct sk_buff *);
	int (*open)(void *);
	void (*close)(int, void *);
	void (*remove)(void *);
	int (*read)(int, struct sk_buff **skb, struct uml_net_private *);
	int (*write)(int, struct sk_buff **skb, struct uml_net_private *);
	
	void (*add_address)(unsigned char *, unsigned char *, void *);
	void (*delete_address)(unsigned char *, unsigned char *, void *);
	int (*set_mtu)(int mtu, void *);
	int user[1];
};

struct net_kern_info {
	void (*init)(struct net_device *, int);
	unsigned short (*protocol)(struct sk_buff *);
	int (*read)(int, struct sk_buff **skb, struct uml_net_private *);
	int (*write)(int, struct sk_buff **skb, struct uml_net_private *);
};

struct transport {
	struct list_head list;
	char *name;
	int (*setup)(char *, struct uml_net *);
};

extern struct net_device *ether_init(int);
extern unsigned short ether_protocol(struct sk_buff *);
extern int setup_etheraddr(char *str, unsigned char *addr);
extern struct sk_buff *ether_adjust_skb(struct sk_buff *skb, int extra);
extern int tap_setup_common(char *str, char *type, char **dev_name, 
			    char *hw_addr, int *hw_setup, char **gate_addr);
extern void register_transport(struct transport *new);

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
