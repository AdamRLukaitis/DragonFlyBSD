/*
 * SYS/THREAD.H
 *
 *	Implements the architecture independant portion of the LWKT 
 *	subsystem.
 * 
 * $DragonFly: src/sys/sys/thread.h,v 1.4 2003/06/21 07:54:57 dillon Exp $
 */

#ifndef _SYS_THREAD_H_
#define _SYS_THREAD_H_

struct globaldata;
struct proc;
struct thread;
struct lwkt_queue;
struct lwkt_token;
struct lwkt_wait;
struct lwkt_msg;
struct lwkt_port;
struct lwkt_cpu_msg;
struct lwkt_cpu_port;
struct lwkt_rwlock;

typedef struct lwkt_queue	*lwkt_queue_t;
typedef struct lwkt_token	*lwkt_token_t;
typedef struct lwkt_wait	*lwkt_wait_t;
typedef struct lwkt_msg		*lwkt_msg_t;
typedef struct lwkt_port	*lwkt_port_t;
typedef struct lwkt_cpu_msg	*lwkt_cpu_msg_t;
typedef struct lwkt_cpu_port	*lwkt_cpu_port_t;
typedef struct lwkt_rwlock	*lwkt_rwlock_t;
typedef struct thread 		*thread_t;

typedef TAILQ_HEAD(lwkt_queue, thread) lwkt_queue;
typedef TAILQ_HEAD(lwkt_msg_queue, lwkt_msg) lwkt_msg_queue;

#include <machine/thread.h>

/*
 * Tokens arbitrate access to information.  They are 'soft' arbitrators
 * in that they are associated with cpus rather then threads, making the
 * optimal aquisition case very fast if your cpu already happens to own the
 * token you are requesting.
 */
typedef struct lwkt_token {
    int		t_cpu;		/* the current owner of the token */
    int		t_reqcpu;	/* return ownership to this cpu on release */
#if 0
    int		t_pri;		/* raise thread priority to hold token */
#endif
} lwkt_token;

/*
 * Wait structures deal with blocked threads.  Due to the way remote cpus
 * interact with these structures stable storage must be used.
 */
typedef struct lwkt_wait {
    lwkt_queue	wa_waitq;	/* list of waiting threads */
    lwkt_token	wa_token;	/* who currently owns the list */
    int		wa_gen;
    int		wa_count;
} lwkt_wait;

/*
 * The standarding message and port structure for communications between
 * threads.
 */
typedef struct lwkt_msg {
    TAILQ_ENTRY(lwkt_msg) ms_node;
    lwkt_port_t	ms_replyport;
    int		ms_cmd;
    int		ms_flags;
    int		ms_error;
} lwkt_msg;

#define MSGF_DONE	0x0001
#define MSGF_REPLY	0x0002
#define MSGF_QUEUED	0x0004

typedef struct lwkt_port {
    lwkt_msg_queue	mp_msgq;
    lwkt_wait		mp_wait;
} lwkt_port;

#define mp_token	mp_wait.wa_token

/*
 * The standard message and queue structure used for communications between
 * cpus.  Messages are typically queued via a machine-specific non-linked
 * FIFO matrix allowing any cpu to send a message to any other cpu without
 * blocking.
 */
typedef struct lwkt_cpu_msg {
    void	(*cm_func)(lwkt_cpu_msg_t msg);	/* primary dispatch function */
    int		cm_code;		/* request code if applicable */
    int		cm_cpu;			/* reply to cpu */
    thread_t	cm_originator;		/* originating thread for wakeup */
} lwkt_cpu_msg;

/*
 * reader/writer lock
 */
typedef struct lwkt_rwlock {
    lwkt_token	rw_token;
    thread_t	rw_owner;
    int		rw_count;
} lwkt_rwlock;

#define rw_token	rw_wait.wa_token

/*
 * Thread structure.  Note that ownership of a thread structure is special
 * cased and there is no 'token'.  A thread is always owned by td_cpu and
 * any manipulation of the thread by some other cpu must be done through
 * cpu_*msg() functions.  e.g. you could request ownership of a thread that
 * way, or hand a thread off to another cpu by changing td_cpu and sending
 * a schedule request to the other cpu.
 */
struct thread {
    TAILQ_ENTRY(thread) td_threadq;
    struct proc	*td_proc;	/* (optional) associated process */
    struct pcb	*td_pcb;	/* points to pcb and top of kstack */
    int		td_cpu;		/* cpu owning the thread */
    int		td_pri;		/* 0-31, 0=highest priority */
    int		td_flags;	/* THF flags */
    int		td_gen;		/* wait queue chasing generation number */
    char	*td_kstack;	/* kernel stack */
    char	*td_sp;		/* kernel stack pointer for LWKT restore */
    void	(*td_switch)(struct thread *ntd);
    lwkt_wait_t td_wait;	/* thread sitting on wait structure */
    struct mi_thread td_mach;
};

/*
 * Thread flags.  Note that the RUNNING state is independant from the
 * RUNQ/WAITQ state.  That is, a thread's queueing state can be manipulated
 * while it is running.  If a thread is preempted it will always be moved
 * back to the RUNQ if it isn't on it.
 */
#define TDF_RUNNING		0x0001	/* currently running */
#define TDF_RUNQ		0x0002	/* on run queue */

/*
 * Thread priorities.  Typically only one thread from any given
 * user process scheduling queue is on the LWKT run queue at a time.
 * Remember that there is one LWKT run queue per cpu.
 *
 * Critical sections are handled by bumping td_pri above TDPRI_MAX, which
 * causes interrupts to be masked as they occur.  When this occurs
 * mycpu->gd_reqpri will be raised (possibly just set to TDPRI_CRIT for
 * interrupt masking).
 */
#define TDPRI_IDLE_THREAD	0	/* the idle thread */
#define TDPRI_USER_IDLE		4	/* user scheduler idle */
#define TDPRI_USER_NORM		6	/* user scheduler normal */
#define TDPRI_USER_REAL		8	/* user scheduler real time */
#define TDPRI_KERN_USER		10	/* kernel / block in syscall */
#define TDPRI_SOFT_NORM		14	/* kernel / normal */
#define TDPRI_SOFT_TIMER	16	/* kernel / timer */
#define TDPRI_INT_SUPPORT	20	/* kernel / high priority support */
#define TDPRI_INT_LOW		27	/* low priority interrupt */
#define TDPRI_INT_MED		28	/* medium priority interrupt */
#define TDPRI_INT_HIGH		29	/* high priority interrupt */
#define TDPRI_MAX		31

#define TDPRI_MASK		31
#define TDPRI_CRIT		32	/* high bits of td_pri used for crit */

#define CACHE_NTHREADS		4

#ifdef _KERNEL

extern struct vm_zone	*thread_zone;

extern void lwkt_gdinit(struct globaldata *gd);
extern void lwkt_switch(void);
extern void lwkt_preempt(void);
extern void lwkt_schedule(thread_t td);
extern void lwkt_schedule_self(void);
extern void lwkt_deschedule(thread_t td);
extern void lwkt_deschedule_self(void);
extern void lwkt_yield(void);
extern void lwkt_yield_quick(void);

extern void lwkt_block(lwkt_wait_t w);
extern void lwkt_signal(lwkt_wait_t w);
extern void lwkt_gettoken(lwkt_token_t tok);
extern void lwkt_reltoken(lwkt_token_t tok);
extern int  lwkt_regettoken(lwkt_token_t tok);

#endif

#endif

