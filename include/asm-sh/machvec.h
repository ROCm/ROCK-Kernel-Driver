/*
 * include/asm-sh/machvec.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#ifndef _ASM_SH_MACHVEC_H
#define _ASM_SH_MACHVEC_H 1

#include <linux/config.h>
#include <linux/types.h>

struct sh_machine_vector
{
	const char *mv_name;

	int mv_nr_irqs;

	unsigned long (*mv_inb)(unsigned int);
	unsigned long (*mv_inw)(unsigned int);
	unsigned long (*mv_inl)(unsigned int);
	void (*mv_outb)(unsigned long, unsigned int);
	void (*mv_outw)(unsigned long, unsigned int);
	void (*mv_outl)(unsigned long, unsigned int);

	unsigned long (*mv_inb_p)(unsigned int);
	unsigned long (*mv_inw_p)(unsigned int);
	unsigned long (*mv_inl_p)(unsigned int);
	void (*mv_outb_p)(unsigned long, unsigned int);
	void (*mv_outw_p)(unsigned long, unsigned int);
	void (*mv_outl_p)(unsigned long, unsigned int);

	void (*mv_insb)(unsigned int port, void *addr, unsigned long count);
	void (*mv_insw)(unsigned int port, void *addr, unsigned long count);
	void (*mv_insl)(unsigned int port, void *addr, unsigned long count);
	void (*mv_outsb)(unsigned int port, const void *addr, unsigned long count);
	void (*mv_outsw)(unsigned int port, const void *addr, unsigned long count);
	void (*mv_outsl)(unsigned int port, const void *addr, unsigned long count);
	
	unsigned long (*mv_readb)(unsigned long);
	unsigned long (*mv_readw)(unsigned long);
	unsigned long (*mv_readl)(unsigned long);
	void (*mv_writeb)(unsigned char, unsigned long);
	void (*mv_writew)(unsigned short, unsigned long);
	void (*mv_writel)(unsigned int, unsigned long);

	void* (*mv_ioremap)(unsigned long offset, unsigned long size);
	void* (*mv_ioremap_nocache)(unsigned long offset, unsigned long size);
	void (*mv_iounmap)(void *addr);

	unsigned long (*mv_port2addr)(unsigned long offset);
	unsigned long (*mv_isa_port2addr)(unsigned long offset);

	int (*mv_irq_demux)(int irq);

	void (*mv_init_arch)(void);
	void (*mv_init_irq)(void);
	void (*mv_init_pci)(void);
	void (*mv_kill_arch)(int);

	void (*mv_heartbeat)(void);

	unsigned int mv_hw_se : 1;
	unsigned int mv_hw_hp600 : 1;
	unsigned int mv_hw_hd64461 : 1;
};

extern struct sh_machine_vector sh_mv;

/* Machine check macros */
#ifdef CONFIG_SH_GENERIC
#define MACH_SE		(sh_mv.mv_hw_se)
#define MACH_HP600	(sh_mv.mv_hw_hp600)
#define MACH_HD64461	(sh_mv.mv_hw_hd64461)
#else
# ifdef CONFIG_SH_SOLUTION_ENGINE
#  define MACH_SE		1
# else
#  define MACH_SE		0
# endif
# ifdef CONFIG_SH_HP600
#  define MACH_HP600		1
# else
#  define MACH_HP600		0
# endif
# ifdef CONFIG_HD64461
#  define MACH_HD64461		1
# else
#  define MACH_HD64461		0
# endif
#endif

#endif /* _ASM_SH_MACHVEC_H */
