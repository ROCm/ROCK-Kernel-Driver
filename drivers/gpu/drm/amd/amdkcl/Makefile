LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

amdkcl-y += kcl_drm.o main.o symbols.o kcl_fence.o kcl_fence_array.o kcl_drm_cache.o \
	kcl_reservation.o kcl_device_cgroup.o kcl_pci.o kcl_suspend.o \
	kcl_perf_event.o kcl_kthread.o kcl_mm.o kcl_mn.o kcl_io.o \
	kcl_workqueue.o

obj-m += amdkcl.o
