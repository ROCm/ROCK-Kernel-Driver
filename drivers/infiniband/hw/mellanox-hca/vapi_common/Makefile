EXTRA_CFLAGS += \
    -I$(src) \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -DTS_KERNEL_2_6 \
    -DMT_KERNEL -D__LINUX__ -DMAX_ERROR=4 -DMTL_MODULE=VAPICOMMON

# XXX This is bogus, but it should work OK for now (since we don't
# XXX support all architectures yet)
ifdef CONFIG_PPC
EXTRA_CFLAGS += -DMT_BIG_ENDIAN
else
EXTRA_CFLAGS += -DMT_LITTLE_ENDIAN
endif

obj-m += mod_vapi_common.o

mod_vapi_common-objs := \
    vip_array.o \
    vip_hash.o \
    vip_cirq.o \
    vip_delay_unlock.o \
    vapi_common.o \
    vapicommonsyms.o
