EXTRA_CFLAGS += \
    -Idrivers/infiniband/include \
    -DTS_HOST_DRIVER -DTS_KERNEL_2_6 -D_NO_DATA_PATH_TRACE

obj-m += ib_cm.o

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

$(ib_cm-objs:%=$(obj)/%): $(obj)/cm_packet.h

quiet_cmd_gencmpkt = GEN     $@
      cmd_gencmpkt = $(PERL) $(src)/generate_cm_packet.pl $(FTYPE) \
                     $(src)/cm_packet_type.desc > $@ || ( rm -f $@ && exit 1 )
$(obj)/cm_packet.h: $(src)/generate_cm_packet.pl $(src)/cm_packet_type.desc
	$(call if_changed,gencmpkt)

clean-files := cm_packet.h
