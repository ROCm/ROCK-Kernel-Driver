#
# Makefile for the kernel character device drivers.
#

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

obj-y	 += mem.o tty_io.o n_tty.o tty_ioctl.o raw.o pty.o misc.o random.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=	busmouse.o console.o keyboard.o sysrq.o \
			misc.o pty.o random.o selection.o serial.o \
			sonypi.o tty_io.o tty_ioctl.o generic_serial.o rtc.o \
			ip2main.o

KEYMAP   =defkeymap.o
KEYBD    =pc_keyb.o
CONSOLE  =console.o
SERIAL   =serial.o

ifeq ($(ARCH),s390)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL   =
endif

ifeq ($(ARCH),mips)
  ifneq ($(CONFIG_PC_KEYB),y)
    KEYBD    =
  endif
endif

ifeq ($(ARCH),s390x)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  SERIAL   =
endif

ifeq ($(ARCH),m68k)
   ifdef CONFIG_AMIGA
      KEYBD = amikeyb.o
   else
      KEYBD =
   endif
   SERIAL   =
endif

ifeq ($(ARCH),arm)
  ifneq ($(CONFIG_PC_KEYMAP),y)
    KEYMAP   =
  endif
  ifneq ($(CONFIG_PC_KEYB),y)
    KEYBD    =
  endif
endif

ifeq ($(ARCH),sh)
  KEYMAP   =
  KEYBD    =
  CONSOLE  =
  ifeq ($(CONFIG_SH_HP600),y)
  KEYMAP   = defkeymap.o
  KEYBD    = scan_keyb.o hp600_keyb.o
  CONSOLE  = console.o
  endif
  ifeq ($(CONFIG_SH_DMIDA),y)
  # DMIDA does not connect the HD64465 PS/2 keyboard port
  # but we allow for USB keyboards to be plugged in.
  KEYMAP   = defkeymap.o
  KEYBD    = # hd64465_keyb.o pc_keyb.o
  CONSOLE  = console.o
  endif
  ifeq ($(CONFIG_SH_EC3104),y)
  KEYMAP   = defkeymap.o
  KEYBD    = ec3104_keyb.o
  CONSOLE  = console.o
  endif
  ifeq ($(CONFIG_SH_DREAMCAST),y)
  KEYMAP   = defkeymap.o
  KEYBD    =
  CONSOLE  = console.o
  endif
endif

ifeq ($(CONFIG_DECSTATION),y)
  KEYMAP   =
  KEYBD    =
  SERIAL   = decserial.o
endif

ifeq ($(CONFIG_BAGET_MIPS),y)
  KEYBD    =
  SERIAL   =
endif

ifeq ($(CONFIG_NINO),y)
  SERIAL   =
endif

ifneq ($(CONFIG_SUN_SERIAL),)
  SERIAL   =
endif

ifeq ($(CONFIG_QTRONIX_KEYBOARD),y)
  KEYBD    = qtronix.o
  KEYMAP   = qtronixmap.o
endif

obj-$(CONFIG_VT) += vt.o vc_screen.o consolemap.o consolemap_deftbl.o $(CONSOLE) selection.o
obj-$(CONFIG_SERIAL) += $(SERIAL)
obj-$(CONFIG_SERIAL_ACPI) += acpi_serial.o
obj-$(CONFIG_SERIAL_21285) += serial_21285.o
obj-$(CONFIG_SERIAL_SA1100) += serial_sa1100.o
obj-$(CONFIG_SERIAL_AMBA) += serial_amba.o

ifndef CONFIG_SUN_KEYBOARD
  obj-$(CONFIG_VT) += keyboard.o $(KEYMAP) $(KEYBD)
else
  obj-$(CONFIG_PCI) += keyboard.o $(KEYMAP)
endif

obj-$(CONFIG_MAGIC_SYSRQ) += sysrq.o
obj-$(CONFIG_ATARI_DSP56K) += dsp56k.o
obj-$(CONFIG_ROCKETPORT) += rocket.o
obj-$(CONFIG_MOXA_SMARTIO) += mxser.o
obj-$(CONFIG_MOXA_INTELLIO) += moxa.o
obj-$(CONFIG_DIGI) += pcxx.o
obj-$(CONFIG_DIGIEPCA) += epca.o
obj-$(CONFIG_CYCLADES) += cyclades.o
obj-$(CONFIG_STALLION) += stallion.o
obj-$(CONFIG_ISTALLION) += istallion.o
obj-$(CONFIG_COMPUTONE) += ip2.o ip2main.o
obj-$(CONFIG_RISCOM8) += riscom8.o
obj-$(CONFIG_ISI) += isicom.o
obj-$(CONFIG_ESPSERIAL) += esp.o
obj-$(CONFIG_SYNCLINK) += synclink.o
obj-$(CONFIG_SYNCLINKMP) += synclinkmp.o
obj-$(CONFIG_N_HDLC) += n_hdlc.o
obj-$(CONFIG_SPECIALIX) += specialix.o
obj-$(CONFIG_AMIGA_BUILTIN_SERIAL) += amiserial.o
obj-$(CONFIG_A2232) += ser_a2232.o generic_serial.o
obj-$(CONFIG_SX) += sx.o generic_serial.o
obj-$(CONFIG_RIO) += rio/ generic_serial.o
obj-$(CONFIG_SH_SCI) += sh-sci.o generic_serial.o
obj-$(CONFIG_SERIAL167) += serial167.o
obj-$(CONFIG_MVME147_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_MVME162_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_BVME6000_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_SERIAL_TX3912) += generic_serial.o serial_tx3912.o
obj-$(CONFIG_HVC_CONSOLE) += hvc_console.o

obj-$(CONFIG_ATIXL_BUSMOUSE) += atixlmouse.o
obj-$(CONFIG_LOGIBUSMOUSE) += logibusmouse.o
obj-$(CONFIG_PRINTER) += lp.o

obj-$(CONFIG_BUSMOUSE) += busmouse.o
obj-$(CONFIG_DTLK) += dtlk.o
obj-$(CONFIG_R3964) += n_r3964.o
obj-$(CONFIG_APPLICOM) += applicom.o
obj-$(CONFIG_SONYPI) += sonypi.o
obj-$(CONFIG_MS_BUSMOUSE) += msbusmouse.o
obj-$(CONFIG_82C710_MOUSE) += qpmouse.o
obj-$(CONFIG_AMIGAMOUSE) += amigamouse.o
obj-$(CONFIG_ATARIMOUSE) += atarimouse.o
obj-$(CONFIG_ADBMOUSE) += adbmouse.o
obj-$(CONFIG_PC110_PAD) += pc110pad.o
obj-$(CONFIG_RTC) += rtc.o
obj-$(CONFIG_EFI_RTC) += efirtc.o
ifeq ($(CONFIG_PPC),)
  obj-$(CONFIG_NVRAM) += nvram.o
endif
obj-$(CONFIG_TOSHIBA) += toshiba.o
obj-$(CONFIG_I8K) += i8k.o
obj-$(CONFIG_DS1620) += ds1620.o
obj-$(CONFIG_INTEL_RNG) += i810_rng.o
obj-$(CONFIG_QIC02_TAPE) += tpqic02.o
obj-$(CONFIG_FTAPE) += ftape/
obj-$(CONFIG_H8) += h8.o
obj-$(CONFIG_PPDEV) += ppdev.o
obj-$(CONFIG_DZ) += dz.o
obj-$(CONFIG_NWBUTTON) += nwbutton.o
obj-$(CONFIG_NWFLASH) += nwflash.o

# Only one watchdog can succeed. We probe the hardware watchdog
# drivers first, then the softdog driver.  This means if your hardware
# watchdog dies or is 'borrowed' for some reason the software watchdog
# still gives you some cover.

obj-$(CONFIG_PCWATCHDOG) += pcwd.o
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
obj-$(CONFIG_IB700_WDT) += ib700wdt.o
obj-$(CONFIG_MIXCOMWD) += mixcomwd.o
obj-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
obj-$(CONFIG_WDT) += wdt.o
obj-$(CONFIG_WDTPCI) += wdt_pci.o
obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_I810_TCO) += i810-tco.o
obj-$(CONFIG_MACHZ_WDT) += machzwd.o
obj-$(CONFIG_SH_WDT) += shwdt.o
obj-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o
obj-$(CONFIG_MWAVE) += mwave/
obj-$(CONFIG_AGP) += agp/
obj-$(CONFIG_DRM) += drm/
obj-$(CONFIG_PCMCIA) += pcmcia/

host-progs := conmakehash

include $(TOPDIR)/Rules.make

$(obj)/consolemap_deftbl.c: $(src)/$(FONTMAPFILE) $(obj)/conmakehash
	$(obj)/conmakehash $< > $@

$(obj)/defkeymap.c: $(src)/defkeymap.map
	set -e ; loadkeys --mktable $< | sed -e 's/^static *//' > $@

$(obj)/qtronixmap.c: $(src)/qtronixmap.map
	set -e ; loadkeys --mktable $< | sed -e 's/^static *//' > $@
