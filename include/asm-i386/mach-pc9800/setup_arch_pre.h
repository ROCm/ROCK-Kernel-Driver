/* Hook to call BIOS initialisation function */

/* no action for generic */

#define ARCH_SETUP arch_setup_pc9800();

#include <linux/timex.h>
#include <asm/io.h>
#include <asm/pc9800.h>
#include <asm/pc9800_sca.h>

int CLOCK_TICK_RATE;
extern unsigned long tick_usec;	/* ACTHZ          period (usec) */
extern unsigned long tick_nsec;	/* USER_HZ period (nsec) */
unsigned char pc9800_misc_flags;
/* (bit 0) 1:High Address Video ram exists 0:otherwise */

#ifdef CONFIG_SMP
#define MPC_TABLE_SIZE 512
#define MPC_TABLE ((char *) (PARAM+0x400))
char mpc_table[MPC_TABLE_SIZE];
#endif

static  inline void arch_setup_pc9800(void)
{
	CLOCK_TICK_RATE = PC9800_8MHz_P() ? 1996800 : 2457600;
	printk(KERN_DEBUG "CLOCK_TICK_RATE = %d\n", CLOCK_TICK_RATE);
	tick_usec = TICK_USEC; 		/* USER_HZ period (usec) */
	tick_nsec = TICK_NSEC;		/* ACTHZ period (nsec) */

	pc9800_misc_flags = PC9800_MISC_FLAGS;
#ifdef CONFIG_SMP
	if ((*(u32 *)(MPC_TABLE)) == 0x504d4350)
		memcpy(mpc_table, MPC_TABLE, *(u16 *)(MPC_TABLE + 4));
#endif /* CONFIG_SMP */
}
