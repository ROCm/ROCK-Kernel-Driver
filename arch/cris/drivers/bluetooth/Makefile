include $(APPS)/Rules.elinux

all:

install:	src/bluetooth.c include/btcommon.h
	ln -sfn ../../arch/cris/drivers/bluetooth/include ../../../../include/linux/bluetooth
	if ! grep arch/cris/drivers/bluetooth/src/Config.in ../Config.in; then \
	  echo '' >> ../Config.in; \
	  echo 'if [ "$$CONFIG_ETRAX_SERIAL" = "y" ]; then' >> ../Config.in; \
	  echo '  source arch/cris/drivers/bluetooth/src/Config.in' >> ../Config.in; \
	  echo 'fi' >> ../Config.in; \
	fi
	if ! grep bluetooth/src/bt.o ../Makefile; then \
	  perl -pi -e "s:include:obj-\\\$$(CONFIG_BLUETOOTH)                 += bluetooth/src/bt.o\nsubdir-\\\$$(CONFIG_BLUETOOTH)              += bluetooth/src\n\ninclude:" ../Makefile; \
	fi

clean:

src/bluetooth.c:
	@echo "You must install the OpenBT src directory before install can be done here!"
	@exit 1

include/btcommon.h:
	@echo "You must install the OpenBT include directory before install can be done here!"
	@exit 1
