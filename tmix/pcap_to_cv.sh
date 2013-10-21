#!/bin/bash
# tested with tcpdump 4.3.0 and libpcap 1.3.0

if [ -z "$2" ]; then 
	echo "usage: ./pcap_to_cv.sh [--ns2|--tmix] <pcapfile>"; 
	exit
fi


if [ "$1" != "--ns2"  ]; then
	if [ "$1" != "--tmix" ]; then
		echo "usage: ./pcap_to_cv.sh [--ns2|--tmix] <pcapfile>"; 
		exit
	fi
fi

if [ ! -f "$2" ]; then
	echo $2 not found
	exit
fi


TMP=$(tempfile)
TMP1=$(tempfile)
trap "rm -rf $TMP $TMP1" EXIT

# SELECT SYNs
tcpdump -qnttr $2 'tcp[tcpflags] & (tcp-syn|tcp-ack) == 2' | awk '{print $1,$3,$5}' > $TMP 2> /dev/null

#cat $TMP > TMP

ST=$(head -n 1 $TMP | awk '{print $1}')

# CONNECTIONS
cat $TMP | while read line;
do

	TIME=$(echo $line | sed -En 's/([^ ]*) .*/\1/p')
	SRC_IP=$(echo $line | sed -En 's/[^ ]* ([^ ]*)[.][^ ]* .*/\1/p')
	DST_IP=$(echo $line | sed -nE 's/^.* (.*)[.].*/\1/p')
	SRC_PT=$(echo $line | sed -En 's/[^ ]* [^ ]*[.]([^ ]*) .*/\1/p')
	DST_PT=$(echo $line | sed -En 's/.*\<([^ ]*):/\1/p')
	
	tcpdump -S -nttr $2 \
	   src $SRC_IP and src port $SRC_PT \
	   and dst $DST_IP and dst port $DST_PT 2> /dev/null > $TMP1

	cat $TMP1

	{
		sed -nE 's/^([^ ]+).* (ack [^,]*).*/\1 \2/p' $TMP1
		sed -nE 's/^([^ ]+).* seq ([^:]*:)?([^,]*),.*/\1 seq \3/p' $TMP1
		sed -nE 's/^([^ ]+).* Flags ([^,]*).*/\1 \2/p' $TMP1 | grep S
	} | sort | awk -v st=$ST -v cmd_type=$1 -v TIME=$TIME '
	BEGIN {
		tot_ackd = tot_sent = 0
		rtt = 0
	} 

	{
		if ( $1 < TIME ) {
			next
		}

		if ( $2 == "[S]" ) {
			synsent = $1 - st
			if ( flag == 0 ) {
				next
			}
			exit

		}
	
		// current time
		ct = $1 - st
		if (flag == 0) {
			flag = 1
			if (cmd_type == "--tmix") {
				print "C",ct
			} else {
				print "SEQ",int(ct*1000000),0,0,0
				print "w 640000 640000"
			}
		}

		if ( $2 == "ack" ) {

			if (flag==1) {
				flag = 2
				rtt = ct - synsent
			}

			// set connection start time
			if ( ack == 0 ) {
				ackd = 0
				ack = $3 
				next
			}

			if ( $3 > ack ) {
				if (m==0) {
					m = 1
				}
				ackd += $3 - ack
				tot_ackd += $3 - ack
				time_ackd = ct - synsent
				ack = $3;
				#srv = ct-lt
				lt = ct
			} 

		} else {

			// set connection start time
			if ( seq == 0 ) {
				sent = 0
				seq = $3 
				next
			}
		
			if ( $3 > seq ) {
				if ( m == 1 ) {
					m = 0
					if (cmd_type="--tmix") {
						print sent, ackd, (ct-lt)
					} else {
						print ">", sent	
						print "t", 0
						print "<", ackd
						print "t", ct-lt
					}
					sent = ackd = 0
				}
				sent += $3 - seq 
				tot_sent += sent
				time_sent = ct - synsent
				seq = $3
				lt = ct
			} 
		}

	}
	END {
		if ( (ackd != 0) || (sent!=0) ) {
			if ( cmd_type == "--tmix" ) {
				print sent, ackd, 0
			} else {
				print ">", sent	
				print "t", 0
				print "<", ackd
			}
			#print "t"
			print "tx_rx: ", tot_sent, time_sent, tot_ackd, time_ackd, rtt
		}
	}
	'
	echo
done


