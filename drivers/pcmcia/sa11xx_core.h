/*
 * linux/include/asm/arch/pcmcia.h
 *
 * Copyright (C) 2000 John G Dorsey <john+@cs.cmu.edu>
 *
 * This file contains definitions for the low-level SA-1100 kernel PCMCIA
 * interface. Please see linux/Documentation/arm/SA1100/PCMCIA for details.
 */
#ifndef _ASM_ARCH_PCMCIA
#define _ASM_ARCH_PCMCIA

/* include the world */
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/ss.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

struct device;

/* Ideally, we'd support up to MAX_SOCK sockets, but the SA-1100 only
 * has support for two. This shows up in lots of hardwired ways, such
 * as the fact that MECR only has enough bits to configure two sockets.
 * Since it's so entrenched in the hardware, limiting the software
 * in this way doesn't seem too terrible.
 */
#define SA1100_PCMCIA_MAX_SOCK   (2)

struct pcmcia_state {
  unsigned detect: 1,
            ready: 1,
             bvd1: 1,
             bvd2: 1,
           wrprot: 1,
            vs_3v: 1,
            vs_Xv: 1;
};

/*
 * This structure encapsulates per-socket state which we might need to
 * use when responding to a Card Services query of some kind.
 */
struct sa1100_pcmcia_socket {
	struct pcmcia_socket	socket;

	/*
	 * Info from low level handler
	 */
	struct device		*dev;
	unsigned int		nr;
	unsigned int		irq;

	/*
	 * Core PCMCIA state
	 */
	struct pcmcia_low_level *ops;

	unsigned int		status;
	socket_state_t		cs_state;

	unsigned short		spd_io[MAX_IO_WIN];
	unsigned short		spd_mem[MAX_WIN];
	unsigned short		spd_attr[MAX_WIN];

	struct resource		res_skt;
	struct resource		res_io;
	struct resource		res_mem;
	struct resource		res_attr;
	void			*virt_io;

	unsigned int		irq_state;

	struct timer_list	poll_timer;
};

struct pcmcia_low_level {
	struct module *owner;

	int (*hw_init)(struct sa1100_pcmcia_socket *);
	void (*hw_shutdown)(struct sa1100_pcmcia_socket *);

	void (*socket_state)(struct sa1100_pcmcia_socket *, struct pcmcia_state *);
	int (*configure_socket)(struct sa1100_pcmcia_socket *, const socket_state_t *);

	/*
	 * Enable card status IRQs on (re-)initialisation.  This can
	 * be called at initialisation, power management event, or
	 * pcmcia event.
	 */
	void (*socket_init)(struct sa1100_pcmcia_socket *);

	/*
	 * Disable card status IRQs and PCMCIA bus on suspend.
	 */
	void (*socket_suspend)(struct sa1100_pcmcia_socket *);

	/*
	 * Calculate MECR timing clock wait states
	 */
	unsigned int (*socket_get_timing)(struct sa1100_pcmcia_socket *,
			unsigned int cpu_speed, unsigned int cmd_time);
};

struct pcmcia_irqs {
	int sock;
	int irq;
	const char *str;
};

int sa11xx_request_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr);
void sa11xx_free_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr);
void sa11xx_disable_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr);
void sa11xx_enable_irqs(struct sa1100_pcmcia_socket *skt, struct pcmcia_irqs *irqs, int nr);

extern int sa11xx_drv_pcmcia_probe(struct device *dev, struct pcmcia_low_level *ops, int first, int nr);
extern int sa11xx_drv_pcmcia_remove(struct device *dev);

#endif
