EXTRA_CFLAGS += \
    -I$(src) \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -DTS_KERNEL_2_6 \
    -DMT_KERNEL -D__LINUX__ -DMAX_ERROR=4 -DMTL_MODULE=VIPKL

# XXX This is bogus, but it should work OK for now (since we don't
# XXX support all architectures yet)
ifdef CONFIG_PPC
EXTRA_CFLAGS += -DMT_BIG_ENDIAN
else
EXTRA_CFLAGS += -DMT_LITTLE_ENDIAN
endif

obj-m += mod_vipkl.o

mod_vipkl-objs := \
    vipkl.o \
    hobkl.o \
    pdm.o \
    mmu.o \
    cqm.o \
    srqm.o \
    qpm.o \
    em.o \
    vipkl_eq.o \
    vipkl_cqblk.o \
    devmm.o \
    vipkl_mod_obj.o \
    VIP_rsct.o \
    fork_sup_wrap_kernel.o \
    vipkl_wrap_kernel.o \
    vipklsyms.o
