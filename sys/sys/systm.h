/*-
 * Copyright (c) 1982, 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)systm.h	8.7 (Berkeley) 3/29/95
 * $FreeBSD: src/sys/sys/systm.h,v 1.111.2.18 2002/12/17 18:04:02 sam Exp $
 * $DragonFly: src/sys/sys/systm.h,v 1.68 2007/04/30 07:18:56 dillon Exp $
 */

#ifndef _SYS_SYSTM_H_
#define	_SYS_SYSTM_H_

#ifndef _KERNEL
#error "This file should not be included by userland programs."
#else

#ifndef _MACHINE_TYPES_H_
#include <machine/types.h>
#endif
#ifndef _MACHINE_STDARG_H_
#include <machine/stdarg.h>
#endif
#include <machine/atomic.h>
#include <machine/cpufunc.h>
#include <sys/callout.h>

extern int securelevel;		/* system security level (see init(8)) */
extern int kernel_mem_readonly;	/* disable writes to kernel memory */

extern int cold;		/* nonzero if we are doing a cold boot */
extern const char *panicstr;	/* panic message */
extern int dumping;		/* system is dumping */
extern int safepri;		/* safe ipl when cold or panicing */
extern char version[];		/* system version */
extern char copyright[];	/* system copyright */

extern int selwait;		/* select timeout address */

extern u_char curpriority;	/* priority of current process */

extern int physmem;		/* physical memory */

extern cdev_t dumpdev;		/* dump device */
extern long dumplo;		/* offset into dumpdev */

extern cdev_t rootdev;		/* root device */
extern cdev_t rootdevs[2];	/* possible root devices */
extern char *rootdevnames[2];	/* names of possible root devices */

extern int boothowto;		/* reboot flags, from console subsystem */
extern int bootverbose;		/* nonzero to print verbose messages */

extern int maxusers;		/* system tune hint */

extern int ncpus;		/* total number of cpus (real, hyper, virtual)*/
extern int ncpus2;		/* ncpus rounded down to power of 2 */
extern int ncpus2_shift;	/* log base 2 of ncpus2 */
extern int ncpus2_mask;		/* ncpus2 - 1 */
extern int ncpus_fit;		/* round up to a power of 2 */
extern int ncpus_fit_mask;	/* ncpus_fit - 1 */
extern int clocks_running;	/* timing/timeout subsystem is operational */

/* XXX TGEN these don't belong here, they're MD on i386/amd64 */
extern u_int cpu_feature;	/* CPUID_* features */
extern u_int cpu_feature2;	/* CPUID2_* features */

extern int nfs_diskless_valid;	/* NFS diskless params were obtained */
extern vm_paddr_t Maxmem;	/* Highest physical memory address in system */

#ifdef	INVARIANTS		/* The option is always available */
#define	KASSERT(exp,msg)	do { if (!(exp)) panic msg; } while (0)
#define KKASSERT(exp)		if (!(exp)) panic("assertion: %s in %s", #exp, __func__)
#else
#define	KASSERT(exp,msg)
#define	KKASSERT(exp)
#endif

#define	CTASSERT(x)		_CTASSERT(x, __LINE__)
#define	_CTASSERT(x, y)		__CTASSERT(x, y)
#define	__CTASSERT(x, y)	typedef char __assert ## y[(x) ? 1 : -1]

/*
 * General function declarations.
 */

struct intrframe;
struct spinlock;
struct malloc_type;
struct proc;
struct xwait;
struct timeval;
struct tty;
struct uio;
struct globaldata;
struct thread;
struct trapframe;
struct user;
struct vmspace;
struct savetls;

void	Debugger (const char *msg);
void	backtrace(void);
void	mi_gdinit (struct globaldata *gd, int cpu);
void	mi_proc0init(struct globaldata *gd, struct user *proc0paddr);
int	dumpstatus (vm_offset_t addr, off_t count);
int	nullop (void);
int	seltrue (cdev_t dev, int which);
int	ureadc (int, struct uio *);
void	*hashinit (int count, struct malloc_type *type, u_long *hashmask);
void	*phashinit (int count, struct malloc_type *type, u_long *nentries);

int	cpu_sanitize_frame (struct trapframe *);
int	cpu_sanitize_tls (struct savetls *);
void	cpu_halt (void);
void	cpu_reset (void);
void	cpu_boot (int);
void	cpu_rootconf (void);
void	cpu_vmspace_alloc(struct vmspace *);
void	cpu_vmspace_free(struct vmspace *);
void	set_user_TLS(void);
void	set_vkernel_fp(struct trapframe *);
int	kvm_access_check(vm_offset_t, vm_offset_t, int);

vm_paddr_t kvtop(void *addr);
int	is_physical_memory (vm_offset_t addr);

extern uint32_t crc32_tab[];
uint32_t crc32(const void *buf, size_t size);
void	init_param1 (void);
void	init_param2 (int physpages);
void	tablefull (const char *);
int	kvcprintf (char const *, void (*)(int, void*), void *, int,
		      __va_list) __printflike(1, 0);
int	log (int, const char *, ...) __printflike(2, 3);
void	logwakeup (void);
void	log_console (struct uio *);
int	kprintf (const char *, ...) __printflike(1, 2);
int	ksnprintf (char *, size_t, const char *, ...) __printflike(3, 4);
int	ksprintf (char *buf, const char *, ...) __printflike(2, 3);
int	uprintf (const char *, ...) __printflike(1, 2);
int	kvprintf (const char *, __va_list) __printflike(1, 0);
int	kvsnprintf (char *, size_t, const char *, __va_list) __printflike(3, 0);
int     kvsprintf (char *buf, const char *, __va_list) __printflike(2, 0);
int	ttyprintf (struct tty *, const char *, ...) __printflike(2, 3);
int	ksscanf (const char *, char const *, ...);
int	kvsscanf (const char *, char const *, __va_list);

long	strtol (const char *, char **, int);
u_long	strtoul (const char *, char **, int);
quad_t	strtoq (const char *, char **, int);
u_quad_t strtouq (const char *, char **, int);

/*
 * note: some functions commonly used by device drivers may be passed
 * pointers to volatile storage, volatile set to avoid warnings.
 *
 * NOTE: bcopyb() - is a dumb byte-granular bcopy.  This routine is
 *		    explicitly not meant to be sophisticated.
 * NOTE: bcopyi() - is a dumb int-granular bcopy (len is still in bytes).
 *		    This routine is explicitly not meant to be sophisticated.
 */
void	bcopyb (const void *from, void *to, size_t len);
void	bcopyi (const void *from, void *to, size_t len);
void	bcopy (volatile const void *from, volatile void *to, size_t len);
void	ovbcopy (const void *from, void *to, size_t len);
void	bzero (volatile void *buf, size_t len);
void	*memcpy (void *to, const void *from, size_t len);

int	copystr (const void *kfaddr, void *kdaddr, size_t len,
		size_t *lencopied);
int	copyinstr (const void *udaddr, void *kaddr, size_t len,
		size_t *lencopied);
int	copyin (const void *udaddr, void *kaddr, size_t len);
int	copyout (const void *kaddr, void *udaddr, size_t len);

int	fubyte (const void *base);
int	subyte (void *base, int byte);
long	fuword (const void *base);
int	suword (void *base, long word);
int	fusword (void *base);
int	susword (void *base, int word);

void	realitexpire (void *);
void	DELAY(int usec);

void	startprofclock (struct proc *);
void	stopprofclock (struct proc *);
void	setstatclockrate (int hzrate);

/*
 * Console I/O spinlocks - these typically also hard-disable interrupts
 * for the duration.
 */
void	cons_lock(void); 
void	cons_unlock(void);

/*
 * Kernel environment support functions and sundry.
 */
char	*kgetenv (const char *name);
int	ksetenv(const char *name, const char *value);
int	kunsetenv(const char *name);
void	kfreeenv(char *env);
int	ktestenv(const char *name);
/* XXX TGEN Get rid of these compat defines. */
#define	testenv	ktestenv
#define	freeenv kfreeenv
int	kgetenv_int (const char *name, int *data);
int	kgetenv_string (const char *name, char *data, int size);
int	kgetenv_quad (const char *name, quad_t *data);
extern char *kern_envp;

#ifdef APM_FIXUP_CALLTODO 
void	adjust_timeout_calltodo (struct timeval *time_change); 
#endif /* APM_FIXUP_CALLTODO */ 

#include <sys/libkern.h>

/* Initialize the world */
void	mi_startup (void);
void	consinit (void);
void	cpu_initclocks (void *arg __unused);
void	nchinit (void);
void	usrinfoinit (void);

/* Finalize the world. */
void	shutdown_nice (int);

/*
 * Kernel to clock driver interface.
 */
void	inittodr (time_t base);
void	resettodr (void);
void	startrtclock (void);

/* Timeouts */
typedef void timeout_t (void *);	/* timeout function type */
#define CALLOUT_HANDLE_INITIALIZER(handle)	\
	{ NULL }

#if 0
/* OBSOLETE INTERFACE */
void	callout_handle_init (struct callout_handle *);
struct	callout_handle timeout (timeout_t *, void *, int);
void	untimeout (timeout_t *, void *, struct callout_handle);
#endif

/* Interrupt management */

/* 
 * For the alpha arch, some of these functions are static __inline, and
 * the others should be.
 */
#ifdef __i386__
void		setdelayed (void);
void		setsoftcambio (void);
void		setsoftcamnet (void);
void		setsoftunused02 (void);
void		setsoftcrypto (void);
void		setsoftunused01 (void);
void		setsofttty (void);
void		setsoftvm (void);
void		setsofttq (void);
void		schedsofttty (void);
void		splz (void);
#endif /* __i386__ */

/*
 * Various callout lists.
 */

/* Exit callout list declarations. */
typedef void (*exitlist_fn) (struct thread *td);

int	at_exit (exitlist_fn function);
int	rm_at_exit (exitlist_fn function);

/* Fork callout list declarations. */
typedef void (*forklist_fn) (struct proc *parent, struct proc *child,
				 int flags);

int	at_fork (forklist_fn function);
int	rm_at_fork (forklist_fn function);

/*
 * Not exactly a callout LIST, but a callout entry.
 * Allow an external module to define a hardware watchdog tickler.
 * Normally a process would do this, but there are times when the
 * kernel needs to be able to hold off the watchdog, when the process
 * is not active, e.g., when dumping core.
 */
typedef void (*watchdog_tickle_fn) (void);

extern watchdog_tickle_fn	wdog_tickler;

/* 
 * Common `proc' functions are declared here so that proc.h can be included
 * less often.
 */
int	tsleep (void *, int, const char *, int);
int	msleep (void *, struct spinlock *, int, const char *, int);
void	tsleep_interlock (void *chan);
void	tstop (void);
void	wakeup (void *chan);
void	wakeup_one (void *chan);
void	wakeup_mycpu (void *chan);
void	wakeup_mycpu_one (void *chan);
void	wakeup_oncpu (struct globaldata *gd, void *chan);
void	wakeup_oncpu_one (struct globaldata *gd, void *chan);
void	wakeup_domain (void *chan, int domain);
void	wakeup_domain_one (void *chan, int domain);

/*
 * Common `cdev_t' stuff are declared here to avoid #include poisoning
 */

int major(cdev_t x);
int minor(cdev_t x);
udev_t dev2udev(cdev_t x);
cdev_t udev2dev(udev_t x, int b);
int uminor(udev_t dev);
int umajor(udev_t dev);
udev_t makeudev(int x, int y);

#endif	/* _KERNEL */
#endif /* !_SYS_SYSTM_H_ */
