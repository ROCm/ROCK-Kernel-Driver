#
# Makefile for the kernel character device drivers.
#

# Only one watchdog can succeed. We probe the hardware watchdog
# drivers first, then the softdog driver.  This means if your hardware
# watchdog dies or is 'borrowed' for some reason the software watchdog
# still gives you some cover.

watchdog-$(CONFIG_PCWATCHDOG) += pcwd.o
watchdog-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
watchdog-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
watchdog-$(CONFIG_IB700_WDT) += ib700wdt.o
watchdog-$(CONFIG_MIXCOMWD) += mixcomwd.o
watchdog-$(CONFIG_SCx200_WDT) += scx200_wdt.o
watchdog-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
watchdog-$(CONFIG_WDT) += wdt.o
watchdog-$(CONFIG_WDTPCI) += wdt_pci.o
watchdog-$(CONFIG_21285_WATCHDOG) += wdt285.o
watchdog-$(CONFIG_977_WATCHDOG) += wdt977.o
watchdog-$(CONFIG_I810_TCO) += i810-tco.o
watchdog-$(CONFIG_MACHZ_WDT) += machzwd.o
watchdog-$(CONFIG_SH_WDT) += shwdt.o
watchdog-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
watchdog-$(CONFIG_SOFT_WATCHDOG) += softdog.o

include $(TOPDIR)/Rules.make
