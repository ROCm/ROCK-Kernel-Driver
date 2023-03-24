/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BOOT_BOOT_H
#define BOOT_BOOT_H

#include <linux/types.h>

#define IPL_START	0x200

#ifndef __ASSEMBLY__

struct vmlinux_info {
	unsigned long default_lma;
	void (*entry)(void);
	unsigned long image_size;	/* does not include .bss */
	unsigned long bss_size;		/* uncompressed image .bss size */
	unsigned long bootdata_off;
	unsigned long bootdata_size;
	unsigned long bootdata_preserved_off;
	unsigned long bootdata_preserved_size;
	unsigned long dynsym_start;
	unsigned long rela_dyn_start;
	unsigned long rela_dyn_end;
	unsigned long amode31_size;
};

void startup_kernel(void);
unsigned long detect_memory(unsigned long *safe_addr);
bool is_ipl_block_dump(void);
void store_ipl_parmblock(void);
unsigned long read_ipl_report(unsigned long safe_addr);
void setup_boot_command_line(void);
void parse_boot_command_line(void);
void verify_facilities(void);
void print_missing_facilities(void);
void sclp_early_setup_buffer(void);
void print_pgm_check_info(void);
unsigned long get_random_base(unsigned long safe_addr);
void __printf(1, 2) decompressor_printk(const char *fmt, ...);
void error(char *m);

/* Symbols defined by linker scripts */
extern const char kernel_version[];
extern unsigned long memory_limit;
extern unsigned long vmalloc_size;
extern int vmalloc_size_set;
extern int kaslr_enabled;
extern char __boot_data_start[], __boot_data_end[];
extern char __boot_data_preserved_start[], __boot_data_preserved_end[];
extern char _decompressor_syms_start[], _decompressor_syms_end[];
extern char _stack_start[], _stack_end[];
extern char _end[];
extern unsigned char _compressed_start[];
extern unsigned char _compressed_end[];
extern struct vmlinux_info _vmlinux_info;
#define vmlinux _vmlinux_info

#endif /* __ASSEMBLY__ */
#endif /* BOOT_BOOT_H */
