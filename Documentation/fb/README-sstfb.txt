
Introduction

	  This is a frame buffer device driver for 3dfx' Voodoo Graphics 
	(aka voodoo 1, aka sst1) and Voodoo² (aka Voodoo 2, aka CVG) based 
	video boards. It's highly experimental code, but is guaranteed to work
	on my computer, with my "Maxi Gamer 3D" and "Maxi Gamer 3d²" boards,
	and with me "between chair and keyboard". Some people tested other
	combinations and it seems that it works.
	  The main page is located at <http://sstfb.sourceforge.net>, and if
	you want the latest version, check out the CVS, as the driver is a work
	in progress, i feel incomfortable with releasing tarballs of something
	not completely working...Don't worry, it's still more than useable
	(I eat my own dog food)

	  Please read the Bug section, and report any success or failure to me
	(Ghozlane Toumi <gtoumi@messel.emse.fr>).
	  BTW, If you have only one monitor , and you don't feel like playing
	with the vga passthrou cable, I can only suggest borrowing a screen
	somewhere... 


Installation 

	  This driver (should) work on ix86, with any 2.2.x kernel (tested
	with x = 19) and "recent" 2.4.x kernel, as a module or compiled in.
	  You can apply the patches found in sstfb/kernel/*-2.{2|4}.x.patch,
	and copy sstfb.c to linux/drivers/video/, or apply a single patch, 
	sstfb/patch-2.{2|4}.x-sstfb-yymmdd to your linux source tree.

	  Then configure your kernel as usual: choose "m" or "y" to 3Dfx Voodoo
	Graphics in section "console". Compile, install, have fun... and please
	drop me a report :)


Module Usage
	
	Warnings.
	# You should read completely this section before issuing any command.
	# If you have only one monitor to play with, once you insmod the
	  module, the 3dfx takes control of the output, so you'll have to
	  plug the monitor to the "normal" video board in order to issue
	  the commands, or you can blindly use sst_dbg_vgapass
          in the tools directory (See Tools). The latest option is pass the
	  parameter vgapass=1 when insmodding the driver. (See Kernel/Modules
	  Options)

	Module insertion:
	# insmod sstfb.o
	  you should see some strange output frome the board: 
	  a big blue square, a green and a red small squares and a vertical
	  white rectangle. why ? the function's name is self explanatory :
	  "sstfb_test()"...
	  (if you don't have a second monitor, you'll have to plug your monitor
	  directely to the 2D videocard to see what you're typing)
	# con2fb /dev/fbx /dev/ttyx
	  bind a tty to the new frame buffer. if you already have a frame
	  buffer driver, the voodoo fb will likely be /dev/fb1. if not, 
	  the device will be /dev/fb0. You can check this by doing a 
	  cat /proc/fb. You can find a copy of con2fb in tools/ directory.
	  if you don't have another fb device, this step is superfluous,
	  as the console subsystem automagicaly binds ttys to the fb.
	# switch to the virtual console you just mapped. "tadaaa" ...

	Module removal:
	# con2fb /dev/fbx /dev/ttyx
	  bind the tty to the old frame buffer so the module can be removed.
	  (how does it work with vgacon ? short answer : it doesn't work)
	# rmmod sstfb


Kernel/Modules Options

	You can pass some otions to sstfb module, and via the kernel command
	line when the driver is compiled in :
	for module : insmod sstfb.o option1=value1 option2=value2 ...
	in kernel :  video=sstfb:option1,option2:value2,option3 ...
	
	sstfb supports the folowing options :
	module		kernel		description

	vgapass=1	vgapass		enable or disable VGA passthrou cable
	vgapass=0	vganopass	when enabled, the monitor will
					get the signal from the VGA board
					and not from the voodoo. default nopass

	mem=x		mem:x		force frame buffer memory in MiB
					allowed values: 1, 2, 4. default detect

	inverse=1	inverse		suposed to enable inverse console.
					doesn't work ...

	clipping=1	clipping	enable or disable clipping . with
	clipping=0	noclipping	clipping enabled, all offscreen reads
					and writes are disgarded. default:
					enable clipping.

	gfxclk=x	gfxclk:x	force graphic clock frequency (in MHz)
					becarefull with this option .
					default is 50Mhz for voodoo1, 75MHz
					for voodoo2. Be carefull, this one is
					dangerous. default=auto

	slowpci=0	slowpci		enable or disable fast PCI read/writes
	slowpci=1	fastpci		default : fastpci

	dev=x		dev:x		attach the driver to device number x
					0 is the first compatible board (in 
					lspci order)

Tools

	These tools are mostly for debugging purposes, but you can 
	find some of these interesting :
	 - con2fb , maps a tty to a fbramebuffer .
		con2fb /dev/fb1 /dev/tty5
	 - sst_dbg_vgapass , changes vga passthrou. You have to recompile the
	driver with SST_DEBUG and SST_DEBUG_IOCTL set to 1
		sst_dbg_vgapass /dev/fb1 1 (enables vga cable)
		sst_dbg_vgapass /dev/fb1 0 (disables vga cable)
	 - glide_reset , resets the voodoo using glide
		use this after rmmoding sstfb, if the module refuses to
		reinsert .

Bugs

	- DO NOT use glide while the sstfb module is in, you'll most likely
	hang your computer.
	- if you see some artefacts (pixels not cleaning and stuff like that), 
	try turning off clipping (clipping=0)
	- the driver don't detect the 4Mb frame buffer voodoos, it seems that
	the 2 last Mbs wrap around. looking into that .
	- The driver is 16 bpp only, 24/32 won't work.
	- The driver is not your_favorite_toy-safe. this includes SMP...
          [Actually from inspection it seems to be safe - Alan]
	- when using XFree86 FBdev (X over fbdev) you may see strange color
	patterns at the border of your windows (the pixels loose the lowest
	byte -> basicaly the blue component nd some of the green) . I'm unable
	to reproduce this with XFree86-3.3, but one of the testers has this
	problem with XFree86-4. I don't know yet if this is the drivers fault
	or X's (most likely the driver, of course).
	- I didn't really test changing the palette, so you may find some weird
	things when playing with that.
	- Sometimes the driver will not recognise the DAC , and the
        initialisation will fail. this is specificaly true for
	voodoo 2 boards , but it should be solved in recent versions. please
	contact me .
	- the 24/32 is not likely to work anytime soon , knowing that the
	hardware does ... unusual thigs in 24/32 bpp

Todo

	- Get rid of the previous paragraph.
	- Buy more coffee.
	- test/port to other arch.
	- try to add panning using tweeks with front and back buffer .
	- try to implement accel en voodoo2 , this board can actualy do a 
	  lot in 2D even if it was sold as a 3D only board ...

ghoz.

-- 
Ghozlane Toumi <gtoumi@messel.emse.fr>


$Date: 2001/08/29 00:21:11 $
http://sstfb.sourceforge.net/README
