/* Various gunk just to reboot the machine. */ 
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


/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

static long no_idt[3];
static int reboot_mode;

#ifdef CONFIG_SMP
int reboot_smp = 0;
static int reboot_cpu = -1;
#endif

static int __init reboot_setup(char *str)
{
	while(1) {
		switch (*str) {
		case 'w': /* "warm" reboot (no memory testing etc) */
			reboot_mode = 0x1234;
			break;
		case 'c': /* "cold" reboot (with memory testing etc) */
			reboot_mode = 0x0;
			break;
#ifdef CONFIG_SMP
		case 's': /* "smp" reboot by executing reset on BSP or other CPU*/
			reboot_smp = 1;
			if (isdigit(str[1]))
				sscanf(str+1, "%d", &reboot_cpu);		
			else if (!strncmp(str,"smp",3))
				sscanf(str+3, "%d", &reboot_cpu); 
				/* we will leave sorting out the final value 
				when we are ready to reboot, since we might not
 				have set up boot_cpu_id or smp_num_cpu */
			break;
#endif
		}
		if((str = strchr(str,',')) != NULL)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);

static inline void kb_wait(void)
{
	int i;

	for (i=0; i<0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

void machine_restart(char * __unused)
{
#if CONFIG_SMP
	int cpuid;
	
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	if (reboot_smp) {

		/* check to see if reboot_cpu is valid 
		   if its not, default to the BSP */
		if ((reboot_cpu == -1) ||  
		      (reboot_cpu > (NR_CPUS -1))  || 
		      !(phys_cpu_present_map & (1<<cpuid))) 
			reboot_cpu = boot_cpu_id;

		reboot_smp = 0;  /* use this as a flag to only go through this once*/
		/* re-run this function on the other CPUs
		   it will fall though this section since we have 
		   cleared reboot_smp, and do the reboot if it is the
		   correct CPU, otherwise it halts. */
		if (reboot_cpu != cpuid)
			smp_call_function((void *)machine_restart , NULL, 1, 0);
	}

	/* if reboot_cpu is still -1, then we want a tradional reboot, 
	   and if we are not running on the reboot_cpu,, halt */
	if ((reboot_cpu != -1) && (cpuid != reboot_cpu)) {
		for (;;)
		__asm__ __volatile__ ("hlt");
	}
	/*
	 * Stop all CPUs and turn off local APICs and the IO-APIC, so
	 * other OSs see a clean IRQ state.
	 */
	smp_send_stop();
	disable_IO_APIC();
#endif
	
	/* rebooting needs to touch the page at absolute addr 0 */
	*((unsigned short *)__va(0x472)) = reboot_mode;
	for (;;) {
		int i;
		/* First fondle with the keyboard controller. */ 
		for (i=0; i<100; i++) {
			kb_wait();
			udelay(50);
			outb(0xfe,0x64);         /* pulse reset low */
			udelay(50);
		}

		/* Could do reset through the northbridge of Hammer here. */

		/* That didn't work - force a triple fault.. */
		__asm__ __volatile__("lidt %0": :"m" (no_idt));
		__asm__ __volatile__("int3");
	}      
}

void machine_halt(void)
{
}

void machine_power_off(void)
{
	if (pm_power_off)
		pm_power_off();
}
