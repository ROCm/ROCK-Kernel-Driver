                   QLogic iSCSI Driver for Kernel 2.6.x

Products supported: QLA4010/QLA4010C

03/15/2005



Contents
--------

1. OS Support

2. Supported Features

3. Release History

4. Saving the Driver Source to Diskette

5. Installing the driver

   5.1  Building the driver from the Source
   5.2  Load the Driver Manually using INSMOD or MODPROBE
   5.3  Making a RAMDISK Image to Load the Driver

6. Driver Parameters
   6.1  System Parameters
   6.2  Multiple LUN Support
   6.3  Driver Command Line Parameters
7. Limitations

8. Contacting QLogic


**********************************************************************


1. OS Support
-------------

This driver works with Linux kernel 2.6.x distributions. 


**********************************************************************


2. Supported Features
---------------------
* Support up to 128 targets

**********************************************************************


3. Release History
------------------

Please refer to Release Notes (release.txt).


**********************************************************************


4. Saving the Driver Source to Diskette
---------------------------------------
1. Download the qla4xxx driver
   qla4xxx-vx.yy.zz.tar.gz file from QLogic's website.

2. If prompted "What would you like to do with this file?" choose 
   "Save this file to disk."

3. Insert a blank diskette and download to the diskette directly.


**********************************************************************
5. Installing the Driver 
------------------------
The kernel sources should be already installed on the system for the relevant
kernel or install it from the RedHat/SuSE source CD(s) before proceeding further. 
Most distributions include the kernel source on the first disc of the distribution CDs.

This section makes extensive use of the build.sh script located in
driver source (extras/build.sh).  This script currently supports
driver compilation (installation and updates) on SLES9 and RHEL4
distributions on main hardware platforms.

The build.sh script supports for following directives:

   # ./extras/build.sh

     Build the driver sources based on the standard SLES9/RHEL4
     build environment.

   # ./extras/build.sh clean

     Clean driver source directory of all build files (i.e.
     *.ko, *.o, etc).

   # ./extras/build.sh new

     Rebuild the driver sources from scratch.
     This is essentially a shortcut for:

        # ./build.sh clean
        # ./build.sh

   # ./extras/build.sh install

     Build and install the driver module files.
     This command performs the following:

        1. Builds the driver .ko files.
        2. Copies the .ko files to the appropriate
           /lib/modules/... directory.
        3. Adds the appropriate directive in the
           modprobe.conf[.local] to remove the qla4xxx_conf
           module when the qla4xxx modules in unloaded.
       
   # ./extras/build.sh initrd

     Build, install, and update the initrd image.
     This command performs the following:

        1. All steps in the 'install' directive.
        2. Adds an qla4xxx_conf entry into the
           /etc/sysconfig/kernel INITRD_MODULES if and only if a
           qla4xxx module already is present in the listing.
        3. Rebuilds the initrd image with the /sbin/mk_initrd
           command.

     NOTE: Refer to the OS specific Driver Disk Kit documentation
         for the procedure to install iSCSI bootable systems.

5.1  Building a Driver from the Source Code 
-------------------------------------------

From the source code, you can build a qla4xxx.ko for your host system, 
and load the driver manually or automatically using a RAMDISK image during
system boot time.

   
1. Using the diskette you created in Section 4, copy the
   qla4xxx-vx.yy.zz.tar.gz file to /qla4xxx. Follow these steps from
   the "/" (root) directory:

       # mkdir qla4xxx
       # cd qla4xxx
       # mount /mnt/floppy
       # cp /mnt/floppy/*.gz] . (the period at the end is required)
       # tar -xvzf *.gz
       # cd x.yy.zz

2. Build the Driver modules from the source code by executing the
   build.sh script.

       # ./extras/build.sh
	
3. To load the driver manually, see section 5.2. To make a RAMDISK
   image to load the driver during system boot time, see section 5.3.


5.2  Load the Driver Manually using INSMOD or MODPROBE
------------------------------------------------------

Before loading the driver manually, first build the driver binary from
the driver source files as described in sections 5.1.

- To load the driver directly from the local build directory, type
  the following in order:

        # insmod qla4xxx.ko
- To load the driver using modprobe:

       1. Install the driver module *.ko file to the appropriate
	  kernel module directory:

	  # ./extras/build.sh install

       2. Type the following to load the driver for qla4xxx HBAs:
                  
          # modprobe -v qla4xxx

 - To unload/uninstall the driver:
	 1. # rmmod qla4xxx or
	 2. # modprobe -r qla4xxx


5.3  Making a RAMDISK Image to Load the Driver
----------------------------------------------

The build.sh script can be used to update and create and initrd
image:

	# ./extras/build.sh initrd

**********************************************************************

6. Driver Parameters
--------------------

6.1  System Parameters
----------------------
Most driver and device parameters are exported via the sysfs as
regular file, so you can change parameters using regular commands. The 
following path is where device parameters can be found:  

/sys/class/scsi_device/<host>:<bus>:<target>:<lun>/device/

timeout - In the 2.6 kernel, a command timeout is 30 seconds, so if a command takes
	longer than 30 seconds, which could happen in a network environment then  
	it may be necessary to increase this timeout value in sysfs for the device.

Example:
	echo 60  >/sys/class/scsi_device/4:0:0:0/device/timeout


The Linux driver does not immediately recognize dynamically added devices automatically;
therefore, a manual rescan must be performed.
rescan - This will initiate a rescan on the device 

	echo "- - -" > /sys/class/scsi_host/hostX/scan
Example:
	echo "- - -" > /sys/class/scsi_host/host6/scan

Where "hostX" is replaced by your "Host number".


6.2  Multiple LUN Support
---------------------------

Support for multiple LUNs can be configured by doing the following: 

(1) If the SCSI Mid-Layer is compiled as a module, add the following
    line to the loadable module configuration file modprobe.conf, found under
    /etc directory to scan for multiple LUNs:

	options scsi_mod max_luns=255
	
NOTE:  If you have multiple adapters, set max_luns to the
       largest number of LUNs supported by any one of these adapters.

**********************************************************************


6.3  Driver Command Line Parameters
-----------------------------------
The driver gets its parameters from the command line itself or from  
modprobe 'option' directive found in the modules.conf file. The parameters 
are in simple <keyword>=value format, i.e. ql4xdiscoverwait=60, 
Where <keyword> is one of the following option parameters:

	Usage: insmod qla4xxx <keyword>=value
	
* 	ql4xmaxqdepth - This parameter defines the maximum queue depth reported 
	to Scsi Mid-Level per device. The Queue depth specifies the number of 
	outstanding requests per lun. (obsolete with sysfs)
	Default: 2.

*	ql4xdiscoverywait - This parameter defines how long to wait for a port
	during boot-time.
	Default: 60

*	ql4xcmdretrycount - This parameter defines the maximum number of Scsi 
	Mid-Level retries allowed per command. 
	Default: 20 

*	extended_error_logging - This parameter enables debug and defines the 
	debug level to use. The debug information s written to /var/log/messages.
	Default:  0  (disable)
	
	All the available parameters can be viewed using one 
	of the following commands:

	# modinfo -p qla4xxx.ko

 Usage examples:

    # insmod qla4xxx.ko ql4xdiscoverwait=60
**********************************************************************
7. Limitations
	None
**********************************************************************


8. Contacting QLogic 
---------------------

Please visit QLogic's website (www.qlogic.com). On this site you will
find product information, our latest drivers, and links for technical
assistance if needed.


======================================================================


    Copyright (c) 2005 QLogic Corporation. All rights reserved 
    worldwide. 




