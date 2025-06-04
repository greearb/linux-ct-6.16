// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include <linux/minmax.h>
#include "coredump.h"
#include "mt7996.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

#define to_rssi(field, rcpi)	((FIELD_GET(field, rcpi) - 220) / 2)

static const struct mt7996_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0,  990, 5010, 17, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0,  615, 5010, 27, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0,  240,  445, 27, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0,  240,  510, 42, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 12, 32, 28, { }, 126 },
		[10] = { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 15, 32, 24, { }, 126 },
		[11] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 18, 32, 28, { },  54 },
		[12] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 27, 32, 24, { },  54 },
	},
};

static const struct mt7996_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  8,  32, 28, 0, 508, 3076, 13, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 140,  240, 17, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 190,  510, 22, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 190,  510, 32, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 323,  343, 13, 1, 32 },
	},
};

static const struct mt7996_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8,  32, 28, 0,  508, 3076,  13, 1,  1 },
		[1] =  { 1, 0, 12,  32, 28, 0,  140,  240,  17, 1,  1 },
		[2] =  { 1, 0,  8,  32, 28, 0,  190,  510,  22, 1,  1 },
		[3] =  { 1, 0,  6,  32, 28, 0,  190,  510,  32, 1,  1 },
		[4] =  { 1, 0,  9, 255, 28, 0,  323,  343,  13, 1, 32 },
		[13] = { 1, 0,  7,  32, 28, 0, 3836, 3856,  14, 1,  1 },
		[14] = { 1, 0,  6,  32, 28, 0,  615, 5010, 110, 1,  1 },
		[15] = { 1, 1,  0,   0,  0, 0,   15, 5010, 110, 0,  0, 12, 32, 28 },
	},
};

static void mt7996_scan_rx(struct mt7996_phy *phy);
static void mt7996_rx_beacon_hint(struct mt7996_phy *phy, struct mt7996_vif *mvif);

static struct mt76_wcid *mt7996_rx_get_wcid(struct mt7996_dev *dev,
					    u16 idx, u8 band_idx)
{
	struct mt7996_sta_link *msta_link;
	struct mt7996_sta *msta;
	struct mt7996_vif *mvif;
	struct mt76_wcid *wcid;
	int i;

	wcid = mt76_wcid_ptr(dev, idx);
	if (!wcid)
		return NULL;

	if (!mt7996_band_valid(dev, band_idx))
		return NULL;

	if (wcid->phy_idx == band_idx)
		return wcid;

	msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
	msta = msta_link->sta;
	if (!msta || !msta->vif)
		return NULL;

	mvif = msta->vif;
	for (i = 0; i < ARRAY_SIZE(mvif->mt76.link); i++) {
		struct mt76_vif_link *mlink;

		mlink = rcu_dereference(mvif->mt76.link[i]);
		if (!mlink)
			continue;

		if (mlink->band_idx != band_idx)
			continue;

		msta_link = rcu_dereference(msta->link[i]);
		break;
	}

	if (!msta_link)
		return wcid;

	return &msta_link->wcid;
}

static struct mt76_wcid *mt7996_get_active_link_wcid(struct mt7996_dev *dev,
						     struct mt76_wcid *old_wcid)
{
	struct mt7996_sta_link *old_msta_link = container_of(old_wcid, struct mt7996_sta_link, wcid);
	struct mt7996_sta_link *msta_link = NULL;
	struct mt7996_sta *msta = old_msta_link->sta;
	int i;

	if (old_wcid->link_id != msta->deflink_id)
		msta_link = rcu_dereference(msta->link[msta->deflink_id]);
	else if (old_wcid->link_id != msta->sec_link)
		msta_link = rcu_dereference(msta->link[msta->sec_link]);

	if (msta_link)
		return &msta_link->wcid;

	for (i = MT_BAND0; i <= MT_BAND2; i++) {
		struct mt76_wcid *tmp =
			mt7996_rx_get_wcid(dev, old_wcid->idx, i);

		if (tmp && !tmp->sta_disabled)
			return tmp;
	}

	return old_wcid;
}

bool mt7996_mac_wtbl_update(struct mt7996_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

u32 mt7996_mac_wtbl_lmac_addr(struct mt7996_dev *dev, u16 wcid, u8 dw)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return MT_WTBL_LMAC_OFFS(wcid, dw);
}

static int mt7996_mac_sta_poll(struct mt76_dev *dev)
{
	u16 sta_list[PER_STA_INFO_MAX_NUM];
	struct mt7996_sta_link *msta_link;
	int i, ret;

	spin_lock_bh(&dev->sta_poll_lock);
	for (i = 0; i < PER_STA_INFO_MAX_NUM; ++i) {
		if (list_empty(&dev->sta_poll_list))
			break;

		msta_link = list_first_entry(&dev->sta_poll_list,
					 struct mt7996_sta_link,
					 wcid.poll_list);
		list_del_init(&msta_link->wcid.poll_list);
		sta_list[i] = msta_link->wcid.idx;
	}
	spin_unlock_bh(&dev->sta_poll_lock);

	if (i == 0)
		return 0;

	ret = mt7996_mcu_get_per_sta_info(dev, UNI_PER_STA_RSSI, i, sta_list);
	if (ret)
		return ret;

	ret = mt7996_mcu_get_per_sta_info(dev, UNI_PER_STA_SNR, i, sta_list);
	if (ret)
		return ret;

	return mt7996_mcu_get_per_sta_info(dev, UNI_PER_STA_PKT_CNT, i, sta_list);
}

/* The HW does not translate the mac header to 802.3 for mesh point */
static int mt7996_reverse_frag0_hdr_trans(struct sk_buff *skb, u16 hdr_gap)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ethhdr *eth_hdr = (struct ethhdr *)(skb->data + hdr_gap);
	struct mt7996_sta_link *msta_link = (struct mt7996_sta_link *)status->wcid;
	__le32 *rxd = (__le32 *)skb->data;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *conf;
	struct ieee80211_hdr hdr;
	u16 frame_control;

	if (le32_get_bits(rxd[3], MT_RXD3_NORMAL_ADDR_TYPE) !=
	    MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[1]) & MT_RXD1_NORMAL_GROUP_4))
		return -EINVAL;

	if (!msta_link->sta || !msta_link->sta->vif)
		return -EINVAL;

	sta = wcid_to_sta(status->wcid);
	vif = container_of((void *)msta_link->sta->vif, struct ieee80211_vif, drv_priv);
	conf = rcu_dereference(vif->link_conf[msta_link->wcid.link_id]);
	if (unlikely(!conf))
		return -ENOLINK;

	/* store the info from RXD and ethhdr to avoid being overridden */
	frame_control = le32_get_bits(rxd[8], MT_RXD8_FRAME_CONTROL);
	hdr.frame_control = cpu_to_le16(frame_control);
	hdr.seq_ctrl = cpu_to_le16(le32_get_bits(rxd[10], MT_RXD10_SEQ_CTRL));
	hdr.duration_id = 0;

	ether_addr_copy(hdr.addr1, vif->addr);
	ether_addr_copy(hdr.addr2, sta->addr);
	switch (frame_control & (IEEE80211_FCTL_TODS |
				 IEEE80211_FCTL_FROMDS)) {
	case 0:
		// TODO:  Possible case where BSSID is wrong? --Ben
		ether_addr_copy(hdr.addr3, conf->bssid);
		break;
	case IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_source);
		break;
	case IEEE80211_FCTL_TODS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		break;
	case IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		ether_addr_copy(hdr.addr4, eth_hdr->h_source);
		break;
	default:
		return -EINVAL;
	}

	skb_pull(skb, hdr_gap + sizeof(struct ethhdr) - 2);
	if (eth_hdr->h_proto == cpu_to_be16(ETH_P_AARP) ||
	    eth_hdr->h_proto == cpu_to_be16(ETH_P_IPX))
		ether_addr_copy(skb_push(skb, ETH_ALEN), bridge_tunnel_header);
	else if (be16_to_cpu(eth_hdr->h_proto) >= ETH_P_802_3_MIN)
		ether_addr_copy(skb_push(skb, ETH_ALEN), rfc1042_header);
	else
		skb_pull(skb, 2);

	if (ieee80211_has_order(hdr.frame_control))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &rxd[11],
		       IEEE80211_HT_CTL_LEN);
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		__le16 qos_ctrl;

		qos_ctrl = cpu_to_le16(le32_get_bits(rxd[10], MT_RXD10_QOS_CTL));
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctrl,
		       IEEE80211_QOS_CTL_LEN);
	}

	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	return 0;
}

static int
mt7996_mac_fill_rx_rate(struct mt7996_dev *dev,
			struct mt76_rx_status *status,
			struct ieee80211_supported_band *sband,
			__le32 *rxv, u8 *mode, u8* nss,
			struct mt76_sta_stats *stats,
			struct mt76_mib_stats *mib)
{
	u32 v0, v2;
	u8 stbc, gi, bw, dcm;
	int i, idx;
	bool cck = false;

	v0 = le32_to_cpu(rxv[0]);
	v2 = le32_to_cpu(rxv[2]);

	idx = FIELD_GET(MT_PRXV_TX_RATE, v0);
	i = idx;
	*nss = FIELD_GET(MT_PRXV_NSTS, v0) + 1;

	stbc = FIELD_GET(MT_PRXV_HT_STBC, v2);
	gi = FIELD_GET(MT_PRXV_HT_SHORT_GI, v2);
	*mode = FIELD_GET(MT_PRXV_TX_MODE, v2);
	dcm = FIELD_GET(MT_PRXV_DCM, v2);
	bw = FIELD_GET(MT_PRXV_FRAME_MODE, v2);

	switch (*mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		i = mt76_get_rate(&dev->mt76, sband, i, cck);
		if (stbc)
			*nss = 2;
		else
			*nss = 1;
		if (stats) {
			if (unlikely(i > 13))
				stats->rx_rate_idx[13]++;
			else
				stats->rx_rate_idx[i]++;
		}
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		*nss = i / 8 + 1;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 31) {
			mib->rx_d_bad_ht_rix++;
			return -EINVAL;
		}
		if (stats) {
			int rix = i % 8;
			stats->rx_rate_idx[rix]++;
		}

		break;
	case MT_PHY_TYPE_VHT:
		status->encoding = RX_ENC_VHT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 11) {
			mib->rx_d_bad_vht_rix++;
			return -EINVAL;
		}
		if (stats)
			stats->rx_rate_idx[i]++;
		break;
	case MT_PHY_TYPE_HE_MU:
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
		status->encoding = RX_ENC_HE;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
			status->he_gi = gi;

		status->he_dcm = dcm;
		if (stats) {
			if (unlikely(i > 13))
				stats->rx_rate_idx[13]++;
			else
				stats->rx_rate_idx[i]++;
		}
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		status->nss = *nss;
		status->encoding = RX_ENC_EHT;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_EHT_GI_3_2)
			status->eht.gi = gi;
		if (stats) {
			if (unlikely(i > 13))
				stats->rx_rate_idx[13]++;
			else
				stats->rx_rate_idx[i]++;
		}
		break;
	default:
		mib->rx_d_bad_mode++;
		return -EINVAL;
	}
	status->rate_idx = i;

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		if (stats)
			stats->rx_bw_20++;
		break;
	case IEEE80211_STA_RX_BW_40:
		if (*mode & MT_PHY_TYPE_HE_EXT_SU &&
		    (idx & MT_PRXV_TX_ER_SU_106T)) {
			status->bw = RATE_INFO_BW_HE_RU;
			status->he_ru =
				NL80211_RATE_INFO_HE_RU_ALLOC_106;
			if (stats) {
				stats->rx_bw_he_ru++;
				stats->rx_ru_106++;
			}
		} else {
			status->bw = RATE_INFO_BW_40;
			if (stats)
				stats->rx_bw_40++;
		}
		break;
	case IEEE80211_STA_RX_BW_80:
		status->bw = RATE_INFO_BW_80;
		if (stats)
			stats->rx_bw_80++;
		break;
	case IEEE80211_STA_RX_BW_160:
		status->bw = RATE_INFO_BW_160;
		if (stats)
			stats->rx_bw_160++;
		break;
	/* rxv reports bw 320-1 and 320-2 separately */
	case IEEE80211_STA_RX_BW_320:
	case IEEE80211_STA_RX_BW_320 + 1:
		status->bw = RATE_INFO_BW_320;
		if (stats)
			stats->rx_bw_320++;
		break;
	default:
		mib->rx_d_bad_bw++;
		return -EINVAL;
	}

	status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;
	if (*mode < MT_PHY_TYPE_HE_SU && gi)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	/* in case stbc is set, the nss value returned by hardware is already
	 * correct (ie, 2 instead of 1 for 1 spatial stream).  But, at least in cases
	 * where we are configured for a single antenna, then the second chain RSSI is '17',
	 * which I take to mean 'not set'.  To keep from adding this to the average rssi up in
	 * mac80211 rx logic, decrease nss here.
	 */
	if (stbc)
		*nss >>= 1;

	status->nss = *nss;

	if (stats) {
		if (*nss > 3)
			stats->rx_nss[3]++;
		else
			stats->rx_nss[*nss - 1]++;
		if (*mode < __MT_PHY_TYPE_MAX)
			stats->rx_mode[*mode]++;
        }

	return 0;
}

static void
mt7996_wed_check_ppe(struct mt7996_dev *dev, struct mt76_queue *q,
		     struct mt7996_sta *msta, struct sk_buff *skb,
		     u32 info)
{
	struct ieee80211_vif *vif;
	struct wireless_dev *wdev;

	if (!msta || !msta->vif)
		return;

	if (!mt76_queue_is_wed_rx(q))
		return;

	if (!(info & MT_DMA_INFO_PPE_VLD))
		return;

	vif = container_of((void *)msta->vif, struct ieee80211_vif,
			   drv_priv);
	wdev = ieee80211_vif_to_wdev(vif);
	skb->dev = wdev->netdev;

	mtk_wed_device_ppe_check(&dev->mt76.mmio.wed, skb,
				 FIELD_GET(MT_DMA_PPE_CPU_REASON, info),
				 FIELD_GET(MT_DMA_PPE_ENTRY, info));
}

static int
mt7996_mac_fill_rx(struct mt7996_dev *dev, enum mt76_rxq_id q,
		   struct sk_buff *skb, u32 *info)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv = NULL;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	u32 csum_mask = MT_RXD3_NORMAL_IP_SUM | MT_RXD3_NORMAL_UDP_TCP_SUM;
	u32 csum_status = *(u32 *)skb->cb;
	u32 mesh_mask = MT_RXD0_MESH | MT_RXD0_MHCP;
	bool is_mesh = (rxd0 & mesh_mask) == mesh_mask;
	bool unicast, insert_ccmp_hdr = false;
	u8 remove_pad, amsdu_info, band_idx;
	u8 mode = 0, qos_ctl = 0;
	bool hdr_trans;
	u16 hdr_gap;
	u16 seq_ctrl = 0;
	__le16 fc = 0;
	int idx;
	u8 hw_aggr = false;
	struct mt7996_sta *msta = NULL;
	struct mt7996_sta_link *msta_link = NULL;
	struct mt76_sta_stats *stats = NULL;
	struct mt76_mib_stats *mib;

	hw_aggr = status->aggr;
	memset(status, 0, sizeof(*status));

	band_idx = FIELD_GET(MT_RXD1_NORMAL_BAND_IDX, rxd1);
	mphy = dev->mt76.phys[band_idx];
	phy = mphy->priv;
	status->phy_idx = mphy->band_idx;

	mib = &phy->mib;
	mib->rx_d_skb++;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR) {
		mib->rx_d_rxd2_amsdu_err++;
		return -EINVAL;
	}

	hdr_trans = rxd2 & MT_RXD2_NORMAL_HDR_TRANS;
	if (hdr_trans && (rxd1 & MT_RXD1_NORMAL_CM))
		return -EINVAL;

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd1 & MT_RXD1_NORMAL_ICV_ERR)
		status->flag |= RX_FLAG_ONLY_MONITOR;

	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7996_rx_get_wcid(dev, idx, band_idx);

	if (status->wcid) {
		msta_link = container_of(status->wcid, struct mt7996_sta_link,
					 wcid);
		stats = &status->wcid->stats;
		msta = msta_link->sta;
		mt76_wcid_add_poll(&dev->mt76, &msta_link->wcid);

		if (status->wcid->sta_disabled)
			status->wcid = mt7996_get_active_link_wcid(dev,
								   status->wcid);
	}

	status->freq = mphy->chandef.chan->center_freq;
	status->band = mphy->chandef.chan->band;
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else if (status->band == NL80211_BAND_6GHZ)
		sband = &mphy->sband_6g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!sband->channels) {
		mib->rx_d_null_channels++;
		return -EINVAL;
	}

	if ((rxd3 & csum_mask) == csum_mask &&
	    !(csum_status & (BIT(0) | BIT(2) | BIT(3))))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (rxd1 & MT_RXD3_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR) {
		mib->rx_d_max_len_err++;
		return -EINVAL;
	}

	rxd += 8;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		fc = cpu_to_le16(FIELD_GET(MT_RXD8_FRAME_CONTROL, v0));
		qos_ctl = FIELD_GET(MT_RXD10_QOS_CTL, v2);
		seq_ctrl = FIELD_GET(MT_RXD10_SEQ_CTRL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len) {
			mib->rx_d_too_short++;
			return -EINVAL;
		}
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2)) {
			case MT_CIPHER_AES_CCMP:
			case MT_CIPHER_CCMP_CCX:
			case MT_CIPHER_CCMP_256:
				insert_ccmp_hdr =
					FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
				fallthrough;
			case MT_CIPHER_TKIP:
			case MT_CIPHER_TKIP_NO_MIC:
			case MT_CIPHER_GCMP:
			case MT_CIPHER_GCMP_256:
				status->iv[0] = data[5];
				status->iv[1] = data[4];
				status->iv[2] = data[3];
				status->iv[3] = data[2];
				status->iv[4] = data[1];
				status->iv[5] = data[0];
				break;
			default:
				break;
			}
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len) {
			mib->rx_d_too_short++;
			return -EINVAL;
		}
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
			status->flag |= RX_FLAG_AMPDU_DETAILS;

			/* all subframes of an A-MPDU have the same timestamp */
			if (phy->rx_ampdu_ts != status->timestamp) {
				if (!++phy->ampdu_ref)
					phy->ampdu_ref++;
			}
			phy->rx_ampdu_ts = status->timestamp;

			status->ampdu_ref = phy->ampdu_ref;
		}

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len) {
			mib->rx_d_too_short++;
			return -EINVAL;
		}
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u32 v3;
		int ret;
		int i;
		u8 nss;

		rxv = rxd;
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len) {
			mib->rx_d_too_short++;
			return -EINVAL;
		}

		v3 = le32_to_cpu(rxv[3]);

		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v3);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v3);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v3);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v3);
		nss = hweight8(mphy->antenna_mask);

		if (msta_link) {
			int i;

			memcpy(msta_link->chain_signal, status->chain_signal,
			       IEEE80211_MAX_CHAINS);
			msta_link->signal = mt76_rx_signal(mphy->antenna_mask,
							   msta_link->chain_signal);

			for (i = 0; i < IEEE80211_MAX_CHAINS; ++i)
				ewma_avg_signal_add(msta_link->chain_signal_avg + i,
						    -msta_link->chain_signal[i]);
			ewma_avg_signal_add(&msta_link->signal_avg, -msta_link->signal);
		}

		/* RXD Group 5 - C-RXV */
		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 24;
			if ((u8 *)rxd - skb->data >= skb->len) {
				mib->rx_d_too_short++;
				return -EINVAL;
			}
		}

		ret = mt7996_mac_fill_rx_rate(dev, status, sband, rxv, &mode, &nss, stats, mib);
		if (ret < 0)
			return ret;

		// TODO: FIXME: 7996:  Check if this is correct, perhaps we can set bits based on chain_signal above?
		for (i = 0; i < nss; i++)
			status->chains |= BIT(i);
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
		//pr_info("fill-rx, wcid: %p first-amsdu: %d  last-amsdu: %d",
		//	status->wcid, status->first_amsdu, status->last_amsdu);

		/* Deal with rx ampdu histogram stats */
		if (status->wcid) {
			status->wcid->ampdu_chain++;
			if (status->last_amsdu) {
				mt76_inc_ampdu_bucket(status->wcid->ampdu_chain, stats);
				//pr_info("fill-rx, last-amsdu, chain-count: %d [0]: %ld  [1]: %ld",
				//	status->wcid->ampdu_chain, stats->rx_ampdu_len[0],
				//	stats->rx_ampdu_len[1]);
				status->wcid->ampdu_chain = 0;
			}
		}
	} else {
		/* Deal with rx ampdu histogram stats */
		if (status->wcid) {
			status->wcid->ampdu_chain++;
			mt76_inc_ampdu_bucket(status->wcid->ampdu_chain, stats);
			status->wcid->ampdu_chain = 0;
		}
	}

	/* IEEE 802.11 fragmentation can only be applied to unicast frames.
	 * Hence, drop fragments with multicast/broadcast RA.
	 * This check fixes vulnerabilities, like CVE-2020-26145.
	 */
	if ((ieee80211_has_morefrags(fc) || seq_ctrl & IEEE80211_SCTL_FRAG) &&
	    FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) != MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		if (mt7996_reverse_frag0_hdr_trans(skb, hdr_gap))
			return -EINVAL;
		hdr_trans = false;
	} else {
		int pad_start = 0;

		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu && !(ieee80211_has_a4(fc) && is_mesh)) {
			pad_start = ieee80211_get_hdrlen_from_skb(skb);
		} else if (hdr_trans && (rxd2 & MT_RXD2_NORMAL_HDR_TRANS_ERROR)) {
			/* When header translation failure is indicated,
			 * the hardware will insert an extra 2-byte field
			 * containing the data length after the protocol
			 * type field. This happens either when the LLC-SNAP
			 * pattern did not match, or if a VLAN header was
			 * detected.
			 */
			pad_start = 12;
			if (get_unaligned_be16(skb->data + pad_start) == ETH_P_8021Q)
				pad_start += 4;
			else
				pad_start = 0;
		}

		if (pad_start) {
			memmove(skb->data + 2, skb->data, pad_start);
			skb_pull(skb, 2);
		}
	}

	if (!hdr_trans) {
		struct ieee80211_hdr *hdr;

		if (insert_ccmp_hdr) {
			u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

			mt76_insert_ccmp_hdr(skb, key_id);
		}

		hdr = mt76_skb_get_hdr(skb);
		fc = hdr->frame_control;

		if (unlikely(ieee80211_is_probe_resp(fc) || ieee80211_is_beacon(fc)))
			mt7996_scan_rx(phy);

		if (ieee80211_is_data_qos(fc)) {
			u8 *qos = ieee80211_get_qos_ctl(hdr);

			seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
			qos_ctl = *qos;

			/* Mesh DA/SA/Length will be stripped after hardware
			 * de-amsdu, so here needs to clear amsdu present bit
			 * to mark it as a normal mesh frame.
			 */
			if (ieee80211_has_a4(fc) && is_mesh && status->amsdu)
				*qos &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
		} else if (ieee80211_is_beacon(fc)) {
			struct ieee80211_hw *hw = phy->mt76->hw;
			struct ieee80211_sta *sta;

			sta = ieee80211_find_sta_by_link_addrs(hw, hdr->addr2, NULL, NULL);
			if (!sta)
				sta = ieee80211_find_sta_by_ifaddr(hw, hdr->addr2, NULL);

			if (sta) {
				msta = (struct mt7996_sta *)sta->drv_priv;

				if (msta && msta->vif)
					mt7996_rx_beacon_hint(phy, msta->vif);
			}
		}
		skb_set_mac_header(skb, (unsigned char *)hdr - skb->data);
	} else {
		status->flag |= RX_FLAG_8023;
		mt7996_wed_check_ppe(dev, &dev->mt76.q_rx[q], msta_link ? msta_link->sta : NULL, skb,
				     *info);
	}

	if (rxv && !(status->flag & RX_FLAG_8023) &&
	    (mode >= MT_PHY_TYPE_HE_SU && mode < MT_PHY_TYPE_EHT_SU)) {
		switch (status->encoding) {
		case RX_ENC_EHT:
			mt76_connac3_mac_decode_eht_radiotap(skb, rxv, mode);
			break;
		case RX_ENC_HE:
			mt76_connac3_mac_decode_he_radiotap(skb, rxv, mode);
			break;
		default:
			break;
		}
	}

	mib->rx_pkts_nic++;
	mib->rx_bytes_nic += skb->len;

	status->wcid_idx = status->wcid ? status->wcid->idx : 0;

	/* WCID 0 = mt76.global_wcid, used for beacons and management frames.
	 * See mt7996_init_hardware()
	 */
	if (!status->wcid_idx || !ieee80211_is_data_qos(fc) || hw_aggr)
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(fc);
	status->qos_ctl = qos_ctl;
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);

	return 0;
}

static void
mt7996_mac_write_txwi_8023(struct mt7996_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid)
{
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	u8 fc_type, fc_stype;
	u16 ethertype;
	bool wmm = false;
	u32 val;

	if (wcid->sta) {
		struct ieee80211_sta *sta = wcid_to_sta(wcid);

		wmm = sta->wme;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	ethertype = get_unaligned_be16(&skb->data[12]);
	if (ethertype >= ETH_P_802_3_MIN)
		val |= MT_TXD1_ETH_802_3;

	txwi[1] |= cpu_to_le32(val);

	fc_type = IEEE80211_FTYPE_DATA >> 2;
	fc_stype = wmm ? IEEE80211_STYPE_QOS_DATA >> 4 : 0;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);

	if (wcid->amsdu)
		txwi[3] |= cpu_to_le32(MT_TXD3_HW_AMSDU);
}

static bool mt7996_is_skb_altx(struct ieee80211_mgmt *mgmt)
{
	__le16 fc = mgmt->frame_control;

	if (ieee80211_is_deauth(fc)) {
		/* In WPA3 cert TC-4.8.1, the deauth must be transmitted without
		 * considering PSM bit
		 */
		return true;
	}

	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_PROTECTED_EHT &&
	    (mgmt->u.action.u.ttlm_req.action_code ==
	    WLAN_PROTECTED_EHT_ACTION_TTLM_REQ ||
	    mgmt->u.action.u.ttlm_req.action_code ==
	    WLAN_PROTECTED_EHT_ACTION_TTLM_RES ||
	    mgmt->u.action.u.ttlm_req.action_code ==
	    WLAN_PROTECTED_EHT_ACTION_TTLM_TEARDOWN))
		return true;

	return false;
}

static void
mt7996_mac_write_txwi_80211(struct mt7996_dev *dev, __le32 *txwi,
			    struct sk_buff *skb,
			    struct ieee80211_key_conf *key,
			    struct mt76_wcid *wcid)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	__le16 fc = hdr->frame_control, sc = hdr->seq_ctrl;
	u16 seqno = le16_to_cpu(sc);
	u8 fc_type, fc_stype;
	u32 val;

	if (mt7996_is_skb_altx(mgmt)) {
		txwi[0] &= ~cpu_to_le32(MT_TXD0_Q_IDX);
		txwi[0] |= cpu_to_le32(FIELD_PREP(MT_TXD0_Q_IDX, MT_LMAC_ALTX0));
	}

	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ) {
		if (is_mt7990(&dev->mt76))
			txwi[6] |= cpu_to_le32(FIELD_PREP(MT_TXD6_TID_ADDBA, tid));
		tid = MT_TX_ADDBA;
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		tid = MT_TX_NORMAL;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	if (!ieee80211_is_data(fc) || multicast ||
	    info->flags & IEEE80211_TX_CTL_USE_MINRATE)
		val |= MT_TXD1_FIXED_RATE;

	if ((key && multicast && ieee80211_is_robust_mgmt_frame(skb)) ||
	    (ieee80211_is_beacon(fc) && wcid->hw_bcn_prot)) {
		val |= MT_TXD1_BIP;
		txwi[3] &= ~cpu_to_le32(MT_TXD3_PROTECT_FRAME);
	}

	txwi[1] |= cpu_to_le32(val);

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	if (ieee80211_has_morefrags(fc) && ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_FIRST);
	else if (ieee80211_has_morefrags(fc) && !ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_MID);
	else if (!ieee80211_has_morefrags(fc) && !ieee80211_is_first_frag(sc))
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_LAST);
	else
		val |= FIELD_PREP(MT_TXD2_FRAG, MT_TX_FRAG_NONE);

	txwi[2] |= cpu_to_le32(val);

	txwi[3] |= cpu_to_le32(FIELD_PREP(MT_TXD3_BCM, multicast));
	if (ieee80211_is_beacon(fc)) {
		txwi[3] &= ~cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);
	}

	if (multicast && ieee80211_vif_is_mld(info->control.vif)) {
		val = MT_TXD3_SN_VALID |
		      FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txwi[3] |= cpu_to_le32(val);
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val = MT_TXD3_SN_VALID |
		      FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txwi[3] |= cpu_to_le32(val);
		txwi[3] &= ~cpu_to_le32(MT_TXD3_HW_AMSDU);
	}

	if (ieee80211_vif_is_mld(info->control.vif) &&
	    (multicast || unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)) ||
	     info->flags & IEEE80211_TX_CTL_INJECTED)) // TODO:  Maybe help with TXO? --Ben
		txwi[5] |= cpu_to_le32(MT_TXD5_FL);

	if (ieee80211_is_nullfunc(fc) && ieee80211_has_a4(fc) &&
	    ieee80211_vif_is_mld(info->control.vif)) {
		txwi[5] |= cpu_to_le32(MT_TXD5_FL);
		txwi[6] |= cpu_to_le32(MT_TXD6_DIS_MAT);
	}

	if (!wcid->sta && ieee80211_is_mgmt(fc))
		txwi[6] |= cpu_to_le32(MT_TXD6_DIS_MAT);
}

static void
mt7996_mac_write_txwi_tm(struct mt7996_phy *phy, struct mt76_wcid *wcid, __le32 *txwi,
			 struct sk_buff *skb)
{
	struct mt76_testmode_data *td;
	const struct ieee80211_rate *r;
	struct mt7996_sta_link *link;
	struct mt7996_sta *msta;
	u8 bw, mode, nss;
	u8 rate_idx;
	u16 rateval = 0;
	u32 val;
	bool cck = false;
	int band;
	int xmit_count = 1;

	link = container_of(wcid, struct mt7996_sta_link, wcid);
	msta = link->sta;

	if (msta->deflink.test.txo_active) {
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(skb);
		struct mt7996_dev *dev = phy->dev;

		mtk_dbg(&dev->mt76, TX, "mt7996-write-txwi-tm, skb: %p skb->len: %d\n",
			skb, skb->len);

		cb->flags |= MT_TX_CB_TXO_USED;
		td = &msta->deflink.test;
	} else {
		if (skb != phy->mt76->test.tx_skb)
			return;
		td = &phy->mt76->test;
	}

	nss = td->tx_rate_nss;
	rate_idx = td->tx_rate_idx;

	switch (td->tx_rate_mode) {
	case MT76_TM_TX_MODE_HT:
		nss = 1 + (rate_idx >> 3);
		mode = MT_PHY_TYPE_HT;
		break;
	case MT76_TM_TX_MODE_VHT:
		mode = MT_PHY_TYPE_VHT;
		break;
	case MT76_TM_TX_MODE_HE_SU:
		mode = MT_PHY_TYPE_HE_SU;
		break;
	case MT76_TM_TX_MODE_HE_EXT_SU:
		mode = MT_PHY_TYPE_HE_EXT_SU;
		break;
	case MT76_TM_TX_MODE_HE_TB:
		mode = MT_PHY_TYPE_HE_TB;
		break;
	case MT76_TM_TX_MODE_HE_MU:
		mode = MT_PHY_TYPE_HE_MU;
		break;
	case MT76_TM_TX_MODE_CCK:
		cck = true;
		fallthrough;
	case MT76_TM_TX_MODE_OFDM:
		band = phy->mt76->chandef.chan->band;
		if (band == NL80211_BAND_2GHZ && !cck)
			rate_idx += 4;

		r = &phy->mt76->hw->wiphy->bands[band]->bitrates[rate_idx];
		val = cck ? r->hw_value_short : r->hw_value;

		mode = val >> 8;
		rate_idx = val & 0xff;
		break;
	default:
		mode = MT_PHY_TYPE_OFDM;
		break;
	}

	if (msta->deflink.test.txo_active) {
		bw = td->txbw;
	} else {
		switch (phy->mt76->chandef.width) {
		case NL80211_CHAN_WIDTH_40:
			bw = 1;
			break;
		case NL80211_CHAN_WIDTH_80:
			bw = 2;
			break;
		case NL80211_CHAN_WIDTH_80P80:
		case NL80211_CHAN_WIDTH_160:
			bw = 3;
			break;
		default:
			bw = 0;
			break;
		}
	}

	if (td->tx_rate_stbc && nss == 1) {
		nss++;
		rateval |= MT_TX_RATE_STBC;
	}

	rateval |= FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		   FIELD_PREP(MT_TX_RATE_MODE, mode) |
		   FIELD_PREP(MT_TX_RATE_NSS, nss - 1);

	txwi[1] |= cpu_to_le32(MT_TXD1_FIXED_RATE);

	if (msta->deflink.test.txo_active) {
		/* td->tx_power is in 1/2 db units, tx descriptor wants 1/4 db units.
		 * So multiply by 2.  The range is -8 to +7.5.  We subtract 32 to
		 * make it based on 0 starting point from user-space perspective.
		 */
		s8 txp = (td->tx_power[0] * 2) - 32;

		/* Support per-skb txpower, 0.25db units.  DW2 31:26 */
		le32p_replace_bits(&txwi[2], txp, MT_TXD2_POWER_OFFSET);

		xmit_count = td->tx_xmit_count;
		if (xmit_count == 0) {
			xmit_count = 1;
			txwi[3] |= cpu_to_le32(MT_TXD3_NO_ACK);
		}
	}

	le32p_replace_bits(&txwi[3], xmit_count, MT_TXD3_REM_TX_COUNT);
	if (td->tx_rate_mode < MT76_TM_TX_MODE_HT)
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);

	/* TODO:  Take tx_dynbw into account in txo mode. */
	val = FIELD_PREP(MT_TXD6_BW, bw) |
	      FIELD_PREP(MT_TXD6_TX_RATE, rateval);
	val |= cpu_to_le32(MT_TXD6_FIXED_BW); /* high bit of bw field */

	// TODO:  FIXME | FIELD_PREP(MT_TXD6_SGI, td->tx_rate_sgi);

	/* for HE_SU/HE_EXT_SU PPDU
	 * - 1x, 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_MU PPDU
	 * - 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_TB PPDU
	 * - 1x, 2x LTF + 1.6us GI
	 * - 4x LTF + 3.2us GI
	 */
	// TODO: FIXME 7996
	//if (mode >= MT_PHY_TYPE_HE_SU)
	//	val |= FIELD_PREP(MT_TXD6_HELTF, td->tx_ltf);

	// TODO:  FIXME 7996
	//if (td->tx_rate_ldpc || (bw > 0 && mode >= MT_PHY_TYPE_HE_SU))
	//	val |= MT_TXD6_LDPC;

	txwi[3] &= ~cpu_to_le32(MT_TXD3_SN_VALID);
	txwi[6] |= cpu_to_le32(val);

#if 0
	// TODO:  FIXME 7996
	if (msta->deflink.test.txo_active) {
		/* see mt7996_tm_set_tx_frames */
		static const u8 spe_idx_map[] = {0, 0, 1, 0, 3, 2, 4, 0,
						 9, 8, 6, 10, 16, 12, 18, 0};
		u32 spe_idx;

		if (td->tx_spe_idx) {
			spe_idx = td->tx_spe_idx;
		} else {
			u8 tx_ant = td->tx_antenna_mask;

			if (!tx_ant) {
				/* use antenna mask that matches our nss */
				tx_ant = GENMASK(nss - 1, 0);
			}
			spe_idx = spe_idx_map[tx_ant];
		}
		txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX, spe_idx));
	} else {
		txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX,
						  phy->test.spe_idx));
	}
#endif
}

void mt7996_mac_write_txwi(struct mt7996_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, int pid,
			   enum mt76_txq_id qid, u32 changed)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	u8 band_idx = (info->hw_queue & MT_TX_HW_QUEUE_PHY) >> 2;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	struct mt76_vif_link *mlink = NULL;
	struct mt7996_vif *mvif;
	unsigned int link_id;
	u16 tx_count = 15;
	u32 val;
	bool inband_disc = !!(changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
					 BSS_CHANGED_FILS_DISCOVERY));
	bool beacon = !!(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED)) && (!inband_disc);

	if (wcid != &dev->mt76.global_wcid)
		link_id = wcid->link_id;
	else
		link_id = u32_get_bits(info->control.flags,
				       IEEE80211_TX_CTRL_MLO_LINK);

	mvif = vif ? (struct mt7996_vif *)vif->drv_priv : NULL;
	if (mvif)
		mlink = rcu_dereference(mvif->mt76.link[link_id]);

	if (mlink) {
		omac_idx = mlink->omac_idx;
		wmm_idx = mlink->wmm_idx;
		band_idx = mlink->band_idx;
	}

	if (inband_disc) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_ALTX0;
	} else if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (qid >= MT_TXQ_PSD) {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = wmm_idx * MT7996_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);

	if (band_idx)
		val |= FIELD_PREP(MT_TXD1_TGID, band_idx);

	txwi[1] = cpu_to_le32(val);
	txwi[2] = 0;

	val = MT_TXD3_SW_POWER_MGMT |
	      FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (key)
		val |= MT_TXD3_PROTECT_FRAME;
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;

	txwi[3] = cpu_to_le32(val);
	txwi[4] = 0;

	val = FIELD_PREP(MT_TXD5_PID, pid);
	if (pid >= MT_PACKET_ID_FIRST)
		val |= MT_TXD5_TX_STATUS_HOST;
	txwi[5] = cpu_to_le32(val);

	val = MT_TXD6_DAS | MT_TXD6_VTA;
	if ((q_idx >= MT_LMAC_ALTX0 && q_idx <= MT_LMAC_BCN0) ||
	    unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		val |= MT_TXD6_DIS_MAT;

	if (is_mt7996(&dev->mt76))
		val |= FIELD_PREP(MT_TXD6_MSDU_CNT, 1);
	else if (is_8023 || !ieee80211_is_mgmt(hdr->frame_control))
		val |= FIELD_PREP(MT_TXD6_MSDU_CNT_V2, 1);

	txwi[6] = cpu_to_le32(val);
	txwi[7] = 0;

	if (is_8023)
		mt7996_mac_write_txwi_8023(dev, txwi, skb, wcid);
	else
		mt7996_mac_write_txwi_80211(dev, txwi, skb, key, wcid);

	if (txwi[1] & cpu_to_le32(MT_TXD1_FIXED_RATE)) {
		bool mcast = ieee80211_is_data(hdr->frame_control) &&
			     is_multicast_ether_addr(hdr->addr1);
		u8 idx = MT7996_BASIC_RATES_TBL;

		if (mlink) {
			if (mcast && mlink->mcast_rates_idx)
				idx = mlink->mcast_rates_idx;
			else if (beacon && mlink->beacon_rates_idx)
				idx = mlink->beacon_rates_idx;
			else
				idx = mlink->basic_rates_idx;
		}

		val = FIELD_PREP(MT_TXD6_TX_RATE, idx) | MT_TXD6_FIXED_BW;
		if (mcast)
			val |= MT_TXD6_DIS_MAT;

		if (dev->mt76.phys[band_idx]->cap.has_6ghz &&
		    dev->mt76.lpi_bcn_enhance &&
		    ieee80211_is_mgmt(hdr->frame_control))
			val |= FIELD_PREP(MT_TXD6_BW, FW_CDBW_80MHZ);

		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}

#ifdef CONFIG_NL80211_TESTMODE
	if (mlink) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		__le16 fc;
		bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
		struct mt76_phy *mphy = &dev->mt76.phy;
		struct mt7996_vif_link *link = (struct mt7996_vif_link *)mlink;

		if (!is_8023)
			fc = hdr->frame_control;

		if (mt76_testmode_enabled(mphy) ||
		    (link->msta_link.test.txo_active &&
		     /* Only do txo overrides for (larger) data frames, this
		      * generally allows connection mgt frames to pass but still
		      * lets us force the data packets we care about.
		      */
		     skb->len >= 400 &&
		     (is_8023 ||
		      ((ieee80211_is_data_qos(fc) || ieee80211_is_data(fc)) &&
		       (!(ieee80211_is_qos_nullfunc(fc) || ieee80211_is_nullfunc(fc)))))))
			mt7996_mac_write_txwi_tm(mphy->priv, wcid, txwi, skb);
	}
#endif
}

static bool
mt7996_tx_use_mgmt(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (ieee80211_is_mgmt(hdr->frame_control))
		return true;

	/* for SDO to bypass specific data frame */
	if (!mt7996_has_wa(dev)) {
		if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
			return true;

		if (ieee80211_has_a4(hdr->frame_control) &&
		    !ieee80211_is_data_present(hdr->frame_control))
			return true;
	}

	return false;
}

int mt7996_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta;
	struct mt7996_vif_link *mconf;
	struct mt76_connac_txp_common *txp;
	struct mt76_txwi_cache *t;
	int id, i, pid, nbuf = tx_info->nbuf - 1;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	u8 *txwi = (u8 *)txwi_ptr;
	u8 link_id;

	mtk_dbg(&dev->mt76, TXV, "mt7996-tx-prepare-skb, skb: %p skb-len: %d wcid: %p qid: %d\n",
		tx_info->skb, tx_info->skb->len, wcid, qid);

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	msta = sta ? (struct mt7996_sta *)sta->drv_priv : &mvif->sta;
	if ((is_8023 || ieee80211_is_data_qos(hdr->frame_control)) && sta && sta->mlo) {
		if (unlikely(tx_info->skb->protocol == cpu_to_be16(ETH_P_PAE))) {
			link_id = msta->deflink_id;
		} else {
			u8 tid = tx_info->skb->priority & IEEE80211_QOS_CTL_TID_MASK;

			link_id = (tid % 2) ? msta->sec_link : msta->deflink_id;
		}
	} else {
		link_id = u32_get_bits(info->control.flags, IEEE80211_TX_CTRL_MLO_LINK);

		if (link_id == IEEE80211_LINK_UNSPECIFIED || (sta && !sta->mlo))
			link_id = wcid->link_id;
	}

	if (link_id != wcid->link_id) {
		struct mt7996_sta_link *msta_link = rcu_dereference(msta->link[link_id]);

		if (msta_link)
			wcid = &msta_link->wcid;
	}

	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[wcid->link_id]);
	if (!mconf) {
		return -ENOLINK;
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;
	t->wcid = wcid->idx;

	id = mt76_token_consume(mdev, &t, mconf->mt76.band_idx);

	if (id == -EBUSY && sta && sta->mlo) {
		struct ieee80211_link_sta *link_sta;
		struct mt7996_vif_link *mconf_temp;
		unsigned int link_id_temp;

		/* For MLO station, try to get token from affiliated link */
		for_each_sta_active_link(vif, sta, link_sta, link_id_temp) {
			mconf_temp = (struct mt7996_vif_link *)
				     rcu_dereference(mvif->mt76.link[link_id_temp]);

			if (!mconf_temp)
				continue;

			id = mt76_token_consume(mdev, &t, mconf_temp->mt76.band_idx);
			if (id >= 0)
				break;
		}
	}


	if (id < 0) {
		mtk_dbg(&dev->mt76, TXV, "mt7996-tx-prepare-skb, token_consume error: %d\n",
			id);
		return id;
	}

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	memset(txwi_ptr, 0, MT_TXD_SIZE);
	/* Transmit non qos data by 802.11 header and need to fill txd by host*/
	if (!is_8023 || pid >= MT_PACKET_ID_FIRST)
		mt7996_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, key,
				      pid, qid, 0);

	/* Since the rules of HW MLD address translation are not fully compatible
	 * with 802.11 EAPOL frame, we do the translation by software
	 */
	if (unlikely(tx_info->skb->protocol == cpu_to_be16(ETH_P_PAE)) && sta->mlo) {
		struct ieee80211_bss_conf *conf;
		struct ieee80211_link_sta *link_sta;
		__le16 fc = hdr->frame_control;

		conf = rcu_dereference(vif->link_conf[wcid->link_id]);
		link_sta = rcu_dereference(sta->link[wcid->link_id]);
		if (!conf || !link_sta)
			return -ENOLINK;

		dma_sync_single_for_cpu(mdev->dma_dev, tx_info->buf[1].addr,
					tx_info->buf[1].len, DMA_TO_DEVICE);

		memcpy(hdr->addr1, link_sta->addr, ETH_ALEN);
		memcpy(hdr->addr2, conf->addr, ETH_ALEN);

		/* EAPOL's SA/DA need to be MLD address in MLO */
		if (ieee80211_has_a4(fc)) {
			memcpy(hdr->addr3, sta->addr, ETH_ALEN);
			memcpy(hdr->addr4, vif->addr, ETH_ALEN);
		} else if (ieee80211_has_tods(fc)) {
			memcpy(hdr->addr3, sta->addr, ETH_ALEN);
		} else if (ieee80211_has_fromds(fc)) {
			memcpy(hdr->addr3, vif->addr, ETH_ALEN);
		}

		dma_sync_single_for_device(mdev->dma_dev, tx_info->buf[1].addr,
					   tx_info->buf[1].len, DMA_TO_DEVICE);

		pr_info("EAPOL: a1=%pM, a2=%pM, a3=%pM\n", hdr->addr1, hdr->addr2, hdr->addr3);
	}

	txp = (struct mt76_connac_txp_common *)(txwi + MT_TXD_SIZE);
	for (i = 0; i < nbuf; i++) {
		u16 len;

		len = FIELD_PREP(MT_TXP_BUF_LEN, tx_info->buf[i + 1].len);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		len |= FIELD_PREP(MT_TXP_DMA_ADDR_H,
				  tx_info->buf[i + 1].addr >> 32);
#endif

		txp->fw.buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->fw.len[i] = cpu_to_le16(len);
	}
	txp->fw.nbuf = nbuf;

	txp->fw.flags = cpu_to_le16(MT_CT_INFO_FROM_HOST);

	if (!is_8023 || pid >= MT_PACKET_ID_FIRST)
		txp->fw.flags |= cpu_to_le16(MT_CT_INFO_APPLY_TXD);

	if (!key)
		txp->fw.flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (!is_8023 && mt7996_tx_use_mgmt(dev, tx_info->skb))
		txp->fw.flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	txp->fw.bss_idx = mconf->mt76.idx; // TODO:  Another place for bssid to be mis-configured?
	txp->fw.token = cpu_to_le16(id);
	txp->fw.rept_wds_wcid = cpu_to_le16(sta ? wcid->idx : 0xfff);

	tx_info->skb = NULL;

	/* pass partial skb header to fw */
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->buf[1].skip_unmap = true;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	return 0;
}

u32 mt7996_wed_init_buf(void *ptr, dma_addr_t phys, int token_id)
{
	struct mt76_connac_fw_txp *txp = ptr + MT_TXD_SIZE;
	__le32 *txwi = ptr;
	u32 val;

	memset(ptr, 0, MT_TXD_SIZE + sizeof(*txp));

	val = FIELD_PREP(MT_TXD0_TX_BYTES, MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CT);
	txwi[0] = cpu_to_le32(val);

	val = BIT(31) |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3);
	txwi[1] = cpu_to_le32(val);

	txp->token = cpu_to_le16(token_id);
	txp->nbuf = 1;
	txp->buf[0] = cpu_to_le32(phys + MT_TXD_SIZE + sizeof(*txp));

	return MT_TXD_SIZE + sizeof(*txp);
}

static void
mt7996_check_tx_ba_status(struct mt76_wcid *wcid, u8 tid)
{
	struct ieee80211_sta *sta;
	struct mt7996_sta *msta;
	struct ieee80211_link_sta *link_sta;

	if (!wcid)
		return;

	sta = wcid_to_sta(wcid);
	msta = (struct mt7996_sta *)sta->drv_priv;

	link_sta = rcu_dereference(sta->link[wcid->link_id]);
	if (!link_sta)
		return;

	if (!sta->mlo && !(link_sta->ht_cap.ht_supported || link_sta->he_cap.has_he))
		return;

	if (test_bit(tid, &wcid->ampdu_state)) {
		ieee80211_refresh_tx_agg_session_timer(sta, tid);
		return;
	}

	if (!msta->last_addba_req_time[tid] ||
	    time_after(jiffies, msta->last_addba_req_time[tid] + ADDBA_RETRY_PERIOD)) {
		set_bit(tid, &wcid->ampdu_state);
		if (ieee80211_start_tx_ba_session(sta, tid, 0) < 0)
			clear_bit(tid, &wcid->ampdu_state);
		msta->last_addba_req_time[tid] = jiffies;
	}
}

static void
mt7996_tx_check_aggr(struct ieee80211_sta *sta, struct sk_buff *skb,
		     struct mt76_wcid *wcid)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	u16 fc, tid;

	tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;

	if (is_8023) {
		fc = IEEE80211_FTYPE_DATA |
		     (sta->wme ? IEEE80211_STYPE_QOS_DATA : IEEE80211_STYPE_DATA);
	} else {
		/* No need to get precise TID for Action/Management Frame,
		 * since it will not meet the following Frame Control
		 * condition anyway.
		 */

		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

		fc = le16_to_cpu(hdr->frame_control) &
		     (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE);
	}

	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	mt7996_check_tx_ba_status(wcid, tid);
}

static void
mt7996_txwi_free(struct mt7996_dev *dev, struct mt76_txwi_cache *t,
		 struct ieee80211_sta *sta, struct mt76_wcid *wcid,
		 struct list_head *free_list,
		 u32 tx_cnt, u32 tx_status, u32 ampdu)
{
	struct mt76_dev *mdev = &dev->mt76;
	__le32 *txwi;
	u16 wcid_idx;
	struct ieee80211_tx_info *info;
	struct ieee80211_tx_rate *rate;
	struct mt7996_phy *phy = &dev->phy;
	struct mt76_mib_stats *mib = &phy->mib;

	mt76_connac_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	rcu_read_lock(); /* protect wcid access */

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		wcid_idx = wcid->idx;

		if (likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
			mt7996_tx_check_aggr(sta, t->skb, wcid);
	} else {
		wcid_idx = le32_get_bits(txwi[9], MT_TXD9_WLAN_IDX);
	}

	info = IEEE80211_SKB_CB(t->skb);

	rate = &info->status.rates[0];
	rate->idx = -1; /* will over-write below if we found wcid */
	info->status.rates[1].idx = -1; /* terminate rate list */

	/* force TX_STAT_AMPDU to be set, or mac80211 will ignore status */
	if (ampdu || (info->flags & IEEE80211_TX_CTL_AMPDU)) {
		info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_CTL_AMPDU;
		info->status.ampdu_len = 1;
	}

	/* update info status based on cached wcid rate info since
	 * txfree path doesn't give us a lot of info.
	 */
	if (wcid) {
		struct mt76_sta_stats *stats = &wcid->stats;
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(t->skb);

		if (wcid->rate.flags & RATE_INFO_FLAGS_MCS) {
			rate->flags |= IEEE80211_TX_RC_MCS;
			rate->idx = wcid->rate.mcs + wcid->rate.nss * 8;
		} else if (wcid->rate.flags & RATE_INFO_FLAGS_VHT_MCS) {
			rate->flags |= IEEE80211_TX_RC_VHT_MCS;
			rate->idx = (wcid->rate.nss << 4) | wcid->rate.mcs;
		} else if (wcid->rate.flags & RATE_INFO_FLAGS_HE_MCS) {
			rate->idx = (wcid->rate.nss << 4) | wcid->rate.mcs;
		} else {
			rate->idx = wcid->rate.mcs;
		}

		switch (wcid->rate.bw) {
		case RATE_INFO_BW_160:
			rate->flags |= IEEE80211_TX_RC_160_MHZ_WIDTH;
			break;
		case RATE_INFO_BW_80:
			rate->flags |= IEEE80211_TX_RC_80_MHZ_WIDTH;
			break;
		case RATE_INFO_BW_40:
			rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
			break;
		}

		stats->tx_attempts += tx_cnt;
		stats->tx_retries += tx_cnt - 1;

		mtk_dbg(&dev->mt76, TX, "mt7915-txwi-free, skb: %p skb->len: %d tx-cnt: %d  tx_status: 0x%x  txo: %d\n",
			t->skb, t->skb->len, tx_cnt, tx_status, !!(cb->flags & MT_TX_CB_TXO_USED));

		if (tx_status == 0) {
			stats->tx_mpdu_ok++;
			stats->tx_bytes += t->skb->len;
		} else {
			stats->tx_failed++;
		}

		if (cb->flags & MT_TX_CB_TXO_USED) {
			stats->txo_tx_mpdu_attempts += tx_cnt;

			if (tx_status == 0)
				stats->txo_tx_mpdu_ok++;
			else
				stats->txo_tx_mpdu_fail++;
		}
	}

	rcu_read_unlock();

	/* Apply the values that this txfree path reports */
	rate->count = tx_cnt;
	if (tx_status == 0) {
		mib->tx_pkts_nic++;
		mib->tx_bytes_nic += t->skb->len;
		info->flags |= IEEE80211_TX_STAT_ACK;
		info->status.ampdu_ack_len = 1;
	} else {
		info->flags &= ~IEEE80211_TX_STAT_ACK;
		info->status.ampdu_ack_len = 0;
	}

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);

out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}

static void
mt7996_mac_tx_free(struct mt7996_dev *dev, void *data, int len)
{
	__le32 *tx_free = (__le32 *)data, *cur_info;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *phy2 = mdev->phys[MT_BAND1];
	struct mt76_phy *phy3 = mdev->phys[MT_BAND2];
	struct mt7996_phy *mphy;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	struct mt76_wcid *wcid = NULL;
	LIST_HEAD(free_list);
	struct sk_buff *skb, *tmp;
	void *end = data + len;
	bool wake = false;
	u16 total, count = 0;
	u8 ver, status;
	enum {
		INVALID_WLAN_ID,
		INVALID_MSDU_ID,
	};

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);
	if (phy2) {
		mt76_queue_tx_cleanup(dev, phy2->q_tx[MT_TXQ_PSD], false);
		mt76_queue_tx_cleanup(dev, phy2->q_tx[MT_TXQ_BE], false);
	}
	if (phy3) {
		mt76_queue_tx_cleanup(dev, phy3->q_tx[MT_TXQ_PSD], false);
		mt76_queue_tx_cleanup(dev, phy3->q_tx[MT_TXQ_BE], false);
	}

	ver = le32_get_bits(tx_free[1], MT_TXFREE1_VER);
	if (WARN_ON_ONCE(ver < 5))
		return;

	total = le32_get_bits(tx_free[0], MT_TXFREE0_MSDU_CNT);
	for (cur_info = &tx_free[2]; count < total; cur_info++) {
		u32 msdu, info;
		u8 i;
		u32 tx_cnt, tx_status, ampdu;

		if (WARN_ON_ONCE((void *)cur_info >= end))
			return;
		/* 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		info = le32_to_cpu(*cur_info);
		if (info & MT_TXFREE_INFO_PAIR) {
			struct mt7996_sta *msta;
			unsigned long valid_links;
			unsigned int link_id;
			u16 idx;

			idx = FIELD_GET(MT_TXFREE_INFO_WLAN_ID, info);
			wcid = mt76_wcid_ptr(dev, idx);
			if (wcid)
				sta = wcid_to_sta(wcid);
			else
				sta = NULL;

			mtk_dbg(mdev, TXV, "mt7996-mac-tx-free, new wcid pair, idx: %d sta: %p wcid: %p\n",
				idx, sta, wcid);
			if (!sta)
				goto next;

			valid_links = sta->valid_links ?: BIT(0);
			msta = (struct mt7996_sta *)sta->drv_priv;
			/* for MLD STA, add all link's wcid to sta_poll_list */
			spin_lock_bh(&mdev->sta_poll_lock);
			for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
				struct mt7996_sta_link *msta_link =
					rcu_dereference(msta->link[link_id]);

				if (msta_link && list_empty(&msta_link->wcid.poll_list))
					list_add_tail(&msta_link->wcid.poll_list, &mdev->sta_poll_list);
			}
			spin_unlock_bh(&mdev->sta_poll_lock);
next:
			/* ver 7 has a new DW with pair = 1, skip it */
			if (ver == 7 && ((void *)(cur_info + 1) < end) &&
			    (le32_to_cpu(*(cur_info + 1)) & MT_TXFREE_INFO_PAIR))
				cur_info++;
			continue;
		} else if (info & MT_TXFREE_INFO_HEADER) {
			u32 tx_retries = 0, tx_failed = 0;

			if (!wcid)
				continue;

			tx_retries =
				FIELD_GET(MT_TXFREE_INFO_COUNT, info) - 1;
			tx_failed = tx_retries +
				!!FIELD_GET(MT_TXFREE_INFO_STAT, info);

			wcid->stats.tx_retries += tx_retries;
			wcid->stats.tx_failed += tx_failed;
			continue;
		}

		for (i = 0; i < 2; i++) {
			msdu = (info >> (15 * i)) & MT_TXFREE_INFO_MSDU_ID;
			if (msdu == MT_TXFREE_INFO_MSDU_ID)
				continue;

			/* TODO:  How to get tx_cnt, tx_status, ampdu*/
			tx_status = 0; /* For now, set txstatus=ok */
			tx_cnt = 1;
			ampdu = 1;

			count++;
			txwi = mt76_token_release(mdev, msdu, &wake);

			if (!txwi) {
				WARN_ON_ONCE(1);
				continue;
			}

			/* More educated tx_status guess, if possible */
			if (txwi->skb) {
				struct mt76_tx_cb *cb = mt76_tx_skb_cb(txwi->skb);
				struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(txwi->skb);

				/* More informed case, we have already done txs work previously */
				if ((cb->flags & MT_TX_CB_TXS_DONE)) {
					tx_status = (tx_info->flags & IEEE80211_TX_STAT_ACK)
						    ? 0  /* Previously ack'd, probably ok */
						    : 1; /* No ack, probably fail */
				}
			}

			mtk_dbg(mdev, TXV, "mt7996-mac-tx-free, msdu: %d, tx-cnt: %d  t_status: %d count: %d/%d\n",
				msdu, tx_cnt, tx_status, count, total);

			mt7996_txwi_free(dev, txwi, sta, wcid, &free_list, tx_cnt, tx_status, ampdu);
			mphy = __mt7996_phy(dev, txwi->phy_idx);
			if (!mphy)
				continue;

			switch (status) {
			case 1:
				mphy->hw_drop++;
				break;
			case 2:
				mphy->mcu_drop++;
				break;
			default:
				break;
			}
		}
	}

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	mt76_worker_schedule(&dev->mt76.tx_worker);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}
}

static bool
mt7996_mac_add_txs_skb(struct mt7996_dev *dev, struct mt76_wcid *wcid,
		       int pid, __le32 *txs_data)
{
	struct mt76_sta_stats *stats = &wcid->stats;
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy;
	struct ieee80211_tx_info *info = NULL;
	struct sk_buff_head list;
	struct rate_info rate = {};
	struct sk_buff *skb = NULL;
	bool cck = false;
	u32 txrate, txs, mode, stbc;
	u32 mcs_idx = 0;

	txs = le32_to_cpu(txs_data[0]);

	mt76_tx_status_lock(mdev, &list);

	/* only report MPDU TXS */
	if (le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT) == 0) {
		skb = mt76_tx_status_skb_get(mdev, wcid, pid, &list);
		if (skb) {
			struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
			struct mt7996_sta_link *msta_link =
				container_of(wcid, struct mt7996_sta_link, wcid);
			struct mt7996_vif *mvif;

			info = IEEE80211_SKB_CB(skb);
			if (!(txs & MT_TXS0_ACK_ERROR_MASK))
				info->flags |= IEEE80211_TX_STAT_ACK;

			info->status.ampdu_len = 1;
			info->status.ampdu_ack_len =
				!!(info->flags & IEEE80211_TX_STAT_ACK);

			info->status.rates[0].idx = -1;

			/* connection monitoring */
			if (msta_link && msta_link->sta)
				mvif = msta_link->sta->vif;
			if (ieee80211_is_nullfunc(hdr->frame_control) && mvif &&
			    mvif->probe[wcid->phy_idx] == (void *)skb &&
			    info->flags & IEEE80211_TX_STAT_ACK) {
				/* reset beacon monitoring */
				mvif->probe[wcid->phy_idx] = NULL;
				mvif->beacon_received_time[wcid->phy_idx] = jiffies;
				mvif->probe_send_count[wcid->phy_idx] = 0;
			}
		}
	}

	if (mtk_wed_device_active(&dev->mt76.mmio.wed) && wcid->sta &&
	    (wcid->tx_info & MT_WCID_TX_INFO_SET)) {
		/* Do not check TX BA status for mgmt frames which are sent at a
		 * fixed rate
		 */
		if (!le32_get_bits(txs_data[3], MT_TXS3_FIXED_RATE))
			mt7996_check_tx_ba_status(wcid, FIELD_GET(MT_TXS0_TID, txs));
	}

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;
	stbc = le32_get_bits(txs_data[3], MT_TXS3_RATE_STBC);

	mtk_dbg(&dev->mt76, TX, "mt7996-mac-add-txs-skb, tx-mcs: %d  tx-nss: %d skb: %p\n",
		rate.mcs, rate.nss, skb);

	if (stbc && rate.nss > 1)
		rate.nss >>= 1;

	if (rate.nss - 1 < ARRAY_SIZE(stats->tx_nss))
		stats->tx_nss[rate.nss - 1]++;
	if (rate.mcs < ARRAY_SIZE(stats->tx_mcs))
		mcs_idx = rate.mcs;

	mode = FIELD_GET(MT_TX_RATE_MODE, txrate);
	switch (mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		mphy = mt76_dev_phy(mdev, wcid->phy_idx);

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else if (mphy->chandef.chan->band == NL80211_BAND_6GHZ)
			sband = &mphy->sband_6g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate.mcs = mt76_get_rate(mphy->dev, sband, rate.mcs, cck);
		rate.legacy = sband->bitrates[rate.mcs].bitrate;
		if (info)
			info->status.rates[0].idx = rate.mcs;
		mcs_idx = rate.mcs;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate.mcs > 31)
			goto out;

		rate.flags = RATE_INFO_FLAGS_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;

		if (info) {
			info->status.rates[0].idx = rate.mcs;
			info->status.rates[0].flags |= IEEE80211_TX_RC_MCS;
		}
		mcs_idx = rate.mcs % 8;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate.mcs > 9)
			goto out;

		rate.flags = RATE_INFO_FLAGS_VHT_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;

		if (info) {
			info->status.rates[0].idx = (rate.nss << 4) | rate.mcs;
			info->status.rates[0].flags |= IEEE80211_TX_RC_VHT_MCS;
		}
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (rate.mcs > 11)
			goto out;

		rate.he_gi = wcid->rate.he_gi;
		rate.he_dcm = FIELD_GET(MT_TX_RATE_DCM, txrate);
		rate.flags = RATE_INFO_FLAGS_HE_MCS;
		if (info)
			info->status.rates[0].idx = (rate.nss << 4) | rate.mcs;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		if (rate.mcs > 13)
			goto out;

		rate.eht_gi = wcid->rate.eht_gi;
		rate.flags = RATE_INFO_FLAGS_EHT_MCS;
		if (info)
			info->status.rates[0].idx = (rate.nss << 4) | rate.mcs;
		break;
	default:
		goto out;
	}

	stats->tx_mcs[mcs_idx]++;
	stats->tx_mode[mode]++;

	switch (FIELD_GET(MT_TXS0_BW, txs)) {
	case IEEE80211_STA_RX_BW_320:
		rate.bw = RATE_INFO_BW_320;
		stats->tx_bw[4]++;
		break;
	case IEEE80211_STA_RX_BW_160:
		rate.bw = RATE_INFO_BW_160;
		stats->tx_bw[3]++;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate.bw = RATE_INFO_BW_80;
		stats->tx_bw[2]++;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate.bw = RATE_INFO_BW_40;
		stats->tx_bw[1]++;
		break;
	default:
		rate.bw = RATE_INFO_BW_20;
		stats->tx_bw[0]++;
		break;
	}
	wcid->rate = rate;

out:
	if (skb)
		mt76_tx_status_skb_done(mdev, skb, &list);
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

static void mt7996_mac_add_txs(struct mt7996_dev *dev, void *data)
{
	struct mt7996_sta_link *msta_link;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u8 pid;

	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);
	pid = le32_get_bits(txs_data[3], MT_TXS3_PID);

	mtk_dbg(&dev->mt76, TX, "mt7996-mac-add-txs, format: %d wcidx: %d  pid: %d\n",
		le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT), wcidx, pid);

	if (pid < MT_PACKET_ID_NO_SKB)
		return;

	rcu_read_lock();

	wcid = mt76_wcid_ptr(dev, wcidx);
	if (!wcid)
		goto out;

	mt7996_mac_add_txs_skb(dev, wcid, pid, txs_data);

	if (!wcid->sta)
		goto out;

	msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
	mt76_wcid_add_poll(&dev->mt76, &msta_link->wcid);

out:
	rcu_read_unlock();
}

bool mt7996_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	if (type != PKT_TYPE_NORMAL) {
		u32 sw_type = le32_get_bits(rxd[0], MT_RXD0_SW_PKT_TYPE_MASK);

		if (unlikely((sw_type & MT_RXD0_SW_PKT_TYPE_MAP) ==
			     MT_RXD0_SW_PKT_TYPE_FRAME))
			return true;
	}

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7996_mac_tx_free(dev, data, len);
		return false;
	case PKT_TYPE_TXS:
		for (rxd += MT_TXS_HDR_SIZE; rxd + MT_TXS_SIZE <= end; rxd += MT_TXS_SIZE)
			mt7996_mac_add_txs(dev, rxd);
		return false;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7996_debugfs_rx_fw_monitor(dev, data, len);
		return false;
	case PKT_TYPE_RX_EVENT:
	case PKT_TYPE_NORMAL:
		/* These are handled elsewhere, do not warn about them. */
		return true;
	default:
		mtk_dbg(mdev, MSG, "mt7996-rx-check, pkt-type: %d not handled.\n", type);
		return true;
	}
}

void mt7996_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;
	int len;

	/* drop the skb when rxd is corrupted */
	len = le32_get_bits(rxd[0], MT_RXD0_LENGTH);
	if (unlikely(len != skb->len))
		goto drop;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	if (type != PKT_TYPE_NORMAL) {
		u32 sw_type = le32_get_bits(rxd[0], MT_RXD0_SW_PKT_TYPE_MASK);

		if (unlikely((sw_type & MT_RXD0_SW_PKT_TYPE_MAP) ==
			     MT_RXD0_SW_PKT_TYPE_FRAME))
			type = PKT_TYPE_NORMAL;
	}

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2) &&
		    q == MT_RXQ_TXFREE_BAND2) {
			dev_kfree_skb(skb);
			break;
		}

		mt7996_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7996_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += MT_TXS_HDR_SIZE; rxd + MT_TXS_SIZE <= end; rxd += MT_TXS_SIZE)
			mt7996_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7996_debugfs_rx_fw_monitor(dev, skb->data, skb->len);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL:
		if (!mt7996_mac_fill_rx(dev, q, skb, info)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		mtk_dbg(mdev, MSG, "mt7996-mac-queue-rx-skb, unhandled type: %d\n", type);
		goto drop;
	}

	return;

drop:
	dev_kfree_skb(skb);
}

void mt7996_mac_cca_stats_reset(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	u32 reg = MT_WF_PHYRX_BAND_RX_CTRL1(phy->mt76->band_idx);

	mt76_clear(dev, reg, MT_WF_PHYRX_BAND_RX_CTRL1_STSCNT_EN);
	mt76_set(dev, reg, BIT(11) | BIT(9));
}

void mt7996_mac_reset_counters(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	int i;

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_AGG_CNT(band_idx, i));

	phy->mt76->survey_time = ktime_get_boottime();

	memset(phy->mt76->aggr_stats, 0, sizeof(phy->mt76->aggr_stats));

	/* reset airtime counters */
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(band_idx),
		 MT_WF_RMAC_MIB_RXTIME_CLR);

	mt7996_mcu_get_chan_mib_info(phy, true);
}

void mt7996_mac_set_coverage_class(struct mt7996_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_phy *phy2 = mt7996_phy2(dev);
	struct mt7996_phy *phy3 = mt7996_phy3(dev);
	u32 reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	u8 band_idx = phy->mt76->band_idx;
	int offset;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (phy2)
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       phy2->coverage_class);

	if (phy3)
		coverage_class = max_t(s16, coverage_class,
				       phy3->coverage_class);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(band_idx), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(band_idx), ofdm + reg_offset);
}

void mt7996_mac_enable_nf(struct mt7996_dev *dev, u8 band)
{
	mt76_set(dev, MT_WF_PHYRX_CSD_BAND_RXTD12(band),
		 MT_WF_PHYRX_CSD_BAND_RXTD12_IRPI_SW_CLR_ONLY |
		 MT_WF_PHYRX_CSD_BAND_RXTD12_IRPI_SW_CLR);

	mt76_set(dev, MT_WF_PHYRX_BAND_RX_CTRL1(band),
		 FIELD_PREP(MT_WF_PHYRX_BAND_RX_CTRL1_IPI_EN, 0x5));
}

static u8
mt7996_phy_get_nf(struct mt7996_phy *phy, u8 band_idx)
{
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	struct mt7996_dev *dev = phy->dev;
	u32 val, sum = 0, n = 0;
	int ant, i;

	for (ant = 0; ant < hweight8(phy->mt76->antenna_mask); ant++) {
		u32 reg = MT_WF_PHYRX_CSD_IRPI(band_idx, ant);

		for (i = 0; i < ARRAY_SIZE(nf_power); i++, reg += 4) {
			val = mt76_rr(dev, reg);
			sum += val * nf_power[i];
			n += val;
		}
	}

	return n ? sum / n : 0;
}

void mt7996_update_channel(struct mt76_phy *mphy)
{
	struct mt7996_phy *phy = mphy->priv;
	struct mt76_channel_state *state = mphy->chan_state;
	int nf;

	mt7996_mcu_get_chan_mib_info(phy, false);

	nf = mt7996_phy_get_nf(phy, mphy->band_idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state->noise = -(phy->noise >> 4);
}

static bool
mt7996_wait_reset_state(struct mt7996_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->recovery.state) & state),
				 MT7996_RESET_TIMEOUT);

	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7996_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long update = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);
	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		for_each_set_bit(link_id, &update, IEEE80211_MLD_MAX_NUM_LINKS) {
			struct ieee80211_bss_conf *conf =
					link_conf_dereference_protected(vif, link_id);

			mt7996_mcu_add_beacon(hw, vif, conf, conf->enable_beacon);
		}
		break;
	default:
		break;
	}
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_update_beacons(struct mt7996_dev *dev)
{
	struct mt76_phy *phy2, *phy3;

	ieee80211_iterate_active_interfaces(dev->mt76.hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7996_update_vif_beacon, dev->mt76.hw);

	phy2 = dev->mt76.phys[MT_BAND1];
	if (!phy2)
		return;

	ieee80211_iterate_active_interfaces(phy2->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7996_update_vif_beacon, phy2->hw);

	phy3 = dev->mt76.phys[MT_BAND2];
	if (!phy3)
		return;

	ieee80211_iterate_active_interfaces(phy3->hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7996_update_vif_beacon, phy3->hw);
}

void mt7996_tx_token_put(struct mt7996_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id) {
		struct mt76_phy *phy = mt76_dev_phy(&dev->mt76, txwi->phy_idx);

		mt7996_txwi_free(dev, txwi, NULL, NULL, NULL, 0, 1, 0);
		dev->mt76.token_count--;
		phy->tokens--;
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}

static int
mt7996_mac_restart(struct mt7996_dev *dev)
{
	struct mt7996_phy *phy2, *phy3;
	struct mt76_dev *mdev = &dev->mt76;
	int i, ret;

	phy2 = mt7996_phy2(dev);
	phy3 = mt7996_phy3(dev);

	if (dev->hif2) {
		mt76_wr(dev, MT_INT1_MASK_CSR, 0x0);
		mt76_wr(dev, MT_INT1_SOURCE_CSR, ~0);
	}

	if (dev_is_pci(mdev->dev)) {
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0x0);
		if (dev->hif2)
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0x0);
	}

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	if (phy2)
		set_bit(MT76_RESET, &phy2->mt76->state);
	if (phy3)
		set_bit(MT76_RESET, &phy3->mt76->state);

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);
	if (phy2)
		mt76_txq_schedule_all(phy2->mt76);
	if (phy3)
		mt76_txq_schedule_all(phy3->mt76);

	/* disable all tx/rx napi */
	mt76_worker_disable(&dev->mt76.tx_worker);
	mt76_for_each_q_rx(mdev, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt76_queue_is_wed_rro(&mdev->q_rx[i]))
			continue;

		if (mdev->q_rx[i].ndesc)
			napi_disable(&dev->mt76.napi[i]);
	}
	napi_disable(&dev->mt76.tx_napi);

	/* token reinit */
	mt7996_tx_token_put(dev);
	idr_init(&dev->mt76.token);

	mt7996_dma_reset(dev, true);

	mt76_for_each_q_rx(mdev, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt76_queue_is_wed_rro(&mdev->q_rx[i]))
			continue;

		if (mdev->q_rx[i].ndesc) {
			napi_enable(&dev->mt76.napi[i]);
			local_bh_disable();
			napi_schedule(&dev->mt76.napi[i]);
			local_bh_enable();
		}
	}
	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);

	mt76_wr(dev, MT_INT_MASK_CSR, dev->mt76.mmio.irqmask);
	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);
	if (dev->hif2) {
		mt76_wr(dev, MT_INT1_MASK_CSR, dev->mt76.mmio.irqmask);
		mt76_wr(dev, MT_INT1_SOURCE_CSR, ~0);
	}
	if (dev_is_pci(mdev->dev)) {
		mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);
		if (dev->hif2)
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
	}

	/* load firmware */
	ret = mt7996_mcu_init_firmware(dev);
	if (ret)
		goto out;

	/* set the necessary init items */
	ret = mt7996_mcu_set_eeprom(dev);
	if (ret)
		goto out;

	mt7996_mac_init(dev);
	mt7996_init_txpower(&dev->phy);
	mt7996_init_txpower(phy2);
	mt7996_init_txpower(phy3);
	ret = mt7996_txbf_init(dev);

	if (test_bit(MT76_STATE_RUNNING, &dev->mphy.state)) {
		ret = mt7996_run(&dev->phy);
		if (ret)
			goto out;
	}

	if (phy2 && test_bit(MT76_STATE_RUNNING, &phy2->mt76->state)) {
		ret = mt7996_run(phy2);
		if (ret)
			goto out;
	}

	if (phy3 && test_bit(MT76_STATE_RUNNING, &phy3->mt76->state)) {
		ret = mt7996_run(phy3);
		if (ret)
			goto out;
	}

out:
	/* reset done */
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);
	if (phy3)
		clear_bit(MT76_RESET, &phy3->mt76->state);

	napi_enable(&dev->mt76.tx_napi);
	local_bh_disable();
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	mt76_worker_enable(&dev->mt76.tx_worker);
	return ret;
}

static void
mt7996_mac_full_reset(struct mt7996_dev *dev)
{
	struct mt7996_phy *phy2, *phy3;
	int i;

	phy2 = mt7996_phy2(dev);
	phy3 = mt7996_phy3(dev);
	dev->recovery.hw_full_reset = true;

	ieee80211_stop_queues(mt76_hw(dev));
	if (phy2)
		ieee80211_stop_queues(phy2->mt76->hw);
	if (phy3)
		ieee80211_stop_queues(phy3->mt76->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	if (phy2) {
		set_bit(MT76_RESET, &phy2->mt76->state);
		set_bit(MT76_MCU_RESET, &phy2->mt76->state);
	}
	if (phy3) {
		set_bit(MT76_RESET, &phy3->mt76->state);
		set_bit(MT76_MCU_RESET, &phy3->mt76->state);
	}

	cancel_work_sync(&dev->wed_rro.work);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	if (phy2)
		cancel_delayed_work_sync(&phy2->mt76->mac_work);
	if (phy3)
		cancel_delayed_work_sync(&phy3->mt76->mac_work);
	cancel_delayed_work_sync(&dev->scs_work);

	mutex_lock(&dev->mt76.mutex);
	for (i = 0; i < 10; i++) {
		if (!mt7996_mac_restart(dev))
			break;
	}
	mutex_unlock(&dev->mt76.mutex);

	if (i == 10)
		dev_err(dev->mt76.dev, "chip full reset failed\n");

	ieee80211_restart_hw(mt76_hw(dev));
	if (phy2)
		ieee80211_restart_hw(phy2->mt76->hw);
	if (phy3)
		ieee80211_restart_hw(phy3->mt76->hw);

	ieee80211_wake_queues(mt76_hw(dev));
	if (phy2)
		ieee80211_wake_queues(phy2->mt76->hw);
	if (phy3)
		ieee80211_wake_queues(phy3->mt76->hw);

	dev->recovery.hw_full_reset = false;
	ieee80211_queue_delayed_work(mt76_hw(dev),
				     &dev->mphy.mac_work,
				     MT7996_WATCHDOG_TIME);
	if (phy2)
		ieee80211_queue_delayed_work(phy2->mt76->hw,
					     &phy2->mt76->mac_work,
					     MT7996_WATCHDOG_TIME);
	if (phy3)
		ieee80211_queue_delayed_work(phy3->mt76->hw,
					     &phy3->mt76->mac_work,
					     MT7996_WATCHDOG_TIME);
	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->scs_work, HZ);
}

void mt7996_mac_reset_work(struct work_struct *work)
{
	struct mt7996_phy *phy2, *phy3;
	struct mt7996_dev *dev;
	int i;

	dev = container_of(work, struct mt7996_dev, reset_work);
	phy2 = mt7996_phy2(dev);
	phy3 = mt7996_phy3(dev);

	/* chip full reset */
	if (dev->recovery.restart) {
		/* disable WA/WM WDT */
		mt76_clear(dev, MT_WFDMA0_MCU_HOST_INT_ENA,
			   MT_MCU_CMD_WDT_MASK);

		if (READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WA_WDT)
			dev->recovery.wa_reset_count++;
		else
			dev->recovery.wm_reset_count++;

		mt7996_mac_full_reset(dev);

		/* enable mcu irq */
		mt7996_irq_enable(dev, MT_INT_MCU_CMD);
		mt7996_irq_disable(dev, 0);

		/* enable WA/WM WDT */
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA, MT_MCU_CMD_WDT_MASK);

		dev->recovery.state = MT_MCU_CMD_NORMAL_STATE;
		dev->recovery.restart = false;
		return;
	}

	if (!(READ_ONCE(dev->recovery.state) & MT_MCU_CMD_STOP_DMA))
		return;

	dev->recovery.l1_reset_last = dev->recovery.l1_reset;
	dev_info(dev->mt76.dev,"\n%s L1 SER recovery start.",
		 wiphy_name(dev->mt76.hw->wiphy));

	ieee80211_stop_queues(mt76_hw(dev));
	if (phy2)
		ieee80211_stop_queues(phy2->mt76->hw);
	if (phy3)
		ieee80211_stop_queues(phy3->mt76->hw);

	dev_info(dev->mt76.dev,"%s L1 SER queue stop done.",
		 wiphy_name(dev->mt76.hw->wiphy));

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);

	if (phy2)
		set_bit(MT76_RESET, &phy2->mt76->state);
	if (phy3)
		set_bit(MT76_RESET, &phy3->mt76->state);
	wake_up(&dev->mt76.mcu.wait);

	mt76_worker_disable(&dev->mt76.tx_worker);

	dev_info(dev->mt76.dev,"%s L1 SER disable tx_work done.",
		 wiphy_name(dev->mt76.hw->wiphy));

	mt76_for_each_q_rx(&dev->mt76, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt76_queue_is_wed_rro(&dev->mt76.q_rx[i]))
			continue;

		napi_disable(&dev->mt76.napi[i]);
	}
	napi_disable(&dev->mt76.tx_napi);

	mutex_lock(&dev->mt76.mutex);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_STOPPED);

	dev_info(dev->mt76.dev,"%s L1 SER dma stop done.",
		 wiphy_name(dev->mt76.hw->wiphy));

	if (mt7996_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7996_dma_reset(dev, false);

		mt7996_tx_token_put(dev);

		dev_info(dev->mt76.dev,"%s L1 SER token put done.",
			wiphy_name(dev->mt76.hw->wiphy));

		idr_init(&dev->mt76.token);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_INIT);
		mt7996_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7996_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	/* enable DMA Tx/Tx and interrupt */
	mt7996_dma_start(dev, false, false);

	dev_info(dev->mt76.dev,"%s L1 SER dma start done.",
		 wiphy_name(dev->mt76.hw->wiphy));

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		u32 wed_irq_mask = MT_INT_RRO_RX_DONE | MT_INT_TX_DONE_BAND2 |
				   dev->mt76.mmio.irqmask;

		if (mtk_wed_get_rx_capa(&dev->mt76.mmio.wed))
			wed_irq_mask &= ~MT_INT_RX_DONE_RRO_IND;

		mt76_wr(dev, MT_INT_MASK_CSR, wed_irq_mask);

		mtk_wed_device_start_hw_rro(&dev->mt76.mmio.wed, wed_irq_mask,
					    true);
		mt7996_irq_enable(dev, wed_irq_mask);
		mt7996_irq_disable(dev, 0);
	}

	if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2)) {
		mt76_wr(dev, MT_INT_PCIE1_MASK_CSR, MT_INT_TX_RX_DONE_EXT);
		mtk_wed_device_start(&dev->mt76.mmio.wed_hif2,
				     MT_INT_TX_RX_DONE_EXT);
	}

	dev_info(dev->mt76.dev,"%s L1 SER wed start done.",
		 wiphy_name(dev->mt76.hw->wiphy));

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);
	if (phy3)
		clear_bit(MT76_RESET, &phy3->mt76->state);

	mt76_for_each_q_rx(&dev->mt76, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt76_queue_is_wed_rro(&dev->mt76.q_rx[i]))
			continue;

		napi_enable(&dev->mt76.napi[i]);
		local_bh_disable();
		napi_schedule(&dev->mt76.napi[i]);
		local_bh_enable();
	}

	tasklet_schedule(&dev->mt76.irq_tasklet);

	mt76_worker_enable(&dev->mt76.tx_worker);

	napi_enable(&dev->mt76.tx_napi);
	local_bh_disable();
	napi_schedule(&dev->mt76.tx_napi);
	local_bh_enable();

	ieee80211_wake_queues(mt76_hw(dev));
	if (phy2)
		ieee80211_wake_queues(phy2->mt76->hw);
	if (phy3)
		ieee80211_wake_queues(phy3->mt76->hw);

	mt7996_update_beacons(dev);

	dev_info(dev->mt76.dev,"\n%s L1 SER recovery completed.",
		 wiphy_name(dev->mt76.hw->wiphy));
}

/* firmware coredump */
static void mt7996_mac_fw_coredump(struct mt7996_dev *dev, u8 type)
{
	const struct mt7996_mem_region *mem_region;
	struct mt7996_crash_data *crash_data;
	struct mt7996_mem_hdr *hdr;
	size_t buf_len;
	int i;
	u32 num;
	u8 *buf;

	mutex_lock(&dev->dump_mutex);

	crash_data = mt7996_coredump_new(dev, type);
	if (!crash_data) {
		mutex_unlock(&dev->dump_mutex);
		return;
	}

	mem_region = mt7996_coredump_get_mem_layout(dev, type, &num);
	if (!mem_region || !crash_data->memdump_buf_len) {
		mutex_unlock(&dev->dump_mutex);
		goto skip_memdump;
	}

	buf = crash_data->memdump_buf;
	buf_len = crash_data->memdump_buf_len;

	/* dumping memory content... */
	dev_info(dev->mt76.dev, "%s start coredump for %s\n",
		 wiphy_name(dev->mt76.hw->wiphy),
		 ((type == MT7996_RAM_TYPE_WA) ? "WA" : "WM"));
	memset(buf, 0, buf_len);
	for (i = 0; i < num; i++) {
		if (mem_region->len > buf_len) {
			dev_warn(dev->mt76.dev, "%s len %zu is too large\n",
				 mem_region->name, mem_region->len);
			break;
		}

		/* reserve space for the header */
		hdr = (void *)buf;
		buf += sizeof(*hdr);
		buf_len -= sizeof(*hdr);

		mt7996_memcpy_fromio(dev, buf, mem_region->start,
				     mem_region->len);

		strscpy(hdr->name, mem_region->name, sizeof(hdr->name));
		hdr->start = mem_region->start;
		hdr->len = mem_region->len;

		if (!mem_region->len)
			/* note: the header remains, just with zero length */
			break;

		buf += mem_region->len;
		buf_len -= mem_region->len;

		mem_region++;
	}

	mutex_unlock(&dev->dump_mutex);

skip_memdump:
	 mt7996_coredump_submit(dev, type);
}


void mt7996_mac_dump_work(struct work_struct *work)
{
	struct mt7996_dev *dev;

	dev = container_of(work, struct mt7996_dev, dump_work);
	if (dev->dump_state == MT7996_COREDUMP_MANUAL_WA ||
	    READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WA_WDT)
		mt7996_mac_fw_coredump(dev, MT7996_RAM_TYPE_WA);

	if (dev->dump_state == MT7996_COREDUMP_MANUAL_WM ||
	    READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WM_WDT)
		mt7996_mac_fw_coredump(dev, MT7996_RAM_TYPE_WM);

	if (READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WDT_MASK)
		queue_work(dev->mt76.wq, &dev->reset_work);

	dev->dump_state = MT7996_COREDUMP_IDLE;
}

void mt7996_coredump(struct mt7996_dev *dev, u8 state)
{
	if (state == MT7996_COREDUMP_IDLE ||
	    state >= __MT7996_COREDUMP_TYPE_MAX)
		return;

	if (dev->dump_state != MT7996_COREDUMP_IDLE)
		return;

	dev->dump_state = state;
	dev_info(dev->mt76.dev, "%s attempting grab coredump\n",
		 wiphy_name(dev->mt76.hw->wiphy));

	queue_work(dev->mt76.wq, &dev->dump_work);
}

void mt7996_reset(struct mt7996_dev *dev)
{
	dev_info(dev->mt76.dev, "%s SER recovery state: 0x%08x\n",
		 wiphy_name(dev->mt76.hw->wiphy), READ_ONCE(dev->recovery.state));

	if (!dev->recovery.hw_init_done)
		return;

	if (dev->recovery.hw_full_reset)
		return;

	/* wm/wa exception: do full recovery */
	if (READ_ONCE(dev->recovery.state) & MT_MCU_CMD_WDT_MASK) {
		dev->recovery.restart = true;
		dev_info(dev->mt76.dev,
			 "%s indicated firmware crash, attempting recovery\n",
			 wiphy_name(dev->mt76.hw->wiphy));

		mt7996_irq_disable(dev, MT_INT_MCU_CMD);
		queue_work(dev->mt76.wq, &dev->dump_work);
		mt7996_coredump(dev, MT7996_COREDUMP_AUTO);
		return;
	}

	if ((READ_ONCE(dev->recovery.state) & MT_MCU_CMD_STOP_DMA))
		dev->recovery.l1_reset++;

	queue_work(dev->mt76.wq, &dev->reset_work);
	wake_up(&dev->reset_wait);
}

void mt7996_mac_update_stats(struct mt7996_phy *phy)
{
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	u32 cnt;
	int i;

	cnt = mt76_rr(dev, MT_MIB_RSCR1(band_idx));
	mib->fcs_err_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR33(band_idx));
	mib->rx_fifo_full_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR31(band_idx));
	mib->rx_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR6(band_idx));
	mib->channel_idle_cnt += FIELD_GET(MT_MIB_SDR6_CHANNEL_IDL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_RVSR0(band_idx));
	mib->rx_vector_mismatch_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR35(band_idx));
	mib->rx_delimiter_fail_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR36(band_idx));
	mib->rx_len_mismatch_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR0(band_idx));
	mib->tx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR2(band_idx));
	mib->tx_stop_q_empty_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR3(band_idx));
	mib->tx_mpdu_attempts_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR4(band_idx));
	mib->tx_mpdu_success_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR27(band_idx));
	mib->rx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR28(band_idx));
	mib->rx_ampdu_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR29(band_idx));
	mib->rx_ampdu_valid_subframe_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RSCR30(band_idx));
	mib->rx_ampdu_valid_subframe_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR27(band_idx));
	mib->tx_rwp_fail_cnt += FIELD_GET(MT_MIB_SDR27_TX_RWP_FAIL_CNT, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR28(band_idx));
	mib->tx_rwp_need_cnt += FIELD_GET(MT_MIB_SDR28_TX_RWP_NEED_CNT, cnt);

	cnt = mt76_rr(dev, MT_UMIB_RPDCR(band_idx));
	mib->rx_pfdrop_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_RVSR1(band_idx));
	mib->rx_vec_queue_overflow_drop_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR1(band_idx));
	mib->rx_ba_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR0(band_idx));
	mib->tx_bf_ebf_ppdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR1(band_idx));
	mib->tx_bf_ibf_ppdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR2(band_idx));
	mib->tx_mu_bf_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR5(band_idx));
	mib->tx_mu_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR6(band_idx));
	mib->tx_mu_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_TSCR7(band_idx));
	mib->tx_su_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR3(band_idx));
	mib->tx_bf_rx_fb_ht_cnt += cnt;
	mib->tx_bf_rx_fb_all_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR4(band_idx));
	mib->tx_bf_rx_fb_vht_cnt += cnt;
	mib->tx_bf_rx_fb_all_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR5(band_idx));
	mib->tx_bf_rx_fb_he_cnt += cnt;
	mib->tx_bf_rx_fb_all_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR6(band_idx));
	mib->tx_bf_rx_fb_eht_cnt += cnt;
	mib->tx_bf_rx_fb_all_cnt += cnt;

	cnt = mt76_rr(dev, MT_ETBF_RX_FB_CONT(band_idx));
	mib->tx_bf_rx_fb_bw = FIELD_GET(MT_ETBF_RX_FB_BW, cnt);
	mib->tx_bf_rx_fb_nc_cnt += FIELD_GET(MT_ETBF_RX_FB_NC, cnt);
	mib->tx_bf_rx_fb_nr_cnt += FIELD_GET(MT_ETBF_RX_FB_NR, cnt);

	cnt = mt76_rr(dev, MT_MIB_BSCR7(band_idx));
	mib->tx_bf_fb_trig_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_BSCR17(band_idx));
	mib->tx_bf_fb_cpl_cnt += cnt;

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		cnt = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += cnt;
		mib->tx_amsdu_cnt += cnt;
	}

	/* rts count */
	cnt = mt76_rr(dev, MT_MIB_BTSCR5(band_idx));
	mib->rts_cnt += cnt;

	/* rts retry count */
	cnt = mt76_rr(dev, MT_MIB_BTSCR6(band_idx));
	mib->rts_retries_cnt += cnt;

	/* ba miss count */
	cnt = mt76_rr(dev, MT_MIB_BTSCR0(band_idx));
	mib->ba_miss_cnt += cnt;

	/* ack fail count */
	cnt = mt76_rr(dev, MT_MIB_BFTFCR(band_idx));
	mib->ack_fail_cnt += cnt;

	for (i = 0; i < 16; i++) {
		cnt = mt76_rr(dev, MT_TX_AGG_CNT(band_idx, i));
		phy->mt76->aggr_stats[i] += cnt;
	}
}

void mt7996_set_wireless_amsdu(struct ieee80211_hw *hw, u8 en)
{
	if (en)
		ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	else
		ieee80211_hw_clear(hw, SUPPORTS_AMSDU_IN_AMPDU);
}

void mt7996_mac_sta_rc_work(struct work_struct *work)
{
	struct mt7996_dev *dev = container_of(work, struct mt7996_dev, rc_work);
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct mt76_vif_link *mlink;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct mt7996_vif *mvif;
	LIST_HEAD(list);
	u32 changed;
	u8 link_id;

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	list_splice_init(&dev->sta_rc_list, &list);

	while (!list_empty(&list)) {
		msta_link = list_first_entry(&list, struct mt7996_sta_link,
					     rc_list);
		list_del_init(&msta_link->rc_list);

		changed = msta_link->changed;
		msta_link->changed = 0;
		sta = wcid_to_sta(&msta_link->wcid);
		link_id = msta_link->wcid.link_id;
		mvif = msta_link->sta->vif;
		vif = container_of((void *)mvif, struct ieee80211_vif,
				   drv_priv);

		mlink = rcu_dereference(mvif->mt76.link[link_id]);
		if (!mlink)
			continue;

		spin_unlock_bh(&dev->mt76.sta_poll_lock);

		link = (struct mt7996_vif_link *)mlink;

		if (changed & IEEE80211_RC_VHT_OMN_CHANGED)
			mt7996_mcu_set_fixed_field(dev, msta_link->sta, NULL,
						   msta_link->wcid.link_id,
						   RATE_PARAM_VHT_OMN_UPDATE);
		else if (changed & (IEEE80211_RC_SUPP_RATES_CHANGED |
				    IEEE80211_RC_NSS_CHANGED |
				    IEEE80211_RC_BW_CHANGED))
			mt7996_mcu_add_rate_ctrl(dev, msta_link->sta, vif,
						 msta_link->wcid.link_id,
						 true);

		if (changed & IEEE80211_RC_SMPS_CHANGED)
			mt7996_mcu_set_fixed_field(dev, msta_link->sta, NULL,
						   msta_link->wcid.link_id,
						   RATE_PARAM_MMPS_UPDATE);

		spin_lock_bh(&dev->mt76.sta_poll_lock);
	}

	spin_unlock_bh(&dev->mt76.sta_poll_lock);
}

void mt7996_mac_work(struct work_struct *work)
{
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;
	struct mt76_dev *mdev;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;
	mdev = mphy->dev;

	mutex_lock(&mdev->mutex);

	mt76_update_survey(mphy);

	/* this method is called about every 100ms.  Some pkt counters are 16-bit,
	 * so poll every 200ms to keep overflows at a minimum.
	 */
	if (++mphy->mac_work_count == 2) {
		mphy->mac_work_count = 0;
		int i;

		mt7996_mac_update_stats(phy);

		/* Update DEV-wise information only in
		 * the MAC work of the first band running.
		 */
		for (i = MT_BAND0; i <= mphy->band_idx; ++i) {
			if (i == mphy->band_idx) {
				mt7996_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_RATE);
				mt7996_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_AIR_TIME);
				mt7996_mac_sta_poll(mdev);
				// if (mtk_wed_device_active(&mdev->mmio.wed)) {
					mt7996_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_ADM_STAT);
					mt7996_mcu_get_all_sta_info(mdev, UNI_ALL_STA_TXRX_MSDU_COUNT);
				// }
				mt7996_mcu_get_all_sta_info(mdev, UNI_ALL_STA_RX_MPDU_COUNT);

				mt7996_mcu_get_bss_acq_pkt_cnt(phy->dev);

				if (mphy->mac_work_count == 100) {
					//if (phy->dev->idxlog_enable && mt7996_mcu_fw_time_sync(mdev))
					//	dev_err(mdev->dev, "Failed to synchronize time with FW.\n");
					mphy->mac_work_count = 0;
				}
			} else if (mt7996_band_valid(phy->dev, i) &&
			           test_bit(MT76_STATE_RUNNING, &mdev->phys[i]->state))
				break;
		}
	}

	mutex_unlock(&mdev->mutex);

	mt76_tx_status_check(mdev, false);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7996_WATCHDOG_TIME);
}

static void mt7996_dfs_stop_radar_detector(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	int rdd_idx = mt7996_get_rdd_idx(phy, false);

	if (rdd_idx < 0)
		return;

	mt7996_mcu_rdd_cmd(dev, RDD_STOP, rdd_idx, 0);
}

static int mt7996_dfs_start_rdd(struct mt7996_dev *dev, int rdd_idx)
{
	int err, region;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		region = 0;
		break;
	case NL80211_DFS_JP:
		region = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		region = 1;
		break;
	}

	err = mt7996_mcu_rdd_cmd(dev, RDD_START, rdd_idx, region);
	if (err < 0)
		return err;

	return mt7996_mcu_rdd_cmd(dev, RDD_DET_MODE, rdd_idx, 1);
}

static int mt7996_dfs_start_radar_detector(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	int err, rdd_idx;

	rdd_idx = mt7996_get_rdd_idx(phy, false);
	if (rdd_idx < 0)
		return -EINVAL;

	/* start CAC */
	err = mt7996_mcu_rdd_cmd(dev, RDD_CAC_START, rdd_idx, 0);
	if (err < 0)
		return err;

	err = mt7996_dfs_start_rdd(dev, rdd_idx);

	return err;
}

static int
mt7996_dfs_init_radar_specs(struct mt7996_phy *phy)
{
	const struct mt7996_dfs_radar_spec *radar_specs;
	struct mt7996_dev *dev = phy->dev;
	int err, i;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		err = mt7996_mcu_set_fcc5_lpn(dev, 8);
		if (err < 0)
			return err;
		break;
	case NL80211_DFS_ETSI:
		radar_specs = &etsi_radar_specs;
		break;
	case NL80211_DFS_JP:
		radar_specs = &jp_radar_specs;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(radar_specs->radar_pattern); i++) {
		err = mt7996_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7996_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7996_dfs_init_radar_detector(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	enum mt76_dfs_state dfs_state, prev_state;
	int err, rdd_idx = mt7996_get_rdd_idx(phy, false);

	prev_state = phy->mt76->dfs_state;
	dfs_state = mt76_phy_dfs_state(phy->mt76);

	if (prev_state == dfs_state || rdd_idx < 0)
		return 0;

	if (prev_state == MT_DFS_STATE_UNKNOWN)
		mt7996_dfs_stop_radar_detector(phy);

	if (dfs_state == MT_DFS_STATE_DISABLED)
		goto stop;

	if (prev_state <= MT_DFS_STATE_DISABLED) {
		err = mt7996_dfs_init_radar_specs(phy);
		if (err < 0)
			return err;

		err = mt7996_dfs_start_radar_detector(phy);
		if (err < 0)
			return err;

		phy->mt76->dfs_state = MT_DFS_STATE_CAC;
	}

	if (dfs_state == MT_DFS_STATE_CAC)
		return 0;

	err = mt7996_mcu_rdd_cmd(dev, RDD_CAC_END, rdd_idx, 0);
	if (err < 0) {
		phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
		return err;
	}

	phy->mt76->dfs_state = MT_DFS_STATE_ACTIVE;
	return 0;

stop:
	err = mt7996_mcu_rdd_cmd(dev, RDD_NORMAL_START, rdd_idx, 0);
	if (err < 0)
		return err;

	mt7996_dfs_stop_radar_detector(phy);
	phy->mt76->dfs_state = MT_DFS_STATE_DISABLED;

	return 0;
}

static int
mt7996_mac_twt_duration_align(int duration)
{
	return duration << 8;
}

static u64
mt7996_mac_twt_sched_list_add(struct mt7996_dev *dev,
			      struct mt7996_twt_flow *flow)
{
	struct mt7996_twt_flow *iter, *iter_next;
	u32 duration = flow->duration << 8;
	u64 start_tsf;

	iter = list_first_entry_or_null(&dev->twt_list,
					struct mt7996_twt_flow, list);
	if (!iter || !iter->sched || iter->start_tsf > duration) {
		/* add flow as first entry in the list */
		list_add(&flow->list, &dev->twt_list);
		return 0;
	}

	list_for_each_entry_safe(iter, iter_next, &dev->twt_list, list) {
		start_tsf = iter->start_tsf +
			    mt7996_mac_twt_duration_align(iter->duration);
		if (list_is_last(&iter->list, &dev->twt_list))
			break;

		if (!iter_next->sched ||
		    iter_next->start_tsf > start_tsf + duration) {
			list_add(&flow->list, &iter->list);
			goto out;
		}
	}

	/* add flow as last entry in the list */
	list_add_tail(&flow->list, &dev->twt_list);
out:
	return start_tsf;
}

static int mt7996_mac_check_twt_req(struct ieee80211_twt_setup *twt)
{
	struct ieee80211_twt_params *twt_agrt;
	u64 interval, duration;
	u16 mantissa;
	u8 exp;

	/* only individual agreement supported */
	if (twt->control & IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST)
		return -EOPNOTSUPP;

	/* only 256us unit supported */
	if (twt->control & IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT)
		return -EOPNOTSUPP;

	twt_agrt = (struct ieee80211_twt_params *)twt->params;

	/* explicit agreement not supported */
	if (!(twt_agrt->req_type & cpu_to_le16(IEEE80211_TWT_REQTYPE_IMPLICIT)))
		return -EOPNOTSUPP;

	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP,
			le16_to_cpu(twt_agrt->req_type));
	mantissa = le16_to_cpu(twt_agrt->mantissa);
	duration = twt_agrt->min_twt_dur << 8;

	interval = (u64)mantissa << exp;
	if (interval < duration)
		return -EOPNOTSUPP;

	return 0;
}

static bool
mt7996_mac_twt_param_equal(struct mt7996_sta_link *msta_link,
			   struct ieee80211_twt_params *twt_agrt)
{
	u16 type = le16_to_cpu(twt_agrt->req_type);
	u8 exp;
	int i;

	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, type);
	for (i = 0; i < MT7996_MAX_STA_TWT_AGRT; i++) {
		struct mt7996_twt_flow *f;

		if (!(msta_link->twt.flowid_mask & BIT(i)))
			continue;

		f = &msta_link->twt.flow[i];
		if (f->duration == twt_agrt->min_twt_dur &&
		    f->mantissa == twt_agrt->mantissa &&
		    f->exp == exp &&
		    f->protection == !!(type & IEEE80211_TWT_REQTYPE_PROTECTION) &&
		    f->flowtype == !!(type & IEEE80211_TWT_REQTYPE_FLOWTYPE) &&
		    f->trigger == !!(type & IEEE80211_TWT_REQTYPE_TRIGGER))
			return true;
	}

	return false;
}

void mt7996_mac_add_twt_setup(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta,
			      struct ieee80211_twt_setup *twt)
{
	enum ieee80211_twt_setup_cmd setup_cmd = TWT_SETUP_CMD_REJECT;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct ieee80211_twt_params *twt_agrt = (void *)twt->params;
	struct mt7996_sta_link *msta_link = &msta->deflink;
	u16 req_type = le16_to_cpu(twt_agrt->req_type);
	enum ieee80211_twt_setup_cmd sta_setup_cmd;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_twt_flow *flow;
	u8 flowid, table_id, exp;

	if (mt7996_mac_check_twt_req(twt))
		goto out;

	mutex_lock(&dev->mt76.mutex);

	if (dev->twt.n_agrt == MT7996_MAX_TWT_AGRT)
		goto unlock;

	if (hweight8(msta_link->twt.flowid_mask) ==
	    ARRAY_SIZE(msta_link->twt.flow))
		goto unlock;

	if (twt_agrt->min_twt_dur < MT7996_MIN_TWT_DUR) {
		setup_cmd = TWT_SETUP_CMD_DICTATE;
		twt_agrt->min_twt_dur = MT7996_MIN_TWT_DUR;
		goto unlock;
	}

	if (mt7996_mac_twt_param_equal(msta_link, twt_agrt))
		goto unlock;

	flowid = ffs(~msta_link->twt.flowid_mask) - 1;
	twt_agrt->req_type &= ~cpu_to_le16(IEEE80211_TWT_REQTYPE_FLOWID);
	twt_agrt->req_type |= le16_encode_bits(flowid,
					       IEEE80211_TWT_REQTYPE_FLOWID);

	table_id = ffs(~dev->twt.table_mask) - 1;
	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, req_type);
	sta_setup_cmd = FIELD_GET(IEEE80211_TWT_REQTYPE_SETUP_CMD, req_type);

	flow = &msta_link->twt.flow[flowid];
	memset(flow, 0, sizeof(*flow));
	INIT_LIST_HEAD(&flow->list);
	flow->wcid = msta_link->wcid.idx;
	flow->table_id = table_id;
	flow->id = flowid;
	flow->duration = twt_agrt->min_twt_dur;
	flow->mantissa = twt_agrt->mantissa;
	flow->exp = exp;
	flow->protection = !!(req_type & IEEE80211_TWT_REQTYPE_PROTECTION);
	flow->flowtype = !!(req_type & IEEE80211_TWT_REQTYPE_FLOWTYPE);
	flow->trigger = !!(req_type & IEEE80211_TWT_REQTYPE_TRIGGER);

	if (sta_setup_cmd == TWT_SETUP_CMD_REQUEST ||
	    sta_setup_cmd == TWT_SETUP_CMD_SUGGEST) {
		u64 interval = (u64)le16_to_cpu(twt_agrt->mantissa) << exp;
		u64 flow_tsf, curr_tsf;
		u32 rem;

		flow->sched = true;
		flow->start_tsf = mt7996_mac_twt_sched_list_add(dev, flow);
		curr_tsf = __mt7996_get_tsf(hw, &msta->vif->deflink);
		div_u64_rem(curr_tsf - flow->start_tsf, interval, &rem);
		flow_tsf = curr_tsf + interval - rem;
		twt_agrt->twt = cpu_to_le64(flow_tsf);
	} else {
		list_add_tail(&flow->list, &dev->twt_list);
	}
	flow->tsf = le64_to_cpu(twt_agrt->twt);

	if (mt7996_mcu_twt_agrt_update(dev, &msta->vif->deflink, flow,
				       MCU_TWT_AGRT_ADD))
		goto unlock;

	setup_cmd = TWT_SETUP_CMD_ACCEPT;
	dev->twt.table_mask |= BIT(table_id);
	msta_link->twt.flowid_mask |= BIT(flowid);
	dev->twt.n_agrt++;

unlock:
	mutex_unlock(&dev->mt76.mutex);
out:
	twt_agrt->req_type &= ~cpu_to_le16(IEEE80211_TWT_REQTYPE_SETUP_CMD);
	twt_agrt->req_type |=
		le16_encode_bits(setup_cmd, IEEE80211_TWT_REQTYPE_SETUP_CMD);
	twt->control = twt->control & IEEE80211_TWT_CONTROL_RX_DISABLED;
}

void mt7996_mac_twt_teardown_flow(struct mt7996_dev *dev,
				  struct mt7996_vif_link *link,
				  struct mt7996_sta_link *msta_link,
				  u8 flowid)
{
	struct mt7996_twt_flow *flow;

	lockdep_assert_held(&dev->mt76.mutex);

	if (flowid >= ARRAY_SIZE(msta_link->twt.flow))
		return;

	if (!(msta_link->twt.flowid_mask & BIT(flowid)))
		return;

	flow = &msta_link->twt.flow[flowid];
	if (mt7996_mcu_twt_agrt_update(dev, link, flow, MCU_TWT_AGRT_DELETE))
		return;

	list_del_init(&flow->list);
	msta_link->twt.flowid_mask &= ~BIT(flowid);
	dev->twt.table_mask &= ~BIT(flow->table_id);
	dev->twt.n_agrt--;
}

static void mt7996_scan_rx(struct mt7996_phy *phy)
{
	struct mt76_dev *dev = &phy->dev->mt76;
	struct ieee80211_vif *vif = dev->scan.vif;
	struct mt7996_vif *mvif;

        if (!vif || !test_bit(MT76_SCANNING, &phy->mt76->state))
                return;

	if (test_and_clear_bit(MT76_SCANNING_WAIT_BEACON, &phy->mt76->state)) {
		mvif = (struct mt7996_vif *)vif->drv_priv;
		set_bit(MT76_SCANNING_BEACON_DONE, &phy->mt76->state);
		cancel_delayed_work(&dev->scan_work);
		ieee80211_queue_delayed_work(phy->mt76->hw, &dev->scan_work, 0);
	}
}

static void
mt7996_rx_beacon_hint(struct mt7996_phy *phy, struct mt7996_vif *mvif)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_vif_link *mconf;
	int band_idx = phy->mt76->band_idx;
	unsigned int link_id = mvif->mt76.band_to_link[band_idx];

	mvif->beacon_received_time[band_idx] = jiffies;

	if (link_id >= IEEE80211_LINK_UNSPECIFIED)
		return;

	if (mvif->lost_links & BIT(link_id)) {
		mvif->lost_links &= ~BIT(link_id);
		mt76_dbg(&dev->mt76, MT76_DBG_STA,
			 "%s: link %d: resume beacon monitoring\n",
			 __func__, link_id);
	}

	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	if (!mconf) {
		mvif->tx_paused_links &= ~BIT(link_id);
		return;
	}

	/* resume TX */
	if (mconf->state == MT7996_STA_CHSW_PAUSE_TX &&
	    mconf->next_state != MT7996_STA_CHSW_RESUME_TX) {
		mconf->next_state = MT7996_STA_CHSW_RESUME_TX;
		cancel_delayed_work(&mconf->sta_chsw_work);
		ieee80211_queue_delayed_work(phy->mt76->hw, &mconf->sta_chsw_work, 0);
	}
}

static int
mt7996_beacon_mon_send_probe(struct mt7996_phy *phy, struct mt7996_vif *mvif,
			     struct ieee80211_bss_conf *conf, unsigned int link_id)
{
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);
	struct ieee80211_hw *hw = phy->mt76->hw;
	struct mt7996_sta_link *msta_link;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	int ret = 0, band_idx = phy->mt76->band_idx;
	int band;

	rcu_read_lock();

	msta_link = rcu_dereference(mvif->sta.link[link_id]);
	if (!msta_link || msta_link->wcid.phy_idx != band_idx) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!ieee80211_hw_check(hw, REPORTS_TX_ACK_STATUS)) {
		/* probe request is not supported yet */
		ret = -EOPNOTSUPP;
		goto unlock;
	}

	/* FIXME: bss conf should not be all-zero before beacon mon work is canecled */
	if (!is_valid_ether_addr(conf->bssid) ||
	    !is_valid_ether_addr(conf->addr)) {
		/* invalid address */
		ret = -EINVAL;
		goto unlock;
	}

	skb = ieee80211_nullfunc_get(hw, vif, link_id, false);
	if (!skb) {
		ret = -ENOMEM;
		goto unlock;
	}

	info = IEEE80211_SKB_CB(skb);
	/* frame injected by driver */
	info->flags |= (IEEE80211_TX_CTL_REQ_TX_STATUS |
			IEEE80211_TX_CTL_INJECTED |
			IEEE80211_TX_CTL_NO_PS_BUFFER);
	if (ieee80211_vif_is_mld(vif))
		info->control.flags |= u32_encode_bits(link_id, IEEE80211_TX_CTRL_MLO_LINK);

	band = phy->mt76->main_chandef.chan->band;

	skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	if (!ieee80211_tx_prepare_skb(hw, vif, skb, band, NULL)) {
		rcu_read_unlock();
		ieee80211_free_txskb(hw, skb);
		return -EINVAL;
	}

	local_bh_disable();
	mt76_tx(phy->mt76, NULL, &msta_link->wcid, skb);
	local_bh_enable();

	mvif->probe[band_idx] = (void *)skb;
	mvif->probe_send_count[band_idx]++;
	mvif->probe_send_time[band_idx] = jiffies;

unlock:
	rcu_read_unlock();
	return ret;
}

void mt7996_beacon_mon_work(struct work_struct *work)
{
	struct mt7996_vif *mvif = container_of(work, struct mt7996_vif, beacon_mon_work.work);
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);
	struct mt7996_dev *dev = mvif->dev;
	struct ieee80211_hw *hw = mt76_hw(dev);
	unsigned long next_time = ULONG_MAX, valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;
	enum monitor_state {
		MON_STATE_BEACON_MON,
		MON_STATE_SEND_PROBE,
		MON_STATE_LINK_LOST,
		MON_STATE_DISCONN,
	};

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *conf;
		struct mt7996_vif_link *mconf;
		struct mt7996_phy *phy;
		unsigned long timeout, loss_duration;
		int ret, band_idx;
		enum monitor_state state = MON_STATE_BEACON_MON;
		bool tx_paused = mvif->tx_paused_links & BIT(link_id);
		char lost_reason[64];

		conf = link_conf_dereference_protected(vif, link_id);
		mconf = mt7996_vif_link(dev, vif, link_id);
		if (!conf || !mconf)
			continue;

		/* skip lost links */
		if (mvif->lost_links & BIT(link_id))
			continue;

		phy = mconf->phy;
		band_idx = phy->mt76->band_idx;
		if (mvif->probe[band_idx]) {
			loss_duration = msecs_to_jiffies(MT7996_MAX_PROBE_TIMEOUT);
			timeout = mvif->probe_send_time[band_idx] + loss_duration;
			if (time_after_eq(jiffies, timeout)) {
				if (mvif->probe_send_count[band_idx] == MT7996_MAX_PROBE_TRIES)
					state = MON_STATE_LINK_LOST;
				else
					state = MON_STATE_SEND_PROBE;
			}
		} else {
			loss_duration = msecs_to_jiffies(MT7996_MAX_BEACON_LOSS *
							 conf->beacon_int);
			timeout = mvif->beacon_received_time[band_idx] + loss_duration;
			if (time_after_eq(jiffies, timeout)) {
				mt76_dbg(&dev->mt76, MT76_DBG_STA,
					 "%s: link %d: detect %d beacon loss\n",
					 __func__, link_id, MT7996_MAX_BEACON_LOSS);
				state = MON_STATE_SEND_PROBE;
			}
		}

		switch (state) {
		case MON_STATE_BEACON_MON:
			break;
		case MON_STATE_SEND_PROBE:
			if (!tx_paused) {
				ret = mt7996_beacon_mon_send_probe(phy, mvif, conf, link_id);
				if (!ret) {
					timeout = MT7996_MAX_PROBE_TIMEOUT +
						  mvif->probe_send_time[band_idx];
					mt76_dbg(&dev->mt76, MT76_DBG_STA,
						 "%s: link %d: send nullfunc to AP %pM, try %d/%d\n",
						 __func__, link_id, conf->bssid,
						 mvif->probe_send_count[band_idx],
						 MT7996_MAX_PROBE_TRIES);
					break;
				}
			}
			fallthrough;
		case MON_STATE_LINK_LOST:
			mvif->lost_links |= BIT(link_id);
			mvif->probe[band_idx] = NULL;
			mvif->probe_send_count[band_idx] = 0;
			if (state == MON_STATE_LINK_LOST)
				snprintf(lost_reason, sizeof(lost_reason),
					 "No ack for nullfunc frame");
			else if (tx_paused)
				snprintf(lost_reason, sizeof(lost_reason),
					 "Failed to send nullfunc frame (TX paused)");
			else
				snprintf(lost_reason, sizeof(lost_reason),
					 "Failed to send nullfunc frame (%d)", ret);
			mt76_dbg(&dev->mt76, MT76_DBG_STA,
				 "%s: link %d: %s to AP %pM, stop monitoring the lost link\n",
				 __func__, link_id, lost_reason, conf->bssid);
			if (mvif->lost_links != valid_links)
				break;
			fallthrough;
		case MON_STATE_DISCONN:
		default:
			mutex_unlock(&dev->mt76.mutex);
			goto disconn;
		}
		next_time = min(next_time, timeout - jiffies);
	}
	mutex_unlock(&dev->mt76.mutex);

	if (next_time == ULONG_MAX)
		goto disconn;

	ieee80211_queue_delayed_work(hw, &mvif->beacon_mon_work, next_time);
	return;

disconn:
	mt76_dbg(&dev->mt76, MT76_DBG_STA,
		 "%s: all links are lost, disconnecting\n", __func__);
	ieee80211_connection_loss(vif);
}

static void mt7996_sta_chsw_state_reset(struct mt7996_vif_link *mconf)
{
	struct mt7996_dev *dev = mconf->phy->dev;
	struct mt7996_vif *mvif = mconf->msta_link.sta->vif;
	unsigned int link_id = mconf->msta_link.wcid.link_id;

	lockdep_assert_held(&dev->mt76.mutex);

	mvif->tx_paused_links &= ~BIT(link_id);
	mconf->state = MT7996_STA_CHSW_IDLE;
	mconf->next_state = MT7996_STA_CHSW_IDLE;
	mconf->pause_timeout = 0;
}

void mt7996_sta_chsw_work(struct work_struct *work)
{
	struct mt7996_vif_link *mconf =
			container_of(work, struct mt7996_vif_link, sta_chsw_work.work);
	struct mt7996_dev *dev = mconf->phy->dev;
	struct mt7996_vif *mvif = mconf->msta_link.sta->vif;
	struct ieee80211_vif *vif =
			container_of((void *)mvif, struct ieee80211_vif, drv_priv);
	unsigned int link_id = mconf->msta_link.wcid.link_id;
	struct ieee80211_neg_ttlm merged_ttlm;
	struct ieee80211_sta *sta;
	bool success = true;
	int ret;

	mutex_lock(&dev->mt76.mutex);

	mconf->state = mconf->next_state;

	switch (mconf->state) {
	case MT7996_STA_CHSW_PAUSE_TX:
		mvif->tx_paused_links |= BIT(link_id);
		mconf->next_state = MT7996_STA_CHSW_TIMEOUT;
		break;
	case MT7996_STA_CHSW_RESUME_TX:
	case MT7996_STA_CHSW_TIMEOUT:
		mvif->tx_paused_links &= ~BIT(link_id);
		mconf->next_state = MT7996_STA_CHSW_IDLE;
		mconf->pause_timeout = 0;
		success = false;
		break;
	default:
		mt7996_sta_chsw_state_reset(mconf);
		mutex_unlock(&dev->mt76.mutex);
		return;
	}

	sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!sta)
		goto fail;

	mt7996_get_merged_ttlm(vif, &merged_ttlm);
	ret = mt7996_mcu_peer_mld_ttlm_req(dev, vif, sta, &merged_ttlm);
	if (ret)
		goto fail;

	mutex_unlock(&dev->mt76.mutex);

	/* FIXME: trigger connection drop as a workaround for TX pause timeout */
	if (mconf->state == MT7996_STA_CHSW_PAUSE_TX ||
	    mconf->state == MT7996_STA_CHSW_TIMEOUT)
		ieee80211_chswitch_done(vif, success, link_id);

	cancel_delayed_work(&mconf->sta_chsw_work);
	ieee80211_queue_delayed_work(dev->mt76.hw, &mconf->sta_chsw_work,
				     msecs_to_jiffies(mconf->pause_timeout));
	return;

fail:
	mt76_dbg(&dev->mt76, MT76_DBG_STA, "%s: link %d: fail to %s tx (%d)\n",
		 __func__, link_id,
		 mconf->state == MT7996_STA_CHSW_PAUSE_TX ? "pause" : "resume", ret);
	mt7996_sta_chsw_state_reset(mconf);
	mutex_unlock(&dev->mt76.mutex);

	/* trigger connection drop */
	ieee80211_chswitch_done(vif, false, link_id);
	return;
}
