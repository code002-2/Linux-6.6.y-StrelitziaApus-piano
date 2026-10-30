// SPDX-License-Identifier: GPL-2.0-only
/*
 *  iptables module to match inet_addr_type() of an ip.
 *
 *  Copyright (c) 2004 Patrick McHardy <kaber@trash.net>
 *  (C) 2007 Laszlo Attila Toth <panther@balabit.hu>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <net/route.h>

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_fib.h>
#endif

#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/xt_addrtype.h>
#include <linux/netfilter/x_tables.h>

#ifdef CONFIG_RUST
/* The decision logic lives in xt_addrtype_rust.rs; the address classification
 * and the IPv6 route lookups stay here, where the networking stack is. */
extern bool rust_addrtype_type_hit(u32 addr_type, u16 mask);
extern int rust_addrtype_v6_bits(u32 addr_type, u16 mask);
extern bool rust_addrtype_v6_lookup_hit(u32 lookup, u16 mask);
extern bool rust_addrtype_combine(u16 source, u16 dest, bool invert_source,
				  bool invert_dest, bool src_hit, bool dst_hit);
extern int rust_addrtype_checkentry(u32 flags, u32 hook_mask, u32 family,
				    u16 source, u16 dest, bool ipv6_enabled);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Xtables: address type match");
MODULE_ALIAS("ipt_addrtype");
MODULE_ALIAS("ip6t_addrtype");

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static u32 match_lookup_rt6(struct net *net, const struct net_device *dev,
			    const struct in6_addr *addr, u16 mask)
{
	struct flowi6 flow;
	struct rt6_info *rt;
	u32 ret = 0;
	int route_err;

	memset(&flow, 0, sizeof(flow));
	flow.daddr = *addr;
	if (dev)
		flow.flowi6_oif = dev->ifindex;

	if (dev && (mask & XT_ADDRTYPE_LOCAL)) {
		if (nf_ipv6_chk_addr(net, addr, dev, true))
			ret = XT_ADDRTYPE_LOCAL;
	}

	route_err = nf_ip6_route(net, (struct dst_entry **)&rt,
				 flowi6_to_flowi(&flow), false);
	if (route_err)
		return XT_ADDRTYPE_UNREACHABLE;

	if (rt->rt6i_flags & RTF_REJECT)
		ret = XT_ADDRTYPE_UNREACHABLE;

	if (dev == NULL && rt->rt6i_flags & RTF_LOCAL)
		ret |= XT_ADDRTYPE_LOCAL;
	if (ipv6_anycast_destination((struct dst_entry *)rt, addr))
		ret |= XT_ADDRTYPE_ANYCAST;

	dst_release(&rt->dst);
	return ret;
}

static bool match_type6(struct net *net, const struct net_device *dev,
				const struct in6_addr *addr, u16 mask)
{
#ifdef CONFIG_RUST
	int addr_type = ipv6_addr_type(addr);
	int bits = rust_addrtype_v6_bits(addr_type, mask);

	if (bits >= 0)
		return bits != 0;

	return rust_addrtype_v6_lookup_hit(match_lookup_rt6(net, dev, addr, mask),
					   mask);
#else
	int addr_type = ipv6_addr_type(addr);

	if ((mask & XT_ADDRTYPE_MULTICAST) &&
	    !(addr_type & IPV6_ADDR_MULTICAST))
		return false;
	if ((mask & XT_ADDRTYPE_UNICAST) && !(addr_type & IPV6_ADDR_UNICAST))
		return false;
	if ((mask & XT_ADDRTYPE_UNSPEC) && addr_type != IPV6_ADDR_ANY)
		return false;

	if ((XT_ADDRTYPE_LOCAL | XT_ADDRTYPE_ANYCAST |
	     XT_ADDRTYPE_UNREACHABLE) & mask)
		return !!(mask & match_lookup_rt6(net, dev, addr, mask));
	return true;
#endif
}

static bool
addrtype_mt6(struct net *net, const struct net_device *dev,
	const struct sk_buff *skb, const struct xt_addrtype_info_v1 *info)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	bool ret __maybe_unused = true;

#ifdef CONFIG_RUST
	{
		bool src_hit = info->source &&
			match_type6(net, dev, &iph->saddr, info->source);
		bool dst_hit = info->dest &&
			match_type6(net, dev, &iph->daddr, info->dest);

		return rust_addrtype_combine(info->source, info->dest,
					     info->flags & XT_ADDRTYPE_INVERT_SOURCE,
					     !!(info->flags & XT_ADDRTYPE_INVERT_DEST),
					     src_hit, dst_hit);
	}
#else
	if (info->source)
		ret &= match_type6(net, dev, &iph->saddr, info->source) ^
		       (info->flags & XT_ADDRTYPE_INVERT_SOURCE);
	if (ret && info->dest)
		ret &= match_type6(net, dev, &iph->daddr, info->dest) ^
		       !!(info->flags & XT_ADDRTYPE_INVERT_DEST);
	return ret;
#endif
}
#endif

static inline bool match_type(struct net *net, const struct net_device *dev,
			      __be32 addr, u_int16_t mask)
{
#ifdef CONFIG_RUST
	return rust_addrtype_type_hit(inet_dev_addr_type(net, dev, addr), mask);
#else
	return !!(mask & (1 << inet_dev_addr_type(net, dev, addr)));
#endif
}

static bool
addrtype_mt_v0(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct net *net = xt_net(par);
	const struct xt_addrtype_info *info = par->matchinfo;
	const struct iphdr *iph = ip_hdr(skb);
	bool ret __maybe_unused = true;

#ifdef CONFIG_RUST
	{
		bool src_hit = info->source &&
			match_type(net, NULL, iph->saddr, info->source);
		bool dst_hit = info->dest &&
			match_type(net, NULL, iph->daddr, info->dest);

		return rust_addrtype_combine(info->source, info->dest,
					     info->invert_source, info->invert_dest,
					     src_hit, dst_hit);
	}
#else
	if (info->source)
		ret &= match_type(net, NULL, iph->saddr, info->source) ^
		       info->invert_source;
	if (info->dest)
		ret &= match_type(net, NULL, iph->daddr, info->dest) ^
		       info->invert_dest;

	return ret;
#endif
}

static bool
addrtype_mt_v1(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct net *net = xt_net(par);
	const struct xt_addrtype_info_v1 *info = par->matchinfo;
	const struct iphdr *iph;
	const struct net_device *dev = NULL;
	bool ret __maybe_unused = true;

	if (info->flags & XT_ADDRTYPE_LIMIT_IFACE_IN)
		dev = xt_in(par);
	else if (info->flags & XT_ADDRTYPE_LIMIT_IFACE_OUT)
		dev = xt_out(par);

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	if (xt_family(par) == NFPROTO_IPV6)
		return addrtype_mt6(net, dev, skb, info);
#endif
	iph = ip_hdr(skb);

#ifdef CONFIG_RUST
	{
		bool src_hit = info->source &&
			match_type(net, dev, iph->saddr, info->source);
		bool dst_hit = info->dest &&
			match_type(net, dev, iph->daddr, info->dest);

		return rust_addrtype_combine(info->source, info->dest,
					     info->flags & XT_ADDRTYPE_INVERT_SOURCE,
					     !!(info->flags & XT_ADDRTYPE_INVERT_DEST),
					     src_hit, dst_hit);
	}
#else
	if (info->source)
		ret &= match_type(net, dev, iph->saddr, info->source) ^
		       (info->flags & XT_ADDRTYPE_INVERT_SOURCE);
	if (ret && info->dest)
		ret &= match_type(net, dev, iph->daddr, info->dest) ^
		       !!(info->flags & XT_ADDRTYPE_INVERT_DEST);
	return ret;
#endif
}

static int addrtype_mt_checkentry_v1(const struct xt_mtchk_param *par)
{
	const char *errmsg __maybe_unused = "both incoming and outgoing interface limitation cannot be selected";
	struct xt_addrtype_info_v1 *info = par->matchinfo;

#ifdef CONFIG_RUST
	{
		static const char *const msgs[] = {
			NULL,
			"both incoming and outgoing interface limitation cannot be selected",
			"output interface limitation not valid in PREROUTING and INPUT",
			"input interface limitation not valid in POSTROUTING and OUTPUT",
			"ipv6 BLACKHOLE matching not supported",
			"ipv6 PROHIBIT (THROW, NAT ..) matching not supported",
			"ipv6 does not support BROADCAST matching",
		};
		int err = rust_addrtype_checkentry(info->flags, par->hook_mask,
						   par->family, info->source,
						   info->dest,
						   IS_ENABLED(CONFIG_IP6_NF_IPTABLES));

		if (err) {
			pr_info_ratelimited("%s\n", msgs[err]);
			return -EINVAL;
		}
		return 0;
	}
#else
	if (info->flags & XT_ADDRTYPE_LIMIT_IFACE_IN &&
	    info->flags & XT_ADDRTYPE_LIMIT_IFACE_OUT)
		goto err;

	if (par->hook_mask & ((1 << NF_INET_PRE_ROUTING) |
	    (1 << NF_INET_LOCAL_IN)) &&
	    info->flags & XT_ADDRTYPE_LIMIT_IFACE_OUT) {
		errmsg = "output interface limitation not valid in PREROUTING and INPUT";
		goto err;
	}

	if (par->hook_mask & ((1 << NF_INET_POST_ROUTING) |
	    (1 << NF_INET_LOCAL_OUT)) &&
	    info->flags & XT_ADDRTYPE_LIMIT_IFACE_IN) {
		errmsg = "input interface limitation not valid in POSTROUTING and OUTPUT";
		goto err;
	}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	if (par->family == NFPROTO_IPV6) {
		if ((info->source | info->dest) & XT_ADDRTYPE_BLACKHOLE) {
			errmsg = "ipv6 BLACKHOLE matching not supported";
			goto err;
		}
		if ((info->source | info->dest) >= XT_ADDRTYPE_PROHIBIT) {
			errmsg = "ipv6 PROHIBIT (THROW, NAT ..) matching not supported";
			goto err;
		}
		if ((info->source | info->dest) & XT_ADDRTYPE_BROADCAST) {
			errmsg = "ipv6 does not support BROADCAST matching";
			goto err;
		}
	}
#endif
	return 0;
err:
	pr_info_ratelimited("%s\n", errmsg);
	return -EINVAL;
#endif
}

static struct xt_match addrtype_mt_reg[] __read_mostly = {
	{
		.name		= "addrtype",
		.family		= NFPROTO_IPV4,
		.match		= addrtype_mt_v0,
		.matchsize	= sizeof(struct xt_addrtype_info),
		.me		= THIS_MODULE
	},
	{
		.name		= "addrtype",
		.family		= NFPROTO_IPV4,
		.revision	= 1,
		.match		= addrtype_mt_v1,
		.checkentry	= addrtype_mt_checkentry_v1,
		.matchsize	= sizeof(struct xt_addrtype_info_v1),
		.me		= THIS_MODULE
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name		= "addrtype",
		.family		= NFPROTO_IPV6,
		.revision	= 1,
		.match		= addrtype_mt_v1,
		.checkentry	= addrtype_mt_checkentry_v1,
		.matchsize	= sizeof(struct xt_addrtype_info_v1),
		.me		= THIS_MODULE
	},
#endif
};

static int __init addrtype_mt_init(void)
{
	return xt_register_matches(addrtype_mt_reg,
				   ARRAY_SIZE(addrtype_mt_reg));
}

static void __exit addrtype_mt_exit(void)
{
	xt_unregister_matches(addrtype_mt_reg, ARRAY_SIZE(addrtype_mt_reg));
}

module_init(addrtype_mt_init);
module_exit(addrtype_mt_exit);
