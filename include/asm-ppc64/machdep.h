#ifdef __KERNEL__
#ifndef _PPC_MACHDEP_H
#define _PPC_MACHDEP_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/seq_file.h>

struct pt_regs;
struct pci_bus;	
struct pci_dev;
struct device_node;
struct TceTable;
struct rtc_time;

#ifdef CONFIG_SMP
struct smp_ops_t {
	void  (*message_pass)(int target, int msg, unsigned long data, int wait);
	int   (*probe)(void);
	void  (*kick_cpu)(int nr);
	void  (*setup_cpu)(int nr);
	void  (*take_timebase)(void);
	void  (*give_timebase)(void);
};
#endif

struct machdep_calls {
	void            (*hpte_invalidate)(unsigned long slot,
					   unsigned long va,
					   int large,
					   int local);
	long		(*hpte_updatepp)(unsigned long slot, 
					 unsigned long newpp, 
					 unsigned long va,
					 int large);
	void            (*hpte_updateboltedpp)(unsigned long newpp, 
					       unsigned long ea);
	long		(*insert_hpte)(unsigned long hpte_group,
				       unsigned long vpn,
				       unsigned long prpn,
				       int secondary, 
				       unsigned long hpteflags, 
				       int bolted,
				       int large);
	long		(*remove_hpte)(unsigned long hpte_group);
	void		(*flush_hash_range)(unsigned long context,
					    unsigned long number,
					    int local);
	void		(*make_pte)(void *htab, unsigned long va,
				    unsigned long pa,
				    int mode,
				    unsigned long hash_mask,
				    int large);

	void		(*tce_build)(struct TceTable * tbl,
				     long tcenum,
				     unsigned long uaddr,
				     int direction);
	void		(*tce_free_one)(struct TceTable *tbl,
				        long tcenum);    

	void		(*setup_arch)(void);
	/* Optional, may be NULL. */
	void		(*setup_residual)(struct seq_file *m, int cpu_id);
	/* Optional, may be NULL. */
	void		(*get_cpuinfo)(struct seq_file *m);
	/* Optional, may be NULL. */
	unsigned int	(*irq_cannonicalize)(unsigned int irq);
	void		(*init_IRQ)(void);
	void		(*init_ras_IRQ)(void);
	int		(*get_irq)(struct pt_regs *);
	
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. */
	void		(*init)(void);

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);

	long		(*time_init)(void); /* Optional, may be NULL */
	int		(*set_rtc_time)(struct rtc_time *);
	void		(*get_rtc_time)(struct rtc_time *);
	void		(*get_boot_time)(struct rtc_time *);
	void		(*calibrate_decr)(void);

  	void		(*progress)(char *, unsigned short);

	unsigned char 	(*nvram_read_val)(int addr);
	void		(*nvram_write_val)(int addr, unsigned char val);

	/* Debug interface.  Low level I/O to some terminal device */
	void		(*udbg_putc)(unsigned char c);
	unsigned char	(*udbg_getc)(void);
	int		(*udbg_getc_poll)(void);

	/* PCI interfaces */
	int (*pcibios_read_config)(struct device_node *dn, int where, int size, u32 *val);
	int (*pcibios_write_config)(struct device_node *dn, int where, int size, u32 val);

	/* Called after scanning the bus, before allocating
	 * resources
	 */
	void (*pcibios_fixup)(void);

       /* Called for each PCI bus in the system
        * when it's probed
        */
	void (*pcibios_fixup_bus)(struct pci_bus *);

       /* Called when pci_enable_device() is called (initial=0) or
        * when a device with no assigned resource is found (initial=1).
        * Returns 0 to allow assignement/enabling of the device
        */
        int  (*pcibios_enable_device_hook)(struct pci_dev *, int initial);

	void* (*pci_dev_io_base)(unsigned char bus, unsigned char devfn, int physical);
	void* (*pci_dev_mem_base)(unsigned char bus, unsigned char devfn);
	int (*pci_dev_root_bridge)(unsigned char bus, unsigned char devfn);

	/* this is for modules, since _machine can be a define -- Cort */
	int ppc_machine;

#ifdef CONFIG_SMP
	/* functions for dealing with other cpus */
	struct smp_ops_t smp_ops;
#endif /* CONFIG_SMP */
};

extern struct machdep_calls ppc_md;
extern char cmd_line[512];

extern void setup_pci_ptrs(void);

#endif /* _PPC_MACHDEP_H */
#endif /* __KERNEL__ */
