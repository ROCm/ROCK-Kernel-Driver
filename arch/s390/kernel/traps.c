/*
 *  arch/s390/kernel/traps.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/traps.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/mathemu.h>
#if CONFIG_REMOTE_DEBUG
#include <asm/gdb-stub.h>
#endif

/* Called from entry.S only */
extern void handle_per_exception(struct pt_regs *regs);

typedef void pgm_check_handler_t(struct pt_regs *, long);
pgm_check_handler_t *pgm_check_table[128];

extern pgm_check_handler_t default_trap_handler;
extern pgm_check_handler_t do_page_fault;

asmlinkage int system_call(void);

#define DO_ERROR(trapnr, signr, str, name, tsk) \
asmlinkage void name(struct pt_regs * regs, long error_code) \
{ \
        tsk->thread.error_code = error_code; \
        tsk->thread.trap_no = trapnr; \
	die_if_no_fixup(str,regs,error_code); \
        force_sig(signr, tsk); \
}

/* TODO: define these as 'pgm_check_handler_t xxx;'
asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void double_fault(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void coprocessor_error(void);
asmlinkage void reserved(void);
asmlinkage void alignment_check(void);
asmlinkage void spurious_interrupt_bug(void);
*/

int kstack_depth_to_print = 24;

/*
 * These constants are for searching for possible module text
 * segments.  VMALLOC_OFFSET comes from mm/vmalloc.c; MODULE_RANGE is
 * a guess of how much space is likely to be vmalloced.
 */
#define VMALLOC_OFFSET (8*1024*1024)
#define MODULE_RANGE (8*1024*1024)

void show_crashed_task_info(void)
{
	printk("CPU:    %d\n",smp_processor_id());
	printk("Process %s (pid: %d, stackpage=%08X)\n",
                current->comm, current->pid, 4096+(addr_t)current);
	show_regs(current,NULL,NULL);
}
#if 0
static void show_registers(struct pt_regs *regs)
{
        printk("CPU:    %d\nPSW:    %08lx %08lx\n",
                smp_processor_id(), (unsigned long) regs->psw.mask,
                (unsigned long) regs->psw.addr);
        printk("GPRS:\n");

        printk("%08lx  %08lx  %08lx  %08lx\n",
                regs->gprs[0], regs->gprs[1],
                regs->gprs[2], regs->gprs[3]);
        printk("%08lx  %08lx  %08lx  %08lx\n",
                regs->gprs[4], regs->gprs[5],
                regs->gprs[6], regs->gprs[7]);
        printk("%08lx  %08lx  %08lx  %08lx\n",
                regs->gprs[8], regs->gprs[9],
                regs->gprs[10], regs->gprs[11]);
        printk("%08lx  %08lx  %08lx  %08lx\n",
                regs->gprs[12], regs->gprs[13],
                regs->gprs[14], regs->gprs[15]);
        printk("Process %s (pid: %d, stackpage=%08lx)\nStack: ",
                current->comm, current->pid, 4096+(unsigned long)current);
/*
        stack = (unsigned long *) esp;
        for(i=0; i < kstack_depth_to_print; i++) {
                if (((long) stack & 4095) == 0)
                        break;
                if (i && ((i % 8) == 0))
                        printk("\n       ");
                printk("%08lx ", get_seg_long(ss,stack++));
        }
        printk("\nCall Trace: ");
        stack = (unsigned long *) esp;
        i = 1;
        module_start = PAGE_OFFSET + (max_mapnr << PAGE_SHIFT);
        module_start = ((module_start + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1));
        module_end = module_start + MODULE_RANGE;
        while (((long) stack & 4095) != 0) {
                addr = get_seg_long(ss, stack++); */
                /*
                 * If the address is either in the text segment of the
                 * kernel, or in the region which contains vmalloc'ed
                 * memory, it *may* be the address of a calling
                 * routine; if so, print it so that someone tracing
                 * down the cause of the crash will be able to figure
                 * out the call path that was taken.
                 */
/*                if (((addr >= (unsigned long) &_stext) &&
                     (addr <= (unsigned long) &_etext)) ||
                    ((addr >= module_start) && (addr <= module_end))) {
                        if (i && ((i % 8) == 0))
                                printk("\n       ");
                        printk("[<%08lx>] ", addr);
                        i++;
                }
        }
        printk("\nCode: ");
        for(i=0;i<20;i++)
                printk("%02x ",0xff & get_seg_byte(regs->xcs & 0xffff,(i+(char *)regs->eip)));
        printk("\n");
*/
}
#endif


spinlock_t die_lock;

void die(const char * str, struct pt_regs * regs, long err)
{
        console_verbose();
        spin_lock_irq(&die_lock);
        printk("%s: %04lx\n", str, err & 0xffff);
        show_crashed_task_info();
        spin_unlock_irq(&die_lock);
        do_exit(SIGSEGV);
}

int check_for_fixup(struct pt_regs * regs)
{
        if (!(regs->psw.mask & PSW_PROBLEM_STATE)) {
		unsigned long fixup;
		fixup = search_exception_table(regs->psw.addr);
		if (fixup) {
			regs->psw.addr = fixup;
			return 1;
		}
	}
	return 0;
}

int do_debugger_trap(struct pt_regs *regs,int signal)
{
	if(regs->psw.mask&PSW_PROBLEM_STATE)
	{
		if(current->flags & PF_PTRACED)
			force_sig(signal,current);
		else
			return 1;
	}
	else
	{
#if CONFIG_REMOTE_DEBUG
		if(gdb_stub_initialised)
		{
			gdb_stub_handle_exception((gdb_pt_regs *)regs,signal);
			return 0;
		}
#endif
		return 1;
	}
	return 0;
}

static void die_if_no_fixup(const char * str, struct pt_regs * regs, long err)
{
	if (!(regs->psw.mask & PSW_PROBLEM_STATE)) {
		unsigned long fixup;
		fixup = search_exception_table(regs->psw.addr);
		if (fixup) {
			regs->psw.addr = fixup;
			return;
		}
		die(str, regs, err);
	}
}

asmlinkage void default_trap_handler(struct pt_regs * regs, long error_code)
{
        current->thread.error_code = error_code;
        current->thread.trap_no = error_code;
        die_if_no_fixup("Unknown program exception",regs,error_code);
        force_sig(SIGSEGV, current);
}

DO_ERROR(2, SIGILL, "privileged operation", privileged_op, current)
DO_ERROR(3, SIGILL, "execute exception", execute_exception, current)
DO_ERROR(5, SIGSEGV, "addressing exception", addressing_exception, current)
DO_ERROR(9, SIGFPE, "fixpoint divide exception", divide_exception, current)
DO_ERROR(0x12, SIGILL, "translation exception", translation_exception, current)
DO_ERROR(0x13, SIGILL, "special operand exception", special_op_exception, current)
DO_ERROR(0x15, SIGILL, "operand exception", operand_exception, current)

/* need to define
DO_ERROR( 6, SIGILL,  "invalid operand", invalid_op, current)
DO_ERROR( 8, SIGSEGV, "double fault", double_fault, current)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun, last_task_used_math)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS, current)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present, current)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment, current)
DO_ERROR(17, SIGSEGV, "alignment check", alignment_check, current)
DO_ERROR(18, SIGSEGV, "reserved", reserved, current)
DO_ERROR(19, SIGSEGV, "cache flush denied", cache_flush_denied, current)
*/

#ifdef CONFIG_IEEEFPU_EMULATION

asmlinkage void illegal_op(struct pt_regs * regs, long error_code)
{
        __u8 opcode[6];
	__u16 *location;
	int do_sig = 0;
	int problem_state=(regs->psw.mask & PSW_PROBLEM_STATE);

        lock_kernel();
	location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
	if(problem_state)
		get_user(*((__u16 *) opcode), location);
	else
		*((__u16 *)opcode)=*((__u16 *)location);
	if(*((__u16 *)opcode)==S390_BREAKPOINT_U16)
        {
		if(do_debugger_trap(regs,SIGTRAP))
			do_sig=1;
	}
        else if (problem_state )
	{
		if (opcode[0] == 0xb3) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			do_sig = math_emu_b3(opcode, regs);
                } else if (opcode[0] == 0xed) {
			get_user(*((__u32 *) (opcode+2)),
				 (__u32 *)(location+1));
			do_sig = math_emu_ed(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb299) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			do_sig = math_emu_srnm(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29c) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			do_sig = math_emu_stfpc(opcode, regs);
		} else if (*((__u16 *) opcode) == 0xb29d) {
			get_user(*((__u16 *) (opcode+2)), location+1);
			do_sig = math_emu_lfpc(opcode, regs);
		} else
			do_sig = 1;
        } else
		do_sig = 1;
	if (do_sig) {
		current->thread.error_code = error_code;
		current->thread.trap_no = 1;
		force_sig(SIGILL, current);
		die_if_no_fixup("illegal operation", regs, error_code);
        }
        unlock_kernel();
}

asmlinkage void specification_exception(struct pt_regs * regs, long error_code)
{
        __u8 opcode[6];
	__u16 *location;
	int do_sig = 0;

        lock_kernel();
        if (regs->psw.mask & 0x00010000L) {
		location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_le(opcode, regs);
			break;
		default:
			do_sig = 1;
			break;
                }
        } else
		do_sig = 1;
	if (do_sig) {
		current->thread.error_code = error_code;
		current->thread.trap_no = 1;
		force_sig(SIGILL, current);
		die_if_no_fixup("illegal operation", regs, error_code);
        }
        unlock_kernel();
}

asmlinkage void data_exception(struct pt_regs * regs, long error_code)
{
        __u8 opcode[6];
	__u16 *location;
	int do_sig = 0;

        lock_kernel();
        if (regs->psw.mask & 0x00010000L) {
		location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
		get_user(*((__u16 *) opcode), location);
		switch (opcode[0]) {
		case 0x28: /* LDR Rx,Ry   */
			math_emu_ldr(opcode);
			break;
		case 0x38: /* LER Rx,Ry   */
			math_emu_ler(opcode);
			break;
		case 0x60: /* STD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_std(opcode, regs);
			break;
		case 0x68: /* LD R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_ld(opcode, regs);
			break;
		case 0x70: /* STE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_ste(opcode, regs);
			break;
		case 0x78: /* LE R,D(X,B) */
			get_user(*((__u16 *) (opcode+2)), location+1);
			math_emu_le(opcode, regs);
			break;
		case 0xb3:
			get_user(*((__u16 *) (opcode+2)), location+1);
			do_sig = math_emu_b3(opcode, regs);
			break;
                case 0xed:
			get_user(*((__u32 *) (opcode+2)),
				 (__u32 *)(location+1));
			do_sig = math_emu_ed(opcode, regs);
			break;
	        case 0xb2:
			if (opcode[1] == 0x99) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				do_sig = math_emu_srnm(opcode, regs);
			} else if (opcode[1] == 0x9c) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				do_sig = math_emu_stfpc(opcode, regs);
			} else if (opcode[1] == 0x9d) {
				get_user(*((__u16 *) (opcode+2)), location+1);
				do_sig = math_emu_lfpc(opcode, regs);
			} else
				do_sig = 1;
			break;
		default:
			do_sig = 1;
			break;
                }
        } else
		do_sig = 1;
	if (do_sig) {
		current->thread.error_code = error_code;
		current->thread.trap_no = 1;
		force_sig(SIGILL, current);
		die_if_no_fixup("illegal operation", regs, error_code);
        }
        unlock_kernel();
}

#else
DO_ERROR(1, SIGILL, "illegal operation", illegal_op, current)
DO_ERROR(6, SIGILL, "specification exception", specification_exception, current)
DO_ERROR(7, SIGILL, "data exception", data_exception, current)
#endif /* CONFIG_IEEEFPU_EMULATION */


/* init is done in lowcore.S and head.S */

void __init trap_init(void)
{
        int i;

        for (i = 0; i < 128; i++)
          pgm_check_table[i] = &default_trap_handler;
        pgm_check_table[1] = &illegal_op;
        pgm_check_table[2] = &privileged_op;
        pgm_check_table[3] = &execute_exception;
        pgm_check_table[5] = &addressing_exception;
        pgm_check_table[6] = &specification_exception;
        pgm_check_table[7] = &data_exception;
        pgm_check_table[9] = &divide_exception;
        pgm_check_table[0x12] = &translation_exception;
        pgm_check_table[0x13] = &special_op_exception;
        pgm_check_table[0x15] = &operand_exception;
        pgm_check_table[4] = &do_page_fault;
        pgm_check_table[0x10] = &do_page_fault;
        pgm_check_table[0x11] = &do_page_fault;
        pgm_check_table[0x1C] = &privileged_op;
}


void handle_per_exception(struct pt_regs *regs)
{
	if(regs->psw.mask&PSW_PROBLEM_STATE)
	{
		per_struct *per_info=&current->thread.per_info;
		per_info->lowcore.words.perc_atmid=S390_lowcore.per_perc_atmid;
		per_info->lowcore.words.address=S390_lowcore.per_address;
		per_info->lowcore.words.access_id=S390_lowcore.per_access_id;
	}
	if(do_debugger_trap(regs,SIGTRAP))
	{
		/* I've seen this possibly a task structure being reused ? */
		printk("Spurious per exception detected\n");
		printk("switching off per tracing for this task.\n");
		show_crashed_task_info();
		/* Hopefully switching off per tracing will help us survive */
		regs->psw.mask &= ~PSW_PER_MASK;
	}
}

