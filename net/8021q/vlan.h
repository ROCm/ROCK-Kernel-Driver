#ifndef __BEN_VLAN_802_1Q_INC__
#define __BEN_VLAN_802_1Q_INC__

#include <linux/if_vlan.h>

/*  Uncomment this if you want debug traces to be shown. */
/* #define VLAN_DEBUG */

#define VLAN_ERR KERN_ERR
#define VLAN_INF KERN_ALERT
#define VLAN_DBG KERN_ALERT /* change these... to debug, having a hard time
                             * changing the log level at run-time..for some reason.
                             */

/*

These I use for memory debugging.  I feared a leak at one time, but
I never found it..and the problem seems to have dissappeared.  Still,
I'll bet they might prove useful again... --Ben


#define VLAN_MEM_DBG(x, y, z) printk(VLAN_DBG __FUNCTION__ ":  "  x, y, z);
#define VLAN_FMEM_DBG(x, y) printk(VLAN_DBG __FUNCTION__  ":  " x, y);
*/

/* This way they don't do anything! */
#define VLAN_MEM_DBG(x, y, z) 
#define VLAN_FMEM_DBG(x, y)


extern unsigned short vlan_name_type;

/* Counter for how many NON-VLAN protos we've received on a VLAN. */
extern unsigned long vlan_bad_proto_recvd;

int vlan_ioctl_handler(unsigned long arg);

/* Add some headers for the public VLAN methods. */
int unregister_802_1Q_vlan_device(const char* vlan_IF_name);
struct net_device *register_802_1Q_vlan_device(const char* eth_IF_name,
                                               unsigned short VID);

#endif /* !(__BEN_VLAN_802_1Q_INC__) */
