new-CWV kernel module (draft-ietf-tcpm-newcwv)

newcwv is a Linux kernel module that implements the TCP modification described
in draft-ietf-tcpm-newcwv. It is based on the 'tcp\_congestion\_ops' kernel hooks
for congestion control. We tested it with kernel 3.8.2.

---------------------------------------------------------------------------
Installation
---------------------------------------------------------------------------

First, change the KDIR in Makefile to the linux source code folder and then use
make command to compile the c file. It will create tcp\_newcwv.ko file along
with some other files. (Problem? look at problems faced section). 

Then use these commands to insert the module. These commands require root
privilege; so use   sudo   if in user mode.

1.The command for inserting the module in the running kernel:

	insmod  tcp_newcwv.ko	 
	
it can be checked by  

	cat   /proc/modules
	
where the first entry should show the tcp\_newcwv module.


2.Set the congestion control algorithm to 'newcwv' using the command:
	echo   "newcwv"  >   /proc/sys/net/ipv4/tcp_congestion_control


3.Run a client-server application; whether the tcp\_newcwv was used can be
tested from the kernel log:
	
	dmesg   |    grep "CWVDEBUG"

should show some TCP stats during the application data transfer.


---------------------------------------------------------------------------
Troubleshooting
---------------------------------------------------------------------------

1.insmod: error inserting 'tcp\_newcwv.ko': -1 Invalid module format

This problem occurred when the module was tried to be inserted into any kernel
other than linux-3.8.2. By compiling it, with the current version, it can be
resolved.  In Ubuntu:

	sudo apt-get install build-essential linux-headers-$(uname -r)

Then you need to change the Makefile as follows:

	obj-m := tcp_newcwv.o
	KVERSION = $(shell uname -r)
	PWD := $(shell pwd)

	all: 
		$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules	
	clean:
		$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

And execute 'make' that compiles the .c into a kernel module.


2.insmod: error inserting 'tcp\_newcwv.ko': -1 Required key not available

This problem is due to kernel compilation settings. It is required to sign
the module for the current kernel using the following commands:

	cd  /usr/src/linux   OR  cd ~/kernel/linux-3.8.2   (for Ubuntu) 
	perl scripts/sign-file ./signing_key.priv ./signing_key.x509 /path_to/module

and then insert the module.

