/*
 * arch/ia64/boot/bootloader.c
 *
 * Loads an ELF kernel.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 *
 * 01/07/99 S.Eranian modified to pass command line arguments to kernel
 */
#include <linux/config.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/elf.h>
#include <asm/pal.h>
#include <asm/pgtable.h>
#include <asm/sal.h>
#include <asm/system.h>

/* Simulator system calls: */

#define SSC_CONSOLE_INIT		20
#define SSC_GETCHAR			21
#define SSC_PUTCHAR			31
#define SSC_OPEN			50
#define SSC_CLOSE			51
#define SSC_READ			52
#define SSC_WRITE			53
#define SSC_GET_COMPLETION		54
#define SSC_WAIT_COMPLETION		55
#define SSC_CONNECT_INTERRUPT		58
#define SSC_GENERATE_INTERRUPT		59
#define SSC_SET_PERIODIC_INTERRUPT	60
#define SSC_GET_RTC			65
#define SSC_EXIT			66
#define SSC_LOAD_SYMBOLS		69
#define SSC_GET_TOD			74

#define SSC_GET_ARGS			75

struct disk_req {
	unsigned long addr;
	unsigned len;
};

struct disk_stat {
	int fd;
	unsigned count;
};

#include "../kernel/fw-emu.c"

static void
cons_write (const char *buf)
{
	unsigned long ch;

	while ((ch = *buf++) != '\0') {
		ssc(ch, 0, 0, 0, SSC_PUTCHAR);
		if (ch == '\n')
		  ssc('\r', 0, 0, 0, SSC_PUTCHAR);
	}
}

void
enter_virtual_mode (unsigned long new_psr)
{
	long tmp;

	asm volatile ("movl %0=1f" : "=r"(tmp));
	asm volatile ("mov cr.ipsr=%0" :: "r"(new_psr));
	asm volatile ("mov cr.iip=%0" :: "r"(tmp));
	asm volatile ("mov cr.ifs=r0");
	asm volatile ("rfi;;");
	asm volatile ("1:");
}

#define MAX_ARGS 32

void
_start (void)
{
	register long sp asm ("sp");
	static char stack[16384] __attribute__ ((aligned (16)));
	static char mem[4096];
	static char buffer[1024];
	unsigned long flags, off;
	int fd, i;
	struct disk_req req;
	struct disk_stat stat;
	struct elfhdr *elf;
	struct elf_phdr *elf_phdr;	/* program header */
	unsigned long e_entry, e_phoff, e_phnum;
	char *kpath, *args;
	long arglen = 0;

	asm volatile ("movl gp=__gp;;" ::: "memory");
	asm volatile ("mov sp=%0" :: "r"(stack) : "memory");
	asm volatile ("bsw.1;;");
#ifdef CONFIG_ITANIUM_ASTEP_SPECIFIC
	asm volative ("nop 0;; nop 0;; nop 0;;");
#endif /* CONFIG_ITANIUM_ASTEP_SPECIFIC */

	ssc(0, 0, 0, 0, SSC_CONSOLE_INIT);

	/*
	 * S.Eranian: extract the commandline argument from the 
	 * simulator
	 *
	 * The expected format is as follows:
         *
	 *	kernelname args...
	 *
	 * Both are optional but you can't have the second one without the 
	 * first.
	 */
	arglen = ssc((long) buffer, 0, 0, 0, SSC_GET_ARGS);

	kpath = "vmlinux";
	args = buffer;
	if (arglen > 0) {
		kpath = buffer;
		while (*args != ' ' && *args != '\0')
			++args, --arglen;
		if (*args == ' ')
			*args++ = '\0', --arglen;
	}

	if (arglen <= 0) {
		args = "";
		arglen = 1;
	}

	fd = ssc((long) kpath, 1, 0, 0, SSC_OPEN);

	if (fd < 0) {
		cons_write(kpath);
		cons_write(": file not found, reboot now\n");
		for(;;);
	}
	stat.fd = fd;
	off = 0;

	req.len = sizeof(mem);
	req.addr = (long) mem;
	ssc(fd, 1, (long) &req, off, SSC_READ);
	ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);

	elf = (struct elfhdr *) mem;
	if (elf->e_ident[0] == 0x7f && strncmp(elf->e_ident + 1, "ELF", 3) != 0) {
		cons_write("not an ELF file\n");
		return;
	}
	if (elf->e_type != ET_EXEC) {
		cons_write("not an ELF executable\n");
		return;
	}
	if (!elf_check_arch(elf)) {
		cons_write("kernel not for this processor\n");
		return;
	}

	e_entry = elf->e_entry;
	e_phnum = elf->e_phnum;
	e_phoff = elf->e_phoff;

	cons_write("loading ");
	cons_write(kpath);
	cons_write("...\n");

	for (i = 0; i < e_phnum; ++i) {
		req.len = sizeof(*elf_phdr);
		req.addr = (long) mem;
		ssc(fd, 1, (long) &req, e_phoff, SSC_READ);
		ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);
		if (stat.count != sizeof(*elf_phdr)) {
			cons_write("failed to read phdr\n");
			return;
		}
		e_phoff += sizeof(*elf_phdr);

		elf_phdr = (struct elf_phdr *) mem;
		req.len = elf_phdr->p_filesz;
		req.addr = __pa(elf_phdr->p_vaddr);
		ssc(fd, 1, (long) &req, elf_phdr->p_offset, SSC_READ);
		ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);
		memset((char *)__pa(elf_phdr->p_vaddr) + elf_phdr->p_filesz, 0,
		       elf_phdr->p_memsz - elf_phdr->p_filesz);
	}
	ssc(fd, 0, 0, 0, SSC_CLOSE);

	cons_write("starting kernel...\n");

	/* fake an I/O base address: */
	asm volatile ("mov ar.k0=%0" :: "r"(0xffffc000000UL));

	/*
	 * Install a translation register that identity maps the
	 * kernel's 256MB page.
	 */
	ia64_clear_ic(flags);
	ia64_set_rr(          0, (0x1000 << 8) | (_PAGE_SIZE_1M << 2));
	ia64_set_rr(PAGE_OFFSET, (ia64_rid(0, PAGE_OFFSET) << 8) | (_PAGE_SIZE_256M << 2));
	ia64_srlz_d();
	ia64_itr(0x3, 0, 1024*1024,
		 pte_val(mk_pte_phys(1024*1024, __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RWX))),
		 _PAGE_SIZE_1M);
	ia64_itr(0x3, 1, PAGE_OFFSET,
		 pte_val(mk_pte_phys(0, __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RWX))),
		 _PAGE_SIZE_256M);
	ia64_srlz_i();

	enter_virtual_mode(flags | IA64_PSR_IT | IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT
			   | IA64_PSR_DFH | IA64_PSR_BN);

	sys_fw_init(args, arglen);

	ssc(0, (long) kpath, 0, 0, SSC_LOAD_SYMBOLS);

	/*
	 * Install the kernel's command line argument on ZERO_PAGE
	 * just after the botoparam structure.
	 * In case we don't have any argument just put \0
	 */
	memcpy(((struct ia64_boot_param *)ZERO_PAGE_ADDR) + 1, args, arglen);
	sp = __pa(&stack);

	asm volatile ("br.sptk.few %0" :: "b"(e_entry));

	cons_write("kernel returned!\n");
	ssc(-1, 0, 0, 0, SSC_EXIT);
}
