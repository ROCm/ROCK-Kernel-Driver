EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_services.o ib_poll.o ib_packet_lib.o ib_core.o

ib_services-objs := \
    services_main.o \
    services_trace.o \
    services_hash.o \
    services_io.o \
    services_thread.o \
    services_seq_file.o \
    services_rbtree.o \
    services_export.o

ib_poll-objs := \
    poll_main.o \
    poll_timer.o \
    poll_export.o

ib_packet_lib-objs := \
    packet_lib_main.o \
    smp_access.o \
    smp_conv.o   \
    smp_print.o  \
    smp_export.o \
    pm_access.o  \
    pm_conv.o    \
    pm_print.o   \
    pm_export.o

ib_core-objs := \
    core_main.o \
    core_device.o \
    core_pd.o \
    core_ah.o \
    core_qp.o \
    core_cq.o \
    core_mr.o \
    core_fmr.o \
    core_mcast.o \
    core_async.o \
    core_cache.o \
    core_proc.o \
    core_export.o

$(obj)/core_proc.o: $(obj)/pm_access.h $(obj)/pm_types.h

$(ib_packet_lib-objs:%=$(obj)/%): \
    $(obj)/smp_access.h $(obj)/smp_types.h \
    $(obj)/pm_access.h $(obj)/pm_types.h 

quiet_cmd_gendecode = GEN     $@
      cmd_gendecode = $(PERL) $(src)/generate_pkt_access.pl $(FTYPE) \
                      $< > $@ || ( rm -f $@ && exit 1 )

$(obj)/smp_access.h: FTYPE := header
$(obj)/smp_access.h: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/smp_types.h: FTYPE := type
$(obj)/smp_types.h: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/smp_access.c: FTYPE := access
$(obj)/smp_access.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/smp_conv.c: FTYPE := conv
$(obj)/smp_conv.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/smp_print.c: FTYPE := print
$(obj)/smp_print.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/smp_export.c: FTYPE := export
$(obj)/smp_export.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_access.h: FTYPE := header
$(obj)/pm_access.h: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_types.h: FTYPE := type
$(obj)/pm_types.h: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_access.c: FTYPE := access
$(obj)/pm_access.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_conv.c: FTYPE := conv
$(obj)/pm_conv.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_print.c: FTYPE := print
$(obj)/pm_print.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

$(obj)/pm_export.c: FTYPE := export
$(obj)/pm_export.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl
	$(call if_changed,gendecode)

clean-files := \
    smp_access.h smp_types.h smp_acces.c smp_conv.c smp_print.c smp_export.c \
    pm_access.h pm_types.h pm_acces.c pm_conv.c pm_print.c pm_export.c
