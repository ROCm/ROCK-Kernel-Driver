ifeq ($(CONFIG_X86),y)
ifeq ($(CONFIG_X86_64),)
CONFIG_IA32=y
endif
endif

flags-$(CONFIG_IA32)	+= -Dlinux32
flags-$(CONFIG_X86_64)	+= -Dlinux32_64
flags-$(CONFIG_IA64)	+= -Dlinux64
#flags-$(CONFIG_PERFMON) += -DPERFMON_SUPPORTED

EXTRA_CFLAGS += -I$(src)/include \
		-DKERNEL_26X \
		-DUSE_TYPE_WAIT_QUEUE_NEW \
		-DUSE_TYPE_FILE_OPS_NEW \
		-DUSE_MM_HEADER \
		-DEXPORTED_FOR_EACH_PROCESS \
		-DUSE_PARENT_TASK_FIELD \
		-DREMAP_PAGE_RANGE_REQUIRES_EXTRA_ARGS \
		-DDSA_SUPPORT_MMAP \
		-DENABLE_TGID \
		-DALLOW_LBRS \
		$(flags-y)

obj-$(CONFIG_VTUNE)	:= vtune_drv.o
vtune_drv-y		:= vtune.o sys_call_table.o
vtune_drv-$(CONFIG_IA32) += vtlib32.o vtoshooks32.o vtxsys32.o \
			   vtlibcommon32.o apic32.o
vtune_drv-$(CONFIG_X86_64) += vtlib32_64.o vtoshooks32_64.o vtxsys32_64.o \
			      apic32_64.o vtlibcommon32.o
vtune_drv-$(CONFIG_IA64) += vtlib64.o vtoshooks64.o vtxsys64.o
