#ifndef __HISAX_HSCX_H__
#define __HISAX_HSCX_H__

#include <linux/kernel.h>
#include "fsm.h"
#include "hisax_if.h"

#define HSCX_BUFMAX	4096

#define HSCX_IOM1 0
#define HSCX_IPAC 1

struct hscx {
	void *priv;
	u_long flags;
	struct hisax_b_if b_if;
	int mode;
	int channel;
	u_char tsaxr;
	struct sk_buff *tx_skb;
	int tx_cnt;
	u_char rcvbuf[HSCX_BUFMAX];
	int rcvidx;

	u_char (*read_hscx)      (struct hscx *, u_char);
	void   (*write_hscx)     (struct hscx *, u_char, u_char);
	void   (*read_hscx_fifo) (struct hscx *, u_char *, int);
	void   (*write_hscx_fifo)(struct hscx *, u_char *, int);
};

void hscx_init(struct hscx *hscx);
void hscx_b_l2l1(struct hisax_if *hisax_b_if, int pr, void *arg);

void hscx_setup(struct hscx *hscx);
void hscx_irq(struct hscx *hscx);

#endif
