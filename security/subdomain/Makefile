# $Id: Makefile 4994 2005-08-03 17:35:16Z tonyj $
#
# Makefile for SubDomain Linux Security Module
#

SUBDOMAINVER=1.2-13.42r5011imnx_suse
SD_CFLAGS=-DSUBDOMAIN_PROCATTR -DSUBDOMAIN_FS -DPRINTK_TEMPFIX

EXTRA_CFLAGS += -DSUBDOMAIN_VERSION=\"${SUBDOMAINVER}\" ${SD_CFLAGS}

obj-$(CONFIG_SECURITY_SUBDOMAIN) += subdomain.o

subdomain-objs := main.o list.o sysctl.o lsm.o subdomainfs.o pcre_exec.o capabilities.o subdomain_version.o
