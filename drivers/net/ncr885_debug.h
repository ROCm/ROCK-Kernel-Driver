#ifndef _H_NCR885_DEBUG
#define _H_NCR885_DEBUG

struct ncr885e_regs {
  unsigned long tx_status;
  unsigned long rx_status;
  unsigned long mac_config;
  unsigned long tx_control;
  unsigned long rx_control;
  unsigned long tx_cmd_ptr;
  unsigned long rx_cmd_ptr;
  unsigned long int_status;
};

#ifndef __KERNEL__

struct ncr885e_private {

  struct dbdma_cmd *head;
  struct dbdma_cmd *tx_cmds;
  struct dbdma_cmd *rx_cmds;
  struct dbdma_cmd *stop_cmd;

  struct sk_buff *tx_skbufs[NR_TX_RING];
  struct sk_buff *rx_skbufs[NR_RX_RING];

  int rx_current;
  int rx_dirty;

  int tx_dirty;
  int tx_current;

  unsigned short tx_status[NR_TX_RING];

  unsigned char tx_fullup;
  unsigned char tx_active;
  
  struct net_device_stats  stats;

  struct device *dev;

  struct timer_list tx_timeout;
  int timeout_active;

  spinlock_t lock;
};

#endif /* __KERNEL__ */


#define NCR885E_GET_PRIV   _IOR('N',1,sizeof( struct ncr885e_private ))
#define NCR885E_GET_REGS   _IOR('N',2,sizeof( struct ncr885e_regs ))

#endif
