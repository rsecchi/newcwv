/*
 * New-CWV implementation for Linux: draft-ietf-tcpm-newcwv-04.txt
 */

#include <linux/module.h>
#include <net/tcp.h>

#define UNDEF_PIPEACK 	                -1
#define PIPEACK_INIT  			TCP_INFINITE_SSTHRESH
#define TCP_RESTART_WINDOW		1
#define FIVEMINS  			(HZ*300)
#define NO_OF_BINS                      4
#define IS_VALID                        0x0002
#define IS_RECOVERY                     0x0001
#define nextbin(x)  (((x)+1) & 0x03)
#define prevbin(x)  (((x)-1) & 0x03)

/* contains newcwv state variables */
struct newcwv {
	int psample[NO_OF_BINS];	/* pipeACK samples circular buffer */
	u32 time_stamp[NO_OF_BINS];	/* pipeACK sample timestamps */
	int pipeack;		/* pipeACK value after filtering */
	u8 rsvd;
	u8 head;		/* index for psample array */
	u16 flags;
	u32 prior_in_flight;	/* Packets in flight for cwnd reduction */
	u32 prior_retrans;	/* Retransmission before going into FR */
	u32 prev_snd_una;	/* snd_una when last record kept */
	u32 prev_snd_nxt;	/* snd_una when last record kept */
	u32 cwnd_valid_ts;	/* last time cwnd was found 'validated' */
	u32 psp;		/* pipeACK Sampling Period */
};

/* helper function for division */
static u32 divide_or_zero(u32 dividend, u32 divisor)
{
	if (divisor == 0)
		return 0;
	else
		return (u32) (dividend / divisor);
}

/* adds an element to the circular buffer for maximum filter */
static void add_element(struct newcwv *nc, int val)
{
	nc->head = nextbin(nc->head);
	nc->psample[nc->head] = val;
	nc->time_stamp[nc->head] = tcp_time_stamp;
}

/* This fuction removes all the expired elements from the circular buffer
 * and returns the maximum from the remaining elements
 */
static int remove_expired_element(struct newcwv *nc)
{
	int k = nc->head;
	int tmp = nc->psample[nc->head];

	while (nc->psample[k] != UNDEF_PIPEACK) {
		/* remove expired */
		if (nc->time_stamp[k] < tcp_time_stamp - nc->psp) {
			nc->psample[k] = UNDEF_PIPEACK;
			return tmp;
		}

		/* search the maximum */
		if (nc->psample[k] > tmp)
			tmp = nc->psample[k];

		k = prevbin(k);
		if (k == nc->head)
			return tmp;
	}

	return tmp;
}

/* is TCP in the validated phase? */
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


/* updates pipeack when an ACK is received */
static void update_pipeack(struct sock *sk)
{
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	int tmp_pipeack;

	nc->psp = max(3 * (tp->srtt >> 3), (u32) HZ);

	if (tp->snd_una >= nc->prev_snd_nxt) {

		/* now get a new pipeack sample */
		tmp_pipeack = tp->snd_una - nc->prev_snd_una;
		nc->prev_snd_una = tp->snd_una;
		nc->prev_snd_nxt = tp->snd_nxt;

		/* create a new element at the end of current pmp */
		if (tcp_time_stamp > nc->time_stamp[nc->head] + (nc->psp >> 2))
			add_element(nc, tmp_pipeack);
		else if (tmp_pipeack > nc->psample[nc->head])
			nc->psample[nc->head] = tmp_pipeack;
	}

	nc->pipeack = remove_expired_element(nc);

	/* check if cwnd is validated */
	if (tcp_is_in_vp(tp, nc->pipeack)) {
		nc->flags |= IS_VALID;
		nc->cwnd_valid_ts = tcp_time_stamp;
	} else {
		nc->flags &= ~IS_VALID;
		datalim_closedown(sk);
	}
}

/* initialises newcwv variables */
static void tcp_newcwv_init(struct sock *sk)
{
	int i;
	struct newcwv *nc = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	nc->prev_snd_una = tp->snd_una;
	nc->prev_snd_nxt = tp->snd_nxt;

	nc->cwnd_valid_ts = tcp_time_stamp;
	nc->flags = IS_VALID;

	nc->psp = max(3 * (tp->srtt >> 3), (u32) HZ);

	nc->head = 0;
	nc->psample[0] = UNDEF_PIPEACK;
	nc->pipeack = UNDEF_PIPEACK;
}


/* cong_avoid action: non dubious ACK received */
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

	/* The following is the Reno behaviour */

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

	pipeack = (nc->pipeack == UNDEF_PIPEACK) ? 0 : (u32)
	    nc->pipeack;
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

	pipeack = (nc->pipeack == UNDEF_PIPEACK) ? 0 : (u32)
	    nc->pipeack;

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

	case CA_EVENT_COMPLETE_CWR:
		if (!(nc->flags & IS_VALID))
			tcp_newcwv_end_recovery(sk);
		break;

	case CA_EVENT_LOSS:
		tcp_newcwv_init(sk);
		break;

	case CA_EVENT_SLOW_ACK:

		switch (icsk->icsk_ca_state) {
		case TCP_CA_Recovery:
			if (!nc->flags)
				tcp_newcwv_enter_recovery(sk);
			break;

		case TCP_CA_Open:
		case TCP_CA_Disorder:
		default:
			break;
		}
		break;

	case CA_EVENT_CWND_RESTART:
	case CA_EVENT_FAST_ACK:
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
