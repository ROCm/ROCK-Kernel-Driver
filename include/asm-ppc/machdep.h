#ifdef __KERNEL__
#ifndef _PPC_MACHDEP_H
#define _PPC_MACHDEP_H

#include <linux/config.h>

#ifdef CONFIG_APUS
#include <asm-m68k/machdep.h>
#endif

struct pt_regs;
struct pci_bus;	

struct machdep_calls {
	void		(*setup_arch)(void);
	/* Optional, may be NULL. */
	int		(*setup_residual)(char *buffer);
	/* Optional, may be NULL. */
	int		(*get_cpuinfo)(char *buffer);
	/* Optional, may be NULL. */
	unsigned int	(*irq_cannonicalize)(unsigned int irq);
	void		(*init_IRQ)(void);
	int		(*get_irq)(struct pt_regs *);
	void		(*post_irq)( struct pt_regs *, int );
	
	/* A general init function, called by ppc_init in init/main.c.
	   May be NULL. */
	void		(*init)(void);

	void		(*restart)(char *cmd);
	void		(*power_off)(void);
	void		(*halt)(void);

	long		(*time_init)(void); /* Optional, may be NULL */
	int		(*set_rtc_time)(unsigned long nowtime);
	unsigned long	(*get_rtc_time)(void);
	void		(*calibrate_decr)(void);

	void		(*heartbeat)(void);
	unsigned long	heartbeat_reset;
	unsigned long	heartbeat_count;

  	void		(*progress)(char *, unsigned short);

	unsigned char 	(*nvram_read_val)(int addr);
	void		(*nvram_write_val)(int addr, unsigned char val);

/* Tons of keyboard stuff. */
	int		(*kbd_setkeycode)(unsigned int scancode,
				unsigned int keycode);
	int		(*kbd_getkeycode)(unsigned int scancode);
	int		(*kbd_translate)(unsigned char scancode,
				unsigned char *keycode,
				char raw_mode);
	char		(*kbd_unexpected_up)(unsigned char keycode);
	void		(*kbd_leds)(unsigned char leds);
	void		(*kbd_init_hw)(void);
#ifdef CONFIG_MAGIC_SYSRQ
	unsigned char 	*ppc_kbd_sysrq_xlate;
#endif

	/* PCI interfaces */
	int (*pcibios_read_config_byte)(unsigned char bus,
		unsigned char dev_fn, unsigned char offset, unsigned char *val);
	int (*pcibios_read_config_word)(unsigned char bus,
		unsigned char dev_fn, unsigned char offset, unsigned short *val);
	int (*pcibios_read_config_dword)(unsigned char bus,
		unsigned char dev_fn, unsigned char offset, unsigned int *val);
	int (*pcibios_write_config_byte)(unsigned char bus,
		unsigned char dev_fn, unsigned char offset, unsigned char val);
	int (*pcibios_write_config_word)(unsigned char bus, 
		unsigned char dev_fn, unsigned char offset, unsigned short val);
	int (*pcibios_write_config_dword)(unsigned char bus,
		unsigned char dev_fn, unsigned char offset, unsigned int val);
	void (*pcibios_fixup)(void);
	void (*pcibios_fixup_bus)(struct pci_bus *);

	void* (*pci_dev_io_base)(unsigned char bus, unsigned char devfn, int physical);
	void* (*pci_dev_mem_base)(unsigned char bus, unsigned char devfn);
	int (*pci_dev_root_bridge)(unsigned char bus, unsigned char devfn);

	/* this is for modules, since _machine can be a define -- Cort */
	int ppc_machine;
};

extern struct machdep_calls ppc_md;
extern char cmd_line[512];

extern void setup_pci_ptrs(void);

/*
 * Power macintoshes have either a CUDA or a PMU controlling
 * system reset, power, NVRAM, RTC.
 */
typedef enum sys_ctrler_kind {
	SYS_CTRLER_UNKNOWN = 0,
	SYS_CTRLER_CUDA = 1,
	SYS_CTRLER_PMU = 2,
} sys_ctrler_t;

extern sys_ctrler_t sys_ctrler;

#endif /* _PPC_MACHDEP_H */
#endif /* __KERNEL__ */
