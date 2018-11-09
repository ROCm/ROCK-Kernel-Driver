LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

amdkcl-y += kcl_drm.o main.o symbols.o kcl_fence.o kcl_fence_array.o \
	kcl_kthread.o kcl_io.o kcl_reservation.o  \
	kcl_pci.o kcl_mn.o kcl_bitmap.o

obj-m += amdkcl.o
