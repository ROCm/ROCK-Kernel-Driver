#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-mpu401-objs := mpu401.o
snd-mpu401-uart-objs := mpu401_uart.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_MPU401) += snd-mpu401.o snd-mpu401-uart.o
obj-$(CONFIG_SND_ALS100) += snd-mpu401-uart.o
obj-$(CONFIG_SND_AZT2320) += snd-mpu401-uart.o
obj-$(CONFIG_SND_AZT3328) += snd-mpu401-uart.o
obj-$(CONFIG_SND_DT019X) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ES18XX) += snd-mpu401-uart.o
obj-$(CONFIG_SND_OPL3SA2) += snd-mpu401-uart.o
obj-$(CONFIG_SND_AD1816A) += snd-mpu401-uart.o
obj-$(CONFIG_SND_CS4231) += snd-mpu401-uart.o
obj-$(CONFIG_SND_CS4232) += snd-mpu401-uart.o
obj-$(CONFIG_SND_CS4236) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ES1688) += snd-mpu401-uart.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-mpu401-uart.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-mpu401-uart.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-mpu401-uart.o
obj-$(CONFIG_SND_OPTI93X) += snd-mpu401-uart.o
obj-$(CONFIG_SND_SB16) += snd-mpu401-uart.o
obj-$(CONFIG_SND_SBAWE) += snd-mpu401-uart.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ALS4000) += snd-mpu401-uart.o
obj-$(CONFIG_SND_CMIPCI) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ES1938) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ES1968) += snd-mpu401-uart.o
obj-$(CONFIG_SND_FM801) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ICE1712) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ICE1724) += snd-mpu401-uart.o
obj-$(CONFIG_SND_INTEL8X0) += snd-mpu401-uart.o
obj-$(CONFIG_SND_SONICVIBES) += snd-mpu401-uart.o
obj-$(CONFIG_SND_VIA82XX) += snd-mpu401-uart.o
obj-$(CONFIG_SND_ALI5451) += snd-mpu401-uart.o
obj-$(CONFIG_SND_TRIDENT) += snd-mpu401-uart.o
obj-$(CONFIG_SND_YMFPCI) += snd-mpu401-uart.o
obj-$(CONFIG_SND_PC98_CS4232) += snd-mpu401-uart.o
obj-$(CONFIG_SND_SSCAPE) += snd-mpu401-uart.o

obj-m := $(sort $(obj-m))
