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
	__ksymtab         : AT(ADDR(__ksymtab) - LOAD_OFFSET) {		\
		__start___ksymtab = .;					\
		*(__ksymtab)						\
		__stop___ksymtab = .;					\
	}								\
									\
	/* Kernel symbol table: GPL-only symbols */			\
	__ksymtab_gpl     : AT(ADDR(__ksymtab_gpl) - LOAD_OFFSET) {	\
		__start___ksymtab_gpl = .;				\
		*(__ksymtab_gpl)					\
		__stop___ksymtab_gpl = .;				\
	}								\
									\
	/* Kernel symbol table: Normal symbols */			\
	__kcrctab         : AT(ADDR(__kcrctab) - LOAD_OFFSET) {		\
		__start___kcrctab = .;					\
		*(__kcrctab)						\
		__stop___kcrctab = .;					\
	}								\
									\
	/* Kernel symbol table: GPL-only symbols */			\
	__kcrctab_gpl     : AT(ADDR(__kcrctab_gpl) - LOAD_OFFSET) {	\
		__start___kcrctab_gpl = .;				\
		*(__kcrctab_gpl)					\
		__stop___kcrctab_gpl = .;				\
	}								\
									\
	/* Kernel symbol table: strings */				\
        __ksymtab_strings : AT(ADDR(__ksymtab_strings) - LOAD_OFFSET) {	\
		*(__ksymtab_strings)					\
	}

#define SECURITY_INIT							\
	.security_initcall.init : {					\
		__security_initcall_start = .;				\
		*(.security_initcall.init) 				\
		__security_initcall_end = .;				\
	}
