#
# S/390 character devices
#

obj-y += ctrlchar.o keyboard.o defkeymap.o

tub3270-objs := raw3270.o tty3270.o fs3270.o
obj-$(CONFIG_TN3270) += tub3270.o
obj-$(CONFIG_TN3270_CONSOLE) += con3270.o

obj-$(CONFIG_TN3215) += con3215.o

obj-$(CONFIG_SCLP) += sclp.o sclp_rw.o
obj-$(CONFIG_SCLP_TTY) += sclp_tty.o
obj-$(CONFIG_SCLP_CONSOLE) += sclp_con.o
obj-$(CONFIG_SCLP_VT220_TTY) += sclp_vt220.o
obj-$(CONFIG_SCLP_CPI) += sclp_cpi.o

tape-$(CONFIG_S390_TAPE_BLOCK) += tape_block.o
tape-$(CONFIG_PROC_FS) += tape_proc.o
tape-objs := tape_core.o tape_std.o tape_char.o tape_class.o $(tape-y)
obj-$(CONFIG_S390_TAPE) += tape.o
obj-$(CONFIG_S390_TAPE_34XX) += tape_34xx.o
