/* net/atm/common.h - ATM sockets (common part for PVC and SVC) */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_COMMON_H
#define NET_ATM_COMMON_H

#include <linux/net.h>
#include <linux/poll.h> /* for poll_table */


int atm_create(struct socket *sock,int protocol,int family);
int atm_release(struct socket *sock);
int atm_connect(struct socket *sock,int itf,short vpi,int vci);
int atm_recvmsg(struct socket *sock,struct msghdr *m,int total_len,
    int flags,struct scm_cookie *scm);
int atm_sendmsg(struct socket *sock,struct msghdr *m,int total_len,
  struct scm_cookie *scm);
unsigned int atm_poll(struct file *file,struct socket *sock,poll_table *wait);
int atm_ioctl(struct socket *sock,unsigned int cmd,unsigned long arg);
int atm_setsockopt(struct socket *sock,int level,int optname,char *optval,
    int optlen);
int atm_getsockopt(struct socket *sock,int level,int optname,char *optval,
    int *optlen);

int atm_connect_vcc(struct atm_vcc *vcc,int itf,short vpi,int vci);
void atm_release_vcc_sk(struct sock *sk,int free_sk);
void atm_shutdown_dev(struct atm_dev *dev);

int atm_proc_init(void);

/* SVC */

void svc_callback(struct atm_vcc *vcc);
int svc_change_qos(struct atm_vcc *vcc,struct atm_qos *qos);

/* p2mp */

int create_leaf(struct socket *leaf,struct socket *session);

#endif
