obj-m := tcp_newcwv.o


#KDIR := /lib/modules/3.2.0-32-generic-pae/build
<<<<<<< .mine
KDIR := ../../linux-3.8.2
=======
KVERSION = $(shell uname -r)
#KDIR := ../kernel_src/linux-3.5
>>>>>>> .r1143

PWD := $(shell pwd)

all: 
<<<<<<< .mine
	$(MAKE) -C $(KDIR) M=$(PWD) modules
=======
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	#$(MAKE) -C $(KDIR) M=$(PWD) modules
	#./makefs
>>>>>>> .r1143

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	#$(MAKE) -C $(KDIR) M=$(PWD) clean


