#ifndef LOAD_OFFSET
#define LOAD_OFFSET 0
#endif

#define RODATA								\
	.rodata           : AT(ADDR(.rodata) - LOAD_OFFSET) {		\
		*(.rodata) *(.rodata.*)					\
		*(__vermagic)		/* Kernel version magic */	\
	}								\
									\
	.rodata1          : AT(ADDR(.rodata1) - LOAD_OFFSET) {		\
		*(.rodata1)						\
	}								\
									\
	/* Kernel symbol table: Normal symbols */			\
	__start___ksymtab = .;						\
	__ksymtab         : AT(ADDR(__ksymtab) - LOAD_OFFSET) {		\
		*(__ksymtab)						\
	}								\
	__stop___ksymtab = .;						\
									\
	/* Kernel symbol table: GPL-only symbols */			\
	__start___gpl_ksymtab = .;					\
	__gpl_ksymtab     : AT(ADDR(__gpl_ksymtab) - LOAD_OFFSET) {	\
		*(__gpl_ksymtab)					\
	}								\
	__stop___gpl_ksymtab = .;					\
									\
	/* Kernel symbol table: strings */				\
        __ksymtab_strings : AT(ADDR(__ksymtab_strings) - LOAD_OFFSET) {	\
		*(__ksymtab_strings)					\
	}

