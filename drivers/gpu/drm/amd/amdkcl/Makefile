LINUXINCLUDE := $(DKMS_INCLUDE_PREFIX) $(LINUXINCLUDE)

amdkcl-y += symbols.o kcl_mn.o main.o kcl_device_cgroup.o kcl_drm_cache.o kcl_drm.o \
	kcl_fence_array.o kcl_fence.o kcl_io.o kcl_kthread.o kcl_mm.o kcl_pci.o \
	kcl_perf_event.o kcl_reservation.o dma-resv.o kcl_suspend.o kcl_workqueue.o \
	kcl_seq_file.o kcl_connector.o

obj-m += amdkcl.o
