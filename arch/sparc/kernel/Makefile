# $Id: Makefile,v 1.62 2000/12/15 00:41:17 davem Exp $
# Makefile for the linux kernel.
#

SH = $(CONFIG_SHELL)

O_TARGET	:= kernel.o
EXTRA_TARGETS	:= head.o init_task.o

EXTRA_AFLAGS := -ansi

export-objs := sparc_ksyms.o
IRQ_OBJS := irq.o sun4m_irq.o sun4c_irq.o sun4d_irq.o
obj-y    := entry.o wof.o wuf.o etrap.o rtrap.o traps.o ${IRQ_OBJS} \
	    process.o signal.o ioport.o setup.o idprom.o \
	    sys_sparc.o sunos_asm.o sparc-stub.o systbls.o \
	    time.o windows.o cpu.o devices.o sclow.o \
	    tadpole.o tick14.o ptrace.o sys_solaris.o \
	    unaligned.o muldiv.o pcic.o semaphore.o sparc_ksyms.o

obj-$(CONFIG_SUN4) += sun4setup.o
obj-$(CONFIG_SMP) += trampoline.o smp.o sun4m_smp.o sun4d_smp.o
obj-$(CONFIG_SUN_AUXIO) += auxio.o
obj-$(CONFIG_PCI) += ebus.o
obj-$(CONFIG_SUN_PM) += apc.o pmc.o

ifdef CONFIG_SUNOS_EMUL
obj-y += sys_sunos.o sunos_ioctl.o
endif

include $(TOPDIR)/Rules.make

check_asm: FORCE
	@if [ ! -r $(HPATH)/asm/asm_offsets.h ] ; then \
	  touch $(HPATH)/asm/asm_offsets.h ; \
	fi
	@echo "/* Automatically generated. Do not edit. */" > asm_offsets.h
	@echo "#ifndef __ASM_OFFSETS_H__" >> asm_offsets.h
	@echo "#define __ASM_OFFSETS_H__" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#ifndef CONFIG_SMP" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" > tmp.c
	@echo "#undef CONFIG_SMP" >> tmp.c
	@echo "#include <linux/sched.h>" >> tmp.c
	$(CPP) $(CPPFLAGS) tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm_data.c
	@echo "#include <linux/config.h>" >> check_asm_data.c
	@echo "#undef CONFIG_SMP" >> check_asm_data.c
	@echo "#include <linux/sched.h>" >> check_asm_data.c
	@echo "unsigned int check_asm_data[] = {" >> check_asm_data.c
	$(SH) ./check_asm.sh -data task tmp.i check_asm_data.c
	$(SH) ./check_asm.sh -data mm tmp.i check_asm_data.c
	$(SH) ./check_asm.sh -data thread tmp.i check_asm_data.c
	@echo '};' >> check_asm_data.c
	$(CC) $(CFLAGS) -S -o check_asm_data.s check_asm_data.c
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c
	@echo 'extern int printf(const char *fmt, ...);' >>check_asm.c
	@echo "unsigned int check_asm_data[] = {" >> check_asm.c
	$(SH) ./check_asm.sh -ints check_asm_data.s check_asm.c
	@echo "};" >> check_asm.c
	@echo 'int main(void) {' >> check_asm.c
	@echo 'int i = 0;' >> check_asm.c
	$(SH) ./check_asm.sh -printf task tmp.i check_asm.c
	$(SH) ./check_asm.sh -printf mm tmp.i check_asm.c
	$(SH) ./check_asm.sh -printf thread tmp.i check_asm.c
	@echo 'return 0; }' >> check_asm.c
	@rm -f tmp.[ci] check_asm_data.[cs]
	$(HOSTCC) -o check_asm check_asm.c
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#else /* CONFIG_SMP */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#include <linux/config.h>" > tmp.c
	@echo "#undef CONFIG_SMP" >> tmp.c
	@echo "#define CONFIG_SMP 1" >> tmp.c
	@echo "#include <linux/sched.h>" >> tmp.c
	$(CPP) $(CPPFLAGS) tmp.c -o tmp.i
	@echo "/* Automatically generated. Do not edit. */" > check_asm_data.c
	@echo "#include <linux/config.h>" >> check_asm_data.c
	@echo "#undef CONFIG_SMP" >> check_asm_data.c
	@echo "#define CONFIG_SMP 1" >> check_asm_data.c
	@echo "#include <linux/sched.h>" >> check_asm_data.c
	@echo "unsigned int check_asm_data[] = {" >> check_asm_data.c
	$(SH) ./check_asm.sh -data task tmp.i check_asm_data.c
	$(SH) ./check_asm.sh -data mm tmp.i check_asm_data.c
	$(SH) ./check_asm.sh -data thread tmp.i check_asm_data.c
	@echo '};' >> check_asm_data.c
	$(CC) $(CFLAGS) -S -o check_asm_data.s check_asm_data.c
	@echo "/* Automatically generated. Do not edit. */" > check_asm.c
	@echo 'extern int printf(const char *fmt, ...);' >>check_asm.c
	@echo "unsigned int check_asm_data[] = {" >> check_asm.c
	$(SH) ./check_asm.sh -ints check_asm_data.s check_asm.c
	@echo "};" >> check_asm.c
	@echo 'int main(void) {' >> check_asm.c
	@echo 'int i = 0;' >> check_asm.c
	$(SH) ./check_asm.sh -printf task tmp.i check_asm.c
	$(SH) ./check_asm.sh -printf mm tmp.i check_asm.c
	$(SH) ./check_asm.sh -printf thread tmp.i check_asm.c
	@echo 'return 0; }' >> check_asm.c
	@rm -f tmp.[ci] check_asm_data.[cs]
	$(HOSTCC) -o check_asm check_asm.c
	./check_asm >> asm_offsets.h
	@rm -f check_asm check_asm.c
	@echo "" >> asm_offsets.h
	@echo "#endif /* CONFIG_SMP */" >> asm_offsets.h
	@echo "" >> asm_offsets.h
	@echo "#endif /* __ASM_OFFSETS_H__ */" >> asm_offsets.h
	@if test -r $(HPATH)/asm/asm_offsets.h; then \
	  if cmp -s asm_offsets.h $(HPATH)/asm/asm_offsets.h; then \
	    echo $(HPATH)/asm/asm_offsets.h is unchanged; \
	    rm -f asm_offsets.h; \
	  else \
	    mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; \
	  fi; \
	else \
	  mv -f asm_offsets.h $(HPATH)/asm/asm_offsets.h; \
	fi
