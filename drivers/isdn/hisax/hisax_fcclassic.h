#include "hisax_if.h"
#include "hisax_isac.h"
#include "hisax_hscx.h"

#include <linux/pci.h>

struct fritz_adapter {
	unsigned int io;
	unsigned int irq;
	unsigned int cfg_reg;
	unsigned int isac_base;
	unsigned int isac_fifo;
	unsigned int hscx_base[2];
	unsigned int hscx_fifo[2];
	struct isac isac;

	struct hscx hscx[2];
};
