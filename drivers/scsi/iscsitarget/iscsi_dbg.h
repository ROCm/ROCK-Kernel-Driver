#ifndef ISCSI_DBG_H
#define ISCSI_DBG_H

#define D_SETUP		(1UL << 0)
#define D_EXIT		(1UL << 1)
#define D_GENERIC	(1UL << 2)
#define D_READ		(1UL << 3)
#define D_WRITE 	(1UL << 4)
#define D_IOV		(1UL << 5)
#define D_THREAD	(1UL << 6)
#define D_TASK_MGT	(1UL << 7)
#define D_DUMP_PDU	(1UL << 8)

#define D_DATA		(D_READ | D_WRITE)

extern unsigned long debug_enable_flags;

#define dprintk(debug, fmt, args...)					\
do {									\
	if ((debug) & debug_enable_flags) {				\
		printk("%s(%d) " fmt, __FUNCTION__, __LINE__, args);	\
	}								\
} while (0)

#define eprintk(fmt, args...)					\
do {								\
	printk("%s(%d) " fmt, __FUNCTION__, __LINE__, args);	\
} while (0)

#define assert(p) do {						\
	if (!(p)) {						\
		printk(KERN_CRIT "BUG at %s:%d assert(%s)\n",	\
		       __FILE__, __LINE__, #p);			\
		dump_stack();					\
		BUG();						\
	}							\
} while (0)

#if D_IOV
static inline void iscsi_dump_iov(struct msghdr *msg)
{
	int i;
	printk("%p, %ld\n", msg->msg_iov, (long)msg->msg_iovlen);
	for (i = 0; i < min_t(size_t, msg->msg_iovlen, ISCSI_CONN_IOV_MAX); i++)
		printk("%d: %p,%ld\n", i, msg->msg_iov[i].iov_base, (long)msg->msg_iov[i].iov_len);
}
#else
#define iscsi_dump_iov(x) do {} while (0)
#endif

#if D_DUMP_PDU
static void iscsi_dump_char(int ch)
{
	static unsigned char text[16];
	static int i = 0;

	if (ch < 0) {
		while ((i % 16) != 0) {
			printk("   ");
			text[i] = ' ';
			i++;
			if ((i % 16) == 0)
				printk(" | %.16s |\n", text);
			else if ((i % 4) == 0)
				printk(" |");
		}
		i = 0;
		return;
	}

	text[i] = (ch < 0x20 || (ch >= 0x80 && ch <= 0xa0)) ? ' ' : ch;
	printk(" %02x", ch);
	i++;
	if ((i % 16) == 0) {
		printk(" | %.16s |\n", text);
		i = 0;
	} else if ((i % 4) == 0)
		printk(" |");
}

static inline void iscsi_dump_pdu(struct iscsi_pdu *pdu)
{
	unsigned char *buf;
	int i;

	buf = (void *)&pdu->bhs;
	printk("BHS: (%p,%lu)\n", buf, sizeof(pdu->bhs));
	for (i = 0; i < sizeof(pdu->bhs); i++)
		iscsi_dump_char(*buf++);
	iscsi_dump_char(-1);

	buf = (void *)pdu->ahs;
	printk("AHS: (%p,%d)\n", buf, pdu->ahssize);
	for (i = 0; i < pdu->ahssize; i++)
		iscsi_dump_char(*buf++);
	iscsi_dump_char(-1);

	printk("Data: (%d)\n", pdu->datasize);
}

#else
#define iscsi_dump_pdu(x) do {} while (0)
#endif

#define show_param(param)\
{\
	eprintk("%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",\
		(param)->initial_r2t,\
		(param)->immediate_data,\
		(param)->max_connections,\
		(param)->max_recv_data_length,\
		(param)->max_xmit_data_length,\
		(param)->max_burst_length,\
		(param)->first_burst_length,\
		(param)->default_wait_time,\
		(param)->default_retain_time,\
		(param)->max_outstanding_r2t,\
		(param)->data_pdu_inorder,\
		(param)->data_sequence_inorder,\
		(param)->error_recovery_level,\
		(param)->header_digest,\
		(param)->data_digest);\
}

#endif
