#
# Makefile for s390-specific library files..
#

EXTRA_AFLAGS := -traditional

lib-y += delay.o 
lib-$(CONFIG_ARCH_S390_31) += memset.o strcmp.o strncpy.o uaccess.o
lib-$(CONFIG_ARCH_S390X) += memset64.o strcmp64.o strncpy64.o uaccess64.o
