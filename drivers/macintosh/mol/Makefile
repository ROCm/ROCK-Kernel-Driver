
MOL_OBJS =	\
		_fault.o \
		_dev.o \
		_misc.o \
		_mmu.o \
		_hostirq.o \
		init.o \
		hash.o \
		emu.o \
		mmu.o \
		mmu_fb.o \
		mmu_io.o \
		mmu_tracker.o \
		skiplist.o \
		mtable.o \
		fault.o \
		context.o \
		ptaccess.o \
		misc.o \
		_traps.o \
		actions.o


mol-objs	:= $(MOL_OBJS)
obj-m		:= mol.o sheep.o

MOL_INCLUDE_DIR = $(srctree)/$(src)/include
MOL_ASMFLAGS    = $(ASMFLAGS) $(INCLUDES) -D__ASSEMBLY__ -D__KERNEL__
EXTRA_CFLAGS	= -I$(MOL_INCLUDE_DIR) -I$(obj)/include

$(obj)/asm_offsets.c:	$(srctree)/$(src)/asm_offsets.c $(MOL_INCLUDE_DIR)/asm_offsets.h
	rm -f $@
	cat $(srctree)/$(src)/asm_offsets.c $(MOL_INCLUDE_DIR)/asm_offsets.h > $@

$(obj)/_traps.o:	$(obj)/asm_offsets.h $(srctree)/$(src)/asm-files/*.S
$(obj)/include: FORCE
	mkdir -vp $@

$(obj)/_%.o: $(src)/asm-files/%.S
	echo "  AS [x]   $@"
	$(RM) $@ $@.s
	$(CPP) $(LINUXINCLUDE) -I$(MOL_INCLUDE_DIR) -I$(src) $(MOL_ASMFLAGS) $< | m4 > $@.m4
	cat $@.m4 > $@.s
	$(AS) $@.s $(AS_FLAGS) -o $@
	$(RM) $@.s $@.m4

$(addprefix $(obj)/,$(MOL_OBJS)): $(obj)/%.o: $(obj)/include/kconfig.h
$(obj)/include/kconfig.h:
	$(RM) -fv $(obj)/include/kconfig.h
	echo "/* nothing */" > $(obj)/include/kconfig.h

$(obj)/asm_offsets.h:   $(MOL_INCLUDE_DIR)/archinclude.h $(MOL_INCLUDE_DIR)/kernel_vars.h $(MOL_INCLUDE_DIR)/mac_registers.h $(obj)/include/kconfig.h
$(obj)/asm_offsets.h:   $(srctree)/$(src)/asm_offsets.c $(MOL_INCLUDE_DIR)/asm_offsets.inc
	$(RM) -fv $(obj)/tmp-offsets.c $@ ; cat $^ > $(obj)/tmp-offsets.c
	$(CC) -D__KERNEL__ $(CFLAGS) $(LINUXINCLUDE) -I$(MOL_INCLUDE_DIR) -I$(src) -Wall -S $(obj)/tmp-offsets.c
	echo "/* WARNING! Automatically generated from 'shared/asm_offsets.c' - DO NOT EDIT! */" > $@
	grep '^#' tmp-offsets.s >> $@
	$(RM) -fv $(obj)/tmp-offsets.*

