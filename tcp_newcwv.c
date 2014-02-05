/*
 * New-CWV implementation for Linux: draft-ietf-tcpm-newcwv-04.txt
 */

#include <linux/module.h>
#include <net/tcp.h>

#define INVALID_PIPEACK 		-1
#define UNDEF_PIPEACK 	                -1
#define PIPEACK_INIT  			TCP_INFINITE_SSTHRESH
#define TCP_RESTART_WINDOW		1
#define FIVEMINS  			(HZ*300)
#define RTT_EST 			1

#define INVALID_INDEX			-1
#define NO_OF_BINS                      4
#define IS_VALID                        0x0002
#define IS_RECOVERY                     0x0001
#define nextbin(x)  (((x)+1) % NO_OF_BINS)

struct newcwv {
	int psample[NO_OF_BINS];    /* pipeACK samples collected every PMP */
	u32 time_stamp[NO_OF_BINS]; /* pipeACK sample timestamps */ 
	int pipeack;		    /* pipeACK value after filtering */

	u8 head, tail;              /* indexes for psample array */ 
	u16 flags;                  

	u32 prior_in_flight;	/* Packets in flight for cwnd reduction */
	u32 prior_retrans;	/* Retransmission before going into FR */
	u32 prev_snd_una;	/* snd_una when last record kept */
	u32 prev_snd_nxt;	/* snd_una when last record kept */
	u32 cwnd_valid_ts;	/* last time cwnd was found 'validated' */
	u32 psp;		/* pipeACK Sampling Period (see Internet draft) */
};

/* helper function for division */
static u32 divide_or_zero(u32 dividend, u32 divisor)
{
	if (divisor == 0)
		return 0;
	else
		return (u32) (dividend / divisor);
}

/* 
 * This function adds an element to the linked list of pipeack samples 
 * and returns the new element pointer
 */
static void add_element(struct newcwv *nc, int val)
{
	if (nc->head == nextbin(nc->tail))
		nc->head = nextbin(nc->head);

	if (nc->head == INVALID_INDEX) {
		nc->head = 0;
		nc->tail = 0;
	} else
		nc->tail = nextbin(nc->tail);

	nc->psample[nc->tail] = val;
	nc->time_stamp[nc->tail] = tcp_time_stamp;
}

/* 
 * This fuction removes all the expired elements based on their time_stamp
 * and return a pipeack element: value = the maximum from the remaining
 * elements time_stamp  = the time stamp of the last element
 * next = the pointer to the last element
 */
static int remove_expired_element(struct newcwv *nc)
{

	int tmp_pa = nc->pipeack;
	bool changed = false;
	short tmp1;

	if (nc->head == INVALID_INDEX)
		return INVALID_PIPEACK;

	while (nc->time_stamp[nc->head] < (tcp_time_stamp - nc->psp)) {
		changed = true;
		nc->psample[nc->head] = INVALID_PIPEACK;
		nc->time_stamp[nc->tail] = 0;

		if (nc->tail == nc->head) {
			nc->head = nc->tail = INVALID_INDEX;
			return INVALID_PIPEACK;
		} else
			nc->head = nextbin(nc->head);
	}

	if (changed) {
		tmp1 = nc->head;
		while (tmp1 != nc->tail) {
			if (nc->psample[tmp1] > tmp_pa)
				tmp_pa = nc->psample[tmp1];
			tmp1 = nextbin(tmp1);
		}
	}
	return tmp_pa;
}

/* returns the pipeack value */
static int get_pipeack_variable(struct newcwv *nc)
{
	return (nc->pipeack ==
		INVALID_PIPEACK) ? UNDEF_PIPEACK : nc->pipeack;
}

/* Is TCP in the validated phase? */
static inline bool tcp_is_in_vp(struct tcp_sock *tp, int pa)
{
	if (pa == UNDEF_PIPEACK)
		return true;
	else
		return ((pa << 1) >= (tp->snd_cwnd * tp->mss_cache));
}

/* reduces the cwnd after 5mins of non-validated phase */
static void datalim_closedown(struct sock *sk)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 nc_ts;

	nc_ts = nc->cwnd_valid_ts;
	while ((tcp_time_stamp - nc_ts) > FIVEMINS) {
		nc_ts += FIVEMINS;
		nc->cwnd_valid_ts = nc_ts;
		tp->snd_ssthresh =
		    max((3 * tp->snd_cwnd) >> 2, tp->snd_ssthresh);
		tp->snd_cwnd =
		    max_t(u32, tp->snd_cwnd >> 1, TCP_INIT_CWND);
	}
}

/* calculates the newcwv variables */
static void calculate_params(struct sock *sk, u32 srtt)
{
	struct newcwv *nc = inet_csk_ca(sk);

	/* (srtt >> 3) is the RTT in HZ */
	nc->psp = max(3 * (srtt >> 3), (u32) HZ);
}

/* update pipeack when ACK is received */
static void update_pipeack(struct sock *sk)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	int tmp_pipeack = 0;

	calculate_params(sk, tp->srtt);
	nc->pipeack = remove_expired_element(nc);

	if (tp->snd_una >= nc->prev_snd_nxt) {
		tmp_pipeack = tp->snd_una - nc->prev_snd_una;
		nc->prev_snd_una = tp->snd_una;
		nc->prev_snd_nxt = tp->snd_nxt;

		/* 
		 * need to keep this valid pipeack sample 
		 * into new bin if there are no elements*/
		if (nc->head == INVALID_INDEX)
			add_element(nc, tmp_pipeack);

		if (nc->tail > INVALID_INDEX)
			if (tcp_time_stamp >
			    nc->time_stamp[nc->tail] + (nc->psp >> 2))
				add_element(nc, tmp_pipeack);

		/* Maximum filter (see Internet draft) */
		if (tmp_pipeack > nc->pipeack) {
			nc->pipeack = tmp_pipeack;
			nc->psample[nc->tail] = tmp_pipeack;
		}
	}

	/* Check if cwnd is validated */
	if (tcp_is_in_vp(tp, get_pipeack_variable(nc))) {
		nc->flags |= IS_VALID;
		nc->cwnd_valid_ts = tcp_time_stamp;
	} else {
		nc->flags &= ~IS_VALID;
		datalim_closedown(sk);
	}

}

/* initialise newcwv variables */
static void tcp_newcwv_init(struct sock *sk)
{
	int i;
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	nc->prev_snd_una = tp->snd_una;
	nc->prev_snd_nxt = tp->snd_nxt;

	nc->cwnd_valid_ts = tcp_time_stamp;
	nc->flags = IS_VALID;

	calculate_params(sk, tp->srtt);

	nc->head = INVALID_INDEX;
	nc->tail = INVALID_INDEX;
	for (i = 0; i < NO_OF_BINS; i++) {
		nc->psample[i] = INVALID_PIPEACK;
		nc->time_stamp[i] = 0;
	}
	nc->pipeack = INVALID_PIPEACK;
}


/* cong_avoid action */
static void tcp_newcwv_cong_avoid(struct sock *sk, u32 ack, u32 in_flight)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	nc->prior_in_flight = in_flight;
	nc->prior_retrans = tp->total_retrans;

	update_pipeack(sk);

	/* Check if cwnd is validated */
	if (!(nc->flags & IS_VALID) && !tcp_is_cwnd_limited(sk, in_flight))
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

/* newcwv actions in fast recovery */
static void tcp_newcwv_enter_recovery(struct sock *sk)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 pipeack;

	nc->flags |= IS_RECOVERY;

	pipeack = (get_pipeack_variable(nc) == UNDEF_PIPEACK) ? 0 : (u32)
	    get_pipeack_variable(nc);

	pipeack = divide_or_zero(pipeack, (u32) tp->mss_cache);

	tp->snd_cwnd = max(pipeack, nc->prior_in_flight) >> 1;

	/* make sure the min. value for cwnd is 1 */
	tp->snd_cwnd = (tp->snd_cwnd < 1) ? 1 : tp->snd_cwnd;

}

/* newcwv actions at the end of recovery */
static void tcp_newcwv_end_recovery(struct sock *sk)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	u32 retrans, pipeack;

	pipeack = (get_pipeack_variable(nc) == UNDEF_PIPEACK) ? 0 : (u32)
	    get_pipeack_variable(nc);

	/* converts bytes to segments */
	pipeack = divide_or_zero(pipeack, (u32) tp->mss_cache);

	retrans = tp->total_retrans - nc->prior_retrans;
	tp->snd_cwnd = (max(pipeack, nc->prior_in_flight) - retrans) >> 1;
	if (tp->snd_cwnd < TCP_RESTART_WINDOW)
		tp->snd_cwnd = TCP_RESTART_WINDOW;

	tp->snd_ssthresh = tp->snd_cwnd;
	nc->flags &= ~IS_RECOVERY;

	/* restart cwv machine */
	tcp_newcwv_init(sk);

}

/* newcwv actions corresponding to event */
static void tcp_newcwv_event(struct sock *sk, enum tcp_ca_event event)
{
	struct newcwv *nc = inet_csk_ca(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);

	switch (event) {
	case CA_EVENT_TX_START:
		datalim_closedown(sk);
		break;

	case CA_EVENT_CWND_RESTART:
		break;

	case CA_EVENT_COMPLETE_CWR:
		if (!(nc->flags & IS_VALID))
			tcp_newcwv_end_recovery(sk);
		break;

	case CA_EVENT_LOSS:
		tcp_newcwv_init(sk);
		break;

	case CA_EVENT_SLOW_ACK:

		switch (icsk->icsk_ca_state) {
		case TCP_CA_Open:
		case TCP_CA_Disorder:
			break;

		case TCP_CA_Recovery:
			if (!nc->flags)
				tcp_newcwv_enter_recovery(sk);
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
	.flags = TCP_CONG_NON_RESTRICTED,
	.name = "newcwv",
	.init = tcp_newcwv_init,
	.owner = THIS_MODULE,
	.ssthresh = tcp_reno_ssthresh,
	.cong_avoid = tcp_newcwv_cong_avoid,
	.cwnd_event = tcp_newcwv_event,
	.min_cwnd = tcp_reno_min_cwnd,
};


/* newcwv registered as congestion control in Linux */
static int __init tcp_newcwv_register(void)
{

	BUILD_BUG_ON(sizeof(struct newcwv) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_newcwv);

	return 0;
}

/* unregister when module is disabled */
static void __exit tcp_newcwv_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_newcwv);
}

module_init(tcp_newcwv_register);
module_exit(tcp_newcwv_unregister);

MODULE_AUTHOR("Ziaul Hossain/Raffaello Secchi");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NewCwv Reno variant");
