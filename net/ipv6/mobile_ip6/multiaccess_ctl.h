/*  
 * 2001 (c) Oy L M Ericsson Ab
 *
 * Author: NomadicLab / Ericsson Research <ipv6@nomadiclab.com>
 *
 * $Id: s.multiaccess_ctl.h 1.6 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 */

#ifndef _MULTIACCESS_CTL_H
#define _MULTIACCESS_CTL_H

/* status */
#define MA_IFACE_NOT_PRESENT 0x01
#define MA_IFACE_NOT_USED    0x02
#define MA_IFACE_HAS_ROUTER  0x04
#define MA_IFACE_CURRENT     0x10

struct ma_if_uinfo {
	int        interface_id;
	int        preference;
	__u8       status;
};
/*
 *  @ma_ctl_get_preferred_id: returns most preferred interface id
 */
int ma_ctl_get_preferred_if(void);

/* @ma_ctl_get_preference: returns preference for an interface
 * @name: name of the interface (dev->name)
 */
int ma_ctl_get_preference(int ifi);

/*
 * Public function: ma_ctl_set_preference
 * Description: Set preference of an existing interface (called by ioctl)
 * Returns:
 */
void ma_ctl_set_preference(unsigned long);

/*
 * Public function: ma_ctl_add_iface
 * Description: Inform control module to insert a new interface
 * Returns: 0 if success, any other number means an error
 */
void ma_ctl_add_iface(int);

/*
 * Public function: ma_ctl_del_iface
 * Description: Inform control module to remove an obsolete interface
 * Returns: 0 if success, any other number means an error
 */
int ma_ctl_del_iface(int);

/*
 * Public function: ma_ctl_upd_iface
 * Description: Inform control module of status change.
 * Returns: 0 if success, any other number means an error
 */
int ma_ctl_upd_iface(int, int, int *);

/*
 * Public function: ma_ctl_init
 * Description: XXX
 * Returns: XXX
 */
void ma_ctl_init(void);

/*
 * Public function: ma_ctl_clean
 * Description: XXX
 * Returns: -
 */
void ma_ctl_clean(void);


#endif
