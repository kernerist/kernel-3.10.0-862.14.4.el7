/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Ethernet-type device handling.
 *
 * Version:	@(#)eth.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Florian  La Roche, <rzsfl@rz.uni-sb.de>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:
 *		Mr Linux	: Arp problems
 *		Alan Cox	: Generic queue tidyup (very tiny here)
 *		Alan Cox	: eth_header ntohs should be htons
 *		Alan Cox	: eth_rebuild_header missing an htons and
 *				  minor other things.
 *		Tegge		: Arp bug fixes.
 *		Florian		: Removed many unnecessary functions, code cleanup
 *				  and changes for new arp and skbuff.
 *		Alan Cox	: Redid header building to reflect new format.
 *		Alan Cox	: ARP only when compiled with CONFIG_INET
 *		Greg Page	: 802.2 and SNAP stuff.
 *		Alan Cox	: MAC layer pointers/new format.
 *		Paul Gortmaker	: eth_copy_and_sum shouldn't csum padding.
 *		Alan Cox	: Protect against forwarding explosions with
 *				  older network drivers and IFF_ALLMULTI.
 *	Christer Weinigel	: Better rebuild header message.
 *             Andrew Morton    : 26Feb01: kill ether_setup() - use netdev_boot_setup().
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#ifndef __GENKSYMS__
#include <linux/of_net.h>
#include <linux/pci.h>
#endif
#include <net/dst.h>
#include <net/arp.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <net/dsa.h>
#include <net/flow_dissector.h>
#include <asm/uaccess.h>
#ifndef __GENKSYMS__
#include <net/pkt_sched.h>
#endif

__setup("ether=", netdev_boot_setup);

/**
 * eth_header - create the Ethernet header
 * @skb:	buffer to alter
 * @dev:	source device
 * @type:	Ethernet type field
 * @daddr: destination address (NULL leave destination address)
 * @saddr: source address (NULL use device source address)
 * @len:   packet length (<= skb->len)
 *
 *
 * Set the protocol type. For a packet of type ETH_P_802_3/2 we put the length
 * in here instead.
 */
int eth_header(struct sk_buff *skb, struct net_device *dev,
	       unsigned short type,
	       const void *daddr, const void *saddr, unsigned int len)
{
	struct ethhdr *eth = skb_push(skb, ETH_HLEN);

	if (type != ETH_P_802_3 && type != ETH_P_802_2)
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 *      Set the source hardware address.
	 */

	if (!saddr)
		saddr = dev->dev_addr;
	memcpy(eth->h_source, saddr, ETH_ALEN);

	if (daddr) {
		memcpy(eth->h_dest, daddr, ETH_ALEN);
		return ETH_HLEN;
	}

	/*
	 *      Anyway, the loopback-device should never use this function...
	 */

	if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
		memset(eth->h_dest, 0, ETH_ALEN);
		return ETH_HLEN;
	}

	return -ETH_HLEN;
}
EXPORT_SYMBOL(eth_header);

/**
 * eth_rebuild_header- rebuild the Ethernet MAC header.
 * @skb: socket buffer to update
 *
 * This is called after an ARP or IPV6 ndisc it's resolution on this
 * sk_buff. We now let protocol (ARP) fill in the other fields.
 *
 * This routine CANNOT use cached dst->neigh!
 * Really, it is used only when dst->neigh is wrong.
 */
int eth_rebuild_header(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	struct net_device *dev = skb->dev;

	switch (eth->h_proto) {
#ifdef CONFIG_INET
	case htons(ETH_P_IP):
		return arp_find(eth->h_dest, skb);
#endif
	default:
		printk(KERN_DEBUG
		       "%s: unable to resolve type %X addresses.\n",
		       dev->name, ntohs(eth->h_proto));

		memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
		break;
	}

	return 0;
}
EXPORT_SYMBOL(eth_rebuild_header);

/**
 * eth_get_headlen - determine the the length of header for an ethernet frame
 * @data: pointer to start of frame
 * @len: total length of frame
 *
 * Make a best effort attempt to pull the length for all of the headers for
 * a given frame in a linear buffer.
 */
u32 eth_get_headlen(void *data, unsigned int len)
{
	const unsigned int flags = FLOW_DISSECTOR_F_PARSE_1ST_FRAG;
	const struct ethhdr *eth = (const struct ethhdr *)data;
	struct flow_keys keys;

	/* this should never happen, but better safe than sorry */
	if (len < sizeof(*eth))
		return len;

	/* parse any remaining L2/L3 headers, check for L4 */
	if (!skb_flow_dissect_flow_keys_buf(&keys, data, eth->h_proto,
					    sizeof(*eth), len, flags))
		return max_t(u32, keys.control.thoff, sizeof(*eth));

	/* parse for any L4 headers */
	return min_t(u32, __skb_get_poff(NULL, data, &keys, len), len);
}
EXPORT_SYMBOL(eth_get_headlen);

/**
 * eth_type_trans - determine the packet's protocol ID.
 * @skb: received socket data
 * @dev: receiving network device
 *
 * The rule here is that we
 * assume 802.3 if the type field is short enough to be a length.
 * This is normal practice and works for any 'now in use' protocol.
 */
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct ethhdr *eth;

	skb->dev = dev;
	skb_reset_mac_header(skb);
	skb_pull_inline(skb, ETH_HLEN);
	eth = eth_hdr(skb);

	if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
		if (ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}

	/*
	 *      This ALLMULTI check should be redundant by 1.4
	 *      so don't forget to remove it.
	 *
	 *      Seems, you forgot to remove it. All silly devices
	 *      seems to set IFF_PROMISC.
	 */

	else if (1 /*dev->flags&IFF_PROMISC */ ) {
		if (unlikely(!ether_addr_equal_64bits(eth->h_dest,
						      dev->dev_addr)))
			skb->pkt_type = PACKET_OTHERHOST;
	}

	/*
	 * Some variants of DSA tagging don't have an ethertype field
	 * at all, so we check here whether one of those tagging
	 * variants has been configured on the receiving interface,
	 * and if so, set skb->protocol without looking at the packet.
	 */
	if (unlikely(netdev_uses_dsa(dev)))
		return htons(ETH_P_XDSA);

	if (likely(eth_proto_is_802_3(eth->h_proto)))
		return eth->h_proto;

	/*
	 *      This is a magic hack to spot IPX packets. Older Novell breaks
	 *      the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *      layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *      won't work for fault tolerant netware but does for the rest.
	 */
	if (unlikely(skb->len >= 2 && *(unsigned short *)(skb->data) == 0xFFFF))
		return htons(ETH_P_802_3);

	/*
	 *      Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}
EXPORT_SYMBOL(eth_type_trans);

/**
 * eth_header_parse - extract hardware address from packet
 * @skb: packet to extract header from
 * @haddr: destination buffer
 */
int eth_header_parse(const struct sk_buff *skb, unsigned char *haddr)
{
	const struct ethhdr *eth = eth_hdr(skb);
	memcpy(haddr, eth->h_source, ETH_ALEN);
	return ETH_ALEN;
}
EXPORT_SYMBOL(eth_header_parse);

/**
 * eth_header_cache - fill cache entry from neighbour
 * @neigh: source neighbour
 * @hh: destination cache entry
 * @type: Ethernet type field
 *
 * Create an Ethernet header template from the neighbour.
 */
int eth_header_cache(const struct neighbour *neigh, struct hh_cache *hh, __be16 type)
{
	struct ethhdr *eth;
	const struct net_device *dev = neigh->dev;

	eth = (struct ethhdr *)
	    (((u8 *) hh->hh_data) + (HH_DATA_OFF(sizeof(*eth))));

	if (type == htons(ETH_P_802_3))
		return -1;

	eth->h_proto = type;
	memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
	memcpy(eth->h_dest, neigh->ha, ETH_ALEN);
	hh->hh_len = ETH_HLEN;
	return 0;
}
EXPORT_SYMBOL(eth_header_cache);

/**
 * eth_header_cache_update - update cache entry
 * @hh: destination cache entry
 * @dev: network device
 * @haddr: new hardware address
 *
 * Called by Address Resolution module to notify changes in address.
 */
void eth_header_cache_update(struct hh_cache *hh,
			     const struct net_device *dev,
			     const unsigned char *haddr)
{
	memcpy(((u8 *) hh->hh_data) + HH_DATA_OFF(sizeof(struct ethhdr)),
	       haddr, ETH_ALEN);
}
EXPORT_SYMBOL(eth_header_cache_update);

/**
 * eth_prepare_mac_addr_change - prepare for mac change
 * @dev: network device
 * @p: socket address
 */
int eth_prepare_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!(dev->priv_flags & IFF_LIVE_ADDR_CHANGE) && netif_running(dev))
		return -EBUSY;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	return 0;
}
EXPORT_SYMBOL(eth_prepare_mac_addr_change);

/**
 * eth_commit_mac_addr_change - commit mac change
 * @dev: network device
 * @p: socket address
 */
void eth_commit_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
}
EXPORT_SYMBOL(eth_commit_mac_addr_change);

/**
 * eth_mac_addr - set new Ethernet hardware address
 * @dev: network device
 * @p: socket address
 *
 * Change hardware address of device.
 *
 * This doesn't change hardware matching, so needs to be overridden
 * for most real devices.
 */
int eth_mac_addr(struct net_device *dev, void *p)
{
	int ret;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret < 0)
		return ret;
	eth_commit_mac_addr_change(dev, p);
	return 0;
}
EXPORT_SYMBOL(eth_mac_addr);

/**
 * eth_change_mtu - set new MTU size
 * @dev: network device
 * @new_mtu: new Maximum Transfer Unit
 *
 * Allow changing MTU size. Needs to be overridden for devices
 * supporting jumbo frames.
 */
int eth_change_mtu(struct net_device *dev, int new_mtu)
{
	/* RHEL - For newer drivers show deprecation warning and for older
	 * ones preserve old behavior.
	 */
	if (get_ndo_ext(dev->netdev_ops, ndo_change_mtu))
		netdev_warn(dev, "%s is deprecated\n", __func__);
	else if (new_mtu < 68 || new_mtu > ETH_DATA_LEN)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}
EXPORT_SYMBOL(eth_change_mtu);

int eth_validate_addr(struct net_device *dev)
{
	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	return 0;
}
EXPORT_SYMBOL(eth_validate_addr);

const struct header_ops eth_header_ops ____cacheline_aligned = {
	.create		= eth_header,
	.parse		= eth_header_parse,
	.rebuild	= eth_rebuild_header,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

/*
 * RHEL: The macros ether_setup and alloc_etherdev_mqs need to be undefined
 * here. For details, please see their definition in include/linux/netdevice.h
 * and include/linux/etherdevice.h
 */
#undef ether_setup
#undef alloc_etherdev_mqs

/**
 * ether_setup - setup Ethernet network device
 * @dev: network device
 *
 * Fill in the fields of the device structure with Ethernet-generic values.
 *
 * RHEL: This function is preserved for existing binary modules compiled
 * against RHEL-7.4 and older.
 */
void ether_setup(struct net_device *dev)
{
	dev->header_ops		= &eth_header_ops;
	dev->type		= ARPHRD_ETHER;
	dev->hard_header_len 	= ETH_HLEN;
	dev->mtu		= ETH_DATA_LEN;
	dev->addr_len		= ETH_ALEN;
	dev->tx_queue_len	= DEFAULT_TX_QUEUE_LEN;
	dev->flags		= IFF_BROADCAST|IFF_MULTICAST;
	dev->priv_flags		|= IFF_TX_SKB_SHARING;

	memset(dev->broadcast, 0xFF, ETH_ALEN);

}
EXPORT_SYMBOL(ether_setup);

/**
 * ether_setup_rh - setup Ethernet network device
 * @dev: network device
 *
 * Fill in the fields of the device structure with Ethernet-generic values.
 *
 * RHEL: This variant also initializes .min_mtu & .max_mtu
 */
void ether_setup_rh(struct net_device *dev)
{
	ether_setup(dev);
	dev->extended->min_mtu	= ETH_MIN_MTU;
	dev->extended->max_mtu	= ETH_DATA_LEN;
}
EXPORT_SYMBOL(ether_setup_rh);

/**
 * alloc_etherdev_mqs_rh - Allocates and sets up an Ethernet device
 * @sizeof_priv: Size of additional driver-private structure to be allocated
 *	for this Ethernet device
 * @txqs: The number of TX queues this device has.
 * @rxqs: The number of RX queues this device has.
 *
 * Fill in the fields of the device structure with Ethernet-generic
 * values. Basically does everything except registering the device.
 *
 * Constructs a new net device, complete with a private data area of
 * size (sizeof_priv).  A 32-byte (not bit) alignment is enforced for
 * this private data area.
 *
 * RHEL: This function uses ether_setup_rh() that also initializes
 * .{min,max}_mtu members to their default values.
 */

struct net_device *alloc_etherdev_mqs_rh(int sizeof_priv, unsigned int txqs,
					 unsigned int rxqs)
{
	return alloc_netdev_mqs(sizeof_priv, "eth%d", ether_setup_rh, txqs,
				rxqs);
}
EXPORT_SYMBOL(alloc_etherdev_mqs_rh);

/*
 * RHEL: This function is preserved for existing binary modules compiled
 * against RHEL-7.4 and older.
 */
struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs,
				      unsigned int rxqs)
{
	return alloc_netdev_mqs(sizeof_priv, "eth%d", ether_setup, txqs, rxqs);
}
EXPORT_SYMBOL(alloc_etherdev_mqs);

static size_t _format_mac_addr(char *buf, int buflen,
			       const unsigned char *addr, int len)
{
	int i;
	char *cp = buf;

	for (i = 0; i < len; i++) {
		cp += scnprintf(cp, buflen - (cp - buf), "%02x", addr[i]);
		if (i == len - 1)
			break;
		cp += scnprintf(cp, buflen - (cp - buf), ":");
	}
	return cp - buf;
}

ssize_t sysfs_format_mac(char *buf, const unsigned char *addr, int len)
{
	size_t l;

	l = _format_mac_addr(buf, PAGE_SIZE, addr, len);
	l += scnprintf(buf + l, PAGE_SIZE - l, "\n");
	return (ssize_t)l;
}
EXPORT_SYMBOL(sysfs_format_mac);

struct sk_buff **eth_gro_receive(struct sk_buff **head,
				 struct sk_buff *skb)
{
	struct sk_buff *p, **pp = NULL;
	struct ethhdr *eh, *eh2;
	unsigned int hlen, off_eth;
	const struct packet_offload *ptype;
	__be16 type;
	int flush = 1;

	off_eth = skb_gro_offset(skb);
	hlen = off_eth + sizeof(*eh);
	eh = skb_gro_header_fast(skb, off_eth);
	if (skb_gro_header_hard(skb, hlen)) {
		eh = skb_gro_header_slow(skb, hlen, off_eth);
		if (unlikely(!eh))
			goto out;
	}

	flush = 0;

	for (p = *head; p; p = p->next) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		eh2 = (struct ethhdr *)(p->data + off_eth);
		if (compare_ether_header(eh, eh2)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	type = eh->h_proto;

	rcu_read_lock();
	ptype = gro_find_receive_by_type(type);
	if (ptype == NULL) {
		flush = 1;
		goto out_unlock;
	}

	skb_gro_pull(skb, sizeof(*eh));
	skb_gro_postpull_rcsum(skb, eh, sizeof(*eh));
	pp = call_gro_receive(ptype->callbacks.gro_receive, head, skb);

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}
EXPORT_SYMBOL(eth_gro_receive);

int eth_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct ethhdr *eh = (struct ethhdr *)(skb->data + nhoff);
	__be16 type = eh->h_proto;
	struct packet_offload *ptype;
	int err = -ENOSYS;

	if (skb->encapsulation)
		skb_set_inner_mac_header(skb, nhoff);

	rcu_read_lock();
	ptype = gro_find_complete_by_type(type);
	if (ptype != NULL)
		err = ptype->callbacks.gro_complete(skb, nhoff +
						    sizeof(struct ethhdr));

	rcu_read_unlock();
	return err;
}
EXPORT_SYMBOL(eth_gro_complete);

static struct packet_offload eth_packet_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_TEB),
	.priority = 10,
	.callbacks = {
		.gro_receive = eth_gro_receive,
		.gro_complete = eth_gro_complete,
	},
};

static int __init eth_offload_init(void)
{
	dev_add_offload(&eth_packet_offload);

	return 0;
}

fs_initcall(eth_offload_init);

unsigned char * __weak arch_get_platform_mac_address(void)
{
	return NULL;
}

int eth_platform_get_mac_address(struct device *dev, u8 *mac_addr)
{
	const unsigned char *addr;
	struct device_node *dp;

	if (dev_is_pci(dev))
		dp = pci_device_to_OF_node(to_pci_dev(dev));
	else
		dp = dev->of_node;

	addr = NULL;
	if (dp)
		addr = of_get_mac_address(dp);
	if (!addr)
		addr = arch_get_platform_mac_address();

	if (!addr)
		return -ENODEV;

	ether_addr_copy(mac_addr, addr);
	return 0;
}
EXPORT_SYMBOL(eth_platform_get_mac_address);
