/* net/atm/addr.h - Local ATM address registry */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_ADDR_H
#define NET_ATM_ADDR_H

#include <linux/atm.h>
#include <linux/atmdev.h>


void reset_addr(struct atm_dev *dev);
int add_addr(struct atm_dev *dev,struct sockaddr_atmsvc *addr);
int del_addr(struct atm_dev *dev,struct sockaddr_atmsvc *addr);
int get_addr(struct atm_dev *dev,struct sockaddr_atmsvc *u_buf,int size);

#endif
