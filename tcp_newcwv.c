/* method 7: bug fixes with FS of prev. algorithm 
 * the recovery method updated as well
 */
 
#undef __KERNEL__
#define __KERNEL__

#undef MODULE
#define MODULE

#include <net/tcp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <net/dst.h>
#include <net/inet_common.h>
#include <linux/ipsec.h>
#include <asm/unaligned.h>
#include <net/netdma.h>

#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/types.h>

/* starting from IW? */
#define INVALID_PIPEACK_VALUE 		-1
#define INVALID_PIPEACK_RETURN_VALUE 	-1
#define PIPEACK_INIT  			TCP_INFINITE_SSTHRESH
#define TCP_RESTART_WINDOW		1
#define FIVEMINS  			(HZ*300)
#define ONESEC 				HZ
#define RTT_EST 			1
#define MAX_NO_OF_BINS			4
#define INVALID_INDEX			-1

u32 PSP;	/* pipeACK Sampling Period: valid duration of a pipeACK in bin */
u32 PMP;	/* pipeACK measurement period: bin duration */
u32 NO_OF_BINS;
bool in_recovery;


struct newcwv {
	
	int32_t pipeack[MAX_NO_OF_BINS];
	u32 time_stamp[MAX_NO_OF_BINS];
	int32_t pipeack_variable;		/* contains the value of the pipeack */
	int head:3;
	int tail:3;
	
	int is_valid:2;               	/* =0 when TCP rate is data-limited */
	u32 prior_in_flight;        	/* Packets in flight for cwnd reduction */
	u32 prior_retrans;          	/* Retransmission before going into FR */
	u32 prev_snd_una;		/* snd_una when last record kept */ 
	u32 prev_snd_nxt;		/* snd_una when last record kept */
	u32 cwnd_valid_ts;		/* last time cwnd was found 'validated' */
	u32 cwnd_before_recovery;	
};


static u32 my_division(u32 dividend, u32 divisor){
	u32 i=0;
	if (divisor == 0 )
		return i;
	while (divisor < dividend){
		dividend = dividend - divisor;
		i++;
	}
	return i;
}


/* this function will add an element to the Linked list of pipeack samples and return the new element pointer */
void add_element(struct newcwv *nc, int32_t val){
	//array is full; this entry will overwrite the prev. value;advance head
	//printk("CWVDEBUG_9: add A : head %d   tail %d\n", nc->head,nc->tail);
	if ( nc->head == ((nc->tail+1) % MAX_NO_OF_BINS) )  
		nc->head = (nc->head +1) % MAX_NO_OF_BINS;
	
	if (nc->head == INVALID_INDEX){//beginning of the array
		nc->head = 0;
		nc->tail = 0;
	}
	else
		nc->tail = (nc->tail+1)% MAX_NO_OF_BINS;
	//store the value in the tail
	nc->pipeack[nc->tail] = val;
	nc->time_stamp[nc->tail] = tcp_time_stamp;
	//printk("CWVDEBUG_9: add B: head %d   tail %d\n", nc->head,nc->tail);
}


/* this fuction will remove all the expired elements based on the time_stamp 
	and return a pipeack element: value = the maximum from the remaining elements
	time_stamp  = the time stamp of the last element
	next = the pointer to the last element
	*/
int32_t remove_expired_element_and_return_pipeack_element(struct newcwv *nc){
	
	int32_t tmp_pa = nc->pipeack_variable;
	bool changed = false;
	short tmp1;
	
	//printk("CWVDEBUG_9: rem A head %d   tail %d\n", nc->head,nc->tail);
	// if no element in the list, then the pipeack is not valid
	if (nc->head == INVALID_INDEX)
		return INVALID_PIPEACK_VALUE;
	
	// start checking from the beginning of the list for expired elements
	while ( nc->time_stamp[nc->head] < (tcp_time_stamp - PSP) ){
		//printk("CWVDEBUG_5: inside for removing element\n");
		changed = true;
		nc->pipeack[nc->head] = INVALID_PIPEACK_VALUE;
		nc->time_stamp[nc->tail]= 0;
		
		//that was the last element
		if (nc->tail == nc->head){
			nc->head = nc->tail =INVALID_INDEX;
			return INVALID_PIPEACK_VALUE;
		}
		else
			nc->head = (nc->head +1) % MAX_NO_OF_BINS;
		
	}
	
	// if changes made into the list, check rest of the list for the maximum
	if (changed){
		tmp1=nc->head;
		while (tmp1!= nc->tail){
			if ( nc->pipeack[tmp1]> tmp_pa)
				tmp_pa = nc->pipeack[tmp1];
			tmp1 = (tmp1 + 1) % MAX_NO_OF_BINS;
		}
	}
	//printk("CWVDEBUG_9: rem B: head %d   tail %d\n", nc->head,nc->tail);
	return tmp_pa;
}

/*
static int32_t get_pipeack_value(struct newcwv *nc){
	int32_t max; int i=1;
	max = nc->pipeack[0];	
	while ( i < NO_OF_BINS ) {		
		if ( nc->pipeack[i] > max )
			max = nc->pipeack[i];
		i++;
	}	
	if ( max == INVALID_PIPEACK_VALUE )
			return INVALID_PIPEACK_RETURN_VALUE;
	return max;			
}
*/

/* 	returns the usable pipeack value*/
int32_t get_pipeack_variable( struct newcwv* nc){
	return (nc->pipeack_variable == INVALID_PIPEACK_VALUE)? INVALID_PIPEACK_RETURN_VALUE : nc->pipeack_variable;
}

/*
static void printpipeack(struct newcwv *nc){
	int i;
	printk("CWVDEBUG_9: ");
	for (i=0;i<NO_OF_BINS;i++)
		printk("%ld   ", nc->pipeack[i]);
	printk("\n");
}
*/

/* print all the values in the linked list */
void print_pipeack_array(struct newcwv *nc){
	int i;
	printk ("CWVDEBUG_9: ");
	for(i=0; i< MAX_NO_OF_BINS;i++){
		printk ("%d  ", nc->pipeack[i]);
	}
	printk("\nCWVDEBUG_9: ");
	for(i=0; i< MAX_NO_OF_BINS;i++){
		printk ("%u  ", nc->time_stamp[i]);
	}
	printk("\n");
}

static inline u16 convert_16bit_endian (u16 value){
	return ((value << 8) | ((value >> 8) & 0x00FF) );
}

static void printstat(struct sock* sk, int level, int in_flight){
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	
	printk("CWVDEBUG_%d: snd_una %u  snd_nxt %u pipeack %d cwnd %u  is_valid %u flight %u ssthresh %u PMP %u PSP %u sport %u dport %u\n",
	        level,
		tp->snd_una,
		tp->snd_nxt,
		get_pipeack_variable(nc) ,
		tp->snd_cwnd,
		nc->is_valid,
		in_flight,
		tp->snd_ssthresh,
		PMP,
		PSP,
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_sport),
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_dport)
	);
}

/*	original proposal for validity check 	*/
static inline bool tcp_is_in_vp(struct tcp_sock*tp, int32_t pa)
{
	if (pa == INVALID_PIPEACK_RETURN_VALUE)
	 return true;
	else
	 return ( (pa<<1) >= (tp->snd_cwnd * tp->mss_cache) ) ;
}




/* testing a new method for validity : if pipeACK is greater than cwnd 
static inline bool tcp_is_in_vp(struct tcp_sock*tp, int32_t pa)
{
	return pa > (tp->snd_cwnd * tp->mss_cache);
}
*/

static void datalim_closedown(struct sock*sk)
{
        struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 nc_ts;

	nc_ts = nc->cwnd_valid_ts;
	while ( (tcp_time_stamp - nc_ts) > FIVEMINS ) {
		printk("CWVDEBUG_2: >5mins cwnd(before reducing) : %d ", tp->snd_cwnd);
		nc_ts += FIVEMINS;
		nc->cwnd_valid_ts = nc_ts;	
		tp->snd_ssthresh = max((3*tp->snd_cwnd)>>2, tp->snd_ssthresh);
		tp->snd_cwnd = max_t(u32, tp->snd_cwnd >> 1, TCP_INIT_CWND);
		printk(" after : %d \n", tp->snd_cwnd);
				
	}	 
}


static void calculate_params(u32 srtt){
	// update PSP, PMP, no_of_bins from srtt
	u32 bin;
	u32 rtt = srtt >> 3;
	PSP = max ( 3*rtt , (u32)ONESEC);
	bin =  my_division ( PSP , rtt );
	if (bin==0) bin=1;
	if (bin < MAX_NO_OF_BINS)
		NO_OF_BINS =  bin;
	else 
		NO_OF_BINS = MAX_NO_OF_BINS; //ensure minimum value 1 and maximum value 5
	PMP = my_division ( PSP, NO_OF_BINS );
}


static void update_pipeack(struct sock *sk)
{
        struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp= tcp_sk(sk);
	int32_t tmp_pipeack=0;
	
	calculate_params(tp->srtt);
	nc->pipeack_variable = remove_expired_element_and_return_pipeack_element(nc);
	
	if (tp->snd_una >= nc->prev_snd_nxt) {
		tmp_pipeack = tp->snd_una - nc->prev_snd_una;
		nc->prev_snd_una = tp->snd_una;
		nc->prev_snd_nxt = tp->snd_nxt;
		
		// keep this valid pipeack sample into new bin if there are no elements 
		if (  nc->head == INVALID_INDEX )
			add_element(nc,tmp_pipeack);
			//printk("CWVDEBUG_5: creating element\n");
		//or PMP has passed
		if (nc->tail > INVALID_INDEX) {
			if ( tcp_time_stamp >  nc->time_stamp[nc->tail] + PMP )
				add_element(nc,tmp_pipeack);
		}

		
		// maximum filter : store the new larger value in the current bin
		if ( tmp_pipeack > nc->pipeack_variable ) {
			nc->pipeack_variable = tmp_pipeack;
			nc->pipeack[nc->tail] = tmp_pipeack;
			//printk("CWVDEBUG_5: checking max element\n");
		}	
		
	}
	
	//print_pipeack_array(nc);
		
	/* Check if cwnd is validated */
	if (tcp_is_in_vp(tp,get_pipeack_variable(nc))) 
	{
		nc->is_valid = 1;
		nc->cwnd_valid_ts = tcp_time_stamp;
	} else {
		nc->is_valid = 0;	
		datalim_closedown(sk);
	}

}


static void tcp_newcwv_init(struct sock *sk)
{
	int i;
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	
	nc->prev_snd_una = tp->snd_una;
	nc->prev_snd_nxt = tp->snd_nxt;
	
	//printk("start_zia %u %u\n", tp->write_seq, sk->sk_sndbuf);
	
	nc->cwnd_valid_ts = tcp_time_stamp;
	
	nc->is_valid = 1;
	
	//nc->dupack_counter = 0;

	calculate_params(tp->srtt);
	nc->head = INVALID_INDEX;
	nc->tail = INVALID_INDEX;
	for (i=0;i<NO_OF_BINS;i++){
		nc->pipeack[i] = INVALID_PIPEACK_VALUE;
		nc->time_stamp[i]= 0;
	}
	nc->pipeack_variable = INVALID_PIPEACK_VALUE;
	nc->cwnd_before_recovery=0;
	
	printk("CWVDEBUG_0: with draft OCT 2013: method 5 new pipeACK array %d PSP : %u PMP: %u\n", NO_OF_BINS, PSP, PMP);
}


static void tcp_newcwv_reset(struct sock *sk)
{
	int i;
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	nc->prev_snd_una = tp->snd_una;
	nc->prev_snd_nxt = tp->snd_nxt;
		
	nc->cwnd_valid_ts = tcp_time_stamp;
	
	nc->is_valid = 1;
	
	calculate_params(tp->srtt);
	
	// remove the list
	nc->head = INVALID_INDEX;
	nc->tail = INVALID_INDEX;
	for ( i=0;i<NO_OF_BINS;i++){
		nc->pipeack[i]= INVALID_PIPEACK_VALUE;
		nc->time_stamp[i] = 0;
	}
	nc->pipeack_variable = INVALID_PIPEACK_VALUE;
}


static void tcp_newcwv_cong_avoid(struct sock *sk, u32 ack, u32 in_flight)
{
        struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	nc->prior_in_flight = in_flight;
	nc->prior_retrans = tp->total_retrans;

	printk("write_zia %u\n",tp->write_seq);
	
	update_pipeack(sk);

	printstat(sk,1,in_flight);

	/* Check if cwnd is validated */
	if ( !(nc->is_valid) && !tcp_is_cwnd_limited(sk, in_flight))
        //if ( !(nc->is_valid) )
                return;

	/* The following isi the Reno behaviour */	
	/* NB: ABC (RFC 3465) was disabled */

        /* In "safe" area, increase. */
        if (tp->snd_cwnd <= tp->snd_ssthresh)
                tcp_slow_start(tp);

        /* In dangerous area, increase slowly. */
        else 
                tcp_cong_avoid_ai(tp, tp->snd_cwnd);
        
}


static void tcp_newcwv_enter_recovery(struct sock* sk, u32 cwnd_before_recovery){
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 pipeack;
	
	pipeack =  (get_pipeack_variable(nc)==INVALID_PIPEACK_RETURN_VALUE)? 0 : (u32)get_pipeack_variable(nc);
	// convert bytes to segments
	pipeack = my_division( pipeack, (u32)tp->mss_cache);
	
	printk("CWVDEBUG_3: strating recovery with pa %u cwnd %u sport %u dport %u\n",
		pipeack, cwnd_before_recovery,
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_sport),
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_dport));
	in_recovery = true;
	
	tp->snd_cwnd = 	 max(pipeack,nc->prior_in_flight) >> 1;
	
	// make sure the min. value for cwnd is 1
	tp->snd_cwnd = (tp->snd_cwnd < 1)? 1: tp->snd_cwnd;
	
	printstat(sk,3,0);
}


static void tcp_newcwv_end_recovery(struct sock* sk)
{
        struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 retrans,pipeack;
	
	printk("CWVDEBUG_3: ending recovery sport %u dport %u\n",
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_sport),
		convert_16bit_endian(tp->inet_conn.icsk_inet.inet_dport));
	printstat(sk,3,0);
	
	pipeack =  (get_pipeack_variable(nc)==INVALID_PIPEACK_RETURN_VALUE)? 0 : (u32)get_pipeack_variable(nc);
	// convert bytes to segments
	pipeack = my_division( pipeack, (u32)tp->mss_cache);
	
	retrans =  tp->total_retrans - nc->prior_retrans;
	tp->snd_cwnd = ( max(pipeack,nc->prior_in_flight) - retrans) >> 1;
	if ( tp->snd_cwnd < TCP_RESTART_WINDOW ) 
		tp->snd_cwnd = TCP_RESTART_WINDOW;
	
	tp->snd_ssthresh = tp->snd_cwnd;
	in_recovery = false;
	
	printk("CWVDEBUG_3: finished recovery with pr_in_flt: %u total_rxs: %u  pr_rxs: %u sport %u dport %u\n",
	       nc->prior_in_flight, tp->total_retrans, nc->prior_retrans,
	       convert_16bit_endian(tp->inet_conn.icsk_inet.inet_sport),
	       convert_16bit_endian(tp->inet_conn.icsk_inet.inet_dport));
	/* restart cwv machine */
	tcp_newcwv_reset(sk);
	printstat(sk,3,0);
}


static void tcp_newcwv_event(struct sock* sk, enum tcp_ca_event event)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);
	
	switch(event) {
	  case CA_EVENT_TX_START:
		printk("CWVDEBUG_3: START\n");
		printstat(sk,3,0);
		datalim_closedown(sk);	
		break;
	  
	  case CA_EVENT_CWND_RESTART:
		printk("CWVDEBUG_3: CWRestart\n");
		break;
	
          case CA_EVENT_COMPLETE_CWR:
		printk("CWVDEBUG_3: CWR - end recovery\n");
		printstat(sk,3,0);
		if (!nc->is_valid) // set the cwnd according to draft in non_validated period
			tcp_newcwv_end_recovery(sk);
		break;

	  case CA_EVENT_FRTO:
		printk("CWVDEBUG_3: FRTO\n");
		printstat(sk,3,tcp_packets_in_flight(tp));
		tcp_newcwv_init(sk);
		break;
	
	  case CA_EVENT_LOSS:
		printk("CWVDEBUG_3: LOSS\n");
		printstat(sk,3,tcp_packets_in_flight(tp));
		tcp_newcwv_init(sk);
		break;
	  
	  case CA_EVENT_SLOW_ACK:
		//nc->dupack_counter++;
		//printk("CWVDEBUG_3: SLOW_ACK cwnd %u state %u\n",
		       //tp->snd_cwnd, icsk->icsk_ca_state);
		
		switch(icsk->icsk_ca_state){
			case TCP_CA_Open:
			case TCP_CA_Disorder:
				printk("CWVDEBUG_3: Disorder\n");
				nc->cwnd_before_recovery  = tp->snd_cwnd;
				printstat(sk,3,tcp_packets_in_flight(tp));
				break;
			
			case TCP_CA_Recovery:
				printk("CWVDEBUG_3: Recovery\n");
				printstat(sk,3,tcp_packets_in_flight(tp));
				if (!nc->is_valid && !in_recovery)
					tcp_newcwv_enter_recovery(sk, nc->cwnd_before_recovery);
				break;
			
			default:
				break;
		}
		break;
	  
	  case CA_EVENT_FAST_ACK:
		break;
	  
	  default:
		break;
	}

}

struct tcp_congestion_ops tcp_newcwv = {
        .flags          = TCP_CONG_NON_RESTRICTED,
        .name           = "newcwv",
	.init           = tcp_newcwv_init,
        .owner          = THIS_MODULE,
        .ssthresh       = tcp_reno_ssthresh,
        .cong_avoid     = tcp_newcwv_cong_avoid,
	.cwnd_event     = tcp_newcwv_event,
        .min_cwnd       = tcp_reno_min_cwnd,
};


/******************************************************
 * this section for  reading/writing file from the kernel module
 * 3 functions for opening, closing and writing a filefrom online resource:
 * http://stackoverflow.com/questions/1184274/how-to-read-write-files-within-a-linux-kernel-module/1184346#1184346
 *
 * Need to reset the parameters tcp_slow_start_after_idle and tcp_frto for standard TCP operation
 */

struct file* file_open(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
        return NULL;
    }
    return filp;
}


void file_close(struct file* file) {
    filp_close(file, NULL);
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

/*************************************/


static int __init tcp_newcwv_register(void)
{
	struct file* fp;
	unsigned char *ssai="0", *frto="0";
	int status;
	
	BUILD_BUG_ON(sizeof(struct newcwv) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_newcwv);
	
	/* Need to reset the variables 'tcp_slow_start_after_idle' and
	 * 'tcp_frto' to make sure TCP works as per standard
	 */
	fp = file_open("/proc/sys/net/ipv4/tcp_slow_start_after_idle",O_CREAT | O_WRONLY, S_IRWXU);
	if (fp == NULL)
		printk("\n\n*** ERROR: \n Could not reset tcp_slow_start_after_idle\n");
	else{
		status = file_write(fp,0,ssai,1);
		if (status >=0)
			printk("Reset successful. tcp_slow_satrt_after_idle : %s\n", ssai);
		else
			printk("\n\n*** ERROR: \n Could not reset tcp_slow_start_after_idle\n");
		file_close(fp);
	}
	
	fp = file_open("/proc/sys/net/ipv4/tcp_frto", O_CREAT | O_WRONLY, S_IRWXU);
	if (fp == NULL)
		printk("\n\n*** ERROR: \n Could not reset tcp_frto\n");
	else{
		status = file_write(fp,0,frto,1);
		if (status >=0)
			printk("Reset Succeessful. tcp_frto : %s\n", frto);
		else
			printk("\n\n*** ERROR: \n Could not reset tcp_frto\n");
		file_close(fp);
	}

	return 0;
}



static void __exit tcp_newcwv_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_newcwv);
}

module_init(tcp_newcwv_register);
module_exit(tcp_newcwv_unregister);


MODULE_AUTHOR("Ziaul Hossain/Raffaello Secchi");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NewCwv Reno variant");

