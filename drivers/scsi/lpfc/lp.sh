d=$1
s=drivers/scsi/lpfc/
n=drivers/net/lpfc/
mkdir $s $n
set -ex
for i in \
lpfcLINUXlan.c \
; do
cp -fv $d/$i $n
done

for i in \
README \
lpfc.conf \
core.c \
lpfc_core.c \
prod_linux.c \
lpfcLINUXfcp.c \
elx.h \
elx_cfgparm.h \
elx_clock.h \
elx_crtn.h \
elx_disc.h \
elx_hw.h \
elx_ioctl.h \
elx_logmsg.h \
elx_mem.h \
elx_os.h \
elx_os_scsiport.h \
elx_sched.h \
elx_scsi.h \
elx_sli.h \
elx_util.h \
hbaapi.h \
lpfc_cfgparm.h \
lpfc_crtn.h \
lpfc_diag.h \
lpfc_disc.h \
lpfc_hba.h \
lpfc_hw.h \
lpfc_ioctl.h \
lpfc_ip.h \
lpfc_module_param.h \
prod_crtn.h \
prod_os.h \
; do
cp -fv $d/$i $s
done
