#define RODATA                                                                \
	.rodata : { *(.rodata) *(.rodata.*) *(.rodata1) }                     \
	.rodata1 : { *(.rodata1) }                                            \
                                                                              \
	.kstrtab : { *(.kstrtab) }                                            \
                                                                              \
	/* Kernel version magic */                                            \
	__vermagic : { *(__vermagic) }                                        \
                                                                              \
	/* Kernel symbol table */                                             \
	__start___ksymtab = .;                                                \
	__ksymtab : { *(__ksymtab) }                                          \
	__stop___ksymtab = .;                                                 \
                                                                              \
	/* Kernel symbol table: GPL-only symbols */                           \
	__start___gpl_ksymtab = .;                                            \
	__gpl_ksymtab : { *(__gpl_ksymtab) }                                  \
	__stop___gpl_ksymtab = .;                                             \
                                                                              \
	/* Kernel symbol table: strings */                                    \
        __ksymtab_strings : { *(__ksymtab_strings) }                          \
                                                                              \
	/* All kernel symbols */                                              \
	__start___kallsyms = .;                                               \
	__kallsyms : { *(__kallsyms) }                                        \
	__stop___kallsyms = .;
