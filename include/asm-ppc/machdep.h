#ifdef __KERNEL__
#ifndef _PPC_MACHDEP_H
#define _PPC_MACHDEP_H

#include <linux/config.h>

#ifdef CONFIG_APUS
#include <asm-m68k/machdep.h>
#endif

struct pt_regs;
struct pci_bus;	
struct pci_dev;

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

	unsigned long	(*find_end_of_memory)(void);
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

	/*
	 * optional PCI "hooks"
	 */

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

		/* Called at then very end of pcibios_init()
		 */
	void (*pcibios_after_init)(void);
		 

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
