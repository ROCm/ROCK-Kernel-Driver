ccflags-y := -Iinclude/drm -Idrivers/gpu/drm/amdsoc/
ccflags-y += -Idrivers/gpu/drm/amdsoc/include/
ccflags-y += -Idrivers/gpu/drm/amd/include/bus/
ccflags-y += -Idrivers/gpu/drm/amd/acp/include
ccflags-y += -Idrivers/gpu/drm/amd/include/
ccflags-y += -Idrivers/gpu/drm/amd/include/asic_reg/acp
snd-soc-acp-pcm-objs	:= acp-pcm-dma.o
snd-soc-acp-rt286-mach-objs := acp-rt286.o

obj-$(CONFIG_SND_SOC_AMD_ACP) += snd-soc-acp-pcm.o
obj-$(CONFIG_SND_SOC_AMD_CZ_RT286_MACH) += snd-soc-acp-rt286-mach.o
