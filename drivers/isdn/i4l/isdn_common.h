/* $Id: isdn_common.h,v 1.21.6.1 2001/09/23 22:24:31 kai Exp $
 *
 * header for Linux ISDN subsystem
 * common used functions and debugging-switches (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/isdn.h>

#undef  ISDN_DEBUG_MODEM_OPEN
#undef  ISDN_DEBUG_MODEM_IOCTL
#undef  ISDN_DEBUG_MODEM_WAITSENT
#undef  ISDN_DEBUG_MODEM_HUP
#undef  ISDN_DEBUG_MODEM_ICALL
#undef  ISDN_DEBUG_MODEM_DUMP
#undef  ISDN_DEBUG_MODEM_VOICE
#undef  ISDN_DEBUG_AT
#define  ISDN_DEBUG_NET_DUMP
#define  ISDN_DEBUG_NET_DIAL
#define  ISDN_DEBUG_NET_ICALL
#define  ISDN_DEBUG_STATCALLB
#define  ISDN_DEBUG_COMMAND

#ifdef ISDN_DEBUG_NET_DIAL
#define dbg_net_dial(arg...) printk(KERN_DEBUG arg)
#else
#define dbg_net_dial(arg...) do {} while (0)
#endif

#ifdef ISDN_DEBUG_NET_ICALL
#define dbg_net_icall(arg...) printk(KERN_DEBUG arg)
#else
#define dbg_net_icall(arg...) do {} while (0)
#endif

#ifdef ISDN_DEBUG_STATCALLB
#define dbg_statcallb(arg...) printk(KERN_DEBUG arg)
#else
#define dbg_statcallb(arg...) do {} while (0)
#endif

#define isdn_BUG() \
do { printk(KERN_WARNING "ISDN Bug at %s:%d\n", __FILE__, __LINE__); \
} while(0)

#define HERE printk("%s:%d (%s)\n", __FILE__, __LINE__, __FUNCTION__)

/* Prototypes */
extern void isdn_MOD_INC_USE_COUNT(void);
extern void isdn_MOD_DEC_USE_COUNT(void);
extern void isdn_lock_drivers(void);
extern void isdn_unlock_drivers(void);
extern void isdn_free_channel(int di, int ch, int usage);
extern int isdn_dc2minor(int di, int ch);
extern void isdn_info_update(void);
extern char *isdn_map_eaz2msn(char *msn, int di);
extern void isdn_timer_ctrl(int tf, int onoff);
extern void isdn_unexclusive_channel(int di, int ch);
extern int isdn_getnum(char **);
extern int isdn_msncmp( const char *,  const char *);
extern int isdn_add_channels(driver *, int, int, int);
#if defined(ISDN_DEBUG_NET_DUMP) || defined(ISDN_DEBUG_MODEM_DUMP)
extern void isdn_dumppkt(char *, u_char *, int, int);
#else
static inline void isdn_dumppkt(char *s, u_char *d, int l, int m) { }
#endif

struct dial_info {
	int            l2_proto;
	int            l3_proto;
	struct T30_s  *fax;
	unsigned char  si1;
	unsigned char  si2;
	unsigned char *msn;
	unsigned char *phone;
};

extern struct list_head isdn_net_devs;

extern int   isdn_get_free_slot(int, int, int, int, int, char *);
extern void  isdn_slot_free(int slot, int usage);
extern void  isdn_slot_all_eaz(int slot);
extern int   isdn_slot_command(int slot, int cmd, isdn_ctrl *);
extern int   isdn_slot_dial(int slot, struct dial_info *dial);
extern char *isdn_slot_map_eaz2msn(int slot, char *msn);
extern int   isdn_slot_write(int slot, struct sk_buff *);
extern int   isdn_slot_readbchan(int slot, u_char *, u_char *, int);
extern int   isdn_slot_hdrlen(int slot);
extern int   isdn_slot_driver(int slot);
extern int   isdn_slot_channel(int slot);
extern int   isdn_slot_usage(int slot);
extern void  isdn_slot_set_usage(int slot, int usage);
extern char *isdn_slot_num(int slot);
extern int   isdn_slot_m_idx(int slot);
extern void  isdn_slot_set_m_idx(int slot, int midx);
extern void  isdn_slot_set_rx_netdev(int sl, isdn_net_dev *nd);
extern void  isdn_slot_set_st_netdev(int sl, isdn_net_dev *nd);
extern isdn_net_dev *isdn_slot_rx_netdev(int sl);
extern isdn_net_dev *isdn_slot_st_netdev(int sl);
extern int   isdn_hard_header_len(void);
