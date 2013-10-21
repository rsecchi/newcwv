newcwv
======

new-CWV kernel module (draft-ietf-tcpm-newcwv)


newcwv is a Linux kernel module that implements the TCP modification described
in draft-ietf-tcpm-newcwv. It is based on the 'tcp_congestion_ops' kernel hooks
for congestion control. We tested it with kernel 3.8.2.


KDIR variable in Makefile should point to the Linux kernel sources.
'make' will compile the module.


The module is loaded in memory using: insmod ./tcp_newcwv.ko
Then, enable the module using: echo "newcwv" > /proc/sys/net/ipv4/tcp_congestion_control

