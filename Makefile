obj-m := tcp_newcwv.o


KDIR := /home/raffaello/virtualbox/ubuntu-precise/

PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean


