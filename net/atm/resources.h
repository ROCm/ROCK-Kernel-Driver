/* net/atm/resources.h - ATM-related resources */

/* Written 1995-1998 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_RESOURCES_H
#define NET_ATM_RESOURCES_H

#include <linux/config.h>
#include <linux/atmdev.h>


extern struct atm_dev *atm_devs;
extern struct atm_vcc *nodev_vccs; /* VCCs not linked to any device */


struct sock *alloc_atm_vcc_sk(int family);
void free_atm_vcc_sk(struct sock *sk);


#ifdef CONFIG_PROC_FS

#include <linux/proc_fs.h>

int atm_proc_dev_register(struct atm_dev *dev);
void atm_proc_dev_deregister(struct atm_dev *dev);

#endif

#endif
