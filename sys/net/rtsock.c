/*
 * Copyright (c) 2004, 2005 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1988, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)rtsock.c	8.7 (Berkeley) 10/12/95
 * $FreeBSD: src/sys/net/rtsock.c,v 1.44.2.11 2002/12/04 14:05:41 ru Exp $
 * $DragonFly: src/sys/net/rtsock.c,v 1.41 2007/12/19 11:00:22 sephe Exp $
 */

#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/route.h>
#include <net/raw_cb.h>
#include <net/netmsg2.h>

#ifdef SCTP
extern void sctp_add_ip_address(struct ifaddr *ifa);
extern void sctp_delete_ip_address(struct ifaddr *ifa);
#endif /* SCTP */

MALLOC_DEFINE(M_RTABLE, "routetbl", "routing tables");

static struct route_cb {
	int	ip_count;
	int	ip6_count;
	int	ipx_count;
	int	ns_count;
	int	any_count;
} route_cb;

static const struct sockaddr route_src = { 2, PF_ROUTE, };

struct walkarg {
	int	w_tmemsize;
	int	w_op, w_arg;
	void	*w_tmem;
	struct sysctl_req *w_req;
};

static struct mbuf *
		rt_msg_mbuf (int, struct rt_addrinfo *);
static void	rt_msg_buffer (int, struct rt_addrinfo *, void *buf, int len);
static int	rt_msgsize (int type, struct rt_addrinfo *rtinfo);
static int	rt_xaddrs (char *, char *, struct rt_addrinfo *);
static int	sysctl_dumpentry (struct radix_node *rn, void *vw);
static int	sysctl_iflist (int af, struct walkarg *w);
static int	route_output(struct mbuf *, struct socket *, ...);
static void	rt_setmetrics (u_long, struct rt_metrics *,
			       struct rt_metrics *);

/*
 * It really doesn't make any sense at all for this code to share much
 * with raw_usrreq.c, since its functionality is so restricted.  XXX
 */
static int
rts_abort(struct socket *so)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_abort(so);
	crit_exit();
	return error;
}

/* pru_accept is EOPNOTSUPP */

static int
rts_attach(struct socket *so, int proto, struct pru_attach_info *ai)
{
	struct rawcb *rp;
	int error;

	if (sotorawcb(so) != NULL)
		return EISCONN;	/* XXX panic? */

	rp = kmalloc(sizeof *rp, M_PCB, M_WAITOK | M_ZERO);
	if (rp == NULL)
		return ENOBUFS;

	/*
	 * The critical section is necessary to block protocols from sending
	 * error notifications (like RTM_REDIRECT or RTM_LOSING) while
	 * this PCB is extant but incompletely initialized.
	 * Probably we should try to do more of this work beforehand and
	 * eliminate the critical section.
	 */
	crit_enter();
	so->so_pcb = rp;
	error = raw_attach(so, proto, ai->sb_rlimit);
	rp = sotorawcb(so);
	if (error) {
		crit_exit();
		kfree(rp, M_PCB);
		return error;
	}
	switch(rp->rcb_proto.sp_protocol) {
	case AF_INET:
		route_cb.ip_count++;
		break;
	case AF_INET6:
		route_cb.ip6_count++;
		break;
	case AF_IPX:
		route_cb.ipx_count++;
		break;
	case AF_NS:
		route_cb.ns_count++;
		break;
	}
	rp->rcb_faddr = &route_src;
	route_cb.any_count++;
	soisconnected(so);
	so->so_options |= SO_USELOOPBACK;
	crit_exit();
	return 0;
}

static int
rts_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_bind(so, nam, td); /* xxx just EINVAL */
	crit_exit();
	return error;
}

static int
rts_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_connect(so, nam, td); /* XXX just EINVAL */
	crit_exit();
	return error;
}

/* pru_connect2 is EOPNOTSUPP */
/* pru_control is EOPNOTSUPP */

static int
rts_detach(struct socket *so)
{
	struct rawcb *rp = sotorawcb(so);
	int error;

	crit_enter();
	if (rp != NULL) {
		switch(rp->rcb_proto.sp_protocol) {
		case AF_INET:
			route_cb.ip_count--;
			break;
		case AF_INET6:
			route_cb.ip6_count--;
			break;
		case AF_IPX:
			route_cb.ipx_count--;
			break;
		case AF_NS:
			route_cb.ns_count--;
			break;
		}
		route_cb.any_count--;
	}
	error = raw_usrreqs.pru_detach(so);
	crit_exit();
	return error;
}

static int
rts_disconnect(struct socket *so)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_disconnect(so);
	crit_exit();
	return error;
}

/* pru_listen is EOPNOTSUPP */

static int
rts_peeraddr(struct socket *so, struct sockaddr **nam)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_peeraddr(so, nam);
	crit_exit();
	return error;
}

/* pru_rcvd is EOPNOTSUPP */
/* pru_rcvoob is EOPNOTSUPP */

static int
rts_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
	 struct mbuf *control, struct thread *td)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_send(so, flags, m, nam, control, td);
	crit_exit();
	return error;
}

/* pru_sense is null */

static int
rts_shutdown(struct socket *so)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_shutdown(so);
	crit_exit();
	return error;
}

static int
rts_sockaddr(struct socket *so, struct sockaddr **nam)
{
	int error;

	crit_enter();
	error = raw_usrreqs.pru_sockaddr(so, nam);
	crit_exit();
	return error;
}

static struct pr_usrreqs route_usrreqs = {
	.pru_abort = rts_abort,
	.pru_accept = pru_accept_notsupp,
	.pru_attach = rts_attach,
	.pru_bind = rts_bind,
	.pru_connect = rts_connect,
	.pru_connect2 = pru_connect2_notsupp,
	.pru_control = pru_control_notsupp,
	.pru_detach = rts_detach,
	.pru_disconnect = rts_disconnect,
	.pru_listen = pru_listen_notsupp,
	.pru_peeraddr = rts_peeraddr,
	.pru_rcvd = pru_rcvd_notsupp,
	.pru_rcvoob = pru_rcvoob_notsupp,
	.pru_send = rts_send,
	.pru_sense = pru_sense_null,
	.pru_shutdown = rts_shutdown,
	.pru_sockaddr = rts_sockaddr,
	.pru_sosend = sosend,
	.pru_soreceive = soreceive,
	.pru_sopoll = sopoll
};

static __inline sa_family_t
familyof(struct sockaddr *sa)
{
	return (sa != NULL ? sa->sa_family : 0);
}

/*
 * Routing socket input function.  The packet must be serialized onto cpu 0.
 * We use the cpu0_soport() netisr processing loop to handle it.
 *
 * This looks messy but it means that anyone, including interrupt code,
 * can send a message to the routing socket.
 */
static void
rts_input_handler(struct netmsg *msg)
{
	static const struct sockaddr route_dst = { 2, PF_ROUTE, };
	struct sockproto route_proto;
	struct netmsg_packet *pmsg;
	struct mbuf *m;
	sa_family_t family;

	pmsg = (void *)msg;
	m = pmsg->nm_packet;
	family = pmsg->nm_netmsg.nm_lmsg.u.ms_result;
	route_proto.sp_family = PF_ROUTE;
	route_proto.sp_protocol = family;

	raw_input(m, &route_proto, &route_src, &route_dst);
}

static void
rts_input(struct mbuf *m, sa_family_t family)
{
	struct netmsg_packet *pmsg;
	lwkt_port_t port;

	port = cpu0_soport(NULL, NULL, NULL, 0);
	pmsg = &m->m_hdr.mh_netmsg;
	netmsg_init(&pmsg->nm_netmsg, &netisr_apanic_rport, 
		    0, rts_input_handler);
	pmsg->nm_packet = m;
	pmsg->nm_netmsg.nm_lmsg.u.ms_result = family;
	lwkt_sendmsg(port, &pmsg->nm_netmsg.nm_lmsg);
}

static void *
reallocbuf(void *ptr, size_t len, size_t olen)
{
	void *newptr;

	newptr = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (newptr == NULL)
		return NULL;
	bcopy(ptr, newptr, olen);
	kfree(ptr, M_RTABLE);
	return (newptr);
}

/*
 * Internal helper routine for route_output().
 */
static int
fillrtmsg(struct rt_msghdr **prtm, struct rtentry *rt,
	  struct rt_addrinfo *rtinfo)
{
	int msglen;
	struct rt_msghdr *rtm = *prtm;

	/* Fill in rt_addrinfo for call to rt_msg_buffer(). */
	rtinfo->rti_dst = rt_key(rt);
	rtinfo->rti_gateway = rt->rt_gateway;
	rtinfo->rti_netmask = rt_mask(rt);		/* might be NULL */
	rtinfo->rti_genmask = rt->rt_genmask;		/* might be NULL */
	if (rtm->rtm_addrs & (RTA_IFP | RTA_IFA)) {
		if (rt->rt_ifp != NULL) {
			rtinfo->rti_ifpaddr =
			    TAILQ_FIRST(&rt->rt_ifp->if_addrhead)->ifa_addr;
			rtinfo->rti_ifaaddr = rt->rt_ifa->ifa_addr;
			if (rt->rt_ifp->if_flags & IFF_POINTOPOINT)
				rtinfo->rti_bcastaddr = rt->rt_ifa->ifa_dstaddr;
			rtm->rtm_index = rt->rt_ifp->if_index;
		} else {
			rtinfo->rti_ifpaddr = NULL;
			rtinfo->rti_ifaaddr = NULL;
	    }
	}

	msglen = rt_msgsize(rtm->rtm_type, rtinfo);
	if (rtm->rtm_msglen < msglen) {
		rtm = reallocbuf(rtm, msglen, rtm->rtm_msglen);
		if (rtm == NULL)
			return (ENOBUFS);
		*prtm = rtm;
	}
	rt_msg_buffer(rtm->rtm_type, rtinfo, rtm, msglen);

	rtm->rtm_flags = rt->rt_flags;
	rtm->rtm_rmx = rt->rt_rmx;
	rtm->rtm_addrs = rtinfo->rti_addrs;

	return (0);
}

static void route_output_add_callback(int, int, struct rt_addrinfo *,
					struct rtentry *, void *);
static void route_output_delete_callback(int, int, struct rt_addrinfo *,
					struct rtentry *, void *);
static void route_output_change_callback(int, int, struct rt_addrinfo *,
					struct rtentry *, void *);
static void route_output_lock_callback(int, int, struct rt_addrinfo *, 
					struct rtentry *, void *);

/*ARGSUSED*/
static int
route_output(struct mbuf *m, struct socket *so, ...)
{
	struct rt_msghdr *rtm = NULL;
	struct rtentry *rt;
	struct radix_node_head *rnh;
	struct rawcb *rp = NULL;
	struct pr_output_info *oi;
	struct rt_addrinfo rtinfo;
	int len, error = 0;
	__va_list ap;

	__va_start(ap, so);
	oi = __va_arg(ap, struct pr_output_info *);
	__va_end(ap);

#define gotoerr(e) { error = e; goto flush;}

	if (m == NULL ||
	    (m->m_len < sizeof(long) &&
	     (m = m_pullup(m, sizeof(long))) == NULL))
		return (ENOBUFS);
	if (!(m->m_flags & M_PKTHDR))
		panic("route_output");
	len = m->m_pkthdr.len;
	if (len < sizeof(struct rt_msghdr) ||
	    len != mtod(m, struct rt_msghdr *)->rtm_msglen) {
		rtinfo.rti_dst = NULL;
		gotoerr(EINVAL);
	}
	rtm = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (rtm == NULL) {
		rtinfo.rti_dst = NULL;
		gotoerr(ENOBUFS);
	}
	m_copydata(m, 0, len, (caddr_t)rtm);
	if (rtm->rtm_version != RTM_VERSION) {
		rtinfo.rti_dst = NULL;
		gotoerr(EPROTONOSUPPORT);
	}
	rtm->rtm_pid = oi->p_pid;
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_addrs = rtm->rtm_addrs;
	if (rt_xaddrs((char *)(rtm + 1), (char *)rtm + len, &rtinfo) != 0) {
		rtinfo.rti_dst = NULL;
		gotoerr(EINVAL);
	}
	rtinfo.rti_flags = rtm->rtm_flags;
	if (rtinfo.rti_dst == NULL || rtinfo.rti_dst->sa_family >= AF_MAX ||
	    (rtinfo.rti_gateway && rtinfo.rti_gateway->sa_family >= AF_MAX))
		gotoerr(EINVAL);

	if (rtinfo.rti_genmask != NULL) {
		struct radix_node *n;

#define	clen(s)	(*(u_char *)(s))
		n = rn_addmask((char *)rtinfo.rti_genmask, TRUE, 1);
		if (n != NULL &&
		    rtinfo.rti_genmask->sa_len >= clen(n->rn_key) &&
		    bcmp((char *)rtinfo.rti_genmask + 1,
		         (char *)n->rn_key + 1, clen(n->rn_key) - 1) == 0)
			rtinfo.rti_genmask = (struct sockaddr *)n->rn_key;
		else
			gotoerr(ENOBUFS);
	}

	/*
	 * Verify that the caller has the appropriate privilege; RTM_GET
	 * is the only operation the non-superuser is allowed.
	 */
	if (rtm->rtm_type != RTM_GET && suser_cred(so->so_cred, 0) != 0)
		gotoerr(EPERM);

	switch (rtm->rtm_type) {
	case RTM_ADD:
		if (rtinfo.rti_gateway == NULL) {
			error = EINVAL;
		} else {
			error = rtrequest1_global(RTM_ADD, &rtinfo, 
					  route_output_add_callback, rtm);
		}
		break;
	case RTM_DELETE:
		/*
		 * note: &rtm passed as argument so 'rtm' can be replaced.
		 */
		error = rtrequest1_global(RTM_DELETE, &rtinfo,
					  route_output_delete_callback, &rtm);
		break;
	case RTM_GET:
		rnh = rt_tables[mycpuid][rtinfo.rti_dst->sa_family];
		if (rnh == NULL) {
			error = EAFNOSUPPORT;
			break;
		}
		rt = (struct rtentry *)
		    rnh->rnh_lookup((char *)rtinfo.rti_dst,
		    		    (char *)rtinfo.rti_netmask, rnh);
		if (rt == NULL) {
			error = ESRCH;
			break;
		}
		rt->rt_refcnt++;
		if (fillrtmsg(&rtm, rt, &rtinfo) != 0)
			gotoerr(ENOBUFS);
		--rt->rt_refcnt;
		break;
	case RTM_CHANGE:
		error = rtrequest1_global(RTM_GET, &rtinfo,
					  route_output_change_callback, rtm);
		break;
	case RTM_LOCK:
		error = rtrequest1_global(RTM_GET, &rtinfo,
					  route_output_lock_callback, rtm);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

flush:
	if (rtm != NULL) {
		if (error != 0)
			rtm->rtm_errno = error;
		else
			rtm->rtm_flags |= RTF_DONE;
	}

	/*
	 * Check to see if we don't want our own messages.
	 */
	if (!(so->so_options & SO_USELOOPBACK)) {
		if (route_cb.any_count <= 1) {
			if (rtm != NULL)
				kfree(rtm, M_RTABLE);
			m_freem(m);
			return (error);
		}
		/* There is another listener, so construct message */
		rp = sotorawcb(so);
	}
	if (rtm != NULL) {
		m_copyback(m, 0, rtm->rtm_msglen, (caddr_t)rtm);
		if (m->m_pkthdr.len < rtm->rtm_msglen) {
			m_freem(m);
			m = NULL;
		} else if (m->m_pkthdr.len > rtm->rtm_msglen)
			m_adj(m, rtm->rtm_msglen - m->m_pkthdr.len);
		kfree(rtm, M_RTABLE);
	}
	if (rp != NULL)
		rp->rcb_proto.sp_family = 0; /* Avoid us */
	if (m != NULL)
		rts_input(m, familyof(rtinfo.rti_dst));
	if (rp != NULL)
		rp->rcb_proto.sp_family = PF_ROUTE;
	return (error);
}

static void
route_output_add_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg)
{
	struct rt_msghdr *rtm = arg;

	if (error == 0 && rt != NULL) {
		rt_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx,
		    &rt->rt_rmx);
		rt->rt_rmx.rmx_locks &= ~(rtm->rtm_inits);
		rt->rt_rmx.rmx_locks |=
		    (rtm->rtm_inits & rtm->rtm_rmx.rmx_locks);
		rt->rt_genmask = rtinfo->rti_genmask;
	}
}

static void
route_output_delete_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg)
{
	struct rt_msghdr **rtm = arg;

	if (error == 0 && rt) {
		++rt->rt_refcnt;
		if (fillrtmsg(rtm, rt, rtinfo) != 0) {
			error = ENOBUFS;
			/* XXX no way to return the error */
		}
		--rt->rt_refcnt;
	}
}

static void
route_output_change_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			  struct rtentry *rt, void *arg)
{
	struct rt_msghdr *rtm = arg;
	struct ifaddr *ifa;

	if (error)
		goto done;

	/*
	 * new gateway could require new ifaddr, ifp;
	 * flags may also be different; ifp may be specified
	 * by ll sockaddr when protocol address is ambiguous
	 */
	if (((rt->rt_flags & RTF_GATEWAY) && rtinfo->rti_gateway != NULL) ||
	    rtinfo->rti_ifpaddr != NULL || (rtinfo->rti_ifaaddr != NULL &&
	    sa_equal(rtinfo->rti_ifaaddr, rt->rt_ifa->ifa_addr))
	) {
		error = rt_getifa(rtinfo);
		if (error != 0)
			goto done;
	}
	if (rtinfo->rti_gateway != NULL) {
		error = rt_setgate(rt, rt_key(rt), rtinfo->rti_gateway);
		if (error != 0)
			goto done;
	}
	if ((ifa = rtinfo->rti_ifa) != NULL) {
		struct ifaddr *oifa = rt->rt_ifa;

		if (oifa != ifa) {
			if (oifa && oifa->ifa_rtrequest)
				oifa->ifa_rtrequest(RTM_DELETE, rt, rtinfo);
			IFAFREE(rt->rt_ifa);
			IFAREF(ifa);
			rt->rt_ifa = ifa;
			rt->rt_ifp = rtinfo->rti_ifp;
		}
	}
	rt_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx, &rt->rt_rmx);
	if (rt->rt_ifa && rt->rt_ifa->ifa_rtrequest)
	       rt->rt_ifa->ifa_rtrequest(RTM_ADD, rt, rtinfo);
	if (rtinfo->rti_genmask != NULL)
		rt->rt_genmask = rtinfo->rti_genmask;
done:
	/* XXX no way to return error */
	;
}

static void
route_output_lock_callback(int cmd, int error, struct rt_addrinfo *rtinfo,
			   struct rtentry *rt, void *arg)
{
	struct rt_msghdr *rtm = arg;

	rt->rt_rmx.rmx_locks &= ~(rtm->rtm_inits);
	rt->rt_rmx.rmx_locks |=
		(rtm->rtm_inits & rtm->rtm_rmx.rmx_locks);
}

static void
rt_setmetrics(u_long which, struct rt_metrics *in, struct rt_metrics *out)
{
#define setmetric(flag, elt) if (which & (flag)) out->elt = in->elt;
	setmetric(RTV_RPIPE, rmx_recvpipe);
	setmetric(RTV_SPIPE, rmx_sendpipe);
	setmetric(RTV_SSTHRESH, rmx_ssthresh);
	setmetric(RTV_RTT, rmx_rtt);
	setmetric(RTV_RTTVAR, rmx_rttvar);
	setmetric(RTV_HOPCOUNT, rmx_hopcount);
	setmetric(RTV_MTU, rmx_mtu);
	setmetric(RTV_EXPIRE, rmx_expire);
#undef setmetric
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

/*
 * Extract the addresses of the passed sockaddrs.
 * Do a little sanity checking so as to avoid bad memory references.
 * This data is derived straight from userland.
 */
static int
rt_xaddrs(char *cp, char *cplim, struct rt_addrinfo *rtinfo)
{
	struct sockaddr *sa;
	int i;

	for (i = 0; (i < RTAX_MAX) && (cp < cplim); i++) {
		if ((rtinfo->rti_addrs & (1 << i)) == 0)
			continue;
		sa = (struct sockaddr *)cp;
		/*
		 * It won't fit.
		 */
		if ((cp + sa->sa_len) > cplim) {
			return (EINVAL);
		}

		/*
		 * There are no more...  Quit now.
		 * If there are more bits, they are in error.
		 * I've seen this.  route(1) can evidently generate these. 
		 * This causes kernel to core dump.
		 * For compatibility, if we see this, point to a safe address.
		 */
		if (sa->sa_len == 0) {
			static struct sockaddr sa_zero = {
				sizeof sa_zero, AF_INET,
			};

			rtinfo->rti_info[i] = &sa_zero;
			return (0); /* should be EINVAL but for compat */
		}

		/* Accept the sockaddr. */
		rtinfo->rti_info[i] = sa;
		cp += ROUNDUP(sa->sa_len);
	}
	return (0);
}

static int
rt_msghdrsize(int type)
{
	switch (type) {
	case RTM_DELADDR:
	case RTM_NEWADDR:
		return sizeof(struct ifa_msghdr);
	case RTM_DELMADDR:
	case RTM_NEWMADDR:
		return sizeof(struct ifma_msghdr);
	case RTM_IFINFO:
		return sizeof(struct if_msghdr);
	case RTM_IFANNOUNCE:
	case RTM_IEEE80211:
		return sizeof(struct if_announcemsghdr);
	default:
		return sizeof(struct rt_msghdr);
	}
}

static int
rt_msgsize(int type, struct rt_addrinfo *rtinfo)
{
	int len, i;

	len = rt_msghdrsize(type);
	for (i = 0; i < RTAX_MAX; i++) {
		if (rtinfo->rti_info[i] != NULL)
			len += ROUNDUP(rtinfo->rti_info[i]->sa_len);
	}
	len = ALIGN(len);
	return len;
}

/*
 * Build a routing message in a buffer.
 * Copy the addresses in the rtinfo->rti_info[] sockaddr array
 * to the end of the buffer after the message header.
 *
 * Set the rtinfo->rti_addrs bitmask of addresses present in rtinfo->rti_info[].
 * This side-effect can be avoided if we reorder the addrs bitmask field in all
 * the route messages to line up so we can set it here instead of back in the
 * calling routine.
 */
static void
rt_msg_buffer(int type, struct rt_addrinfo *rtinfo, void *buf, int msglen)
{
	struct rt_msghdr *rtm;
	char *cp;
	int dlen, i;

	rtm = (struct rt_msghdr *) buf;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_type = type;
	rtm->rtm_msglen = msglen;

	cp = (char *)buf + rt_msghdrsize(type);
	rtinfo->rti_addrs = 0;
	for (i = 0; i < RTAX_MAX; i++) {
		struct sockaddr *sa;

		if ((sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1 << i);
		dlen = ROUNDUP(sa->sa_len);
		bcopy(sa, cp, dlen);
		cp += dlen;
	}
}

/*
 * Build a routing message in a mbuf chain.
 * Copy the addresses in the rtinfo->rti_info[] sockaddr array
 * to the end of the mbuf after the message header.
 *
 * Set the rtinfo->rti_addrs bitmask of addresses present in rtinfo->rti_info[].
 * This side-effect can be avoided if we reorder the addrs bitmask field in all
 * the route messages to line up so we can set it here instead of back in the
 * calling routine.
 */
static struct mbuf *
rt_msg_mbuf(int type, struct rt_addrinfo *rtinfo)
{
	struct mbuf *m;
	struct rt_msghdr *rtm;
	int hlen, len;
	int i;

	hlen = rt_msghdrsize(type);
	KASSERT(hlen <= MCLBYTES, ("rt_msg_mbuf: hlen %d doesn't fit", hlen));

	m = m_getl(hlen, MB_DONTWAIT, MT_DATA, M_PKTHDR, NULL);
	if (m == NULL)
		return (NULL);
	mbuftrackid(m, 32);
	m->m_pkthdr.len = m->m_len = hlen;
	m->m_pkthdr.rcvif = NULL;
	rtinfo->rti_addrs = 0;
	len = hlen;
	for (i = 0; i < RTAX_MAX; i++) {
		struct sockaddr *sa;
		int dlen;

		if ((sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1 << i);
		dlen = ROUNDUP(sa->sa_len);
		m_copyback(m, len, dlen, (caddr_t)sa); /* can grow mbuf chain */
		len += dlen;
	}
	if (m->m_pkthdr.len != len) { /* one of the m_copyback() calls failed */
		m_freem(m);
		return (NULL);
	}
	rtm = mtod(m, struct rt_msghdr *);
	bzero(rtm, hlen);
	rtm->rtm_msglen = len;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_type = type;
	return (m);
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that a redirect has occurred, a routing lookup
 * has failed, or that a protocol has detected timeouts to a particular
 * destination.
 */
void
rt_missmsg(int type, struct rt_addrinfo *rtinfo, int flags, int error)
{
	struct sockaddr *dst = rtinfo->rti_info[RTAX_DST];
	struct rt_msghdr *rtm;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return;
	m = rt_msg_mbuf(type, rtinfo);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE | flags;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = rtinfo->rti_addrs;
	rts_input(m, familyof(dst));
}

void
rt_dstmsg(int type, struct sockaddr *dst, int error)
{
	struct rt_msghdr *rtm;
	struct rt_addrinfo addrs;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return;
	bzero(&addrs, sizeof(struct rt_addrinfo));
	addrs.rti_info[RTAX_DST] = dst;
	m = rt_msg_mbuf(type, &addrs);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = addrs.rti_addrs;
	rts_input(m, familyof(dst));
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that the status of a network interface has changed.
 */
void
rt_ifmsg(struct ifnet *ifp)
{
	struct if_msghdr *ifm;
	struct mbuf *m;
	struct rt_addrinfo rtinfo;

	if (route_cb.any_count == 0)
		return;
	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	m = rt_msg_mbuf(RTM_IFINFO, &rtinfo);
	if (m == NULL)
		return;
	ifm = mtod(m, struct if_msghdr *);
	ifm->ifm_index = ifp->if_index;
	ifm->ifm_flags = ifp->if_flags;
	ifm->ifm_data = ifp->if_data;
	ifm->ifm_addrs = 0;
	rts_input(m, 0);
}

static void
rt_ifamsg(int cmd, struct ifaddr *ifa)
{
	struct ifa_msghdr *ifam;
	struct rt_addrinfo rtinfo;
	struct mbuf *m;
	struct ifnet *ifp = ifa->ifa_ifp;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_ifaaddr = ifa->ifa_addr;
	rtinfo.rti_ifpaddr = TAILQ_FIRST(&ifp->if_addrhead)->ifa_addr;
	rtinfo.rti_netmask = ifa->ifa_netmask;
	rtinfo.rti_bcastaddr = ifa->ifa_dstaddr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	ifam = mtod(m, struct ifa_msghdr *);
	ifam->ifam_index = ifp->if_index;
	ifam->ifam_metric = ifa->ifa_metric;
	ifam->ifam_flags = ifa->ifa_flags;
	ifam->ifam_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(ifa->ifa_addr));
}

void
rt_rtmsg(int cmd, struct rtentry *rt, struct ifnet *ifp, int error)
{
	struct rt_msghdr *rtm;
	struct rt_addrinfo rtinfo;
	struct mbuf *m;
	struct sockaddr *dst;

	if (rt == NULL)
		return;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_dst = dst = rt_key(rt);
	rtinfo.rti_gateway = rt->rt_gateway;
	rtinfo.rti_netmask = rt_mask(rt);
	if (ifp != NULL)
		rtinfo.rti_ifpaddr = TAILQ_FIRST(&ifp->if_addrhead)->ifa_addr;
	rtinfo.rti_ifaaddr = rt->rt_ifa->ifa_addr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	rtm = mtod(m, struct rt_msghdr *);
	if (ifp != NULL)
		rtm->rtm_index = ifp->if_index;
	rtm->rtm_flags |= rt->rt_flags;
	rtm->rtm_errno = error;
	rtm->rtm_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(dst));
}

/*
 * This is called to generate messages from the routing socket
 * indicating a network interface has had addresses associated with it.
 * if we ever reverse the logic and replace messages TO the routing
 * socket indicate a request to configure interfaces, then it will
 * be unnecessary as the routing socket will automatically generate
 * copies of it.
 */
void
rt_newaddrmsg(int cmd, struct ifaddr *ifa, int error, struct rtentry *rt)
{
#ifdef SCTP
	/*
	 * notify the SCTP stack
	 * this will only get called when an address is added/deleted
	 * XXX pass the ifaddr struct instead if ifa->ifa_addr...
	 */
	if (cmd == RTM_ADD)
		sctp_add_ip_address(ifa);
	else if (cmd == RTM_DELETE)
		sctp_delete_ip_address(ifa);
#endif /* SCTP */

	if (route_cb.any_count == 0)
		return;

	if (cmd == RTM_ADD) {
		rt_ifamsg(RTM_NEWADDR, ifa);
		rt_rtmsg(RTM_ADD, rt, ifa->ifa_ifp, error);
	} else {
		KASSERT((cmd == RTM_DELETE), ("unknown cmd %d", cmd));
		rt_rtmsg(RTM_DELETE, rt, ifa->ifa_ifp, error);
		rt_ifamsg(RTM_DELADDR, ifa);
	}
}

/*
 * This is the analogue to the rt_newaddrmsg which performs the same
 * function but for multicast group memberhips.  This is easier since
 * there is no route state to worry about.
 */
void
rt_newmaddrmsg(int cmd, struct ifmultiaddr *ifma)
{
	struct rt_addrinfo rtinfo;
	struct mbuf *m = NULL;
	struct ifnet *ifp = ifma->ifma_ifp;
	struct ifma_msghdr *ifmam;

	if (route_cb.any_count == 0)
		return;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_ifaaddr = ifma->ifma_addr;
	if (ifp != NULL && !TAILQ_EMPTY(&ifp->if_addrhead))
		rtinfo.rti_ifpaddr = TAILQ_FIRST(&ifp->if_addrhead)->ifa_addr;
	/*
	 * If a link-layer address is present, present it as a ``gateway''
	 * (similarly to how ARP entries, e.g., are presented).
	 */
	rtinfo.rti_gateway = ifma->ifma_lladdr;

	m = rt_msg_mbuf(cmd, &rtinfo);
	if (m == NULL)
		return;

	ifmam = mtod(m, struct ifma_msghdr *);
	ifmam->ifmam_index = ifp->if_index;
	ifmam->ifmam_addrs = rtinfo.rti_addrs;

	rts_input(m, familyof(ifma->ifma_addr));
}

static struct mbuf *
rt_makeifannouncemsg(struct ifnet *ifp, int type, int what,
		     struct rt_addrinfo *info)
{
	struct if_announcemsghdr *ifan;
	struct mbuf *m;

	if (route_cb.any_count == 0)
		return NULL;

	bzero(info, sizeof(*info));
	m = rt_msg_mbuf(type, info);
	if (m == NULL)
		return NULL;

	ifan = mtod(m, struct if_announcemsghdr *);
	ifan->ifan_index = ifp->if_index;
	strlcpy(ifan->ifan_name, ifp->if_xname, sizeof ifan->ifan_name);
	ifan->ifan_what = what;
	return m;
}

/*
 * This is called to generate routing socket messages indicating
 * IEEE80211 wireless events.
 * XXX we piggyback on the RTM_IFANNOUNCE msg format in a clumsy way.
 */
void
rt_ieee80211msg(struct ifnet *ifp, int what, void *data, size_t data_len)
{
	struct rt_addrinfo info;
	struct mbuf *m;

	m = rt_makeifannouncemsg(ifp, RTM_IEEE80211, what, &info);
	if (m == NULL)
		return;

	/*
	 * Append the ieee80211 data.  Try to stick it in the
	 * mbuf containing the ifannounce msg; otherwise allocate
	 * a new mbuf and append.
	 *
	 * NB: we assume m is a single mbuf.
	 */
	if (data_len > M_TRAILINGSPACE(m)) {
		struct mbuf *n = m_get(MB_DONTWAIT, MT_DATA);
		if (n == NULL) {
			m_freem(m);
			return;
		}
		bcopy(data, mtod(n, void *), data_len);
		n->m_len = data_len;
		m->m_next = n;
	} else if (data_len > 0) {
		bcopy(data, mtod(m, u_int8_t *) + m->m_len, data_len);
		m->m_len += data_len;
	}
	mbuftrackid(m, 33);
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len += data_len;
	mtod(m, struct if_announcemsghdr *)->ifan_msglen += data_len;
	rts_input(m, 0);
}

/*
 * This is called to generate routing socket messages indicating
 * network interface arrival and departure.
 */
void
rt_ifannouncemsg(struct ifnet *ifp, int what)
{
	struct rt_addrinfo addrinfo;
	struct mbuf *m;

	m = rt_makeifannouncemsg(ifp, RTM_IFANNOUNCE, what, &addrinfo);
	if (m != NULL)
		rts_input(m, 0);
}

static int
resizewalkarg(struct walkarg *w, int len)
{
	void *newptr;

	newptr = kmalloc(len, M_RTABLE, M_INTWAIT | M_NULLOK);
	if (newptr == NULL)
		return (ENOMEM);
	if (w->w_tmem != NULL)
		kfree(w->w_tmem, M_RTABLE);
	w->w_tmem = newptr;
	w->w_tmemsize = len;
	return (0);
}

/*
 * This is used in dumping the kernel table via sysctl().
 */
int
sysctl_dumpentry(struct radix_node *rn, void *vw)
{
	struct walkarg *w = vw;
	struct rtentry *rt = (struct rtentry *)rn;
	struct rt_addrinfo rtinfo;
	int error, msglen;

	if (w->w_op == NET_RT_FLAGS && !(rt->rt_flags & w->w_arg))
		return 0;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	rtinfo.rti_dst = rt_key(rt);
	rtinfo.rti_gateway = rt->rt_gateway;
	rtinfo.rti_netmask = rt_mask(rt);
	rtinfo.rti_genmask = rt->rt_genmask;
	if (rt->rt_ifp != NULL) {
		rtinfo.rti_ifpaddr =
		    TAILQ_FIRST(&rt->rt_ifp->if_addrhead)->ifa_addr;
		rtinfo.rti_ifaaddr = rt->rt_ifa->ifa_addr;
		if (rt->rt_ifp->if_flags & IFF_POINTOPOINT)
			rtinfo.rti_bcastaddr = rt->rt_ifa->ifa_dstaddr;
	}
	msglen = rt_msgsize(RTM_GET, &rtinfo);
	if (w->w_tmemsize < msglen && resizewalkarg(w, msglen) != 0)
		return (ENOMEM);
	rt_msg_buffer(RTM_GET, &rtinfo, w->w_tmem, msglen);
	if (w->w_req != NULL) {
		struct rt_msghdr *rtm = w->w_tmem;

		rtm->rtm_flags = rt->rt_flags;
		rtm->rtm_use = rt->rt_use;
		rtm->rtm_rmx = rt->rt_rmx;
		rtm->rtm_index = rt->rt_ifp->if_index;
		rtm->rtm_errno = rtm->rtm_pid = rtm->rtm_seq = 0;
		rtm->rtm_addrs = rtinfo.rti_addrs;
		error = SYSCTL_OUT(w->w_req, rtm, msglen);
		return (error);
	}
	return (0);
}

static int
sysctl_iflist(int af, struct walkarg *w)
{
	struct ifnet *ifp;
	struct ifaddr *ifa;
	struct rt_addrinfo rtinfo;
	int msglen, error;

	bzero(&rtinfo, sizeof(struct rt_addrinfo));
	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		if (w->w_arg && w->w_arg != ifp->if_index)
			continue;
		ifa = TAILQ_FIRST(&ifp->if_addrhead);
		rtinfo.rti_ifpaddr = ifa->ifa_addr;
		msglen = rt_msgsize(RTM_IFINFO, &rtinfo);
		if (w->w_tmemsize < msglen && resizewalkarg(w, msglen) != 0)
			return (ENOMEM);
		rt_msg_buffer(RTM_IFINFO, &rtinfo, w->w_tmem, msglen);
		rtinfo.rti_ifpaddr = NULL;
		if (w->w_req != NULL && w->w_tmem != NULL) {
			struct if_msghdr *ifm = w->w_tmem;

			ifm->ifm_index = ifp->if_index;
			ifm->ifm_flags = ifp->if_flags;
			ifm->ifm_data = ifp->if_data;
			ifm->ifm_addrs = rtinfo.rti_addrs;
			error = SYSCTL_OUT(w->w_req, ifm, msglen);
			if (error)
				return (error);
		}
		while ((ifa = TAILQ_NEXT(ifa, ifa_link)) != NULL) {
			if (af && af != ifa->ifa_addr->sa_family)
				continue;
			if (curproc->p_ucred->cr_prison &&
			    prison_if(curproc->p_ucred, ifa->ifa_addr))
				continue;
			rtinfo.rti_ifaaddr = ifa->ifa_addr;
			rtinfo.rti_netmask = ifa->ifa_netmask;
			rtinfo.rti_bcastaddr = ifa->ifa_dstaddr;
			msglen = rt_msgsize(RTM_NEWADDR, &rtinfo);
			if (w->w_tmemsize < msglen &&
			    resizewalkarg(w, msglen) != 0)
				return (ENOMEM);
			rt_msg_buffer(RTM_NEWADDR, &rtinfo, w->w_tmem, msglen);
			if (w->w_req != NULL) {
				struct ifa_msghdr *ifam = w->w_tmem;

				ifam->ifam_index = ifa->ifa_ifp->if_index;
				ifam->ifam_flags = ifa->ifa_flags;
				ifam->ifam_metric = ifa->ifa_metric;
				ifam->ifam_addrs = rtinfo.rti_addrs;
				error = SYSCTL_OUT(w->w_req, w->w_tmem, msglen);
				if (error)
					return (error);
			}
		}
		rtinfo.rti_netmask = NULL;
		rtinfo.rti_ifaaddr = NULL;
		rtinfo.rti_bcastaddr = NULL;
	}
	return (0);
}

static int
sysctl_rtsock(SYSCTL_HANDLER_ARGS)
{
	int	*name = (int *)arg1;
	u_int	namelen = arg2;
	struct radix_node_head *rnh;
	int	i, error = EINVAL;
	int	origcpu;
	u_char  af;
	struct	walkarg w;

	name ++;
	namelen--;
	if (req->newptr)
		return (EPERM);
	if (namelen != 3 && namelen != 4)
		return (EINVAL);
	af = name[0];
	bzero(&w, sizeof w);
	w.w_op = name[1];
	w.w_arg = name[2];
	w.w_req = req;

	/*
	 * Optional third argument specifies cpu, used primarily for
	 * debugging the route table.
	 */
	if (namelen == 4) {
		if (name[3] < 0 || name[3] >= ncpus)
			return (EINVAL);
		origcpu = mycpuid;
		lwkt_migratecpu(name[3]);
	} else {
		origcpu = -1;
	}
	crit_enter();
	switch (w.w_op) {
	case NET_RT_DUMP:
	case NET_RT_FLAGS:
		for (i = 1; i <= AF_MAX; i++)
			if ((rnh = rt_tables[mycpuid][i]) &&
			    (af == 0 || af == i) &&
			    (error = rnh->rnh_walktree(rnh,
						       sysctl_dumpentry, &w)))
				break;
		break;

	case NET_RT_IFLIST:
		error = sysctl_iflist(af, &w);
	}
	crit_exit();
	if (w.w_tmem != NULL)
		kfree(w.w_tmem, M_RTABLE);
	if (origcpu >= 0)
		lwkt_migratecpu(origcpu);
	return (error);
}

SYSCTL_NODE(_net, PF_ROUTE, routetable, CTLFLAG_RD, sysctl_rtsock, "");

/*
 * Definitions of protocols supported in the ROUTE domain.
 */

static struct domain routedomain;		/* or at least forward */

static struct protosw routesw[] = {
{ SOCK_RAW,	&routedomain,	0,		PR_ATOMIC|PR_ADDR,
  0,		route_output,	raw_ctlinput,	0,
  cpu0_soport,
  raw_init,	0,		0,		0,
  &route_usrreqs
}
};

static struct domain routedomain = {
	PF_ROUTE, "route", NULL, NULL, NULL,
	routesw, &routesw[(sizeof routesw)/(sizeof routesw[0])],
};

DOMAIN_SET(route);

