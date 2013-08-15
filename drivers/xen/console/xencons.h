#include <xen/evtchn.h>
#include <xen/xencons.h>

void xencons_force_flush(void);

/* Interrupt work hooks. Receive data, or kick data out. */
struct pt_regs;
void xencons_rx(char *buf, unsigned len);
void xencons_tx(void);

int xencons_ring_init(void);
int xencons_ring_send(const char *data, unsigned len);
