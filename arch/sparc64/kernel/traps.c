/* $Id: traps.c,v 1.68 2000/11/22 06:50:37 davem Exp $
 * arch/sparc64/kernel/traps.c
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997,1999,2000 Jakub Jelinek (jakub@redhat.com)
 */

/*
 * I like traps on v9, :))))
 */

#include <linux/config.h>
#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>
#include <asm/lsu.h>
#include <asm/psrcompat.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

/* #define SYSCALL_TRACING */
/* #define VERBOSE_SYSCALL_TRACING */
/* #define DEBUG_FPU */

#ifdef SYSCALL_TRACING
#ifdef VERBOSE_SYSCALL_TRACING
struct sdesc {
	int	scall_num;
	char	*name;
	int	num_args;
	char	arg_is_string[6];
} sdesc_entries[] = {
	{ 0, "setup", 0, },
	{ 1, "exit", 1, { 0, } },
	{ 2, "fork", 0, },
	{ 3, "read", 3, { 0, 0, 0, } },
	{ 4, "write", 3, { 0, 0, 0, } },
	{ 5, "open", 3, { 1, 0, 0, } },
	{ 6, "close", 1, { 0, } },
	{ 7, "wait4", 4, { 0, 0, 0, 0, } },
	{ 8, "creat", 2, { 1, 0, } },
	{ 9, "link", 2, { 1, 1, } },
	{ 10, "unlink", 1, { 1, } },
	{ 11, "execv", 2, { 1, 0, } },
	{ 12, "chdir", 1, { 1, } },
	{ 15, "chmod", 2, { 1, 0, } },
	{ 16, "chown", 3, { 1, 0, 0, } },
	{ 17, "brk", 1, { 0, } },
	{ 19, "lseek", 3, { 0, 0, 0, } },
	{ 27, "alarm", 1, { 0, } },
	{ 29, "pause", 0, },
	{ 33, "access", 2, { 1, 0, } },
	{ 36, "sync", 0, },
	{ 37, "kill", 2, { 0, 0, } },
	{ 38, "stat", 2, { 1, 0, } },
	{ 40, "lstat", 2, { 1, 0, } },
	{ 41, "dup", 1, { 0, } },
	{ 42, "pipd", 0, },
	{ 54, "ioctl", 3, { 0, 0, 0, } },
	{ 57, "symlink", 2, { 1, 1, } },
	{ 58, "readlink", 3, { 1, 0, 0, } },
	{ 59, "execve", 3, { 1, 0, 0, } },
	{ 60, "umask", 1, { 0, } },
	{ 62, "fstat", 2, { 0, 0, } },
	{ 64, "getpagesize", 0, },
	{ 71, "mmap", 6, { 0, 0, 0, 0, 0, 0, } },
	{ 73, "munmap", 2, { 0, 0, } },
	{ 74, "mprotect", 3, { 0, 0, 0, } },
	{ 83, "setitimer", 3, { 0, 0, 0, } },
	{ 90, "dup2", 2, { 0, 0, } },
	{ 92, "fcntl", 3, { 0, 0, 0, } },
	{ 93, "select", 5, { 0, 0, 0, 0, 0, } },
	{ 97, "socket", 3, { 0, 0, 0, } },
	{ 98, "connect", 3, { 0, 0, 0, } },
	{ 99, "accept", 3, { 0, 0, 0, } },
	{ 101, "send", 4, { 0, 0, 0, 0, } },
	{ 102, "recv", 4, { 0, 0, 0, 0, } },
	{ 104, "bind", 3, { 0, 0, 0, } },
	{ 105, "setsockopt", 5, { 0, 0, 0, 0, 0, } },
	{ 106, "listen", 2, { 0, 0, } },
	{ 120, "readv", 3, { 0, 0, 0, } },
	{ 121, "writev", 3, { 0, 0, 0, } },
	{ 123, "fchown", 3, { 0, 0, 0, } },
	{ 124, "fchmod", 2, { 0, 0, } },
	{ 128, "rename", 2, { 1, 1, } },
	{ 129, "truncate", 2, { 1, 0, } },
	{ 130, "ftruncate", 2, { 0, 0, } },
	{ 131, "flock", 2, { 0, 0, } },
	{ 136, "mkdir", 2, { 1, 0, } },
	{ 137, "rmdir", 1, { 1, } },
	{ 146, "killpg", 1, { 0, } },
	{ 157, "statfs", 2, { 1, 0, } },
	{ 158, "fstatfs", 2, { 0, 0, } },
	{ 159, "umount", 1, { 1, } },
	{ 167, "mount", 5, { 1, 1, 1, 0, 0, } },
	{ 174, "getdents", 3, { 0, 0, 0, } },
	{ 176, "fchdir", 2, { 0, 0, } },
	{ 198, "sigaction", 3, { 0, 0, 0, } },
	{ 201, "sigsuspend", 1, { 0, } },
	{ 206, "socketcall", 2, { 0, 0, } },
	{ 216, "sigreturn", 0, },
	{ 230, "newselect", 5, { 0, 0, 0, 0, 0, } },
	{ 236, "llseek", 5, { 0, 0, 0, 0, 0, } },
	{ 251, "sysctl", 1, { 0, } },
};
#define NUM_SDESC_ENTRIES (sizeof(sdesc_entries) / sizeof(sdesc_entries[0]))
#endif

#ifdef VERBOSE_SYSCALL_TRACING
static char scall_strbuf[512];
#endif

void syscall_trace_entry(unsigned long g1, struct pt_regs *regs)
{
#ifdef VERBOSE_SYSCALL_TRACING
	struct sdesc *sdp;
	int i;
#endif

#if 0
	if (!current->pid) return;
#endif	
	printk("SYS[%s:%d]: PC(%016lx) <%3d> ",
	       current->comm, current->pid, regs->tpc, (int)g1);
#ifdef VERBOSE_SYSCALL_TRACING
	sdp = NULL;
	for(i = 0; i < NUM_SDESC_ENTRIES; i++)
		if(sdesc_entries[i].scall_num == g1) {
			sdp = &sdesc_entries[i];
			break;
		}
	if(sdp) {
		printk("%s(", sdp->name);
		for(i = 0; i < sdp->num_args; i++) {
			if(i)
				printk(",");
			if(!sdp->arg_is_string[i]) {
				if (current->thread.flags & SPARC_FLAG_32BIT)
					printk("%08x", (unsigned int)regs->u_regs[UREG_I0 + i]);
				else
					printk("%016lx", regs->u_regs[UREG_I0 + i]);
			} else {
				if (current->thread.flags & SPARC_FLAG_32BIT)
					strncpy_from_user(scall_strbuf,
							  (char *)(regs->u_regs[UREG_I0 + i] & 0xffffffff),
							  512);
				else
					strncpy_from_user(scall_strbuf,
							  (char *)regs->u_regs[UREG_I0 + i],
							  512);
				printk("%s", scall_strbuf);
			}
		}
		printk(") ");
	}
#endif
}

unsigned long syscall_trace_exit(unsigned long retval, struct pt_regs *regs)
{
#if 0
	if (current->pid)
#endif
		printk("ret[%016lx]\n", retval);
	return retval;
}
#endif /* SYSCALL_TRACING */

#if 1
void rtrap_check(struct pt_regs *regs)
{
	register unsigned long pgd_phys asm("o1");
	register unsigned long pgd_cache asm("o2");
	register unsigned long g1_or_g3 asm("o3");
	register unsigned long g2 asm("o4");
	unsigned long ctx;

#if 0
	do {
		unsigned long test;
		__asm__ __volatile__("rdpr	%%pstate, %0"
				     : "=r" (test));
		if((test & PSTATE_MG) != 0 ||
		   (test & PSTATE_IE) == 0) {
			printk("rtrap_check: Bogus pstate[%016lx]\n", test);
			return;
		}
	} while(0);
#endif

	__asm__ __volatile__("
	rdpr	%%pstate, %%o5
	wrpr	%%o5, %4, %%pstate
	or	%%g1, %%g3, %2
	mov	%%g2, %3
	mov	%%g7, %0
	mov	%5, %1
	ldxa	[%1] %6, %1
	wrpr	%%o5, 0x0, %%pstate"
	: "=r" (pgd_phys), "=r" (pgd_cache),
	  "=r" (g1_or_g3), "=r" (g2)
	: "i" (PSTATE_IE | PSTATE_MG), "i" (TSB_REG),
	  "i" (ASI_DMMU)
	: "o5");

	ctx = spitfire_get_secondary_context();

	if((pgd_phys != __pa(current->mm->pgd)) ||
	   ((pgd_cache != 0) &&
	    (pgd_cache != pgd_val(current->mm->pgd[0])<<11UL)) ||
	   (g1_or_g3 != (0xfffffffe00000000UL | 0x0000000000000018UL)) ||
#define KERN_HIGHBITS		((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
	   (g2 != (KERN_HIGHBITS | KERN_LOWBITS)) ||
#undef KERN_HIGHBITS
#undef KERN_LOWBITS
	   ((ctx != (current->mm->context & 0x3ff)) ||
	    (ctx == 0) ||
	    (CTX_HWBITS(current->mm->context) != ctx))) {
		printk("SHIT[%s:%d]: "
		       "(PP[%016lx] CACH[%016lx] CTX[%lx] g1g3[%016lx] g2[%016lx]) ",
		       current->comm, current->pid,
		       pgd_phys, pgd_cache, ctx, g1_or_g3, g2);
		printk("SHIT[%s:%d]: "
		       "[PP[%016lx] CACH[%016lx] CTX[%lx]] PC[%016lx:%016lx]\n",
		       current->comm, current->pid,
		       __pa(current->mm->pgd),
		       pgd_val(current->mm->pgd[0]),
		       current->mm->context & 0x3ff,
		       regs->tpc, regs->tnpc);
		show_regs(regs);
#if 1
		__sti();
		while(1)
			barrier();
#endif
	}
}
#endif

void bad_trap (struct pt_regs *regs, long lvl)
{
	siginfo_t info;

	if (lvl < 0x100) {
		char buffer[24];
		
		sprintf (buffer, "Bad hw trap %lx at tl0\n", lvl);
		die_if_kernel (buffer, regs);
	}
	if (regs->tstate & TSTATE_PRIV)
		die_if_kernel ("Kernel bad trap", regs);
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_ILLTRP;
	info.si_addr = (void *)regs->tpc;
	info.si_trapno = lvl - 0x100;
	force_sig_info(SIGILL, &info, current);
}

void bad_trap_tl1 (struct pt_regs *regs, long lvl)
{
	char buffer[24];
	
	sprintf (buffer, "Bad trap %lx at tl>0", lvl);
	die_if_kernel (buffer, regs);
}

void instruction_access_exception (struct pt_regs *regs,
				   unsigned long sfsr, unsigned long sfar)
{
	siginfo_t info;

	if (regs->tstate & TSTATE_PRIV) {
#if 1
		printk("instruction_access_exception: Shit SFSR[%016lx] SFAR[%016lx], going.\n",
		       sfsr, sfar);
#endif
		die_if_kernel("Iax", regs);
	}
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code = SEGV_MAPERR;
	info.si_addr = (void *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGSEGV, &info, current);
}

void data_access_exception (struct pt_regs *regs,
			    unsigned long sfsr, unsigned long sfar)
{
	siginfo_t info;

	if (regs->tstate & TSTATE_PRIV) {
		/* Test if this comes from uaccess places. */
		unsigned long fixup, g2;

		g2 = regs->u_regs[UREG_G2];
		if ((fixup = search_exception_table (regs->tpc, &g2))) {
			/* Ouch, somebody is trying ugly VM hole tricks on us... */
#ifdef DEBUG_EXCEPTIONS
			printk("Exception: PC<%016lx> faddr<UNKNOWN>\n", regs->tpc);
			printk("EX_TABLE: insn<%016lx> fixup<%016lx> "
			       "g2<%016lx>\n", regs->tpc, fixup, g2);
#endif
			regs->tpc = fixup;
			regs->tnpc = regs->tpc + 4;
			regs->u_regs[UREG_G2] = g2;
			return;
		}
		/* Shit... */
#if 1
		printk("data_access_exception: Shit SFSR[%016lx] SFAR[%016lx], going.\n",
		       sfsr, sfar);
#endif
		die_if_kernel("Dax", regs);
	}
#if 0
	else
		rtrap_check(regs);
#endif
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code = SEGV_MAPERR;
	info.si_addr = (void *)sfar;
	info.si_trapno = 0;
	force_sig_info(SIGSEGV, &info, current);
}

#ifdef CONFIG_PCI
/* This is really pathetic... */
/* #define DEBUG_PCI_POKES */
extern volatile int pci_poke_in_progress;
extern volatile int pci_poke_faulted;
#endif

/* When access exceptions happen, we must do this. */
static __inline__ void clean_and_reenable_l1_caches(void)
{
	unsigned long va;

	/* Clean 'em. */
	for(va =  0; va < (PAGE_SIZE << 1); va += 32) {
		spitfire_put_icache_tag(va, 0x0);
		spitfire_put_dcache_tag(va, 0x0);
	}

	/* Re-enable. */
	__asm__ __volatile__("flush %%g6\n\t"
			     "membar #Sync\n\t"
			     "stxa %0, [%%g0] %1\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (LSU_CONTROL_IC | LSU_CONTROL_DC |
				    LSU_CONTROL_IM | LSU_CONTROL_DM),
			     "i" (ASI_LSU_CONTROL)
			     : "memory");
}

void do_iae(struct pt_regs *regs)
{
	siginfo_t info;

	clean_and_reenable_l1_caches();

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_OBJERR;
	info.si_addr = (void *)0;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void do_dae(struct pt_regs *regs)
{
#ifdef CONFIG_PCI
	if(pci_poke_in_progress) {
#ifdef DEBUG_PCI_POKES
		prom_printf(" (POKE tpc[%016lx] tnpc[%016lx] ",
			    regs->tpc, regs->tnpc);
#endif
		pci_poke_faulted = 1;
		regs->tnpc = regs->tpc + 4;


#ifdef DEBUG_PCI_POKES
		prom_printf("PCI) ");
		/* prom_halt(); */
#endif
		clean_and_reenable_l1_caches();
		return;
	}
#endif
	do_iae(regs);
}

static char ecc_syndrome_table[] = {
	0x4c, 0x40, 0x41, 0x48, 0x42, 0x48, 0x48, 0x49,
	0x43, 0x48, 0x48, 0x49, 0x48, 0x49, 0x49, 0x4a,
	0x44, 0x48, 0x48, 0x20, 0x48, 0x39, 0x4b, 0x48,
	0x48, 0x25, 0x31, 0x48, 0x28, 0x48, 0x48, 0x2c,
	0x45, 0x48, 0x48, 0x21, 0x48, 0x3d, 0x04, 0x48,
	0x48, 0x4b, 0x35, 0x48, 0x2d, 0x48, 0x48, 0x29,
	0x48, 0x00, 0x01, 0x48, 0x0a, 0x48, 0x48, 0x4b,
	0x0f, 0x48, 0x48, 0x4b, 0x48, 0x49, 0x49, 0x48,
	0x46, 0x48, 0x48, 0x2a, 0x48, 0x3b, 0x27, 0x48,
	0x48, 0x4b, 0x33, 0x48, 0x22, 0x48, 0x48, 0x2e,
	0x48, 0x19, 0x1d, 0x48, 0x1b, 0x4a, 0x48, 0x4b,
	0x1f, 0x48, 0x4a, 0x4b, 0x48, 0x4b, 0x4b, 0x48,
	0x48, 0x4b, 0x24, 0x48, 0x07, 0x48, 0x48, 0x36,
	0x4b, 0x48, 0x48, 0x3e, 0x48, 0x30, 0x38, 0x48,
	0x49, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x16, 0x48,
	0x48, 0x12, 0x4b, 0x48, 0x49, 0x48, 0x48, 0x4b,
	0x47, 0x48, 0x48, 0x2f, 0x48, 0x3f, 0x4b, 0x48,
	0x48, 0x06, 0x37, 0x48, 0x23, 0x48, 0x48, 0x2b,
	0x48, 0x05, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x32,
	0x26, 0x48, 0x48, 0x3a, 0x48, 0x34, 0x3c, 0x48,
	0x48, 0x11, 0x15, 0x48, 0x13, 0x4a, 0x48, 0x4b,
	0x17, 0x48, 0x4a, 0x4b, 0x48, 0x4b, 0x4b, 0x48,
	0x49, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x1e, 0x48,
	0x48, 0x1a, 0x4b, 0x48, 0x49, 0x48, 0x48, 0x4b,
	0x48, 0x08, 0x0d, 0x48, 0x02, 0x48, 0x48, 0x49,
	0x03, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x4b, 0x48,
	0x49, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x10, 0x48,
	0x48, 0x14, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x4b,
	0x49, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x18, 0x48,
	0x48, 0x1c, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x4b,
	0x4a, 0x0c, 0x09, 0x48, 0x0e, 0x48, 0x48, 0x4b,
	0x0b, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x4b, 0x4a
};

/* cee_trap in entry.S encodes AFSR/UDBH/UDBL error status
 * in the following format.  The AFAR is left as is, with
 * reserved bits cleared, and is a raw 40-bit physical
 * address.
 */
#define CE_STATUS_UDBH_UE		(1UL << (43 + 9))
#define CE_STATUS_UDBH_CE		(1UL << (43 + 8))
#define CE_STATUS_UDBH_ESYNDR		(0xffUL << 43)
#define CE_STATUS_UDBH_SHIFT		43
#define CE_STATUS_UDBL_UE		(1UL << (33 + 9))
#define CE_STATUS_UDBL_CE		(1UL << (33 + 8))
#define CE_STATUS_UDBL_ESYNDR		(0xffUL << 33)
#define CE_STATUS_UDBL_SHIFT		33
#define CE_STATUS_AFSR_MASK		(0x1ffffffffUL)
#define CE_STATUS_AFSR_ME		(1UL << 32)
#define CE_STATUS_AFSR_PRIV		(1UL << 31)
#define CE_STATUS_AFSR_ISAP		(1UL << 30)
#define CE_STATUS_AFSR_ETP		(1UL << 29)
#define CE_STATUS_AFSR_IVUE		(1UL << 28)
#define CE_STATUS_AFSR_TO		(1UL << 27)
#define CE_STATUS_AFSR_BERR		(1UL << 26)
#define CE_STATUS_AFSR_LDP		(1UL << 25)
#define CE_STATUS_AFSR_CP		(1UL << 24)
#define CE_STATUS_AFSR_WP		(1UL << 23)
#define CE_STATUS_AFSR_EDP		(1UL << 22)
#define CE_STATUS_AFSR_UE		(1UL << 21)
#define CE_STATUS_AFSR_CE		(1UL << 20)
#define CE_STATUS_AFSR_ETS		(0xfUL << 16)
#define CE_STATUS_AFSR_ETS_SHIFT	16
#define CE_STATUS_AFSR_PSYND		(0xffffUL << 0)
#define CE_STATUS_AFSR_PSYND_SHIFT	0

/* Layout of Ecache TAG Parity Syndrome of AFSR */
#define AFSR_ETSYNDROME_7_0		0x1UL /* E$-tag bus bits  <7:0> */
#define AFSR_ETSYNDROME_15_8		0x2UL /* E$-tag bus bits <15:8> */
#define AFSR_ETSYNDROME_21_16		0x4UL /* E$-tag bus bits <21:16> */
#define AFSR_ETSYNDROME_24_22		0x8UL /* E$-tag bus bits <24:22> */

static char *syndrome_unknown = "<Unknown>";

asmlinkage void cee_log(unsigned long ce_status,
			unsigned long afar,
			struct pt_regs *regs)
{
	char memmod_str[64];
	char *p;
	unsigned short scode, udb_reg;

	printk(KERN_WARNING "CPU[%d]: Correctable ECC Error "
	       "AFSR[%lx] AFAR[%016lx] UDBL[%lx] UDBH[%lx]\n",
	       smp_processor_id(),
	       (ce_status & CE_STATUS_AFSR_MASK),
	       afar,
	       ((ce_status >> CE_STATUS_UDBL_SHIFT) & 0x3ffUL),
	       ((ce_status >> CE_STATUS_UDBH_SHIFT) & 0x3ffUL));

	udb_reg = ((ce_status >> CE_STATUS_UDBL_SHIFT) & 0x3ffUL);
	if (udb_reg & (1 << 8)) {
		scode = ecc_syndrome_table[udb_reg & 0xff];
		if (prom_getunumber(scode, afar,
				    memmod_str, sizeof(memmod_str)) == -1)
			p = syndrome_unknown;
		else
			p = memmod_str;
		printk(KERN_WARNING "CPU[%d]: UDBL Syndrome[%x] "
		       "Memory Module \"%s\"\n",
		       smp_processor_id(), scode, p);
	}

	udb_reg = ((ce_status >> CE_STATUS_UDBH_SHIFT) & 0x3ffUL);
	if (udb_reg & (1 << 8)) {
		scode = ecc_syndrome_table[udb_reg & 0xff];
		if (prom_getunumber(scode, afar,
				    memmod_str, sizeof(memmod_str)) == -1)
			p = syndrome_unknown;
		else
			p = memmod_str;
		printk(KERN_WARNING "CPU[%d]: UDBH Syndrome[%x] "
		       "Memory Module \"%s\"\n",
		       smp_processor_id(), scode, p);
	}
}

void do_fpe_common(struct pt_regs *regs)
{
	if(regs->tstate & TSTATE_PRIV) {
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
	} else {
		unsigned long fsr = current->thread.xfsr[0];
		siginfo_t info;

		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void *)regs->tpc;
		info.si_trapno = 0;
		info.si_code = __SI_FAULT;
		if ((fsr & 0x1c000) == (1 << 14)) {
			if (fsr & 0x10)
				info.si_code = FPE_FLTINV;
			else if (fsr & 0x08)
				info.si_code = FPE_FLTOVF;
			else if (fsr & 0x04)
				info.si_code = FPE_FLTUND;
			else if (fsr & 0x02)
				info.si_code = FPE_FLTDIV;
			else if (fsr & 0x01)
				info.si_code = FPE_FLTRES;
		}
		force_sig_info(SIGFPE, &info, current);
	}
}

void do_fpieee(struct pt_regs *regs)
{
#ifdef DEBUG_FPU
	printk("fpieee %016lx\n", current->thread.xfsr[0]);
#endif
	do_fpe_common(regs);
}

extern int do_mathemu(struct pt_regs *, struct fpustate *);

void do_fpother(struct pt_regs *regs)
{
	struct fpustate *f = FPUSTATE;
	int ret = 0;

	switch ((current->thread.xfsr[0] & 0x1c000)) {
	case (2 << 14): /* unfinished_FPop */
	case (3 << 14): /* unimplemented_FPop */
		ret = do_mathemu(regs, f);
		break;
	}
	if (ret) return;
#ifdef DEBUG_FPU
	printk("fpother %016lx\n", current->thread.xfsr[0]);
#endif
	do_fpe_common(regs);
}

void do_tof(struct pt_regs *regs)
{
	siginfo_t info;

	if(regs->tstate & TSTATE_PRIV)
		die_if_kernel("Penguin overflow trap from kernel mode", regs);
	info.si_signo = SIGEMT;
	info.si_errno = 0;
	info.si_code = EMT_TAGOVF;
	info.si_addr = (void *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGEMT, &info, current);
}

void do_div0(struct pt_regs *regs)
{
	siginfo_t info;

	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = FPE_INTDIV;
	info.si_addr = (void *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGFPE, &info, current);
}

void instruction_dump (unsigned int *pc)
{
	int i;

	if((((unsigned long) pc) & 3))
		return;

	printk("Instruction DUMP:");
	for(i = -3; i < 6; i++)
		printk("%c%08x%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
}

void user_instruction_dump (unsigned int *pc)
{
	int i;
	unsigned int buf[9];
	
	if((((unsigned long) pc) & 3))
		return;
		
	if(copy_from_user(buf, pc - 3, sizeof(buf)))
		return;

	printk("Instruction DUMP:");
	for(i = 0; i < 9; i++)
		printk("%c%08x%c",i==3?' ':'<',buf[i],i==3?' ':'>');
	printk("\n");
}

void die_if_kernel(char *str, struct pt_regs *regs)
{
	extern void __show_regs(struct pt_regs * regs);
	extern void smp_report_regs(void);
	int count = 0;
	struct reg_window *lastrw;
	
	/* Amuse the user. */
	printk(
"              \\|/ ____ \\|/\n"
"              \"@'/ .. \\`@\"\n"
"              /_| \\__/ |_\\\n"
"                 \\__U_/\n");

	printk("%s(%d): %s\n", current->comm, current->pid, str);
	__asm__ __volatile__("flushw");
	__show_regs(regs);
	if(regs->tstate & TSTATE_PRIV) {
		struct reg_window *rw = (struct reg_window *)
			(regs->u_regs[UREG_FP] + STACK_BIAS);

		/* Stop the back trace when we hit userland or we
		 * find some badly aligned kernel stack.
		 */
		lastrw = (struct reg_window *)current;
		while(rw					&&
		      count++ < 30				&&
		      rw >= lastrw				&&
		      (char *) rw < ((char *) current)
		        + sizeof (union task_union) 		&&
		      !(((unsigned long) rw) & 0x7)) {
			printk("Caller[%016lx]\n", rw->ins[7]);
			lastrw = rw;
			rw = (struct reg_window *)
				(rw->ins[6] + STACK_BIAS);
		}
		instruction_dump ((unsigned int *) regs->tpc);
	} else
		user_instruction_dump ((unsigned int *) regs->tpc);
#ifdef CONFIG_SMP
	smp_report_regs();
#endif
                                                	
	if(regs->tstate & TSTATE_PRIV)
		do_exit(SIGKILL);
	do_exit(SIGSEGV);
}

extern int handle_popc(u32 insn, struct pt_regs *regs);
extern int handle_ldf_stq(u32 insn, struct pt_regs *regs);

void do_illegal_instruction(struct pt_regs *regs)
{
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn;
	siginfo_t info;

	if(tstate & TSTATE_PRIV)
		die_if_kernel("Kernel illegal instruction", regs);
	if(current->thread.flags & SPARC_FLAG_32BIT)
		pc = (u32)pc;
	if (get_user(insn, (u32 *)pc) != -EFAULT) {
		if ((insn & 0xc1ffc000) == 0x81700000) /* POPC */ {
			if (handle_popc(insn, regs))
				return;
		} else if ((insn & 0xc1580000) == 0xc1100000) /* LDQ/STQ */ {
			if (handle_ldf_stq(insn, regs))
				return;
		}
	}
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_ILLOPC;
	info.si_addr = (void *)pc;
	info.si_trapno = 0;
	force_sig_info(SIGILL, &info, current);
}

void mem_address_unaligned(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr)
{
	siginfo_t info;

	if(regs->tstate & TSTATE_PRIV) {
		extern void kernel_unaligned_trap(struct pt_regs *regs,
						  unsigned int insn, 
						  unsigned long sfar, unsigned long sfsr);

		return kernel_unaligned_trap(regs, *((unsigned int *)regs->tpc), sfar, sfsr);
	}
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void *)sfar;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void do_privop(struct pt_regs *regs)
{
	siginfo_t info;

	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_PRVOPC;
	info.si_addr = (void *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGILL, &info, current);
}

void do_privact(struct pt_regs *regs)
{
	do_privop(regs);
}

/* Trap level 1 stuff or other traps we should never see... */
void do_cee(struct pt_regs *regs)
{
	die_if_kernel("TL0: Cache Error Exception", regs);
}

void do_cee_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Cache Error Exception", regs);
}

void do_dae_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Data Access Exception", regs);
}

void do_iae_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Instruction Access Exception", regs);
}

void do_div0_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: DIV0 Exception", regs);
}

void do_fpdis_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU Disabled", regs);
}

void do_fpieee_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU IEEE Exception", regs);
}

void do_fpother_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: FPU Other Exception", regs);
}

void do_ill_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Illegal Instruction Exception", regs);
}

void do_irq_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: IRQ Exception", regs);
}

void do_lddfmna_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: LDDF Exception", regs);
}

void do_stdfmna_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: STDF Exception", regs);
}

void do_paw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Phys Watchpoint Exception", regs);
}

void do_paw_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Phys Watchpoint Exception", regs);
}

void do_vaw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Virt Watchpoint Exception", regs);
}

void do_vaw_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Virt Watchpoint Exception", regs);
}

void do_tof_tl1(struct pt_regs *regs)
{
	die_if_kernel("TL1: Tag Overflow Exception", regs);
}

#ifdef CONFIG_EC_FLUSH_TRAP
void cache_flush_trap(struct pt_regs *regs)
{
#ifndef CONFIG_SMP
	unsigned node = linux_cpus[get_cpuid()].prom_node;
#else
#error cache_flush_trap not supported on sparc64/SMP yet
#endif

#if 0
/* Broken */
	int size = prom_getintdefault(node, "ecache-size", 512*1024);
	int i, j;
	unsigned long addr;
	struct page *page, *end;

	regs->tpc = regs->tnpc;
	regs->tnpc = regs->tnpc + 4;
	if (!capable(CAP_SYS_ADMIN)) return;
	size >>= PAGE_SHIFT;
	addr = PAGE_OFFSET - PAGE_SIZE;
	page = mem_map - 1;
	end = mem_map + max_mapnr;
	for (i = 0; i < size; i++) {
		do {
			addr += PAGE_SIZE;
			page++;
			if (page >= end)
				return;
		} while (!PageReserved(page));
		/* E-Cache line size is 64B. Let us pollute it :)) */
		for (j = 0; j < PAGE_SIZE; j += 64)
			__asm__ __volatile__ ("ldx [%0 + %1], %%g1" : : "r" (j), "r" (addr) : "g1");
	}
#endif
}
#endif

void do_getpsr(struct pt_regs *regs)
{
	regs->u_regs[UREG_I0] = tstate_to_psr(regs->tstate);
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;
}

void trap_init(void)
{
	/* Attach to the address space of init_task. */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	/* NOTE: Other cpus have this done as they are started
	 *       up on SMP.
	 */
}
