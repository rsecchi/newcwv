obj-m := tcp_newcwv.o


KDIR := /home/raffaello/virtualbox/ubuntu-precise/

PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

push: all
	cp tcp_newcwv.ko /usr/src/WM_with_TMIX/
	cd /usr/src/WM_with_TMIX/; ./makefs

indent:
	indent -npro -kr -i8 -ts8 -sob -l80 -ss -ncs -cp1 tcp_newcwv.c

