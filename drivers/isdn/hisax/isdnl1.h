/* $Id: isdnl1.h,v 2.9 2000/06/26 08:59:13 keil Exp $
 *
 * Layer 1 defines
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define D_RCVBUFREADY	0
#define D_XMTBUFREADY	1
#define D_L1STATECHANGE	2
#define D_CLEARBUSY	3
#define D_RX_MON0	4
#define D_RX_MON1	5
#define D_TX_MON0	6
#define D_TX_MON1	7

#define B_RCVBUFREADY 0
#define B_XMTBUFREADY 1

extern void debugl1(struct IsdnCardState *cs, char *fmt, ...);
extern void DChannel_proc_xmt(struct IsdnCardState *cs);
extern void DChannel_proc_rcv(struct IsdnCardState *cs);
extern void l1_msg(struct IsdnCardState *cs, int pr, void *arg);
extern void l1_msg_b(struct PStack *st, int pr, void *arg);

#ifdef L2FRAME_DEBUG
extern void Logl2Frame(struct IsdnCardState *cs, struct sk_buff *skb, char *buf, int dir);
#endif
