EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -Idrivers/infiniband/ulp/ipoib \
    -Idrivers/infiniband/hw/mellanox-hca/include \
    -D__LINUX__ \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-$(CONFIG_INFINIBAND) += \
    ib_core.o \
    ib_mad.o \
    ib_cm.o \
    ib_client_query.o \
    ib_sa_client.o \
    ib_dm_client.o \
    ib_useraccess.o \
    ib_useraccess_cm.o

ib_core-objs := \
    smp_access.o \
    smp_conv.o \
    smp_print.o \
    smp_export.o \
    pm_access.o \
    pm_conv.o \
    pm_print.o \
    pm_export.o \
    header_main.o \
    header_ud.o \
    header_export.o \
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

ib_mad-objs := \
    mad_main.o \
    mad_ib.o \
    mad_filter.o \
    mad_thread.o \
    mad_static.o \
    mad_proc.o \
    mad_export.o

ib_cm-objs := \
    cm_main.o \
    cm_api.o \
    cm_common.o \
    cm_active.o \
    cm_passive.o \
    cm_path_migration.o \
    cm_connection_table.o \
    cm_service_table.o \
    cm_proc.o \
    cm_export.o

ib_client_query-objs := \
    client_query.o \
    client_query_export.o \
    client_query_main.o

ib_sa_client-objs := \
    sa_client_main.o \
    sa_client_query.o \
    sa_client_path_record.o \
    sa_client_multicast.o \
    sa_client_port_info.o \
    sa_client_inform.o \
    sa_client_notice.o \
    sa_client_service.o \
    sa_client_export.o \
    sa_client_node_info.o

ib_dm_client-objs := \
    dm_client_main.o \
    dm_client_query.o \
    dm_client_class_port_info.o \
    dm_client_async_notify.o \
    dm_client_iou_info.o \
    dm_client_ioc_profile.o \
    dm_client_svc_entries.o \
    dm_client_host.o \
    dm_client_export.o

ib_useraccess-objs := \
    useraccess_main.o \
    useraccess_ioctl.o \
    useraccess_mad.o

ib_useraccess_cm-objs := \
    useraccess_cm.o

$(ib_core-objs:%=$(obj)/%): \
    $(obj)/smp_access.h $(obj)/smp_types.h \
    $(obj)/pm_access.h $(obj)/pm_types.h 

$(obj)/mad_static.o: $(obj)/smp_access.h $(obj)/smp_types.h
$(ib_cm-objs:%=$(obj)/%): $(obj)/cm_packet.h

quiet_cmd_gendecode = GEN     $@
      cmd_gendecode = $(PERL) $(src)/generate_pkt_access.pl $(FTYPE) \
                      $< > $@ || ( rm -f $@ && exit 1 )

targets += smp_access.h
$(obj)/smp_access.h: FTYPE := header
$(obj)/smp_access.h: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += smp_types.h
$(obj)/smp_types.h: FTYPE := type
$(obj)/smp_types.h: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += smp_access.c
$(obj)/smp_access.c: FTYPE := access
$(obj)/smp_access.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += smp_conv.c
$(obj)/smp_conv.c: FTYPE := conv
$(obj)/smp_conv.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += smp_print.c
$(obj)/smp_print.c: FTYPE := print
$(obj)/smp_print.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += smp_export.c
$(obj)/smp_export.c: FTYPE := export
$(obj)/smp_export.c: $(src)/smp_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_access.h
$(obj)/pm_access.h: FTYPE := header
$(obj)/pm_access.h: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_types.h
$(obj)/pm_types.h: FTYPE := type
$(obj)/pm_types.h: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_access.c
$(obj)/pm_access.c: FTYPE := access
$(obj)/pm_access.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_conv.c
$(obj)/pm_conv.c: FTYPE := conv
$(obj)/pm_conv.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_print.c
$(obj)/pm_print.c: FTYPE := print
$(obj)/pm_print.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

targets += pm_export.c
$(obj)/pm_export.c: FTYPE := export
$(obj)/pm_export.c: $(src)/pm_packet.desc $(src)/generate_pkt_access.pl FORCE
	$(call if_changed,gendecode)

quiet_cmd_gencmpkt = GEN     $@
      cmd_gencmpkt = $(PERL) $(src)/generate_cm_packet.pl $(FTYPE) \
                     $(src)/cm_packet_type.desc > $@ || ( rm -f $@ && exit 1 )

targets += cm_packet.h
$(obj)/cm_packet.h: $(src)/generate_cm_packet.pl $(src)/cm_packet_type.desc FORCE
	$(call if_changed,gencmpkt)

clean-files := \
    smp_access.h smp_types.h smp_access.c smp_conv.c smp_print.c smp_export.c \
    pm_access.h pm_types.h pm_access.c pm_conv.c pm_print.c pm_export.c \
    cm_packet.h
