/*
 * iproute.c		"ip route".
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *
 * Changes:
 *
 * Rani Assaf <rani@magic.metawire.com> 980929:	resolve addresses
 * Kunihiro Ishiguro <kunihiro@zebra.org> 001102: rtnh_ifindex was not initialized
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <linux/in_route.h>
#include <linux/ip_mp_alg.h>

#include "rt_names.h"
#include "utils.h"
#include "ip_common.h"
#include "iproute.h"

#ifndef RTAX_RTTVAR
#define RTAX_RTTVAR RTAX_HOPS
#endif

static char *mp_alg_names[IP_MP_ALG_MAX+1] = {
	[IP_MP_ALG_NONE] = "none",
	[IP_MP_ALG_RR] = "rr",
	[IP_MP_ALG_DRR] = "drr",
	[IP_MP_ALG_RANDOM] = "random",
	[IP_MP_ALG_WRANDOM] = "wrandom"
};

/*
static int flush_update(void)
{
	if (rtnl_send(&rth, filter.flushb, filter.flushp) < 0) {
		perror("Failed to send flush request\n");
		return -1;
	}
	filter.flushp = 0;
	return 0;
}
int print_route(const struct sockaddr_nl *who, struct nlmsghdr *n, void *arg)
{
	FILE *fp = (FILE*)arg;
	struct rtmsg *r = NLMSG_DATA(n);
	int len = n->nlmsg_len;
	struct rtattr * tb[RTA_MAX+1];
	char abuf[256];
	inet_prefix dst;
	inet_prefix src;
	inet_prefix prefsrc;
	inet_prefix via;
	int host_len = -1;
	SPRINT_BUF(b1);
	

	if (n->nlmsg_type != RTM_NEWROUTE && n->nlmsg_type != RTM_DELROUTE) {
		fprintf(stderr, "Not a route: %08x %08x %08x\n",
			n->nlmsg_len, n->nlmsg_type, n->nlmsg_flags);
		return 0;
	}
	if (filter.flushb && n->nlmsg_type != RTM_NEWROUTE)
		return 0;
	len -= NLMSG_LENGTH(sizeof(*r));
	if (len < 0) {
		fprintf(stderr, "BUG: wrong nlmsg len %d\n", len);
		return -1;
	}

	if (r->rtm_family == AF_INET6)
		host_len = 128;
	else if (r->rtm_family == AF_INET)
		host_len = 32;

	if (r->rtm_family == AF_INET6) {
		if (filter.tb) {
			if (filter.tb < 0) {
				if (!(r->rtm_flags&RTM_F_CLONED))
					return 0;
			} else {
				if (r->rtm_flags&RTM_F_CLONED)
					return 0;
				if (filter.tb == RT_TABLE_LOCAL) {
					if (r->rtm_type != RTN_LOCAL)
						return 0;
				} else if (filter.tb == RT_TABLE_MAIN) {
					if (r->rtm_type == RTN_LOCAL)
						return 0;
				} else {
					return 0;
				}
			}
		}
	} else {
		if (filter.tb > 0 && filter.tb != r->rtm_table)
			return 0;
	}
	if ((filter.protocol^r->rtm_protocol)&filter.protocolmask)
		return 0;
	if ((filter.scope^r->rtm_scope)&filter.scopemask)
		return 0;
	if ((filter.type^r->rtm_type)&filter.typemask)
		return 0;
	if ((filter.tos^r->rtm_tos)&filter.tosmask)
		return 0;
	if (filter.rdst.family &&
	    (r->rtm_family != filter.rdst.family || filter.rdst.bitlen > r->rtm_dst_len))
		return 0;
	if (filter.mdst.family &&
	    (r->rtm_family != filter.mdst.family ||
	     (filter.mdst.bitlen >= 0 && filter.mdst.bitlen < r->rtm_dst_len)))
		return 0;
	if (filter.rsrc.family &&
	    (r->rtm_family != filter.rsrc.family || filter.rsrc.bitlen > r->rtm_src_len))
		return 0;
	if (filter.msrc.family &&
	    (r->rtm_family != filter.msrc.family ||
	     (filter.msrc.bitlen >= 0 && filter.msrc.bitlen < r->rtm_src_len)))
		return 0;
	if (filter.rvia.family && r->rtm_family != filter.rvia.family)
		return 0;
	if (filter.rprefsrc.family && r->rtm_family != filter.rprefsrc.family)
		return 0;

	parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

	memset(&dst, 0, sizeof(dst));
	dst.family = r->rtm_family;
	if (tb[RTA_DST])
		memcpy(&dst.data, RTA_DATA(tb[RTA_DST]), (r->rtm_dst_len+7)/8);
	if (filter.rsrc.family || filter.msrc.family) {
		memset(&src, 0, sizeof(src));
		src.family = r->rtm_family;
		if (tb[RTA_SRC])
			memcpy(&src.data, RTA_DATA(tb[RTA_SRC]), (r->rtm_src_len+7)/8);
	}
	if (filter.rvia.bitlen>0) {
		memset(&via, 0, sizeof(via));
		via.family = r->rtm_family;
		if (tb[RTA_GATEWAY])
			memcpy(&via.data, RTA_DATA(tb[RTA_GATEWAY]), host_len/8);
	}
	if (filter.rprefsrc.bitlen>0) {
		memset(&prefsrc, 0, sizeof(prefsrc));
		prefsrc.family = r->rtm_family;
		if (tb[RTA_PREFSRC])
			memcpy(&prefsrc.data, RTA_DATA(tb[RTA_PREFSRC]), host_len/8);
	}

	if (filter.rdst.family && inet_addr_match(&dst, &filter.rdst, filter.rdst.bitlen))
		return 0;
	if (filter.mdst.family && filter.mdst.bitlen >= 0 &&
	    inet_addr_match(&dst, &filter.mdst, r->rtm_dst_len))
		return 0;

	if (filter.rsrc.family && inet_addr_match(&src, &filter.rsrc, filter.rsrc.bitlen))
		return 0;
	if (filter.msrc.family && filter.msrc.bitlen >= 0 &&
	    inet_addr_match(&src, &filter.msrc, r->rtm_src_len))
		return 0;

	if (filter.rvia.family && inet_addr_match(&via, &filter.rvia, filter.rvia.bitlen))
		return 0;
	if (filter.rprefsrc.family && inet_addr_match(&prefsrc, &filter.rprefsrc, filter.rprefsrc.bitlen))
		return 0;
	if (filter.realmmask) {
		__u32 realms = 0;
		if (tb[RTA_FLOW])
			realms = *(__u32*)RTA_DATA(tb[RTA_FLOW]);
		if ((realms^filter.realm)&filter.realmmask)
			return 0;
	}
	if (filter.iifmask) {
		int iif = 0;
		if (tb[RTA_IIF])
			iif = *(int*)RTA_DATA(tb[RTA_IIF]);
		if ((iif^filter.iif)&filter.iifmask)
			return 0;
	}
	if (filter.oifmask) {
		int oif = 0;
		if (tb[RTA_OIF])
			oif = *(int*)RTA_DATA(tb[RTA_OIF]);
		if ((oif^filter.oif)&filter.oifmask)
			return 0;
	}
	if (filter.flushb && 
	    r->rtm_family == AF_INET6 &&
	    r->rtm_dst_len == 0 &&
	    r->rtm_type == RTN_UNREACHABLE &&
	    tb[RTA_PRIORITY] &&
	    *(int*)RTA_DATA(tb[RTA_PRIORITY]) == -1)
		return 0;

	if (filter.flushb) {
		struct nlmsghdr *fn;
		if (NLMSG_ALIGN(filter.flushp) + n->nlmsg_len > filter.flushe) {
			if (flush_update())
				return -1;
		}
		fn = (struct nlmsghdr*)(filter.flushb + NLMSG_ALIGN(filter.flushp));
		memcpy(fn, n, n->nlmsg_len);
		fn->nlmsg_type = RTM_DELROUTE;
		fn->nlmsg_flags = NLM_F_REQUEST;
		fn->nlmsg_seq = ++rth.seq;
		filter.flushp = (((char*)fn) + n->nlmsg_len) - filter.flushb;
		filter.flushed++;
		if (show_stats < 2)
			return 0;
	}

	if (n->nlmsg_type == RTM_DELROUTE)
		fprintf(fp, "Deleted ");
	if (r->rtm_type != RTN_UNICAST && !filter.type)
		fprintf(fp, "%s ", rtnl_rtntype_n2a(r->rtm_type, b1, sizeof(b1)));

	if (tb[RTA_DST]) {
		if (r->rtm_dst_len != host_len) {
			fprintf(fp, "%s/%u ", rt_addr_n2a(r->rtm_family,
							 RTA_PAYLOAD(tb[RTA_DST]),
							 RTA_DATA(tb[RTA_DST]),
							 abuf, sizeof(abuf)),
				r->rtm_dst_len
				);
		} else {
			fprintf(fp, "%s ", format_host(r->rtm_family,
						       RTA_PAYLOAD(tb[RTA_DST]),
						       RTA_DATA(tb[RTA_DST]),
						       abuf, sizeof(abuf))
				);
		}
	} else if (r->rtm_dst_len) {
		fprintf(fp, "0/%d ", r->rtm_dst_len);
	} else {
		fprintf(fp, "default ");
	}
	if (tb[RTA_SRC]) {
		if (r->rtm_src_len != host_len) {
			fprintf(fp, "from %s/%u ", rt_addr_n2a(r->rtm_family,
							 RTA_PAYLOAD(tb[RTA_SRC]),
							 RTA_DATA(tb[RTA_SRC]),
							 abuf, sizeof(abuf)),
				r->rtm_src_len
				);
		} else {
			fprintf(fp, "from %s ", format_host(r->rtm_family,
						       RTA_PAYLOAD(tb[RTA_SRC]),
						       RTA_DATA(tb[RTA_SRC]),
						       abuf, sizeof(abuf))
				);
		}
	} else if (r->rtm_src_len) {
		fprintf(fp, "from 0/%u ", r->rtm_src_len);
	}
	if (r->rtm_tos && filter.tosmask != -1) {
		SPRINT_BUF(b1);
		fprintf(fp, "tos %s ", rtnl_dsfield_n2a(r->rtm_tos, b1, sizeof(b1)));
	}

	if (tb[RTA_MP_ALGO]) {
		__u32 mp_alg = *(__u32*) RTA_DATA(tb[RTA_MP_ALGO]);
		if (mp_alg > IP_MP_ALG_NONE) {
			fprintf(fp, "mpath %s ",
			    mp_alg < IP_MP_ALG_MAX ? mp_alg_names[mp_alg] : "unknown");
		}
	}

	if (tb[RTA_GATEWAY] && filter.rvia.bitlen != host_len) {
		fprintf(fp, "via %s ", 
			format_host(r->rtm_family,
				    RTA_PAYLOAD(tb[RTA_GATEWAY]),
				    RTA_DATA(tb[RTA_GATEWAY]),
				    abuf, sizeof(abuf)));
	}
	if (tb[RTA_OIF] && filter.oifmask != -1)
		fprintf(fp, "dev %s ", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_OIF])));

	if (!(r->rtm_flags&RTM_F_CLONED)) {
		if (r->rtm_table != RT_TABLE_MAIN && !filter.tb)
			fprintf(fp, " table %s ", rtnl_rttable_n2a(r->rtm_table, b1, sizeof(b1)));
		if (r->rtm_protocol != RTPROT_BOOT && filter.protocolmask != -1)
			fprintf(fp, " proto %s ", rtnl_rtprot_n2a(r->rtm_protocol, b1, sizeof(b1)));
		if (r->rtm_scope != RT_SCOPE_UNIVERSE && filter.scopemask != -1)
			fprintf(fp, " scope %s ", rtnl_rtscope_n2a(r->rtm_scope, b1, sizeof(b1)));
	}
	if (tb[RTA_PREFSRC] && filter.rprefsrc.bitlen != host_len) {
		fprintf(fp, " src %s ", 
			rt_addr_n2a(r->rtm_family,
				    RTA_PAYLOAD(tb[RTA_PREFSRC]),
				    RTA_DATA(tb[RTA_PREFSRC]),
				    abuf, sizeof(abuf)));
	}
	if (tb[RTA_PRIORITY])
		fprintf(fp, " metric %d ", *(__u32*)RTA_DATA(tb[RTA_PRIORITY]));
	if (r->rtm_flags & RTNH_F_DEAD)
		fprintf(fp, "dead ");
	if (r->rtm_flags & RTNH_F_ONLINK)
		fprintf(fp, "onlink ");
	if (r->rtm_flags & RTNH_F_PERVASIVE)
		fprintf(fp, "pervasive ");
	if (r->rtm_flags & RTM_F_EQUALIZE)
		fprintf(fp, "equalize ");
	if (r->rtm_flags & RTM_F_NOTIFY)
		fprintf(fp, "notify ");

	if (tb[RTA_FLOW] && filter.realmmask != ~0U) {
		__u32 to = *(__u32*)RTA_DATA(tb[RTA_FLOW]);
		__u32 from = to>>16;
		to &= 0xFFFF;
		fprintf(fp, "realm%s ", from ? "s" : "");
		if (from) {
			fprintf(fp, "%s/",
				rtnl_rtrealm_n2a(from, b1, sizeof(b1)));
		}
		fprintf(fp, "%s ",
			rtnl_rtrealm_n2a(to, b1, sizeof(b1)));
	}
	if ((r->rtm_flags&RTM_F_CLONED) && r->rtm_family == AF_INET) {
		__u32 flags = r->rtm_flags&~0xFFFF;
		int first = 1;

		fprintf(fp, "%s    cache ", _SL_);

#define PRTFL(fl,flname) if (flags&RTCF_##fl) { \
  flags &= ~RTCF_##fl; \
  fprintf(fp, "%s" flname "%s", first ? "<" : "", flags ? "," : "> "); \
  first = 0; }
		PRTFL(LOCAL, "local");
		PRTFL(REJECT, "reject");
		PRTFL(MULTICAST, "mc");
		PRTFL(BROADCAST, "brd");
		PRTFL(DNAT, "dst-nat");
		PRTFL(SNAT, "src-nat");
		PRTFL(MASQ, "masq");
		PRTFL(DIRECTDST, "dst-direct");
		PRTFL(DIRECTSRC, "src-direct");
		PRTFL(REDIRECTED, "redirected");
		PRTFL(DOREDIRECT, "redirect");
		PRTFL(FAST, "fastroute");
		PRTFL(NOTIFY, "notify");
		PRTFL(TPROXY, "proxy");
#ifdef RTCF_EQUALIZE
		PRTFL(EQUALIZE, "equalize");
#endif
		if (flags)
			fprintf(fp, "%s%x> ", first ? "<" : "", flags);
		if (tb[RTA_CACHEINFO]) {
			struct rta_cacheinfo *ci = RTA_DATA(tb[RTA_CACHEINFO]);
			static int hz;
			if (!hz)
				hz = get_user_hz();
			if (ci->rta_expires != 0)
				fprintf(fp, " expires %dsec", ci->rta_expires/hz);
			if (ci->rta_error != 0)
				fprintf(fp, " error %d", ci->rta_error);
			if (show_stats) {
				if (ci->rta_clntref)
					fprintf(fp, " users %d", ci->rta_clntref);
				if (ci->rta_used != 0)
					fprintf(fp, " used %d", ci->rta_used);
				if (ci->rta_lastuse != 0)
					fprintf(fp, " age %dsec", ci->rta_lastuse/hz);
			}
#ifdef RTNETLINK_HAVE_PEERINFO
			if (ci->rta_id)
				fprintf(fp, " ipid 0x%04x", ci->rta_id);
			if (ci->rta_ts || ci->rta_tsage)
				fprintf(fp, " ts 0x%x tsage %dsec", ci->rta_ts, ci->rta_tsage);
#endif
		}
	} else if (r->rtm_family == AF_INET6) {
		struct rta_cacheinfo *ci = NULL;
		if (tb[RTA_CACHEINFO])
			ci = RTA_DATA(tb[RTA_CACHEINFO]);
		if ((r->rtm_flags & RTM_F_CLONED) || (ci && ci->rta_expires)) {
			static int hz;
			if (!hz)
				hz = get_user_hz();
			if (r->rtm_flags & RTM_F_CLONED)
				fprintf(fp, "%s    cache ", _SL_);
			if (ci->rta_expires)
				fprintf(fp, " expires %dsec", ci->rta_expires/hz);
			if (ci->rta_error != 0)
				fprintf(fp, " error %d", ci->rta_error);
			if (show_stats) {
				if (ci->rta_clntref)
					fprintf(fp, " users %d", ci->rta_clntref);
				if (ci->rta_used != 0)
					fprintf(fp, " used %d", ci->rta_used);
				if (ci->rta_lastuse != 0)
					fprintf(fp, " age %dsec", ci->rta_lastuse/hz);
			}
		} else if (ci) {
			if (ci->rta_error != 0)
				fprintf(fp, " error %d", ci->rta_error);
		}
	}
	if (tb[RTA_METRICS]) {
		int i;
		unsigned mxlock = 0;
		struct rtattr *mxrta[RTAX_MAX+1];

		parse_rtattr(mxrta, RTAX_MAX, RTA_DATA(tb[RTA_METRICS]),
			    RTA_PAYLOAD(tb[RTA_METRICS]));
		if (mxrta[RTAX_LOCK])
			mxlock = *(unsigned*)RTA_DATA(mxrta[RTAX_LOCK]);

		for (i=2; i<=RTAX_MAX; i++) {
			static char *mx_names[] = 
			{
				"mtu",
				"window",
				"rtt",
				"rttvar",
				"ssthresh",
				"cwnd",
				"advmss",
				"reordering",
			};
			static int hz;
			if (mxrta[i] == NULL)
				continue;
			if (!hz)
				hz = get_hz();
			if (i-2 < sizeof(mx_names)/sizeof(char*))
				fprintf(fp, " %s", mx_names[i-2]);
			else
				fprintf(fp, " metric %d", i);
			if (mxlock & (1<<i))
				fprintf(fp, " lock");

			if (i != RTAX_RTT && i != RTAX_RTTVAR)
				fprintf(fp, " %u", *(unsigned*)RTA_DATA(mxrta[i]));
			else {
				unsigned val = *(unsigned*)RTA_DATA(mxrta[i]);

				val *= 1000;
				if (i == RTAX_RTT)
					val /= 8;
				else
					val /= 4;
				if (val >= hz)
					fprintf(fp, " %ums", val/hz);
				else
					fprintf(fp, " %.2fms", (float)val/hz);
			}
		}
	}
	if (tb[RTA_IIF] && filter.iifmask != -1) {
		fprintf(fp, " iif %s", ll_index_to_name(*(int*)RTA_DATA(tb[RTA_IIF])));
	}
	if (tb[RTA_MULTIPATH]) {
		struct rtnexthop *nh = RTA_DATA(tb[RTA_MULTIPATH]);
		int first = 0;

		len = RTA_PAYLOAD(tb[RTA_MULTIPATH]);

		for (;;) {
			if (len < sizeof(*nh))
				break;
			if (nh->rtnh_len > len)
				break;
			if (r->rtm_flags&RTM_F_CLONED && r->rtm_type == RTN_MULTICAST) {
				if (first)
					fprintf(fp, " Oifs:");
				else
					fprintf(fp, " ");
			} else
				fprintf(fp, "%s\tnexthop", _SL_);
			if (nh->rtnh_len > sizeof(*nh)) {
				parse_rtattr(tb, RTA_MAX, RTNH_DATA(nh), nh->rtnh_len - sizeof(*nh));
				if (tb[RTA_GATEWAY]) {
					fprintf(fp, " via %s ", 
						format_host(r->rtm_family,
							    RTA_PAYLOAD(tb[RTA_GATEWAY]),
							    RTA_DATA(tb[RTA_GATEWAY]),
							    abuf, sizeof(abuf)));
				}
			}
			if (r->rtm_flags&RTM_F_CLONED && r->rtm_type == RTN_MULTICAST) {
				return ll_index_to_name(nh->rtnh_ifindex);
				fprintf(fp, " %s", ll_index_to_name(nh->rtnh_ifindex));
				if (nh->rtnh_hops != 1)
					fprintf(fp, "(ttl>%d)", nh->rtnh_hops);
			} else {
				fprintf(fp, " dev %s", ll_index_to_name(nh->rtnh_ifindex));
				fprintf(fp, " weight %d", nh->rtnh_hops+1);
			}
			if (nh->rtnh_flags & RTNH_F_DEAD)
				fprintf(fp, " dead");
			if (nh->rtnh_flags & RTNH_F_ONLINK)
				fprintf(fp, " onlink");
			if (nh->rtnh_flags & RTNH_F_PERVASIVE)
				fprintf(fp, " pervasive");
			len -= NLMSG_ALIGN(nh->rtnh_len);
			nh = RTNH_NEXT(nh);
		}
	}
	fprintf(fp, "\n");
	fflush(fp);
	return 0;
}
*/
int parse_one_nh(struct rtattr *rta, struct rtnexthop *rtnh, int *argcp, char *argvp)
{
	int argc = *argcp;
	char *argv = argvp;

	while (++argv, --argc > 0) {
		if (strcmp(argv, "via") == 0) {
			NEXT_ARG();
			rta_addattr32(rta, 4096, RTA_GATEWAY, get_addr32(argv));
			rtnh->rtnh_len += sizeof(struct rtattr) + 4;
		} else if (strcmp(argv, "dev") == 0) {
			NEXT_ARG();
			if ((rtnh->rtnh_ifindex = ll_name_to_index(argv)) == 0) {
				fprintf(stderr, "Cannot find device \"%s\"\n", argv);
				exit(1);
			}
		} else if (strcmp(argv, "weight") == 0) {
			unsigned w;
			NEXT_ARG();
			if (get_unsigned(&w, argv, 0) || w == 0 || w > 256)
				invarg("\"weight\" is invalid\n", argv);
			rtnh->rtnh_hops = w - 1;
		} else if (strcmp(argv, "onlink") == 0) {
			rtnh->rtnh_flags |= RTNH_F_ONLINK;
		} else
			break;
	}
	*argcp = argc;
	argvp = argv;
	return 0;
}

int parse_nexthops(struct nlmsghdr *n, struct rtmsg *r, int argc, char *argv)
{
	char buf[1024];
	struct rtattr *rta = (void*)buf;
	struct rtnexthop *rtnh;

	rta->rta_type = RTA_MULTIPATH;
	rta->rta_len = RTA_LENGTH(0);
	rtnh = RTA_DATA(rta);

	while (argc > 0) {
		if (strcmp(argv, "nexthop") != 0) {
			fprintf(stderr, "Error: \"nexthop\" or end of line is expected instead of \"%s\"\n", argv);
			exit(-1);
		}
		if (argc <= 1) {
			fprintf(stderr, "Error: unexpected end of line after \"nexthop\"\n");
			exit(-1);
		}
		memset(rtnh, 0, sizeof(*rtnh));
		rtnh->rtnh_len = sizeof(*rtnh);
		rta->rta_len += rtnh->rtnh_len;
		parse_one_nh(rta, rtnh, &argc, argv);
		rtnh = RTNH_NEXT(rtnh);
	}

	if (rta->rta_len > RTA_LENGTH(0))
		addattr_l(n, 1024, RTA_MULTIPATH, RTA_DATA(rta), RTA_PAYLOAD(rta));
	return 0;
}


int iproute_modify(int cmd, unsigned flags, int argc, char *argv)
{
	REQ req;
	char  mxbuf[256];
	struct rtattr * mxrta = (void*)mxbuf;
	unsigned mxlock = 0;
	char  *d = NULL;
	int gw_ok = 0;
	int dst_ok = 0;
	int nhs_ok = 0;
	int scope_ok = 0;
	int table_ok = 0;
	int proto_ok = 0;
//	int type_ok = 0;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST|flags;
	req.n.nlmsg_type = cmd;
	req.r.rtm_family = preferred_family;
	req.r.rtm_table = RT_TABLE_MAIN;
	req.r.rtm_scope = RT_SCOPE_NOWHERE;

	if (cmd != RTM_DELROUTE) {
		req.r.rtm_protocol = RTPROT_BOOT;
		req.r.rtm_scope = RT_SCOPE_UNIVERSE;
		req.r.rtm_type = RTN_UNICAST;
	}

	mxrta->rta_type = RTA_METRICS;
	mxrta->rta_len = RTA_LENGTH(0);

	while (argc > 0) {
		if (strcmp(argv, "src") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			get_addr(&addr, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			addattr_l(&req.n, sizeof(req), RTA_PREFSRC, &addr.data, addr.bytelen);
		} else if (strcmp(argv, "via") == 0) {
			inet_prefix addr;
			gw_ok = 1;
			NEXT_ARG();
			get_addr(&addr, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			addattr_l(&req.n, sizeof(req), RTA_GATEWAY, &addr.data, addr.bytelen);
		} else if (strcmp(argv, "from") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			get_prefix(&addr, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			if (addr.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_SRC, &addr.data, addr.bytelen);
			req.r.rtm_src_len = addr.bitlen;
		} else if (strcmp(argv, "tos") == 0 ||
			   matches(argv, "dsfield") == 0) {
			__u32 tos;
			NEXT_ARG();
			if (rtnl_dsfield_a2n(&tos, argv))
				invarg("\"tos\" value is invalid\n", argv);
			req.r.rtm_tos = tos;
		} else if (matches(argv, "metric") == 0 ||
			   matches(argv, "priority") == 0 ||
			   matches(argv, "preference") == 0) {
			__u32 metric;
			NEXT_ARG();
			if (get_u32(&metric, argv, 0))
				invarg("\"metric\" value is invalid\n", argv);
			addattr32(&req.n, sizeof(req), RTA_PRIORITY, metric);
		} else if (strcmp(argv, "scope") == 0) {
			__u32 scope = 0;
			NEXT_ARG();
			if (rtnl_rtscope_a2n(&scope, argv))
				invarg("invalid \"scope\" value\n", argv);
			req.r.rtm_scope = scope;
			scope_ok = 1;
		} else if (strcmp(argv, "mtu") == 0) {
			unsigned mtu;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_MTU);
				NEXT_ARG();
			}
			if (get_unsigned(&mtu, argv, 0))
				invarg("\"mtu\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_MTU, mtu);
#ifdef RTAX_ADVMSS
		} else if (strcmp(argv, "advmss") == 0) {
			unsigned mss;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_ADVMSS);
				NEXT_ARG();
			}
			if (get_unsigned(&mss, argv, 0))
				invarg("\"mss\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_ADVMSS, mss);
#endif
#ifdef RTAX_REORDERING
		} else if (matches(argv, "reordering") == 0) {
			unsigned reord;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_REORDERING);
				NEXT_ARG();
			}
			if (get_unsigned(&reord, argv, 0))
				invarg("\"reordering\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_REORDERING, reord);
#endif
		} else if (strcmp(argv, "rtt") == 0) {
			unsigned rtt;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_RTT);
				NEXT_ARG();
			}
			if (get_unsigned(&rtt, argv, 0))
				invarg("\"rtt\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_RTT, rtt);
		} else if (matches(argv, "window") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_WINDOW);
				NEXT_ARG();
			}
			if (get_unsigned(&win, argv, 0))
				invarg("\"window\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_WINDOW, win);
		} else if (matches(argv, "cwnd") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_CWND);
				NEXT_ARG();
			}
			if (get_unsigned(&win, argv, 0))
				invarg("\"cwnd\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_CWND, win);
		} else if (matches(argv, "rttvar") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_RTTVAR);
				NEXT_ARG();
			}
			if (get_unsigned(&win, argv, 0))
				invarg("\"rttvar\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_RTTVAR, win);
		} else if (matches(argv, "ssthresh") == 0) {
			unsigned win;
			NEXT_ARG();
			if (strcmp(argv, "lock") == 0) {
				mxlock |= (1<<RTAX_SSTHRESH);
				NEXT_ARG();
			}
			if (get_unsigned(&win, argv, 0))
				invarg("\"ssthresh\" value is invalid\n", argv);
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_SSTHRESH, win);
//		} else if (matches(argv, "realms") == 0) {
//			__u32 realm;
//			NEXT_ARG();
//			if (get_rt_realms(&realm, argv))
//				invarg("\"realm\" value is invalid\n", argv);
//			addattr32(&req.n, sizeof(req), RTA_FLOW, realm);
		} else if (strcmp(argv, "onlink") == 0) {
			req.r.rtm_flags |= RTNH_F_ONLINK;
		} else if (matches(argv, "equalize") == 0 ||
			   strcmp(argv, "eql") == 0) {
			req.r.rtm_flags |= RTM_F_EQUALIZE;
		} else if (strcmp(argv, "nexthop") == 0) {
			nhs_ok = 1;
			break;
		} else if (matches(argv, "protocol") == 0) {
			__u32 prot;
			NEXT_ARG();
			if (rtnl_rtprot_a2n(&prot, argv))
				invarg("\"protocol\" value is invalid\n", argv);
			req.r.rtm_protocol = prot;
			proto_ok =1;
		} else if (matches(argv, "table") == 0) {
			__u32 tid;
			NEXT_ARG();
			if (rtnl_rttable_a2n(&tid, argv))
				invarg("\"table\" value is invalid\n", argv);
			req.r.rtm_table = tid;
			table_ok = 1;
		} else if (strcmp(argv, "dev") == 0 ||
			   strcmp(argv, "oif") == 0) {
			NEXT_ARG();
			d = argv;
		} else if (strcmp(argv, "mpath") == 0 ||
			   strcmp(argv, "mp") == 0) {
			int i;
			__u32 mp_alg = IP_MP_ALG_NONE;

			NEXT_ARG();
			for (i = 1; i < ARRAY_SIZE(mp_alg_names); i++)
				if (strcmp(argv, mp_alg_names[i]) == 0)
					mp_alg = i;
			if (mp_alg == IP_MP_ALG_NONE)
				invarg("\"mpath\" value is invalid\n", argv);
			addattr_l(&req.n, sizeof(req), RTA_MP_ALGO, &mp_alg, sizeof(mp_alg));
		} else {
//			int type;
			inet_prefix dst;

			if (strcmp(argv, "to") == 0) {
				NEXT_ARG();
			}
//			if ((*argv < '0' || *argv > '9') &&
//			    rtnl_rtntype_a2n(&type, argv) == 0) {
//				NEXT_ARG();
//				req.r.rtm_type = type;
//				type_ok = 1;
//			}

			if (dst_ok)
				duparg2("to", argv);
			get_prefix(&dst, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = dst.family;
			req.r.rtm_dst_len = dst.bitlen;
			dst_ok = 1;
			if (dst.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_DST, &dst.data, dst.bytelen);
		}
		argc--; argv++;
	}

	if (d || nhs_ok)  {
		int idx;

		ll_init_map(&rth);

		if (d) {
			if ((idx = ll_name_to_index(d)) == 0) {
				fprintf(stderr, "Cannot find device \"%s\"\n", d);
				return -1;
			}
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (mxrta->rta_len > RTA_LENGTH(0)) {
		if (mxlock)
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_LOCK, mxlock);
		addattr_l(&req.n, sizeof(req), RTA_METRICS, RTA_DATA(mxrta), RTA_PAYLOAD(mxrta));
	}

	if (nhs_ok)
		parse_nexthops(&req.n, &req.r, argc, argv);

	if (!table_ok) {
		if (req.r.rtm_type == RTN_LOCAL ||
		    req.r.rtm_type == RTN_BROADCAST ||
		    req.r.rtm_type == RTN_NAT ||
		    req.r.rtm_type == RTN_ANYCAST)
			req.r.rtm_table = RT_TABLE_LOCAL;
	}
	if (!scope_ok) {
		if (req.r.rtm_type == RTN_LOCAL ||
		    req.r.rtm_type == RTN_NAT)
			req.r.rtm_scope = RT_SCOPE_HOST;
		else if (req.r.rtm_type == RTN_BROADCAST ||
			 req.r.rtm_type == RTN_MULTICAST ||
			 req.r.rtm_type == RTN_ANYCAST)
			req.r.rtm_scope = RT_SCOPE_LINK;
		else if (req.r.rtm_type == RTN_UNICAST ||
			 req.r.rtm_type == RTN_UNSPEC) {
			if (cmd == RTM_DELROUTE)
				req.r.rtm_scope = RT_SCOPE_NOWHERE;
			else if (!gw_ok && !nhs_ok)
				req.r.rtm_scope = RT_SCOPE_LINK;
		}
	}

	if (req.r.rtm_family == AF_UNSPEC)
		req.r.rtm_family = AF_INET;

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0)
		exit(2);

	return 0;
}

REQ iproute_get(int argc, char *argv)
{
	REQ req;
	char  *idev = NULL;
	char  *odev = NULL;
	int connected = 0;
	int from_ok = 0;

	memset(&req, 0, sizeof(req));

	iproute_reset_filter();

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_GETROUTE;
	req.r.rtm_family = preferred_family;
	req.r.rtm_table = 0;
	req.r.rtm_protocol = 0;
	req.r.rtm_scope = 0;
	req.r.rtm_type = 0;
	req.r.rtm_src_len = 0;
	req.r.rtm_dst_len = 0;
	req.r.rtm_tos = 0;
	
	while (argc > 0) {
		if (strcmp(argv, "tos") == 0 ||
		    matches(argv, "dsfield") == 0) {
			__u32 tos;
			NEXT_ARG();
			if (rtnl_dsfield_a2n(&tos, argv))
				invarg("TOS value is invalid\n", argv);
			req.r.rtm_tos = tos;
		} else if (matches(argv, "from") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			from_ok = 1;
			get_prefix(&addr, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			if (addr.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_SRC, &addr.data, addr.bytelen);
			req.r.rtm_src_len = addr.bitlen;
		} else if (matches(argv, "iif") == 0) {
			NEXT_ARG();
			idev = argv;
		} else if (matches(argv, "oif") == 0 ||
			   strcmp(argv, "dev") == 0) {
			NEXT_ARG();
			odev = argv;
		} else if (matches(argv, "notify") == 0) {
			req.r.rtm_flags |= RTM_F_NOTIFY;
		} else if (matches(argv, "connected") == 0) {
			connected = 1;
		} else {
			inet_prefix addr;
			if (strcmp(argv, "to") == 0) {
				NEXT_ARG();
			}
			get_prefix(&addr, argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			if (addr.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_DST, &addr.data, addr.bytelen);
			req.r.rtm_dst_len = addr.bitlen;
		}
		argc--; argv++;
	}

	if (req.r.rtm_dst_len == 0) {
		fprintf(stderr, "need at least destination address\n");
		exit(1);
	}

	ll_init_map(&rth);

	if (idev || odev)  {
		int idx;

		if (idev) {
			if ((idx = ll_name_to_index(idev)) == 0) {
				fprintf(stderr, "Cannot find device \"%s\"\n", idev);
				//return -1;
				exit(1);
			}
			addattr32(&req.n, sizeof(req), RTA_IIF, idx);
		}
		if (odev) {
			if ((idx = ll_name_to_index(odev)) == 0) {
				fprintf(stderr, "Cannot find device \"%s\"\n", odev);
				//return -1;
				exit(1);
			}
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (req.r.rtm_family == AF_UNSPEC)
		req.r.rtm_family = AF_INET;

	if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0)
		exit(2);

	if (connected && !from_ok) {
		struct rtmsg *r = NLMSG_DATA(&req.n);
		int len = req.n.nlmsg_len;
		struct rtattr * tb[RTA_MAX+1];

//		if (print_route(NULL, &req.n, (void*)stdout) < 0) {
//			fprintf(stderr, "An error :-)\n");
//			exit(1);
//		}

		if (req.n.nlmsg_type != RTM_NEWROUTE) {
			fprintf(stderr, "Not a route?\n");
			//return -1;
			exit(1);
		}
		len -= NLMSG_LENGTH(sizeof(*r));
		if (len < 0) {
			fprintf(stderr, "Wrong len %d\n", len);
			//return -1;
			exit(1);
		}

		parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

		if (tb[RTA_PREFSRC]) {
			tb[RTA_PREFSRC]->rta_type = RTA_SRC;
			r->rtm_src_len = 8*RTA_PAYLOAD(tb[RTA_PREFSRC]);
		} else if (!tb[RTA_SRC]) {
			fprintf(stderr, "Failed to connect the route\n");
			//return -1;
			exit(1);
		}
		if (!odev && tb[RTA_OIF])
			tb[RTA_OIF]->rta_type = 0;
		if (tb[RTA_GATEWAY])
			tb[RTA_GATEWAY]->rta_type = 0;
		if (!idev && tb[RTA_IIF])
			tb[RTA_IIF]->rta_type = 0;
		req.n.nlmsg_flags = NLM_F_REQUEST;
		req.n.nlmsg_type = RTM_GETROUTE;

		if (rtnl_talk(&rth, &req.n, 0, 0, &req.n, NULL, NULL) < 0)
			exit(2);
	}

//	if (print_route(NULL, &req.n, (void*)stdout) < 0) {
//		fprintf(stderr, "An error :-)\n");
//		exit(1);
//	}
//	exit(0);
	return req;
}

void iproute_reset_filter()
{
	memset(&filter, 0, sizeof(filter));
	filter.mdst.bitlen = -1;
	filter.msrc.bitlen = -1;
}
