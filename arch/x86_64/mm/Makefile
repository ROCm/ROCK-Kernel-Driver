#
# Makefile for the linux x86_64-specific parts of the memory manager.
#

obj-y	 := init.o fault.o ioremap.o extable.o pageattr.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
obj-$(CONFIG_DISCONTIGMEM) += numa.o
obj-$(CONFIG_K8_NUMA) += k8topology.o

$(obj)/hugetlbpage.c: 
	@ln -sf ../../i386/mm/hugetlbpage.c $(obj)/hugetlbpage.c

clean-files += hugetlbpage.c
