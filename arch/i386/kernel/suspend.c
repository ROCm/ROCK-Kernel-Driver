/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sysrq.h>
#include <linux/compatmac.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <asm/uaccess.h>
#include <asm/acpi.h>
#include <asm/tlbflush.h>

static struct saved_context saved_context;

/*
 * save_processor_context
 * 
 * Save the state of the processor before we go to sleep.
 *
 * return_stack is the value of the stack pointer (%esp) as the caller sees it.
 * A good way could not be found to obtain it from here (don't want to make _too_
 * many assumptions about the layout of the stack this far down.) Also, the 
 * handy little __builtin_frame_pointer(level) where level > 0, is blatantly 
 * buggy - it returns the value of the stack at the proper location, not the 
 * location, like it should (as of gcc 2.91.66)
 * 
 * Note that the context and timing of this function is pretty critical.
 * With a minimal amount of things going on in the caller and in here, gcc
 * does a good job of being just a dumb compiler.  Watch the assembly output
 * if anything changes, though, and make sure everything is going in the right
 * place. 
 */
static inline void save_processor_context (void)
{
	kernel_fpu_begin();

	/*
	 * descriptor tables
	 */
	asm volatile ("sgdt (%0)" : "=m" (saved_context.gdt_limit));
	asm volatile ("sidt (%0)" : "=m" (saved_context.idt_limit));
	asm volatile ("sldt (%0)" : "=m" (saved_context.ldt));
	asm volatile ("str (%0)"  : "=m" (saved_context.tr));

	/*
	 * save the general registers.
	 * note that gcc has constructs to specify output of certain registers,
	 * but they're not used here, because it assumes that you want to modify
	 * those registers, so it tries to be smart and save them beforehand.
	 * It's really not necessary, and kinda fishy (check the assembly output),
	 * so it's avoided. 
	 */
	asm volatile ("movl %%esp, (%0)" : "=m" (saved_context.esp));
	asm volatile ("movl %%eax, (%0)" : "=m" (saved_context.eax));
	asm volatile ("movl %%ebx, (%0)" : "=m" (saved_context.ebx));
	asm volatile ("movl %%ecx, (%0)" : "=m" (saved_context.ecx));
	asm volatile ("movl %%edx, (%0)" : "=m" (saved_context.edx));
	asm volatile ("movl %%ebp, (%0)" : "=m" (saved_context.ebp));
	asm volatile ("movl %%esi, (%0)" : "=m" (saved_context.esi));
	asm volatile ("movl %%edi, (%0)" : "=m" (saved_context.edi));

	/*
	 * segment registers
	 */
	asm volatile ("movw %%es, %0" : "=r" (saved_context.es));
	asm volatile ("movw %%fs, %0" : "=r" (saved_context.fs));
	asm volatile ("movw %%gs, %0" : "=r" (saved_context.gs));
	asm volatile ("movw %%ss, %0" : "=r" (saved_context.ss));

	/*
	 * control registers 
	 */
	asm volatile ("movl %%cr0, %0" : "=r" (saved_context.cr0));
	asm volatile ("movl %%cr2, %0" : "=r" (saved_context.cr2));
	asm volatile ("movl %%cr3, %0" : "=r" (saved_context.cr3));
	asm volatile ("movl %%cr4, %0" : "=r" (saved_context.cr4));

	/*
	 * eflags
	 */
	asm volatile ("pushfl ; popl (%0)" : "=m" (saved_context.eflags));
}

static void
do_fpu_end(void)
{
        /* restore FPU regs if necessary */
	/* Do it out of line so that gcc does not move cr0 load to some stupid place */
        kernel_fpu_end();
}

/*
 * restore_processor_context
 * 
 * Restore the processor context as it was before we went to sleep
 * - descriptor tables
 * - control registers
 * - segment registers
 * - flags
 * 
 * Note that it is critical that this function is declared inline.  
 * It was separated out from restore_state to make that function
 * a little clearer, but it needs to be inlined because we won't have a
 * stack when we get here (so we can't push a return address).
 */
static inline void restore_processor_context (void)
{
	/*
	 * first restore %ds, so we can access our data properly
	 */
	asm volatile (".align 4");
	asm volatile ("movw %0, %%ds" :: "r" ((u16)__KERNEL_DS));


	/*
	 * control registers
	 */
	asm volatile ("movl %0, %%cr4" :: "r" (saved_context.cr4));
	asm volatile ("movl %0, %%cr3" :: "r" (saved_context.cr3));
	asm volatile ("movl %0, %%cr2" :: "r" (saved_context.cr2));
	asm volatile ("movl %0, %%cr0" :: "r" (saved_context.cr0));
	
	/*
	 * segment registers
	 */
	asm volatile ("movw %0, %%es" :: "r" (saved_context.es));
	asm volatile ("movw %0, %%fs" :: "r" (saved_context.fs));
	asm volatile ("movw %0, %%gs" :: "r" (saved_context.gs));
	asm volatile ("movw %0, %%ss" :: "r" (saved_context.ss));

	/*
	 * the other general registers
	 *
	 * note that even though gcc has constructs to specify memory 
	 * input into certain registers, it will try to be too smart
	 * and save them at the beginning of the function.  This is esp.
	 * bad since we don't have a stack set up when we enter, and we 
	 * want to preserve the values on exit. So, we set them manually.
	 */
	asm volatile ("movl %0, %%esp" :: "m" (saved_context.esp));
	asm volatile ("movl %0, %%ebp" :: "m" (saved_context.ebp));
	asm volatile ("movl %0, %%eax" :: "m" (saved_context.eax));
	asm volatile ("movl %0, %%ebx" :: "m" (saved_context.ebx));
	asm volatile ("movl %0, %%ecx" :: "m" (saved_context.ecx));
	asm volatile ("movl %0, %%edx" :: "m" (saved_context.edx));
	asm volatile ("movl %0, %%esi" :: "m" (saved_context.esi));
	asm volatile ("movl %0, %%edi" :: "m" (saved_context.edi));

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	asm volatile ("lgdt (%0)" :: "m" (saved_context.gdt_limit));
	asm volatile ("lidt (%0)" :: "m" (saved_context.idt_limit));
	asm volatile ("lldt (%0)" :: "m" (saved_context.ldt));

	fix_processor_context();

	/*
	 * the flags
	 */
	asm volatile ("pushl %0 ; popfl" :: "m" (saved_context.eflags));

	do_fpu_end();
}

#ifdef CONFIG_ACPI_SLEEP
void do_suspend_lowlevel(int resume)
{
/*
 * FIXME: This function should really be written in assembly. Actually
 * requirement is that it does not touch stack, because %esp will be
 * wrong during resume before restore_processor_context(). Check
 * assembly if you modify this.
 */
	if (!resume) {
		save_processor_context();
		acpi_save_register_state((unsigned long)&&acpi_sleep_done);
		acpi_enter_sleep_state(3);
		return;
	}
acpi_sleep_done:
	acpi_restore_register_state();
	restore_processor_context();
}
#endif

void fix_processor_context(void)
{
	int nr = smp_processor_id();
	struct tss_struct * t = &init_tss[nr];

	set_tss_desc(nr,t);	/* This just modifies memory; should not be neccessary. But... This is neccessary, because 386 hardware has concept of busy tsc or some similar stupidity. */
        gdt_table[__TSS(nr)].b &= 0xfffffdff;

	load_TR(nr);		/* This does ltr */

	load_LDT(&current->mm->context);	/* This does lldt */

	/*
	 * Now maybe reload the debug registers
	 */
	if (current->thread.debugreg[7]){
                loaddebug(&current->thread, 0);
                loaddebug(&current->thread, 1);
                loaddebug(&current->thread, 2);
                loaddebug(&current->thread, 3);
                /* no 4 and 5 */
                loaddebug(&current->thread, 6);
                loaddebug(&current->thread, 7);
	}

}


#ifdef CONFIG_SOFTWARE_SUSPEND
/* Local variables for do_magic */
static int loop __nosavedata = 0;
static int loop2 __nosavedata = 0;

/*
 * (KG): Since we affect stack here, we make this function as flat and easy
 * as possible in order to not provoke gcc to use local variables on the stack.
 * Note that on resume, all (expect nosave) variables will have the state from
 * the time of writing (suspend_save_image) and the registers (including the
 * stack pointer, but excluding the instruction pointer) will be loaded with 
 * the values saved at save_processor_context() time.
 */
void do_magic(int resume)
{
	/* DANGER WILL ROBINSON!
	 *
	 * If this function is too difficult for gcc to optimize, it will crash and burn!
	 * see above.
	 *
	 * DO NOT TOUCH.
	 */

	if (!resume) {
		do_magic_suspend_1();
		save_processor_context();	/* We need to capture registers and memory at "same time" */
		do_magic_suspend_2();		/* If everything goes okay, this function does not return */
		return;
	}

	/* We want to run from swapper_pg_dir, since swapper_pg_dir is stored in constant
	 * place in memory 
	 */

        __asm__( "movl %%ecx,%%cr3\n" ::"c"(__pa(swapper_pg_dir)));

/*
 * Final function for resuming: after copying the pages to their original
 * position, it restores the register state.
 *
 * What about page tables? Writing data pages may toggle
 * accessed/dirty bits in our page tables. That should be no problems
 * with 4MB page tables. That's why we require have_pse.  
 *
 * This loops destroys stack from under itself, so it better should
 * not use any stack space, itself. When this function is entered at
 * resume time, we move stack to _old_ place.  This is means that this
 * function must use no stack and no local variables in registers,
 * until calling restore_processor_context();
 *
 * Critical section here: noone should touch saved memory after
 * do_magic_resume_1; copying works, because nr_copy_pages,
 * pagedir_nosave, loop and loop2 are nosavedata.
 */
	do_magic_resume_1();

	for (loop=0; loop < nr_copy_pages; loop++) {
		/* You may not call something (like copy_page) here: see above */
		for (loop2=0; loop2 < PAGE_SIZE; loop2++) {
			*(((char *)((pagedir_nosave+loop)->orig_address))+loop2) =
				*(((char *)((pagedir_nosave+loop)->address))+loop2);
			__flush_tlb();
		}
	}

	restore_processor_context();

/* Ahah, we now run with our old stack, and with registers copied from
   suspend time */

	do_magic_resume_2();
}
#endif
