#
# Makefile for the linux memory manager.
#

export-objs := shmem.o filemap.o mempool.o page_alloc.o page-writeback.o

obj-y	 := memory.o mmap.o filemap.o fremap.o mprotect.o mlock.o mremap.o \
	    vmalloc.o slab.o bootmem.o swap.o vmscan.o page_alloc.o \
	    oom_kill.o shmem.o highmem.o mempool.o msync.o mincore.o \
	    readahead.o pdflush.o page-writeback.o rmap.o madvise.o \
	    vcache.o truncate.o

obj-$(CONFIG_SWAP)	+= page_io.o swap_state.o swapfile.o

include $(TOPDIR)/Rules.make
