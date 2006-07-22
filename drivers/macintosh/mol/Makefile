
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

MOL_SRC_INCLUDE_DIR := $(srctree)/$(src)/include
MOL_OBJ_INCLUDE_DIR := $(obj)/include
MOL_ASMFLAGS    := $(ASMFLAGS) $(INCLUDES) -D__ASSEMBLY__ -D__KERNEL__
EXTRA_CFLAGS	:= -I$(MOL_SRC_INCLUDE_DIR) -I$(MOL_OBJ_INCLUDE_DIR)

$(obj)/asm_offsets.c:	$(srctree)/$(src)/asm_offsets.c $(MOL_OBJ_INCLUDE_DIR)/asm_offsets.h
	rm -f $@
	cat $(srctree)/$(src)/asm_offsets.c $(MOL_OBJ_INCLUDE_DIR)/asm_offsets.h > $@

$(obj)/_traps.o:	$(MOL_OBJ_INCLUDE_DIR)/asm_offsets.h $(srctree)/$(src)/asm-files/*.S

$(obj)/_%.o: $(src)/asm-files/%.S
	echo "  AS [x]   $@"
	rm -f $@ $@.s
	$(CPP) $(LINUXINCLUDE) -I$(MOL_SRC_INCLUDE_DIR) -I$(MOL_OBJ_INCLUDE_DIR) $(MOL_ASMFLAGS) $< | m4 > $@.m4
	cat $@.m4 > $@.s
	$(AS) $@.s $(AS_FLAGS) -o $@
	rm -f $@.s $@.m4

$(addprefix $(obj)/,$(MOL_OBJS)): $(obj)/%.o: $(MOL_OBJ_INCLUDE_DIR)/kconfig.h
$(MOL_OBJ_INCLUDE_DIR)/kconfig.h:
	mkdir -vp $(MOL_OBJ_INCLUDE_DIR)
	rm -fv $@
	echo "/* nothing */" > $@

$(MOL_OBJ_INCLUDE_DIR)/asm_offsets.h:   $(MOL_SRC_INCLUDE_DIR)/archinclude.h $(MOL_SRC_INCLUDE_DIR)/kernel_vars.h $(MOL_SRC_INCLUDE_DIR)/mac_registers.h $(MOL_OBJ_INCLUDE_DIR)/kconfig.h
$(MOL_OBJ_INCLUDE_DIR)/asm_offsets.h:   $(srctree)/$(src)/asm_offsets.c $(MOL_SRC_INCLUDE_DIR)/asm_offsets.inc
	rm -fv $(obj)/tmp-offsets.c $@ ; cat $^ > $(obj)/tmp-offsets.c
	$(CC) -D__KERNEL__ $(CFLAGS) $(LINUXINCLUDE) -I$(MOL_SRC_INCLUDE_DIR) -I$(MOL_OBJ_INCLUDE_DIR) -Wall -S $(obj)/tmp-offsets.c
	echo "/* WARNING! Automatically generated from 'shared/asm_offsets.c' - DO NOT EDIT! */" > $@
	grep '^#' tmp-offsets.s >> $@
	rm -fv $(obj)/tmp-offsets.*

