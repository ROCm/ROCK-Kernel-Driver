#include "hisax_if.h"
#include "hisax_isac.h"
#include <linux/pci.h>

struct hfcpci_bcs {
	struct hisax_b_if b_if;
	struct hfcpci_adapter *adapter;
	int mode;
	int channel;
	int last_fcnt;

	struct sk_buff *tx_skb;
};

struct hfcpci_adapter {
	struct hisax_d_if d_if;
	spinlock_t hw_lock;
	unsigned int irq;
	void *mmio;
	u8 *fifo;
	dma_addr_t fifo_dma;

	struct FsmInst l1m;
	struct FsmTimer timer;
	struct sk_buff *tx_skb;
	int last_fcnt;

	u8 int_m1, int_m2;
	u8 fifo_en;
	u8 trm;
	u8 sctrl, sctrl_r, sctrl_e;
	u8 nt_mode;
	u8 ctmt;
	u8 mst_m;
	u8 conn;
	u8 bswapped;

	struct hfcpci_bcs bcs[2];
};

