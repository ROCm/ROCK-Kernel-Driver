# Makefile for SubDomain Linux Security Module
#

SUBDOMAINVER=2.0-16.43r5936

EXTRA_CFLAGS += -DSUBDOMAIN_VERSION=\"${SUBDOMAINVER}\" -Wwrite-strings

subdir-$(CONFIG_SECURITY_SUBDOMAIN) += sdmatch
obj-$(CONFIG_SECURITY_SUBDOMAIN) += subdomain.o

subdomain-y := main.o list.o procattr.o lsm.o subdomainfs.o capabilities.o module_interface.o subdomain_version.o
