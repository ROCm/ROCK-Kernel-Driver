#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

ifdef CONFIG_PPC64BRIDGE
.S.o:
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -mppc64bridge -c $< -o $*.o
else
.S.o:
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c $< -o $*.o
endif

ifeq ($(CONFIG_4xx),y)
  KHEAD := head_4xx.o
else
  ifeq ($(CONFIG_8xx),y)
    KHEAD := head_8xx.o
  else
    KHEAD := head.o
  endif
endif

all: $(KHEAD) kernel.o

O_TARGET := kernel.o

export-objs			:= ppc_ksyms.o prep_setup.o 

obj-$(CONFIG_PPC)		:= entry.o traps.o irq.o idle.o time.o misc.o \
					process.o signal.o bitops.o ptrace.o \
					ppc_htab.o semaphore.o syscalls.o  \
					align.o setup.o
obj-$(CONFIG_POWER4)		+= xics.o
obj-$(CONFIG_PCI)		+= pci.o pci-dma.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_PMAC_PBOOK)	+= sleep.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_PPC_RTAS)		+= error_log.o proc_rtas.o
obj-$(CONFIG_NVRAM)		+= pmac_nvram.o
obj-$(CONFIG_PREP_RESIDUAL)	+= residual.o
obj-$(CONFIG_4xx)		+= ppc4xx_pic.o
obj-$(CONFIG_OAK)		+= oak_setup.o
obj-$(CONFIG_WALNUT)		+= walnut_setup.o
ifeq ($(CONFIG_WALNUT),y)
obj-$(CONFIG_PCI)		+= galaxy_pci.o
endif
obj-$(CONFIG_8xx)		+= m8xx_setup.o ppc8xx_pic.o
ifeq ($(CONFIG_8xx),y)
obj-$(CONFIG_PCI)		+= qspan_pci.c
else
obj-$(CONFIG_PPC)		+= hashtable.o
endif
obj-$(CONFIG_MATH_EMULATION)	+= softemu8xx.o
obj-$(CONFIG_MBX)		+= i8259.o
obj-$(CONFIG_APUS)		+= apus_setup.o
obj-$(CONFIG_ALL_PPC)		+= pmac_pic.o pmac_setup.o pmac_time.o prom.o \
					feature.o pmac_pci.o chrp_setup.o \
					chrp_time.o chrp_pci.o 	open_pic.o \
					indirect_pci.o i8259.o prep_pci.o \
					prep_time.o prep_nvram.o ppc_ksyms.o \
					prep_setup.o 
obj-$(CONFIG_PMAC_BACKLIGHT)	+= pmac_backlight.o
obj-$(CONFIG_GEMINI)		+= gemini_prom.o gemini_pci.o gemini_setup.o \
					open_pic.o
obj-$(CONFIG_8260)		+= m8260_setup.o ppc8260_pic.o


include $(TOPDIR)/Rules.make

head.o: head.S ppc_defs.h
head_4xx.o: head_4xx.S ppc_defs.h
head_8xx.o: head_8xx.S ppc_defs.h

ppc_defs.h: mk_defs.c ppc_defs.head \
		$(TOPDIR)/include/asm/mmu.h \
		$(TOPDIR)/include/asm/processor.h \
		$(TOPDIR)/include/asm/pgtable.h \
		$(TOPDIR)/include/asm/ptrace.h
	$(CC) $(CFLAGS) -S mk_defs.c
	cp ppc_defs.head ppc_defs.h
# for bk, this way we can write to the file even if it's not checked out
	chmod u+w ppc_defs.h
	grep '^#define' mk_defs.s >> ppc_defs.h
	rm mk_defs.s

find_name : find_name.c
	$(HOSTCC) $(HOSTCFLAGS) -o find_name find_name.c

checks: checks.c
	$(HOSTCC) -I$(HPATH) $(HOSTCFLAGS) -D__KERNEL__ -fno-builtin -o checks checks.c
	./checks

