/* Various gunk just to reboot the machine. */ 
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/kdebug.h>
#include <asm/delay.h>
#include <asm/hw_irq.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/apic.h>

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

static long no_idt[3];
static enum { 
	BOOT_BIOS = 'b',
	BOOT_TRIPLE = 't',
	BOOT_KBD = 'k'
} reboot_type = BOOT_KBD;
static int reboot_mode = 0;

/* reboot=b[ios] | t[riple] | k[bd] [, [w]arm | [c]old]
   bios	  Use the CPU reboot vector for warm reset
   warm   Don't set the cold reboot flag
   cold   Set the cold reboot flag
   triple Force a triple fault (init)
   kbd    Use the keyboard controller. cold reset (default)
 */ 
static int __init reboot_setup(char *str)
{
	for (;;) {
		switch (*str) {
		case 'w': 
			reboot_mode = 0x1234;
			break;

		case 'c':
			reboot_mode = 0;
			break;

		case 't':
		case 'b':
		case 'k':
			reboot_type = *str;
			break;
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);

/* overwrites random kernel memory. Should not be kernel .text */
#define WARMBOOT_TRAMP 0x1000UL

static void reboot_warm(void)
{
	extern unsigned char warm_reboot[], warm_reboot_end[];
	printk("warm reboot\n");

	local_irq_disable(); 
		
	/* restore identity mapping */
	init_level4_pgt[0] = __pml4(__pa(level3_ident_pgt) | 7); 
	__flush_tlb_all(); 

	/* Move the trampoline to low memory */
	memcpy(__va(WARMBOOT_TRAMP), warm_reboot, warm_reboot_end - warm_reboot); 

	/* Start it in compatibility mode. */
	asm volatile( "   pushq $0\n" 		/* ss */
		     "   pushq $0x2000\n" 	/* rsp */
	             "   pushfq\n"		/* eflags */
		     "   pushq %[cs]\n"
		     "   pushq %[target]\n"
		     "   iretq" :: 
		      [cs] "i" (__KERNEL_COMPAT32_CS), 
		      [target] "b" (WARMBOOT_TRAMP));
}

#ifdef CONFIG_SMP
static void smp_halt(void)
{
	int cpuid = safe_smp_processor_id(); 
		static int first_entry = 1;

		if (first_entry) { 
			first_entry = 0;
			smp_call_function((void *)machine_restart, NULL, 1, 0);
		} 
			
	smp_stop_cpu(); 

	/* AP calling this. Just halt */
	if (cpuid != boot_cpu_id) { 
		for (;;) 
			asm("hlt");
	}

	/* Wait for all other CPUs to have run smp_stop_cpu */
	while (!cpus_empty(cpu_online_map))
		rep_nop(); 
}
#endif

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_restart(char * __unused)
{
	int i;

#ifdef CONFIG_SMP
	smp_halt(); 
#endif

	local_irq_disable();
       
#ifndef CONFIG_SMP
	disable_local_APIC();
#endif

	disable_IO_APIC();
	
	local_irq_enable();
	
	/* Tell the BIOS if we want cold or warm reboot */
	*((unsigned short *)__va(0x472)) = reboot_mode;
       
	for (;;) {
		/* Could also try the reset bit in the Hammer NB */
		switch (reboot_type) { 
		case BOOT_BIOS:
			reboot_warm();

		case BOOT_KBD:
		for (i=0; i<100; i++) {
			kb_wait();
			udelay(50);
			outb(0xfe,0x64);         /* pulse reset low */
			udelay(50);
		}

		case BOOT_TRIPLE: 
		__asm__ __volatile__("lidt (%0)": :"r" (&no_idt));
		__asm__ __volatile__("int3");

			reboot_type = BOOT_KBD;
			break;
		}      
	}      
}

EXPORT_SYMBOL(machine_restart);

void machine_halt(void)
{
}

EXPORT_SYMBOL(machine_halt);

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}

EXPORT_SYMBOL(machine_power_off);
