#
# Makefile for the Linux Traffic Control Unit.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := sched.o

obj-y	:= sch_generic.o

ifeq ($(CONFIG_NET_SCHED), y)

obj-y += sch_api.o sch_fifo.o

ifeq ($(CONFIG_NET_ESTIMATOR), y)
obj-y += estimator.o
endif

ifeq ($(CONFIG_NET_CLS), y)
obj-y += cls_api.o

ifeq ($(CONFIG_NET_CLS_POLICE), y)
obj-y += police.o
endif

endif

ifeq ($(CONFIG_NET_SCH_INGRESS), y)
obj-y += sch_ingress.o 
else
  ifeq ($(CONFIG_NET_SCH_INGRESS), m)
	obj-m += sch_ingress.o 
  endif
endif

ifeq ($(CONFIG_NET_SCH_CBQ), y)
obj-y += sch_cbq.o
else
  ifeq ($(CONFIG_NET_SCH_CBQ), m)
	obj-m += sch_cbq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_CSZ), y)
obj-y += sch_csz.o
else
  ifeq ($(CONFIG_NET_SCH_CSZ), m)
	obj-m += sch_csz.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_HPFQ), y)
obj-y += sch_hpfq.o
else
  ifeq ($(CONFIG_NET_SCH_HPFQ), m)
	obj-m += sch_hpfq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_HFSC), y)
obj-y += sch_hfsc.o
else
  ifeq ($(CONFIG_NET_SCH_HFSC), m)
	obj-m += sch_hfsc.o
  endif
endif


ifeq ($(CONFIG_NET_SCH_SFQ), y)
obj-y += sch_sfq.o
else
  ifeq ($(CONFIG_NET_SCH_SFQ), m)
	obj-m += sch_sfq.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_RED), y)
obj-y += sch_red.o
else
  ifeq ($(CONFIG_NET_SCH_RED), m)
	obj-m += sch_red.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_TBF), y)
obj-y += sch_tbf.o
else
  ifeq ($(CONFIG_NET_SCH_TBF), m)
	obj-m += sch_tbf.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_PRIO), y)
obj-y += sch_prio.o
else
  ifeq ($(CONFIG_NET_SCH_PRIO), m)
	obj-m += sch_prio.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_TEQL), y)
obj-y += sch_teql.o
else
  ifeq ($(CONFIG_NET_SCH_TEQL), m)
	obj-m += sch_teql.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_GRED), y)
obj-y += sch_gred.o
else
  ifeq ($(CONFIG_NET_SCH_GRED), m)
       obj-m += sch_gred.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_DSMARK), y)
obj-y += sch_dsmark.o
else
  ifeq ($(CONFIG_NET_SCH_DSMARK), m)
       obj-m += sch_dsmark.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_TCINDEX), y)
obj-y += cls_tcindex.o
else
  ifeq ($(CONFIG_NET_CLS_TCINDEX), m)
       obj-m += cls_tcindex.o
  endif
endif

ifeq ($(CONFIG_NET_SCH_ATM), y)
obj-y += sch_atm.o
endif

ifeq ($(CONFIG_NET_CLS_U32), y)
obj-y += cls_u32.o
else
  ifeq ($(CONFIG_NET_CLS_U32), m)
	obj-m += cls_u32.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_RSVP), y)
obj-y += cls_rsvp.o
else
  ifeq ($(CONFIG_NET_CLS_RSVP), m)
	obj-m += cls_rsvp.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_RSVP6), y)
obj-y += cls_rsvp6.o
else
  ifeq ($(CONFIG_NET_CLS_RSVP6), m)
	obj-m += cls_rsvp6.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_ROUTE4), y)
obj-y += cls_route.o
else
  ifeq ($(CONFIG_NET_CLS_ROUTE4), m)
	obj-m += cls_route.o
  endif
endif

ifeq ($(CONFIG_NET_CLS_FW), y)
obj-y += cls_fw.o
else
  ifeq ($(CONFIG_NET_CLS_FW), m)
	obj-m += cls_fw.o
  endif
endif

endif

include $(TOPDIR)/Rules.make
