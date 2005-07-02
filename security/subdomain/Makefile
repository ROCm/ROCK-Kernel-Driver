# $Id: Makefile 4471 2005-05-06 00:36:36Z jj $
#
# Makefile for SubDomain Linux Security Module
#

SUBDOMAINVER=1.1-7.42imnx_suse.4494
__NETDOMAIN_CFLAG=
SD_CFLAGS=-DSUBDOMAIN_PROCATTR -DSUBDOMAIN_FS -DPRINTK_TEMPFIX ${__NETDOMAIN_CFLAG}

EXTRA_CFLAGS += -DSUBDOMAIN_VERSION=\"${SUBDOMAINVER}\" ${SD_CFLAGS} -DSD_26_INTERFACE

obj-$(CONFIG_SECURITY_SUBDOMAIN) += subdomain.o

subdomain-objs := main.o list.o sysctl.o lsm.o subdomainfs.o pcre_exec.o capabilities.o subdomain_version.o
