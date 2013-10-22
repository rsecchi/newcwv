obj-m := tcp_newcwv.o


KDIR := ../../linux-3.8.2

PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean


