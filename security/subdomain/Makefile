# $Id: Makefile,v 1.31 2004/10/01 17:28:37 tony Exp $
#
# Makefile for SubDomain Linux Security Module
#

__NETDOMAIN_CFLAG=

SD_CFLAGS=-DSUBDOMAIN_PROCATTR -DSUBDOMAIN_FS ${__NETDOMAIN_CFLAG}

EXTRA_CFLAGS += -DSUBDOMAIN_VERSION=\"${SUBDOMAINVER}\" ${SD_CFLAGS} -DSD_26_INTERFACE

obj-$(CONFIG_SECURITY_SUBDOMAIN) += subdomain.o

subdomain-objs := main.o list.o sysctl.o lsm.o subdomainfs.o pcre_exec.o capabilities.o subdomain_version.o
