#ifndef __ALPHA_MACHVEC_H
#define __ALPHA_MACHVEC_H 1

#include <linux/config.h>
#include <linux/types.h>

/*
 *	This file gets pulled in by asm/io.h from user space. We don't
 *	want most of this escaping.
 */
 
#ifdef __KERNEL__

/* The following structure vectors all of the I/O and IRQ manipulation
   from the generic kernel to the hardware specific backend.  */

struct task_struct;
struct mm_struct;
struct pt_regs;
struct vm_area_struct;
struct linux_hose_info;
struct pci_dev;
struct pci_ops;
struct pci_controler;

struct alpha_machine_vector
{
	/* This "belongs" down below with the rest of the runtime
	   variables, but it is convenient for entry.S if these 
	   two slots are at the beginning of the struct.  */
	unsigned long hae_cache;
	unsigned long *hae_register;

	int nr_irqs;
	int rtc_port;
	int max_asn;
	unsigned long max_dma_address;
	unsigned long irq_probe_mask;
	unsigned long iack_sc;
	unsigned long min_io_address;
	unsigned long min_mem_address;

	void (*mv_pci_tbi)(struct pci_controler *hose,
			   dma_addr_t start, dma_addr_t end);

	unsigned int (*mv_inb)(unsigned long);
	unsigned int (*mv_inw)(unsigned long);
	unsigned int (*mv_inl)(unsigned long);

	void (*mv_outb)(unsigned char, unsigned long);
	void (*mv_outw)(unsigned short, unsigned long);
	void (*mv_outl)(unsigned int, unsigned long);
	
	unsigned long (*mv_readb)(unsigned long);
	unsigned long (*mv_readw)(unsigned long);
	unsigned long (*mv_readl)(unsigned long);
	unsigned long (*mv_readq)(unsigned long);

	void (*mv_writeb)(unsigned char, unsigned long);
	void (*mv_writew)(unsigned short, unsigned long);
	void (*mv_writel)(unsigned int, unsigned long);
	void (*mv_writeq)(unsigned long, unsigned long);

	unsigned long (*mv_ioremap)(unsigned long);
	int (*mv_is_ioaddr)(unsigned long);

	void (*mv_switch_mm)(struct mm_struct *, struct mm_struct *,
			     struct task_struct *, long);
	void (*mv_activate_mm)(struct mm_struct *, struct mm_struct *);

	void (*mv_flush_tlb_current)(struct mm_struct *);
	void (*mv_flush_tlb_current_page)(struct mm_struct * mm,
					  struct vm_area_struct *vma,
					  unsigned long addr);

	void (*update_irq_hw)(unsigned long, unsigned long, int);
	void (*ack_irq)(unsigned long);
	void (*device_interrupt)(unsigned long vector, struct pt_regs *regs);
	void (*machine_check)(u64 vector, u64 la, struct pt_regs *regs);

	void (*init_arch)(void);
	void (*init_irq)(void);
	void (*init_rtc)(void);
	void (*init_pci)(void);
	void (*kill_arch)(int);

	u8 (*pci_swizzle)(struct pci_dev *, u8 *);
	int (*pci_map_irq)(struct pci_dev *, u8, u8);
	struct pci_ops *pci_ops;

	const char *vector_name;

	/* System specific parameters.  */
	union {
	    struct {
		unsigned long gru_int_req_bits;
	    } cia;

	    struct {
		unsigned long gamma_bias;
	    } t2;

	    struct {
		unsigned int route_tab;
	    } sio;
	} sys;
};

extern struct alpha_machine_vector alpha_mv;

#ifdef CONFIG_ALPHA_GENERIC
extern int alpha_using_srm;
#else
#ifdef CONFIG_ALPHA_SRM
#define alpha_using_srm 1
#else
#define alpha_using_srm 0
#endif
#endif /* GENERIC */

#endif
#endif /* __ALPHA_MACHVEC_H */
