// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- linux-c -*-
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: netdev@vger.kernel.org
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 *
 * Fixes:       Mar 22 2001: Martin Bokaemper <mbokaemper@unispherenetworks.com>
 *                - reset skb->pkt_type on incoming packets when MAC was changed
 *                - see that changed MAC is saddr for outgoing packets
 *              Oct 20, 2001:  Ard van Breeman:
 *                - Fix MC-list, finally.
 *                - Flush MC-list on VLAN destroy.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/net_tstamp.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <net/arp.h>
#include <net/macsec.h>
#include <net/netdev_lock.h>

#include "vlan.h"
#include "vlanproc.h"
#include <linux/if_vlan.h>
#include <linux/netpoll.h>

/*
 *	Create the VLAN header for an arbitrary protocol layer
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 *
 *  This is called when the SKB is moving down the stack towards the
 *  physical devices.
 */
static int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
				unsigned short type,
				const void *daddr, const void *saddr,
				unsigned int len)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_hdr *vhdr;
	unsigned int vhdrlen = 0;
	u16 vlan_tci = 0;
	int rc;

	if (!(vlan->flags & VLAN_FLAG_REORDER_HDR)) {
		vhdr = skb_push(skb, VLAN_HLEN);

		vlan_tci = vlan->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb->priority);
		vhdr->h_vlan_TCI = htons(vlan_tci);

		/*
		 *  Set the protocol type. For a packet of type ETH_P_802_3/2 we
		 *  put the length in here instead.
		 */
		if (type != ETH_P_802_3 && type != ETH_P_802_2)
			vhdr->h_vlan_encapsulated_proto = htons(type);
		else
			vhdr->h_vlan_encapsulated_proto = htons(len);

		skb->protocol = vlan->vlan_proto;
		type = ntohs(vlan->vlan_proto);
		vhdrlen = VLAN_HLEN;
	}

	/* Before delegating work to the lower layer, enter our MAC-address */
	if (saddr == NULL)
		saddr = dev->dev_addr;

	/* Now make the underlying real hard header */
	dev = vlan->real_dev;
	rc = dev_hard_header(skb, dev, type, daddr, saddr, len + vhdrlen);
	if (rc > 0)
		rc += vhdrlen;
	return rc;
}

static inline netdev_tx_t vlan_netpoll_send_skb(struct vlan_dev_priv *vlan, struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	return netpoll_send_skb(vlan->netpoll, skb);
#else
	BUG();
	return NETDEV_TX_OK;
#endif
}

static netdev_tx_t vlan_dev_hard_start_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);
	struct sk_buff *oskb = skb;
	unsigned int len;
	int ret;

	ret = -ENOMEM;
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto err;

	/* Handle non-VLAN frames if they are sent to us, for example by DHCP.
	 *
	 * NOTE: THIS ASSUMES DIX ETHERNET, SPECIFICALLY NOT SUPPORTING
	 * OTHER THINGS LIKE FDDI/TokenRing/802.3 SNAPs...
	 */
	if (vlan->flags & VLAN_FLAG_REORDER_HDR ||
	    veth->h_vlan_proto != vlan->vlan_proto) {
		u16 vlan_tci;
		vlan_tci = vlan->vlan_id;
		vlan_tci |= vlan_dev_get_egress_qos_mask(dev, skb->priority);
		__vlan_hwaccel_put_tag(skb, vlan->vlan_proto, vlan_tci);
	}

	skb->dev = vlan->real_dev;
	len = skb->len;
	if (unlikely(netpoll_tx_running(dev)))
		return vlan_netpoll_send_skb(vlan, skb);

	ret = try_dev_queue_xmit(skb, oskb == skb);

	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct vlan_pcpu_stats *stats;

		stats = this_cpu_ptr(vlan->vlan_pcpu_stats);
		u64_stats_update_begin(&stats->syncp);
		u64_stats_inc(&stats->tx_packets);
		u64_stats_add(&stats->tx_bytes, len);
		u64_stats_update_end(&stats->syncp);
	} else if (ret == NET_XMIT_BUSY) {
		/* Calling code should retry, skb was NOT freed. */
		/* sort of a collision, at least */
		this_cpu_inc(vlan_dev_priv(dev)->vlan_pcpu_stats->collisions);
		return NETDEV_TX_BUSY;
	} else {
err:
		this_cpu_inc(vlan->vlan_pcpu_stats->tx_dropped);
	}

	return ret;
}

static int vlan_dev_change_mtu(struct net_device *dev, int new_mtu)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	unsigned int max_mtu = real_dev->mtu;

	if (netif_reduces_vlan_mtu(real_dev))
		max_mtu -= VLAN_HLEN;
	if (max_mtu < new_mtu)
		return -ERANGE;

	WRITE_ONCE(dev->mtu, new_mtu);

	return 0;
}

void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, u16 vlan_prio)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	if (vlan->ingress_priority_map[vlan_prio & 0x7] && !skb_prio)
		vlan->nr_ingress_mappings--;
	else if (!vlan->ingress_priority_map[vlan_prio & 0x7] && skb_prio)
		vlan->nr_ingress_mappings++;

	vlan->ingress_priority_map[vlan_prio & 0x7] = skb_prio;
}

int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, u16 vlan_prio)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct vlan_priority_tci_mapping *mp = NULL;
	struct vlan_priority_tci_mapping *np;
	u32 vlan_qos = (vlan_prio << VLAN_PRIO_SHIFT) & VLAN_PRIO_MASK;

	/* See if a priority mapping exists.. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	while (mp) {
		if (mp->priority == skb_prio) {
			if (mp->vlan_qos && !vlan_qos)
				vlan->nr_egress_mappings--;
			else if (!mp->vlan_qos && vlan_qos)
				vlan->nr_egress_mappings++;
			mp->vlan_qos = vlan_qos;
			return 0;
		}
		mp = mp->next;
	}

	/* Create a new mapping then. */
	mp = vlan->egress_priority_map[skb_prio & 0xF];
	np = kmalloc(sizeof(struct vlan_priority_tci_mapping), GFP_KERNEL);
	if (!np)
		return -ENOBUFS;

	np->next = mp;
	np->priority = skb_prio;
	np->vlan_qos = vlan_qos;
	/* Before inserting this element in hash table, make sure all its fields
	 * are committed to memory.
	 * coupled with smp_rmb() in vlan_dev_get_egress_qos_mask()
	 */
	smp_wmb();
	vlan->egress_priority_map[skb_prio & 0xF] = np;
	if (vlan_qos)
		vlan->nr_egress_mappings++;
	return 0;
}

/* Flags are defined in the vlan_flags enum in
 * include/uapi/linux/if_vlan.h file.
 */
int vlan_dev_change_flags(const struct net_device *dev, u32 flags, u32 mask)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	u32 old_flags = vlan->flags;

	if (mask & ~(VLAN_FLAG_REORDER_HDR | VLAN_FLAG_GVRP |
		     VLAN_FLAG_LOOSE_BINDING | VLAN_FLAG_MVRP |
		     VLAN_FLAG_BRIDGE_BINDING))
		return -EINVAL;

	vlan->flags = (old_flags & ~mask) | (flags & mask);

	if (netif_running(dev) && (vlan->flags ^ old_flags) & VLAN_FLAG_GVRP) {
		if (vlan->flags & VLAN_FLAG_GVRP)
			vlan_gvrp_request_join(dev);
		else
			vlan_gvrp_request_leave(dev);
	}

	if (netif_running(dev) && (vlan->flags ^ old_flags) & VLAN_FLAG_MVRP) {
		if (vlan->flags & VLAN_FLAG_MVRP)
			vlan_mvrp_request_join(dev);
		else
			vlan_mvrp_request_leave(dev);
	}
	return 0;
}

void vlan_dev_get_realdev_name(const struct net_device *dev, char *result, size_t size)
{
	strscpy_pad(result, vlan_dev_priv(dev)->real_dev->name, size);
}

bool vlan_dev_inherit_address(struct net_device *dev,
			      struct net_device *real_dev)
{
	if (dev->addr_assign_type != NET_ADDR_STOLEN)
		return false;

	eth_hw_addr_set(dev, real_dev->dev_addr);
	call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return true;
}

static int vlan_dev_open(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	int err;

	if (!(real_dev->flags & IFF_UP) &&
	    !(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
		return -ENETDOWN;

	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr) &&
	    !vlan_dev_inherit_address(dev, real_dev)) {
		err = dev_uc_add(real_dev, dev->dev_addr);
		if (err < 0)
			goto out;
	}

	ether_addr_copy(vlan->real_dev_addr, real_dev->dev_addr);

	if (vlan->flags & VLAN_FLAG_GVRP)
		vlan_gvrp_request_join(dev);

	if (vlan->flags & VLAN_FLAG_MVRP)
		vlan_mvrp_request_join(dev);

	if (netif_carrier_ok(real_dev) &&
	    !(vlan->flags & VLAN_FLAG_BRIDGE_BINDING))
		netif_carrier_on(dev);
	return 0;

out:
	netif_carrier_off(dev);
	return err;
}

static int vlan_dev_stop(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;

	dev_mc_unsync(real_dev, dev);
	dev_uc_unsync(real_dev, dev);

	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr))
		dev_uc_del(real_dev, dev->dev_addr);

	if (!(vlan->flags & VLAN_FLAG_BRIDGE_BINDING))
		netif_carrier_off(dev);
	return 0;
}

static int vlan_dev_set_mac_address(struct net_device *dev, void *p)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP))
		goto out;

	if (!ether_addr_equal(addr->sa_data, real_dev->dev_addr)) {
		err = dev_uc_add(real_dev, addr->sa_data);
		if (err < 0)
			return err;
	}

	if (!ether_addr_equal(dev->dev_addr, real_dev->dev_addr))
		dev_uc_del(real_dev, dev->dev_addr);

out:
	eth_hw_addr_set(dev, addr->sa_data);
	return 0;
}

static int vlan_hwtstamp_get(struct net_device *dev,
			     struct kernel_hwtstamp_config *cfg)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	return generic_hwtstamp_get_lower(real_dev, cfg);
}

static int vlan_hwtstamp_set(struct net_device *dev,
			     struct kernel_hwtstamp_config *cfg,
			     struct netlink_ext_ack *extack)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	if (!net_eq(dev_net(dev), dev_net(real_dev)))
		return -EOPNOTSUPP;

	return generic_hwtstamp_set_lower(real_dev, cfg, extack);
}

static int vlan_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	struct ifreq ifrr;
	int err = -EOPNOTSUPP;

	strscpy_pad(ifrr.ifr_name, real_dev->name, IFNAMSIZ);
	ifrr.ifr_ifru = ifr->ifr_ifru;

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		err = dev_eth_ioctl(real_dev, &ifrr, cmd);
		break;
	}

	if (!err)
		ifr->ifr_ifru = ifrr.ifr_ifru;

	return err;
}

static int vlan_dev_neigh_setup(struct net_device *dev, struct neigh_parms *pa)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int err = 0;

	if (netif_device_present(real_dev) && ops->ndo_neigh_setup)
		err = ops->ndo_neigh_setup(real_dev, pa);

	return err;
}

#if IS_ENABLED(CONFIG_FCOE)
static int vlan_dev_fcoe_ddp_setup(struct net_device *dev, u16 xid,
				   struct scatterlist *sgl, unsigned int sgc)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = 0;

	if (ops->ndo_fcoe_ddp_setup)
		rc = ops->ndo_fcoe_ddp_setup(real_dev, xid, sgl, sgc);

	return rc;
}

static int vlan_dev_fcoe_ddp_done(struct net_device *dev, u16 xid)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int len = 0;

	if (ops->ndo_fcoe_ddp_done)
		len = ops->ndo_fcoe_ddp_done(real_dev, xid);

	return len;
}

static int vlan_dev_fcoe_enable(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_enable)
		rc = ops->ndo_fcoe_enable(real_dev);
	return rc;
}

static int vlan_dev_fcoe_disable(struct net_device *dev)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_disable)
		rc = ops->ndo_fcoe_disable(real_dev);
	return rc;
}

static int vlan_dev_fcoe_ddp_target(struct net_device *dev, u16 xid,
				    struct scatterlist *sgl, unsigned int sgc)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = 0;

	if (ops->ndo_fcoe_ddp_target)
		rc = ops->ndo_fcoe_ddp_target(real_dev, xid, sgl, sgc);

	return rc;
}
#endif

#ifdef NETDEV_FCOE_WWNN
static int vlan_dev_fcoe_get_wwn(struct net_device *dev, u64 *wwn, int type)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	int rc = -EINVAL;

	if (ops->ndo_fcoe_get_wwn)
		rc = ops->ndo_fcoe_get_wwn(real_dev, wwn, type);
	return rc;
}
#endif

static void vlan_dev_change_rx_flags(struct net_device *dev, int change)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(real_dev, dev->flags & IFF_ALLMULTI ? 1 : -1);
	if (change & IFF_PROMISC)
		dev_set_promiscuity(real_dev, dev->flags & IFF_PROMISC ? 1 : -1);
}

static void vlan_dev_set_rx_mode(struct net_device *vlan_dev)
{
	dev_mc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
	dev_uc_sync(vlan_dev_priv(vlan_dev)->real_dev, vlan_dev);
}

static __be16 vlan_parse_protocol(const struct sk_buff *skb)
{
	struct vlan_ethhdr *veth = (struct vlan_ethhdr *)(skb->data);

	return __vlan_get_protocol(skb, veth->h_vlan_proto, NULL);
}

static const struct header_ops vlan_header_ops = {
	.create	 = vlan_dev_hard_header,
	.parse	 = eth_header_parse,
	.parse_protocol = vlan_parse_protocol,
};

static int vlan_passthru_hard_header(struct sk_buff *skb, struct net_device *dev,
				     unsigned short type,
				     const void *daddr, const void *saddr,
				     unsigned int len)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;

	if (saddr == NULL)
		saddr = dev->dev_addr;

	return dev_hard_header(skb, real_dev, type, daddr, saddr, len);
}

static const struct header_ops vlan_passthru_header_ops = {
	.create	 = vlan_passthru_hard_header,
	.parse	 = eth_header_parse,
	.parse_protocol = vlan_parse_protocol,
};

static const struct device_type vlan_type = {
	.name	= "vlan",
};

static const struct net_device_ops vlan_netdev_ops;

static int vlan_dev_init(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;

	netif_carrier_off(dev);

	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI |
					  IFF_MASTER | IFF_SLAVE);
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	if (vlan->flags & VLAN_FLAG_BRIDGE_BINDING)
		dev->state |= (1 << __LINK_STATE_NOCARRIER);

	dev->hw_features = NETIF_F_HW_CSUM | NETIF_F_SG |
			   NETIF_F_FRAGLIST | NETIF_F_GSO_SOFTWARE |
			   NETIF_F_GSO_ENCAP_ALL |
			   NETIF_F_HIGHDMA | NETIF_F_SCTP_CRC |
			   NETIF_F_FCOE_CRC | NETIF_F_FSO;

	if (real_dev->vlan_features & NETIF_F_HW_MACSEC)
		dev->hw_features |= NETIF_F_HW_MACSEC;

	dev->features |= dev->hw_features;
	dev->lltx = true;
	dev->fcoe_mtu = true;
	netif_inherit_tso_max(dev, real_dev);
	if (dev->features & NETIF_F_VLAN_FEATURES)
		netdev_warn(real_dev, "VLAN features are set incorrectly.  Q-in-Q configurations may not work correctly.\n");

	dev->vlan_features = real_dev->vlan_features &
			     ~(NETIF_F_FCOE_CRC | NETIF_F_FSO);
	dev->hw_enc_features = vlan_tnl_features(real_dev);
	dev->mpls_features = real_dev->mpls_features;

	if (netif_supports_nofcs(real_dev))
		dev->priv_flags |= IFF_SUPP_NOFCS;

	/* ipv6 shared card related stuff */
	dev->dev_id = real_dev->dev_id;

	if (is_zero_ether_addr(dev->dev_addr)) {
		eth_hw_addr_set(dev, real_dev->dev_addr);
		dev->addr_assign_type = NET_ADDR_STOLEN;
	}
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

#if IS_ENABLED(CONFIG_FCOE)
	dev->fcoe_ddp_xid = real_dev->fcoe_ddp_xid;
#endif

	dev->needed_headroom = real_dev->needed_headroom;
	if (vlan_hw_offload_capable(real_dev->features, vlan->vlan_proto)) {
		dev->header_ops      = &vlan_passthru_header_ops;
		dev->hard_header_len = real_dev->hard_header_len;
	} else {
		dev->header_ops      = &vlan_header_ops;
		dev->hard_header_len = real_dev->hard_header_len + VLAN_HLEN;
	}

	dev->netdev_ops = &vlan_netdev_ops;

	SET_NETDEV_DEVTYPE(dev, &vlan_type);

	netdev_lockdep_set_classes(dev);

	vlan->vlan_pcpu_stats = netdev_alloc_pcpu_stats(struct vlan_pcpu_stats);
	if (!vlan->vlan_pcpu_stats)
		return -ENOMEM;

	/* Get vlan's reference to real_dev */
	netdev_hold(real_dev, &vlan->dev_tracker, GFP_KERNEL);

	return 0;
}

/* Note: this function might be called multiple times for the same device. */
void vlan_dev_free_egress_priority(const struct net_device *dev)
{
	struct vlan_priority_tci_mapping *pm;
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(vlan->egress_priority_map); i++) {
		while ((pm = vlan->egress_priority_map[i]) != NULL) {
			vlan->egress_priority_map[i] = pm->next;
			kfree(pm);
		}
	}
}

static void vlan_dev_uninit(struct net_device *dev)
{
	vlan_dev_free_egress_priority(dev);
}

static netdev_features_t vlan_dev_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;
	netdev_features_t old_features = features;
	netdev_features_t lower_features;

	lower_features = netdev_intersect_features((real_dev->vlan_features |
						    NETIF_F_RXCSUM),
						   real_dev->features);

	/* Add HW_CSUM setting to preserve user ability to control
	 * checksum offload on the vlan device.
	 */
	if (lower_features & (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM))
		lower_features |= NETIF_F_HW_CSUM;
	features = netdev_intersect_features(features, lower_features);
	features |= old_features & (NETIF_F_SOFT_FEATURES | NETIF_F_GSO_SOFTWARE);

	return features;
}

static int vlan_ethtool_get_link_ksettings(struct net_device *dev,
					   struct ethtool_link_ksettings *cmd)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	return __ethtool_get_link_ksettings(vlan->real_dev, cmd);
}

static void vlan_ethtool_get_drvinfo(struct net_device *dev,
				     struct ethtool_drvinfo *info)
{
	strscpy(info->driver, vlan_fullname, sizeof(info->driver));
	strscpy(info->version, vlan_version, sizeof(info->version));
	strscpy(info->fw_version, "N/A", sizeof(info->fw_version));
}

static int vlan_ethtool_get_ts_info(struct net_device *dev,
				    struct kernel_ethtool_ts_info *info)
{
	const struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	return ethtool_get_ts_info_by_layer(vlan->real_dev, info);
}

static void vlan_dev_get_stats64(struct net_device *dev,
				 struct rtnl_link_stats64 *stats)
{
	struct vlan_pcpu_stats *p;
	u32 rx_errors = 0, tx_dropped = 0, collisions = 0;
	int i;

	for_each_possible_cpu(i) {
		u64 rxpackets, rxbytes, rxmulticast, txpackets, txbytes;
		unsigned int start;

		p = per_cpu_ptr(vlan_dev_priv(dev)->vlan_pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin(&p->syncp);
			rxpackets	= u64_stats_read(&p->rx_packets);
			rxbytes		= u64_stats_read(&p->rx_bytes);
			rxmulticast	= u64_stats_read(&p->rx_multicast);
			txpackets	= u64_stats_read(&p->tx_packets);
			txbytes		= u64_stats_read(&p->tx_bytes);
		} while (u64_stats_fetch_retry(&p->syncp, start));

		stats->rx_packets	+= rxpackets;
		stats->rx_bytes		+= rxbytes;
		stats->multicast	+= rxmulticast;
		stats->tx_packets	+= txpackets;
		stats->tx_bytes		+= txbytes;
		/* rx_errors & tx_dropped are u32 */
		rx_errors	+= READ_ONCE(p->rx_errors);
		tx_dropped	+= READ_ONCE(p->tx_dropped);
		collisions	+= READ_ONCE(p->collisions);
	}
	stats->rx_errors  = rx_errors;
	stats->tx_dropped = tx_dropped;
	stats->collisions = collisions;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void vlan_dev_poll_controller(struct net_device *dev)
{
	return;
}

static int vlan_dev_netpoll_setup(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	struct netpoll *netpoll;
	int err = 0;

	netpoll = kzalloc(sizeof(*netpoll), GFP_KERNEL);
	err = -ENOMEM;
	if (!netpoll)
		goto out;

	err = __netpoll_setup(netpoll, real_dev);
	if (err) {
		kfree(netpoll);
		goto out;
	}

	vlan->netpoll = netpoll;

out:
	return err;
}

static void vlan_dev_netpoll_cleanup(struct net_device *dev)
{
	struct vlan_dev_priv *vlan= vlan_dev_priv(dev);
	struct netpoll *netpoll = vlan->netpoll;

	if (!netpoll)
		return;

	vlan->netpoll = NULL;
	__netpoll_free(netpoll);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

static int vlan_dev_get_iflink(const struct net_device *dev)
{
	const struct net_device *real_dev = vlan_dev_priv(dev)->real_dev;

	return READ_ONCE(real_dev->ifindex);
}

static int vlan_dev_fill_forward_path(struct net_device_path_ctx *ctx,
				      struct net_device_path *path)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(ctx->dev);

	path->type = DEV_PATH_VLAN;
	path->encap.id = vlan->vlan_id;
	path->encap.proto = vlan->vlan_proto;
	path->dev = ctx->dev;
	ctx->dev = vlan->real_dev;
	if (ctx->num_vlans >= ARRAY_SIZE(ctx->vlan))
		return -ENOSPC;

	ctx->vlan[ctx->num_vlans].id = vlan->vlan_id;
	ctx->vlan[ctx->num_vlans].proto = vlan->vlan_proto;
	ctx->num_vlans++;

	return 0;
}

#if IS_ENABLED(CONFIG_MACSEC)

static const struct macsec_ops *vlan_get_macsec_ops(const struct macsec_context *ctx)
{
	return vlan_dev_priv(ctx->netdev)->real_dev->macsec_ops;
}

static int vlan_macsec_offload(int (* const func)(struct macsec_context *),
			       struct macsec_context *ctx)
{
	if (unlikely(!func))
		return 0;

	return (*func)(ctx);
}

static int vlan_macsec_dev_open(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_dev_open, ctx);
}

static int vlan_macsec_dev_stop(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_dev_stop, ctx);
}

static int vlan_macsec_add_secy(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_add_secy, ctx);
}

static int vlan_macsec_upd_secy(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_upd_secy, ctx);
}

static int vlan_macsec_del_secy(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_del_secy, ctx);
}

static int vlan_macsec_add_rxsc(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_add_rxsc, ctx);
}

static int vlan_macsec_upd_rxsc(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_upd_rxsc, ctx);
}

static int vlan_macsec_del_rxsc(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_del_rxsc, ctx);
}

static int vlan_macsec_add_rxsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_add_rxsa, ctx);
}

static int vlan_macsec_upd_rxsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_upd_rxsa, ctx);
}

static int vlan_macsec_del_rxsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_del_rxsa, ctx);
}

static int vlan_macsec_add_txsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_add_txsa, ctx);
}

static int vlan_macsec_upd_txsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_upd_txsa, ctx);
}

static int vlan_macsec_del_txsa(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_del_txsa, ctx);
}

static int vlan_macsec_get_dev_stats(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_get_dev_stats, ctx);
}

static int vlan_macsec_get_tx_sc_stats(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_get_tx_sc_stats, ctx);
}

static int vlan_macsec_get_tx_sa_stats(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_get_tx_sa_stats, ctx);
}

static int vlan_macsec_get_rx_sc_stats(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_get_rx_sc_stats, ctx);
}

static int vlan_macsec_get_rx_sa_stats(struct macsec_context *ctx)
{
	const struct macsec_ops *ops = vlan_get_macsec_ops(ctx);

	if (!ops)
		return -EOPNOTSUPP;

	return vlan_macsec_offload(ops->mdo_get_rx_sa_stats, ctx);
}

static const struct macsec_ops macsec_offload_ops = {
	/* Device wide */
	.mdo_dev_open = vlan_macsec_dev_open,
	.mdo_dev_stop = vlan_macsec_dev_stop,
	/* SecY */
	.mdo_add_secy = vlan_macsec_add_secy,
	.mdo_upd_secy = vlan_macsec_upd_secy,
	.mdo_del_secy = vlan_macsec_del_secy,
	/* Security channels */
	.mdo_add_rxsc = vlan_macsec_add_rxsc,
	.mdo_upd_rxsc = vlan_macsec_upd_rxsc,
	.mdo_del_rxsc = vlan_macsec_del_rxsc,
	/* Security associations */
	.mdo_add_rxsa = vlan_macsec_add_rxsa,
	.mdo_upd_rxsa = vlan_macsec_upd_rxsa,
	.mdo_del_rxsa = vlan_macsec_del_rxsa,
	.mdo_add_txsa = vlan_macsec_add_txsa,
	.mdo_upd_txsa = vlan_macsec_upd_txsa,
	.mdo_del_txsa = vlan_macsec_del_txsa,
	/* Statistics */
	.mdo_get_dev_stats = vlan_macsec_get_dev_stats,
	.mdo_get_tx_sc_stats = vlan_macsec_get_tx_sc_stats,
	.mdo_get_tx_sa_stats = vlan_macsec_get_tx_sa_stats,
	.mdo_get_rx_sc_stats = vlan_macsec_get_rx_sc_stats,
	.mdo_get_rx_sa_stats = vlan_macsec_get_rx_sa_stats,
};

#endif

static const struct ethtool_ops vlan_ethtool_ops = {
	.get_link_ksettings	= vlan_ethtool_get_link_ksettings,
	.get_drvinfo	        = vlan_ethtool_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= vlan_ethtool_get_ts_info,
};

static const struct net_device_ops vlan_netdev_ops = {
	.ndo_change_mtu		= vlan_dev_change_mtu,
	.ndo_init		= vlan_dev_init,
	.ndo_uninit		= vlan_dev_uninit,
	.ndo_open		= vlan_dev_open,
	.ndo_stop		= vlan_dev_stop,
	.ndo_start_xmit =  vlan_dev_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= vlan_dev_set_mac_address,
	.ndo_set_rx_mode	= vlan_dev_set_rx_mode,
	.ndo_change_rx_flags	= vlan_dev_change_rx_flags,
	.ndo_eth_ioctl		= vlan_dev_ioctl,
	.ndo_neigh_setup	= vlan_dev_neigh_setup,
	.ndo_get_stats64	= vlan_dev_get_stats64,
#if IS_ENABLED(CONFIG_FCOE)
	.ndo_fcoe_ddp_setup	= vlan_dev_fcoe_ddp_setup,
	.ndo_fcoe_ddp_done	= vlan_dev_fcoe_ddp_done,
	.ndo_fcoe_enable	= vlan_dev_fcoe_enable,
	.ndo_fcoe_disable	= vlan_dev_fcoe_disable,
	.ndo_fcoe_ddp_target	= vlan_dev_fcoe_ddp_target,
#endif
#ifdef NETDEV_FCOE_WWNN
	.ndo_fcoe_get_wwn	= vlan_dev_fcoe_get_wwn,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= vlan_dev_poll_controller,
	.ndo_netpoll_setup	= vlan_dev_netpoll_setup,
	.ndo_netpoll_cleanup	= vlan_dev_netpoll_cleanup,
#endif
	.ndo_fix_features	= vlan_dev_fix_features,
	.ndo_get_iflink		= vlan_dev_get_iflink,
	.ndo_fill_forward_path	= vlan_dev_fill_forward_path,
	.ndo_hwtstamp_get	= vlan_hwtstamp_get,
	.ndo_hwtstamp_set	= vlan_hwtstamp_set,
};

static void vlan_dev_free(struct net_device *dev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

	free_percpu(vlan->vlan_pcpu_stats);
	vlan->vlan_pcpu_stats = NULL;

	/* Get rid of the vlan's reference to real_dev */
	netdev_put(vlan->real_dev, &vlan->dev_tracker);
}

void vlan_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags		|= IFF_802_1Q_VLAN | IFF_NO_QUEUE;
	dev->priv_flags		|= IFF_UNICAST_FLT;
	dev->priv_flags		&= ~IFF_TX_SKB_SHARING;
	netif_keep_dst(dev);

	dev->netdev_ops		= &vlan_netdev_ops;
	dev->needs_free_netdev	= true;
	dev->priv_destructor	= vlan_dev_free;
	dev->ethtool_ops	= &vlan_ethtool_ops;

#if IS_ENABLED(CONFIG_MACSEC)
	dev->macsec_ops		= &macsec_offload_ops;
#endif
	dev->min_mtu		= 0;
	dev->max_mtu		= ETH_MAX_MTU;

	eth_zero_addr(dev->broadcast);
}
