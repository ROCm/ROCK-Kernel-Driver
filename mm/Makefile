#
# Makefile for the linux memory manager.
#

mmu-y			:= nommu.o
mmu-$(CONFIG_MMU)	:= fremap.o highmem.o madvise.o memory.o mincore.o \
			   mlock.o mmap.o mprotect.o mremap.o msync.o rmap.o \
			   shmem.o vmalloc.o

obj-y			:= bootmem.o filemap.o mempool.o oom_kill.o fadvise.o \
			   page_alloc.o page-writeback.o pdflush.o readahead.o \
			   slab.o swap.o truncate.o vmscan.o $(mmu-y)

obj-$(CONFIG_SWAP)	+= page_io.o swap_state.o swapfile.o
obj-$(CONFIG_HUGETLBFS)	+= hugetlb.o
