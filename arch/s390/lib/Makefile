#
# Makefile for s390-specific library files..
#

L_TARGET = lib.a

EXTRA_AFLAGS := -traditional

obj-y += delay.o 
obj-$(CONFIG_ARCH_S390_31) += memset.o strcmp.o strncpy.o uaccess.o
obj-$(CONFIG_ARCH_S390X) += memset64.o strcmp64.o strncpy64.o uaccess64.o
