// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7996.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"

#define fw_name(_dev, name, ...)	({			\
	char *_fw;						\
	switch (mt76_chip(&(_dev)->mt76)) {			\
	case MT7992_DEVICE_ID:						\
		switch ((_dev)->var.type) {			\
		case MT7992_VAR_TYPE_23:			\
			_fw = MT7992_##name##_23;		\
			break;					\
		default:					\
			_fw = MT7992_##name;			\
		}						\
		break;						\
	case MT7990_DEVICE_ID:					\
		_fw = MT7990_##name;				\
		break;						\
	case MT7996_DEVICE_ID:						\
	default:						\
		switch ((_dev)->var.type) {			\
		case MT7996_VAR_TYPE_233:			\
			_fw = MT7996_##name##_233;		\
			break;					\
		default:					\
			_fw = MT7996_##name;			\
		}						\
		break;						\
	}							\
	_fw;							\
})

struct mt7996_patch_hdr {
	char build_date[16];
	char platform[4];
	__be32 hw_sw_ver;
	__be32 patch_ver;
	__be16 checksum;
	u16 reserved;
	struct {
		__be32 patch_ver;
		__be32 subsys;
		__be32 feature;
		__be32 n_region;
		__be32 crc;
		u32 reserved[11];
	} desc;
} __packed;

struct mt7996_patch_sec {
	__be32 type;
	__be32 offs;
	__be32 size;
	union {
		__be32 spec[13];
		struct {
			__be32 addr;
			__be32 len;
			__be32 sec_key_idx;
			__be32 align_len;
			u32 reserved[9];
		} info;
	};
} __packed;

struct mt7996_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 n_region;
	u8 format_ver;
	u8 format_flag;
	u8 reserved[2];
	char fw_ver[10];
	char build_date[15];
	u32 crc;
} __packed;

struct mt7996_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 reserved[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 reserved1[15];
} __packed;

struct mbssid_sub_off {
	bool valid;
	u16 offset;
};

struct mt7996_mbssid_data {
	struct mbssid_sub_off mbssid_idx;
	struct mbssid_sub_off ntx_bss_cap;
	bool is_cu_link;
};

#define MCU_PATCH_ADDRESS		0x200000

#define HE_PHY(p, c)			u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)			u8_get_bits(c, IEEE80211_HE_MAC_##m)
#define EHT_PHY(p, c)			u8_get_bits(c, IEEE80211_EHT_PHY_##p)

static bool sr_scene_detect = true;
module_param(sr_scene_detect, bool, 0644);
MODULE_PARM_DESC(sr_scene_detect, "Enable firmware scene detection algorithm");

int mt7996_mcu_get_tx_power_info(struct mt7996_phy *phy, u8 category, void *event)
{
	struct mt7996_dev *dev = phy->dev;
	struct tx_power_ctrl req = {
		.tag = cpu_to_le16(UNI_TXPOWER_SHOW_INFO),
		.len = cpu_to_le16(sizeof(req) - 4),
		.power_ctrl_id = UNI_TXPOWER_SHOW_INFO,
		.show_info_category = category,
		.band_idx = phy->mt76->band_idx,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(TXPOWER),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	memcpy(event, skb->data, sizeof(struct mt7996_mcu_txpower_event));

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_set_tx_power_ctrl(struct mt7996_phy *phy, u8 power_ctrl_id, u8 data)
{
       struct mt7996_dev *dev = phy->dev;
       struct tx_power_ctrl req = {
               .tag = cpu_to_le16(power_ctrl_id),
               .len = cpu_to_le16(sizeof(req) - 4),
               .power_ctrl_id = power_ctrl_id,
               .band_idx = phy->mt76->band_idx,
       };

       switch (power_ctrl_id) {
       case UNI_TXPOWER_SKU_POWER_LIMIT_CTRL:
               req.sku_enable = !!data;
               break;
       case UNI_TXPOWER_PERCENTAGE_CTRL:
               req.percentage_ctrl_enable = !!data;
               break;
       case UNI_TXPOWER_PERCENTAGE_DROP_CTRL:
               req.power_drop_level = data;
               break;
       case UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL:
               req.bf_backoff_enable = !!data;
               break;
       case UNI_TXPOWER_ATE_MODE_CTRL:
               req.ate_mode_enable = !!data;
               break;
       default:
               req.sku_enable = !!data;
       }

       return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TXPOWER),
                                &req, sizeof(req), false);
}

static bool ok_eht(struct ieee80211_link_sta *link_sta)
{
	struct mt7996_sta *msta;
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;

	if (sta) {
		msta = (struct mt7996_sta *)sta->drv_priv;
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	}
	return link_sta->eht_cap.has_eht &&
		(link_sta->conn_settings.mode >= IEEE80211_CONN_MODE_EHT ||
		 (!vif || vif->type != NL80211_IFTYPE_STATION));
}

static bool ok_he(struct ieee80211_link_sta *link_sta)
{
	struct mt7996_sta *msta;
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;

	if (sta) {
		msta = (struct mt7996_sta *)sta->drv_priv;
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	}

	return link_sta->he_cap.has_he &&
		(link_sta->conn_settings.mode >= IEEE80211_CONN_MODE_HE ||
		 (!vif || vif->type != NL80211_IFTYPE_STATION));
}

static bool ok_vht(struct ieee80211_link_sta *link_sta)
{
	struct mt7996_sta *msta;
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;

	if (sta) {
		msta = (struct mt7996_sta *)sta->drv_priv;
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	}

	return link_sta->vht_cap.vht_supported &&
		(link_sta->conn_settings.mode >= IEEE80211_CONN_MODE_VHT ||
		 (!vif || vif->type != NL80211_IFTYPE_STATION));
}

static bool ok_ht(struct ieee80211_link_sta *link_sta)
{
	struct mt7996_sta *msta;
	struct ieee80211_vif *vif = NULL;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;

	if (sta) {
		msta = (struct mt7996_sta *)sta->drv_priv;
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	}

	return link_sta->ht_cap.ht_supported &&
		(link_sta->conn_settings.mode >= IEEE80211_CONN_MODE_HT ||
		 (!vif || vif->type != NL80211_IFTYPE_STATION));
}

static u8
mt7996_mcu_get_sta_nss(u16 mcs_map)
{
	u8 nss;

	for (nss = 8; nss > 0; nss--) {
		u8 nss_mcs = (mcs_map >> (2 * (nss - 1))) & 3;

		if (nss_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			break;
	}

	return nss - 1;
}

static void
mt7996_mcu_set_sta_he_mcs(struct ieee80211_link_sta *link_sta,
			  struct mt7996_vif_link *link,
			  __le16 *he_mcs, u16 mcs_map)
{
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;
	enum nl80211_band band = link->phy->mt76->chandef.chan->band;
	const u16 *mask = link->bitrate_mask.control[band].he_mcs;

	for (nss = 0; nss < max_nss; nss++) {
		int mcs;

		switch ((mcs_map >> (2 * nss)) & 0x3) {
		case IEEE80211_HE_MCS_SUPPORT_0_11:
			mcs = GENMASK(11, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		mcs = mcs ? fls(mcs & mask[nss]) - 1 : -1;

		switch (mcs) {
		case 0 ... 7:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_7;
			break;
		case 8 ... 9:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_9;
			break;
		case 10 ... 11:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_11;
			break;
		default:
			mcs = IEEE80211_HE_MCS_NOT_SUPPORTED;
			break;
		}
		mcs_map &= ~(0x3 << (nss * 2));
		mcs_map |= mcs << (nss * 2);
	}

	*he_mcs = cpu_to_le16(mcs_map);
}

static void
mt7996_mcu_set_sta_vht_mcs(struct ieee80211_link_sta *link_sta,
			   __le16 *vht_mcs, const u16 *mask)
{
	u16 mcs, mcs_map = le16_to_cpu(link_sta->vht_cap.vht_mcs.rx_mcs_map);
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;

	for (nss = 0; nss < max_nss; nss++, mcs_map >>= 2) {
		switch (mcs_map & 0x3) {
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			mcs = GENMASK(8, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		vht_mcs[nss] = cpu_to_le16(mcs & mask[nss]);
	}
}

static void
mt7996_mcu_set_sta_ht_mcs(struct ieee80211_link_sta *link_sta,
			  u8 *ht_mcs, const u8 *mask)
{
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;

	for (nss = 0; nss < max_nss; nss++)
		ht_mcs[nss] = link_sta->ht_cap.mcs.rx_mask[nss] & mask[nss];
}

static int
mt7996_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7996_mcu_rxd *rxd;
	struct mt7996_mcu_uni_event *event;
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	int ret = 0;

	if (!skb) {
		const char* first = "Secondary";

		mdev->mcu_timeouts++;
		if (!mdev->first_failed_mcu_cmd)
			first = "Initial";

		dev_err(mdev->dev, "MCU: %s Failure: Message %08x (cid %lx ext_cid: %lx seq %d) timeout (%d/%d).  Last successful cmd: 0x%x\n",
			first,
			cmd, FIELD_GET(__MCU_CMD_FIELD_ID, cmd),
			FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd), seq,
			mdev->mcu_timeouts, MAX_MCU_TIMEOUTS,
			mdev->last_successful_mcu_cmd);

		if (!mdev->first_failed_mcu_cmd)
			mdev->first_failed_mcu_cmd = cmd;
		return -ETIMEDOUT;
	}

	mdev->mcu_timeouts = 0;
	mdev->last_successful_mcu_cmd = cmd;

	if (mdev->first_failed_mcu_cmd) {
		dev_err(mdev->dev, "MCU: First success after failure: Message %08x (cid %lx ext_cid: %lx seq %d)\n",
			cmd, FIELD_GET(__MCU_CMD_FIELD_ID, cmd),
			FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd), seq);
		mdev->first_failed_mcu_cmd = 0;
	} else {
		/* verbose debugging
		   dev_err(mdev->dev, "MCU: OK response to message %08x (cid %lx ext_cid: %lx seq %d)\n",
		           cmd, FIELD_GET(__MCU_CMD_FIELD_ID, cmd),
		           FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd), seq);
		*/
	}

	rxd = (struct mt7996_mcu_rxd *)skb->data;
	if (seq != rxd->seq) {
		/* This can happen if the previous request didn't wait (which is normal).
		 * Quietly return EAGAIN in that case, it is not something to warn about.
		 */
		return -EAGAIN;
	} else {
		/*
		dev_info(mdev->dev, "NOTE: MCU:  Seq matched in response, seq: %d"
			 "  rxd->seq: %d rxd->eid: %d rxd->ext_eid: %d rxd-option: 0x%x rxd->pkt-type-id: %d cmd: %0x\n",
			 seq, rxd->seq, rxd->eid, rxd->ext_eid, rxd->option, rxd->pkt_type_id, cmd);
		*/
	}

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if ((rxd->option & MCU_UNI_CMD_EVENT) &&
		    rxd->eid == MCU_UNI_EVENT_RESULT) {
		skb_pull(skb, sizeof(*rxd));
		event = (struct mt7996_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		/* skip invalid event */
		if (mcu_cmd != event->cid) {
			dev_info(mdev->dev, "ERROR: MCU:  Seq matched in response, mcu-cmd did not, mcu-cmd: 0x%x cid: 0x%x status: %d\n",
				 mcu_cmd, event->cid, ret);
			ret = -EAGAIN;
		}
		if (ret) {
			mtk_dbg(mdev, CFG, "mcu-parse-response, firmware returned failure code: 0x%x.\n",
				ret);
		}
	} else {
		skb_pull(skb, sizeof(struct mt7996_mcu_rxd));
	}

	return ret;
}

static int
mt7996_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	int txd_len, mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt76_connac2_mcu_uni_txd *uni_txd;
	struct mt76_connac2_mcu_txd *mcu_txd;
	enum mt76_mcuq_id qid;
	__le32 *txd;
	u32 val;
	int ret;
	u8 seq;

	if (dev->recovery.l1_reset_last != dev->recovery.l1_reset) {
		dev_info(dev->mt76.dev,"\n%s L1 SER recovery overlap, drop message %08x.",
			 wiphy_name(dev->mt76.hw->wiphy), cmd);

		dev_kfree_skb(skb);
		return -EPERM;
	}

	mdev->mcu.timeout = 20 * HZ;

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	//dev_info(mdev->dev, "mcu-send-message, msg_seq: %d  seq: %d cmd: 0x%x\n",
	//	 dev->mt76.mcu.msg_seq, seq, cmd);

	if (cmd == MCU_CMD(FW_SCATTER)) {
		qid = MT_MCUQ_FWDL;
		goto exit;
	}

	txd_len = cmd & __MCU_CMD_FIELD_UNI ? sizeof(*uni_txd) : sizeof(*mcu_txd);
	txd = (__le32 *)skb_push(skb, txd_len);
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state) && mt7996_has_wa(dev))
		qid = MT_MCUQ_WA;
	else
		qid = MT_MCUQ_WM;

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	if (cmd & __MCU_CMD_FIELD_UNI) {
		uni_txd = (struct mt76_connac2_mcu_uni_txd *)txd;
		uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
		uni_txd->cid = cpu_to_le16(mcu_cmd);
		uni_txd->s2d_index = MCU_S2D_H2CN;
		uni_txd->pkt_type = MCU_PKT_ID;
		uni_txd->seq = seq;

		if (cmd & __MCU_CMD_FIELD_QUERY)
			uni_txd->option = MCU_CMD_UNI_QUERY_ACK;
		else
			uni_txd->option = MCU_CMD_UNI_EXT_ACK;

		if ((cmd & __MCU_CMD_FIELD_WA) && (cmd & __MCU_CMD_FIELD_WM))
			uni_txd->s2d_index = MCU_S2D_H2CN;
		else if (cmd & __MCU_CMD_FIELD_WA)
			uni_txd->s2d_index = MCU_S2D_H2C;
		else if (cmd & __MCU_CMD_FIELD_WM)
			uni_txd->s2d_index = MCU_S2D_H2N;

		goto exit;
	}

	mcu_txd = (struct mt76_connac2_mcu_txd *)txd;
	mcu_txd->len = cpu_to_le16(skb->len - sizeof(mcu_txd->txd));
	mcu_txd->pq_id = cpu_to_le16(MCU_PQ_ID(MT_TX_PORT_IDX_MCU,
					       MT_TX_MCU_PORT_RX_Q0));
	mcu_txd->pkt_type = MCU_PKT_ID;
	mcu_txd->seq = seq;

	mcu_txd->cid = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	mcu_txd->set_query = MCU_Q_NA;
	mcu_txd->ext_cid = FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd);
	if (mcu_txd->ext_cid) {
		mcu_txd->ext_cid_ack = 1;

		if (cmd & __MCU_CMD_FIELD_QUERY)
			mcu_txd->set_query = MCU_Q_QUERY;
		else
			mcu_txd->set_query = MCU_Q_SET;
	}

	if (cmd & __MCU_CMD_FIELD_WA)
		mcu_txd->s2d_index = MCU_S2D_H2C;
	else
		mcu_txd->s2d_index = MCU_S2D_H2N;

exit:
	if (dev->dbg.dump_mcu_pkt)
		mt7996_packet_log_to_host(dev, skb->data, skb->len, PKT_BIN_DEBUG_MCU, 0);
	if (wait_seq)
		*wait_seq = seq;

	ret = mt76_tx_queue_skb_raw(dev, mdev->q_mcu[qid], skb, 0);
	if (ret)
		dev_info(mdev->dev, "ERROR: mcu-send-message, queue-skb-raw failed: %d  msg_seq: %d  seq: %d cmd: 0x%x\n",
			 ret, dev->mt76.mcu.msg_seq, seq, cmd);
	return ret;
}

int mt7996_mcu_wa_cmd(struct mt7996_dev *dev, int cmd, u32 a1, u32 a2, u32 a3)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 args[3];
	} __packed req = {
		.args = {
			cpu_to_le32(a1),
			cpu_to_le32(a2),
			cpu_to_le32(a3),
		},
	};

	if (mt7996_has_wa(dev))
		return mt76_mcu_send_msg(&dev->mt76, cmd, &req.args,
					 sizeof(req.args), false);

	req.tag = cpu_to_le16(cmd == MCU_WA_PARAM_CMD(QUERY) ? UNI_CMD_SDO_QUERY :
							       UNI_CMD_SDO_SET);
	req.len = cpu_to_le16(sizeof(req) - 4);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO), &req,
				 sizeof(req), false);
}

static void
mt7996_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76_phy *mphy = (struct mt76_phy *)priv;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf;
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	int link_id, band_idx = mphy->band_idx;

	link_id = mvif->mt76.band_to_link[band_idx];
	if (link_id == IEEE80211_LINK_UNSPECIFIED)
		return;

	link_conf = rcu_dereference(vif->link_conf[link_id]);

	if (!link_conf || !link_conf->csa_active || vif->type == NL80211_IFTYPE_STATION)
		return;

	mvif->cs_ready_links = 0;
	mvif->cs_link_id = IEEE80211_LINK_UNSPECIFIED;
	ieee80211_csa_finish(vif, link_id);
	/* remove CSA for affiliated links */
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		if (link_id == link_conf->link_id)
			continue;
		ieee80211_csa_finish(vif, link_id);
	}
}

static void
mt7996_mcu_rx_radar_detected(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_rdd_report *r;

	r = (struct mt7996_mcu_rdd_report *)skb->data;

	dev_info(dev->mt76.dev, "rx-radar-detected: rdd-idx: %d long_detected: %d const_prf_det %d staggered_prf_det: %d"
		 " radar_type_idx: %d periodic_pulse_num: %d  long_pulse_num: %d hw_pulse_num: %d",
		 r->rdd_idx, r->long_detected, r->constant_prf_detected, r->staggered_prf_detected,
		 r->radar_type_idx, r->periodic_pulse_num, r->long_pulse_num, r->hw_pulse_num);

	switch (r->rdd_idx) {
	case MT_RDD_IDX_BAND2:
		mphy = dev->mt76.phys[MT_BAND2];
		break;
	case MT_RDD_IDX_BAND1:
		mphy = dev->mt76.phys[MT_BAND1];
		break;
	case MT_RDD_IDX_BACKGROUND:
		if (!dev->rdd2_phy)
			return;
		mphy = dev->rdd2_phy->mt76;
		break;
	default:
		dev_err(dev->mt76.dev, "Unknown RDD idx %d\n", r->rdd_idx);
		return;
	}

	if (!mphy)
		return;

	if (!dev->ignore_radar) {
		if (r->rdd_idx == MT_RDD_IDX_BACKGROUND)
			cfg80211_background_radar_event(mphy->hw->wiphy,
							&dev->rdd2_chandef,
							GFP_ATOMIC);
		else
			ieee80211_radar_detected(mphy->hw, NULL);
	}
	dev->hw_pattern++;
}

static void
mt7996_mcu_rx_log_message(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_FW_LOG_FORMAT 0
#define UNI_EVENT_FW_LOG_MEMORY	1
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1] + 4, *type;
	struct tlv *tlv = (struct tlv *)data;
	int len;

	if (le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_FORMAT &&
	    le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_MEMORY) {
		len = skb->len - sizeof(*rxd);
		data = (char *)&rxd[1];
		goto out;
	}

	if (le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_FORMAT)
		return;

	data += sizeof(*tlv) + 4;
	len = le16_to_cpu(tlv->len) - sizeof(*tlv) - 4;

out:
	switch (rxd->s2d_index) {
	case 0:
		if (mt7996_debugfs_rx_log(dev, data, len))
			return;

		type = "WM";
		break;
	case 2:
		type = "WA";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type, len, data);
}

static void
mt7996_mcu_cca_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76_phy *mphy = (struct mt76_phy *)priv;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf;
	u8 link_id;

	link_id = mvif->mt76.band_to_link[mphy->band_idx];
	if (link_id == IEEE80211_LINK_UNSPECIFIED)
		return;

	link_conf = rcu_dereference(vif->link_conf[link_id]);
	if (!link_conf || !link_conf->color_change_active ||
	    vif->type == NL80211_IFTYPE_STATION)
		return;

	ieee80211_color_change_finish(vif, link_id);
}

static void
mt7996_mcu_ie_countdown(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_IE_COUNTDOWN_CSA 0
#define UNI_EVENT_IE_COUNTDOWN_BCC 1
	struct header {
		u8 band;
		u8 rsv[3];
	};
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1], *tail;
	struct header *hdr = (struct header *)data;
	struct tlv *tlv = (struct tlv *)(data + 4);

	if (hdr->band >= ARRAY_SIZE(dev->mt76.phys))
		return;

	if (hdr->band && dev->mt76.phys[hdr->band])
		mphy = dev->mt76.phys[hdr->band];

	tail = skb->data + skb->len;
	data += sizeof(struct header);
	while (data + sizeof(struct tlv) < tail && le16_to_cpu(tlv->len)) {
		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_IE_COUNTDOWN_CSA:
			ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_csa_finish, mphy);
			break;
		case UNI_EVENT_IE_COUNTDOWN_BCC:
			ieee80211_iterate_active_interfaces_atomic(mphy->hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_cca_finish, mphy);
			break;
		}

		data += le16_to_cpu(tlv->len);
		tlv = (struct tlv *)data;
	}
}

static int
mt7996_mcu_update_rate(struct rate_info *rate, struct ieee80211_supported_band *sband,
		       u8 mode, u8 bw, u8 mcs, u8 nss, u8 stbc, u8 gi)
{
	struct rate_info tmp_rate = {};

	tmp_rate.mcs = mcs;
	tmp_rate.nss = (stbc && nss > 1) ? nss / 2 : nss;

	switch (mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		if (mcs >= sband->n_bitrates)
			return -EINVAL;

		tmp_rate.legacy = sband->bitrates[mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (mcs > 31)
			return -EINVAL;

		tmp_rate.flags |= RATE_INFO_FLAGS_MCS;
		if (gi)
			tmp_rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (mcs > 9)
			return -EINVAL;

		tmp_rate.flags |= RATE_INFO_FLAGS_VHT_MCS;
		if (gi)
			tmp_rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_PLR:
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		tmp_rate.mcs = mcs & GENMASK(3, 0);
		if (tmp_rate.mcs > 13 || gi > NL80211_RATE_INFO_HE_GI_3_2)
			return -EINVAL;

		tmp_rate.flags |= RATE_INFO_FLAGS_HE_MCS;
		tmp_rate.he_gi = gi;
		tmp_rate.he_dcm = mcs & MT_PRXV_TX_DCM;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		tmp_rate.mcs = mcs & GENMASK(3, 0);
		if (tmp_rate.mcs > 15 || gi > NL80211_RATE_INFO_EHT_GI_3_2)
			return -EINVAL;

		tmp_rate.flags |= RATE_INFO_FLAGS_EHT_MCS;
		tmp_rate.eht_gi = gi;
		break;
	default:
		return -EINVAL;
	}

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		tmp_rate.bw = RATE_INFO_BW_20;
		break;
	case IEEE80211_STA_RX_BW_40:
		tmp_rate.bw = RATE_INFO_BW_40;
		break;
	case IEEE80211_STA_RX_BW_80:
		tmp_rate.bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_160:
		tmp_rate.bw = RATE_INFO_BW_160;
		break;
	case IEEE80211_STA_RX_BW_320:
		tmp_rate.bw = RATE_INFO_BW_320;
		break;
	default:
		return -EINVAL;
	}

	if (mode == MT_PHY_TYPE_HE_EXT_SU && mcs & MT_PRXV_TX_ER_SU_106T) {
		tmp_rate.bw = RATE_INFO_BW_HE_RU;
		tmp_rate.he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_106;
	}
	*rate = tmp_rate;

	return 0;
}

static int
mt7996_mcu_update_trx_rates(struct mt76_wcid *wcid, struct all_sta_trx_rate *mcu_rate)
{
	struct mt7996_sta_link *msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
	struct mt76_dev *dev = &msta_link->sta->vif->dev->mt76;
	struct mt76_phy *phy = mt76_dev_phy(dev, wcid->phy_idx);
	struct ieee80211_supported_band *sband = NULL;
	bool cck;
	int ret;

	/* TX rate */
	cck = false;

	switch (mcu_rate->tx_mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (phy->chandef.chan->band == NL80211_BAND_2GHZ) {
			sband = &phy->sband_2g.sband;
			if (!cck)
				mcu_rate->tx_mcs += 4;
		} else if (phy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &phy->sband_5g.sband;
		else
			sband = &phy->sband_6g.sband;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		mcu_rate->tx_mcs += ((mcu_rate->tx_nss - 1) << 3);
		break;
	default:
		break;
	}

	ret = mt7996_mcu_update_rate(&wcid->rate, sband, mcu_rate->tx_mode,
				     mcu_rate->tx_bw, mcu_rate->tx_mcs,
				     mcu_rate->tx_nss, mcu_rate->tx_stbc,
				     mcu_rate->tx_gi);
	if (ret)
		return ret;

	/* RX rate */
	cck = false;

	switch (mcu_rate->rx_mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (phy->chandef.chan->band == NL80211_BAND_2GHZ)
			sband = &phy->sband_2g.sband;
		else if (phy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &phy->sband_5g.sband;
		else
			sband = &phy->sband_6g.sband;

		mcu_rate->rx_rate = mt76_get_rate(dev, sband, mcu_rate->rx_rate, cck);
		break;
	default:
		break;
	}

	ret = mt7996_mcu_update_rate(&wcid->rx_rate, sband, mcu_rate->rx_mode,
				     mcu_rate->rx_bw, mcu_rate->rx_rate,
				     mcu_rate->rx_nsts + 1, mcu_rate->rx_stbc,
				     mcu_rate->rx_gi);
	return ret;
}

static int
mt7996_mcu_update_tx_gi(struct rate_info *rate, struct all_sta_trx_rate *mcu_rate)
{
	switch (mcu_rate->tx_mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_VHT:
		if (mcu_rate->tx_gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		else
			rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (mcu_rate->tx_gi > NL80211_RATE_INFO_HE_GI_3_2)
			return -EINVAL;
		rate->he_gi = mcu_rate->tx_gi;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		if (mcu_rate->tx_gi > NL80211_RATE_INFO_EHT_GI_3_2)
			return -EINVAL;
		rate->eht_gi = mcu_rate->tx_gi;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
mt7996_mcu_rx_all_sta_info_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_all_sta_info_event *res;
	u16 i;

	skb_pull(skb, sizeof(struct mt7996_mcu_rxd));

	res = (struct mt7996_mcu_all_sta_info_event *)skb->data;

	for (i = 0; i < le16_to_cpu(res->sta_num); i++) {
		u8 ac;
		bool v1;
		u16 wlan_idx;
		struct mt76_wcid *wcid;
		struct mt76_phy *mphy;
		struct ieee80211_sta *sta;
		u32 tx_bytes, rx_bytes, tx_bytes_failed = 0, tx_airtime, rx_airtime,
		    tx_packets, rx_packets;

		switch (le16_to_cpu(res->tag)) {
		case UNI_ALL_STA_TXRX_RATE:
			wlan_idx = le16_to_cpu(res->rate[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);

			if (!wcid)
				break;

			if (mt7996_mcu_update_trx_rates(wcid, &res->rate[i]))
				dev_err(dev->mt76.dev, "Failed to update TX/RX rates.\n");
			break;
		case UNI_ALL_STA_TXRX_ADM_STAT:
			v1 = le16_to_cpu(res->len) == UNI_EVENT_SIZE_ADM_STAT_V1;
			if (v1)
				wlan_idx = le16_to_cpu(res->adm_stat_v1[i].wlan_idx);
			else
				wlan_idx = le16_to_cpu(res->adm_stat_v2[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);

			if (!wcid)
				break;

			mphy = mt76_dev_phy(&dev->mt76, wcid->phy_idx);
			for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ac++) {
				u8 lmac_ac = mt76_connac_lmac_mapping(ac);
				if (v1) {
					tx_bytes = le32_to_cpu(res->adm_stat_v1[i].tx_bytes[lmac_ac]);
					rx_bytes = le32_to_cpu(res->adm_stat_v1[i].rx_bytes[lmac_ac]);
				} else {
					tx_bytes = le32_to_cpu(res->adm_stat_v2[i].tx_bytes[lmac_ac]);
					rx_bytes = le32_to_cpu(res->adm_stat_v2[i].rx_bytes[lmac_ac]);
					tx_bytes_failed = le32_to_cpu(res->adm_stat_v2[i].tx_bytes_failed[lmac_ac]);
				}

				wcid->stats.tx_bytes_per_ac[ac] += tx_bytes;
				wcid->stats.rx_bytes_per_ac[ac] += rx_bytes;
				wcid->stats.tx_bytes_failed_per_ac[ac] += tx_bytes_failed;

				wcid->stats.tx_bytes += tx_bytes;
				wcid->stats.rx_bytes += rx_bytes;
				wcid->stats.tx_bytes_failed += tx_bytes_failed;

				// TODO:  Consider adding later?
				//__mt7996_stat_to_netdev(mphy, wcid,
				//			tx_bytes, rx_bytes, 0, 0);

				//ieee80211_tpt_led_trig_tx(mphy->hw, tx_bytes);
				//ieee80211_tpt_led_trig_rx(mphy->hw, rx_bytes);
			}
			break;
		case UNI_ALL_STA_TXRX_MSDU_COUNT:
			wlan_idx = le16_to_cpu(res->msdu_cnt[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);

			if (!wcid)
				break;

			mphy = mt76_dev_phy(&dev->mt76, wcid->phy_idx);

			tx_packets = le32_to_cpu(res->msdu_cnt[i].tx_msdu_cnt);
			rx_packets = le32_to_cpu(res->msdu_cnt[i].rx_msdu_cnt);

			wcid->stats.tx_packets += tx_packets;
			wcid->stats.rx_packets += rx_packets;

			// TODO:  Consider adding later.
			//__mt7996_stat_to_netdev(mphy, wcid, 0, 0,
			//			tx_packets, rx_packets);
			break;
		case UNI_ALL_STA_TXRX_AIR_TIME:
			wlan_idx = le16_to_cpu(res->airtime[i].wlan_idx);
			wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac) {
				u8 lmac_ac = mt76_connac_lmac_mapping(ac);
				tx_airtime = le32_to_cpu(res->airtime[i].tx[lmac_ac]);
				rx_airtime = le32_to_cpu(res->airtime[i].rx[lmac_ac]);

				wcid->stats.tx_airtime += tx_airtime;
				wcid->stats.rx_airtime += rx_airtime;
				ieee80211_sta_register_airtime(sta, mt76_ac_to_tid(ac),
				                               tx_airtime, rx_airtime);
			}
			break;
		case UNI_ALL_STA_RX_MPDU_COUNT:
#if 0
			// TODO:  Looks like we need later patch to make this work.
			wlan_idx = le16_to_cpu(res->rx_mpdu_cnt[i].wlan_idx);
			wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);
			if (!wcid)
				break;

			wcid->stats.rx_mpdus += le32_to_cpu(res->rx_mpdu_cnt[i].total);
			wcid->stats.rx_fcs_err += le32_to_cpu(res->rx_mpdu_cnt[i].total) -
						  le32_to_cpu(res->rx_mpdu_cnt[i].success);
#endif
			break;
		default:
			break;
		}
	}
}

static void
mt7996_mcu_rx_thermal_notify(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define THERMAL_NOTIFY_TAG 0x4
#define THERMAL_NOTIFY 0x2
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_mcu_thermal_notify *n;
	struct mt7996_phy *phy;

	n = (struct mt7996_mcu_thermal_notify *)skb->data;

	if (le16_to_cpu(n->tag) != THERMAL_NOTIFY_TAG)
		return;

	if (n->event_id != THERMAL_NOTIFY)
		return;

	if (n->band_idx > MT_BAND2)
		return;

	mphy = dev->mt76.phys[n->band_idx];
	if (!mphy)
		return;

	phy = (struct mt7996_phy *)mphy->priv;
	phy->throttle_state = n->duty_percent;
}

void mt7996_mcu_wmm_pbc_work(struct work_struct *work)
{
#define WMM_PBC_QUEUE_NUM	5
#define WMM_PBC_BSS_ALL		0xff
#define WMM_PBC_WLAN_IDX_ALL	0xffff
#define WMM_PBC_BOUND_DEFAULT	0xffff
#define WMM_PBC_LOW_BOUND_VO	1900
#define WMM_PBC_LOW_BOUND_VI	1900
#define WMM_PBC_LOW_BOUND_BE	1500
#define WMM_PBC_LOW_BOUND_BK	900
#define WMM_PBC_LOW_BOUND_MGMT	32
	struct mt7996_dev *dev = container_of(work, struct mt7996_dev, wmm_pbc_work);
	struct {
		u8 bss_idx;
		u8 queue_num;
		__le16 wlan_idx;
		u8 band_idx;
		u8 __rsv[3];
		struct {
			__le16 low;
			__le16 up;
		} __packed bound[WMM_PBC_QUEUE_NUM];
	} __packed req = {
		.bss_idx = WMM_PBC_BSS_ALL,
		.queue_num = WMM_PBC_QUEUE_NUM,
		.wlan_idx = cpu_to_le16(WMM_PBC_WLAN_IDX_ALL),
		.band_idx = dev->mphy.band_idx,
	};
	int i, ret;

#define pbc_acq_low_bound_config(_ac, _bound)								\
	req.bound[mt76_connac_lmac_mapping(_ac)].low = dev->wmm_pbc_enable ? cpu_to_le16(_bound) : 0
	pbc_acq_low_bound_config(IEEE80211_AC_VO, WMM_PBC_LOW_BOUND_VO);
	pbc_acq_low_bound_config(IEEE80211_AC_VI, WMM_PBC_LOW_BOUND_VI);
	pbc_acq_low_bound_config(IEEE80211_AC_BE, WMM_PBC_LOW_BOUND_BE);
	pbc_acq_low_bound_config(IEEE80211_AC_BK, WMM_PBC_LOW_BOUND_BK);
	req.bound[4].low = dev->wmm_pbc_enable
	                   ? cpu_to_le16(WMM_PBC_LOW_BOUND_MGMT) : 0;

	for (i = 0; i < WMM_PBC_QUEUE_NUM; ++i)
		req.bound[i].up = cpu_to_le16(WMM_PBC_BOUND_DEFAULT);

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(PKT_BUDGET_CTRL),
	                        &req, sizeof(req), true);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to configure WMM PBC.\n");
}

static void
mt7996_mcu_rx_bss_acq_pkt_cnt(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_bss_acq_pkt_cnt_event *event = (struct mt7996_mcu_bss_acq_pkt_cnt_event *)skb->data;
	u32 bitmap = le32_to_cpu(event->bss_bitmap);
	u64 sum[IEEE80211_NUM_ACS] = {0};
	u8 ac_cnt = 0;
	int i, j;

	for (i = 0; (i < BSS_ACQ_PKT_CNT_BSS_NUM) && (bitmap & (1 << i)); ++i) {
		for (j = IEEE80211_AC_VO; j < IEEE80211_NUM_ACS; ++j)
			sum[j] += le32_to_cpu(event->bss[i].cnt[mt76_connac_lmac_mapping(j)]);
	}

	for (i = IEEE80211_AC_VO; i < IEEE80211_NUM_ACS; ++i) {
		if (sum[i] > WMM_PKT_THRESHOLD)
			++ac_cnt;
	}

	if (ac_cnt > 1 && !dev->wmm_pbc_enable) {
		dev->wmm_pbc_enable = true;
		queue_work(dev->mt76.wq, &dev->wmm_pbc_work);
	} else if (ac_cnt <= 1 && dev->wmm_pbc_enable) {
		dev->wmm_pbc_enable = false;
		queue_work(dev->mt76.wq, &dev->wmm_pbc_work);
	}
}

static void
mt7996_mcu_rx_ext_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	mtk_dbg(&dev->mt76, MSG, "mt7996-mcu-rx-ext-event, ext-eid: %d\n", rxd->ext_eid);

	switch (rxd->ext_eid) {
	case MCU_EXT_EVENT_FW_LOG_2_HOST:
		mt7996_mcu_rx_log_message(dev, skb);
		break;
	case MCU_EXT_EVENT_BSS_ACQ_PKT_CNT:
		mt7996_mcu_rx_bss_acq_pkt_cnt(dev, skb);
		break;
	default:
		/* in SDK, grep for EventExtEventHandler */
		mtk_dbg(&dev->mt76, WRN, "mt7996, unhandled rx_ext_event: 0x%x", rxd->ext_eid);
		break;
	}
}

static void
mt7996_mcu_rx_unsolicited_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7996_mcu_rx_ext_event(dev, skb);
		break;
	case MCU_UNI_EVENT_THERMAL:
		mt7996_mcu_rx_thermal_notify(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

static void
mt7996_mcu_wed_rro_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_wed_rro_event *event = (void *)skb->data;

	if (!dev->has_rro)
		return;

	skb_pull(skb, sizeof(struct mt7996_mcu_rxd) + 4);

	switch (le16_to_cpu(event->tag)) {
	case UNI_WED_RRO_BA_SESSION_STATUS: {
		struct mt7996_mcu_wed_rro_ba_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt76_rx_tid *tid;
			struct mt76_wcid *wcid;
			u16 idx;

			e = (void *)skb->data;
			idx = le16_to_cpu(e->wlan_id);
			wcid = mt76_wcid_ptr(dev, idx);
			if (!wcid || !wcid->sta)
				break;

			if (e->tid >= ARRAY_SIZE(wcid->aggr))
				break;

			tid = rcu_dereference(wcid->aggr[e->tid]);
			if (!tid)
				break;

			tid->id = le16_to_cpu(e->id);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	case UNI_WED_RRO_BA_SESSION_DELETE: {
		struct mt7996_mcu_wed_rro_ba_delete_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt7996_wed_rro_session_id *session;

			e = (void *)skb->data;
			session = kzalloc(sizeof(*session), GFP_ATOMIC);
			if (!session)
				break;

			session->id = le16_to_cpu(e->session_id);

			spin_lock_bh(&dev->wed_rro.lock);
			list_add_tail(&session->list, &dev->wed_rro.poll_list);
			spin_unlock_bh(&dev->wed_rro.lock);

			ieee80211_queue_work(mt76_hw(dev), &dev->wed_rro.work);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	default:
		break;
	}
}

static void
mt7996_dump_pp_statistic_event(struct mt7996_dev *dev,
			       struct mt7996_mcu_pp_alg_ctrl_event *event)
{
	u32 unit_time = le32_to_cpu(event->pp_timer_intv);

	dev_info(dev->mt76.dev, "band idx = %u\n", le32_to_cpu(event->band_idx));
	dev_info(dev->mt76.dev, "x2 value = %u\n", le32_to_cpu(event->thr_x2_value));
	dev_info(dev->mt76.dev, "x2 shift = %u\n", le32_to_cpu(event->thr_x2_shift));
	dev_info(dev->mt76.dev, "x3 value = %u\n", le32_to_cpu(event->thr_x3_value));
	dev_info(dev->mt76.dev, "x3 shift = %u\n", le32_to_cpu(event->thr_x3_shift));
	dev_info(dev->mt76.dev, "x4 value = %u\n", le32_to_cpu(event->thr_x4_value));
	dev_info(dev->mt76.dev, "x4 shift = %u\n", le32_to_cpu(event->thr_x4_shift));
	dev_info(dev->mt76.dev, "x5 value = %u\n", le32_to_cpu(event->thr_x5_value));
	dev_info(dev->mt76.dev, "x5 shift = %u\n", le32_to_cpu(event->thr_x5_shift));
	dev_info(dev->mt76.dev, "x6 value = %u\n", le32_to_cpu(event->thr_x6_value));
	dev_info(dev->mt76.dev, "x6 shift = %u\n", le32_to_cpu(event->thr_x6_shift));
	dev_info(dev->mt76.dev, "x7 value = %u\n", le32_to_cpu(event->thr_x7_value));
	dev_info(dev->mt76.dev, "x7 shift = %u\n", le32_to_cpu(event->thr_x7_shift));
	dev_info(dev->mt76.dev, "x8 value = %u\n", le32_to_cpu(event->thr_x8_value));
	dev_info(dev->mt76.dev, "x8 shift = %u\n", le32_to_cpu(event->thr_x8_shift));
	dev_info(dev->mt76.dev, "sw_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->sw_pp_time), unit_time);
	dev_info(dev->mt76.dev, "hw_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->hw_pp_time), unit_time);
	dev_info(dev->mt76.dev, "no_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->no_pp_time), unit_time);
	dev_info(dev->mt76.dev, "auto_bw_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->auto_bw_time), unit_time);
	dev_info(dev->mt76.dev, "punct_bitmap = 0x%04x\n",
		 le16_to_cpu(event->punct_bitmap));
}

static void
mt7996_mcu_pp_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_pp_basic_event *event;
	struct mt7996_mcu_pp_dscb_event *dscb_event;
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;
	u16 report_bitmap;

	event = (struct mt7996_mcu_pp_basic_event *)skb->data;

	switch (le16_to_cpu(event->tag)) {
	case UNI_EVENT_STATIC_PP_TAG_CSA_DSCB_IE:
	case UNI_EVENT_STATIC_PP_TAG_DSCB_IE:
		if (!mt7996_band_valid(dev, event->band_idx))
			return;

		mphy = mt76_dev_phy(&dev->mt76, event->band_idx);
		phy = mphy->priv;

		dscb_event = (struct mt7996_mcu_pp_dscb_event *)event;
		report_bitmap = le16_to_cpu(dscb_event->punct_bitmap);

		if (phy->punct_bitmap == report_bitmap)
			return;

		if (phy->pp_mode == PP_FW_MODE) {
			phy->punct_bitmap = report_bitmap;
			/* TODO:  Consider enabling this?
			mt7996_vendor_pp_bitmap_update(phy, report_bitmap);
			*/
		}
		break;
	case UNI_EVENT_PP_TAG_ALG_CTRL:
		mt7996_dump_pp_statistic_event(dev, (struct mt7996_mcu_pp_alg_ctrl_event *)event);
		break;
	}
}

static void
mt7996_mcu_mld_reconf_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7996_mld_event_data *data = priv;
	struct mt7996_mcu_mld_ap_reconf_event *reconf = (void *)data->data;

	if (!ether_addr_equal(vif->addr, data->mld_addr))
		return;

	ieee80211_links_removed(vif, le16_to_cpu(reconf->link_bitmap));
}

static void
mt7996_mcu_mld_attlm_event(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7996_mld_event_data *data = priv;
	struct mt7996_mcu_mld_attlm_timeout_event *ttlm = (void *)data->data;

	if (!ether_addr_equal(vif->addr, data->mld_addr))
		return;

	/*
	 * TODO: Remap the FW event type to MAC80211 event type.
	 * For now, we align it because this is a proprietary implementation.
	 */
	ieee80211_attlm_notify(vif, 0, ttlm->event_type, GFP_ATOMIC);
}

static void
mt7996_mcu_mld_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_mld_event *event = (void *)skb->data;
	struct mt7996_mld_event_data data = {};
	struct tlv *tlv;
	int len;

	memcpy(data.mld_addr, event->mld_addr, ETH_ALEN);
	skb_pull(skb, sizeof(*event));
	tlv = (struct tlv *)skb->data;
	len = skb->len;

	while (len > 0 && le16_to_cpu(tlv->len) <= len) {
		data.data = (u8 *)tlv;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_MLD_ATTLM_TIMEOUT:
			ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_mld_attlm_event, &data);
			break;
		case UNI_EVENT_MLD_RECONF_AP_REM_TIMER:
			ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7996_mcu_mld_reconf_finish, &data);
			break;
		default:
			break;
		}

		len -= le16_to_cpu(tlv->len);
		tlv = (struct tlv *)((u8 *)(tlv) + le16_to_cpu(tlv->len));
	}
}

static void
mt7996_mcu_bss_bcn_crit_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7996_mcu_bss_event *data = priv;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	unsigned long valid_links = vif->valid_links;
	unsigned int link_id;

	if (!ieee80211_vif_is_mld(vif))
		return;

	rcu_read_lock();
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
		if (!mconf)
			continue;

		if (mconf->mt76.idx == data->bss_idx) {
			ieee80211_crit_update_notify(vif, link_id,
						     NL80211_CRIT_UPDATE_NONE,
						     GFP_ATOMIC);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
}

static void
mt7996_mcu_bss_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_bss_event *event = (void *)skb->data;
	struct tlv *tlv = (struct tlv *)event->buf;

	switch (le16_to_cpu(tlv->tag)) {
	case UNI_EVENT_BSS_BCN_CRIT_UPDATE:
		ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
				IEEE80211_IFACE_ITER_RESUME_ALL,
				mt7996_mcu_bss_bcn_crit_finish, event);
		break;
	default:
		dev_err(dev->mt76.dev, "Unknown BSS event tag: %d\n",
			le16_to_cpu(tlv->tag));
		return;
	}
}

static void
mt7996_mcu_uni_rx_unsolicited_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	switch (rxd->eid) {
	case MCU_UNI_EVENT_FW_LOG_2_HOST:
		mt7996_mcu_rx_log_message(dev, skb);
		break;
	case MCU_UNI_EVENT_IE_COUNTDOWN:
		mt7996_mcu_ie_countdown(dev, skb);
		break;
	case MCU_UNI_EVENT_RDD_REPORT:
		mt7996_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_UNI_EVENT_ALL_STA_INFO:
		mt7996_mcu_rx_all_sta_info_event(dev, skb);
		break;
	case MCU_UNI_EVENT_WED_RRO:
		mt7996_mcu_wed_rro_event(dev, skb);
		break;
	case MCU_UNI_EVENT_PP:
		mt7996_mcu_pp_event(dev, skb);
		break;
	case MCU_UNI_EVENT_MLD:
		mt7996_mcu_mld_event(dev, skb);
		break;
	case MCU_UNI_EVENT_BSS_INFO:
		mt7996_mcu_bss_event(dev, skb);
		break;
	case MCU_UNI_EVENT_SR:
		mt7996_mcu_rx_sr_event(dev, skb);
		break;
	case MCU_UNI_EVENT_THERMAL:
		mt7996_mcu_rx_thermal_notify(dev, skb);
		break;
#if 0
	case MCU_UNI_EVENT_TESTMODE_CTRL:
		mt7996_tm_rf_test_event(dev, skb);
#endif
	case MCU_UNI_EVENT_BF:
		mt7996_mcu_rx_bf_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7996_mcu_rx_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_rxd *rxd = (struct mt7996_mcu_rxd *)skb->data;

	if (rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT) {
		mt7996_mcu_uni_rx_unsolicited_event(dev, skb);
		return;
	}

	/* WA still uses legacy event*/
	if (rxd->ext_eid == MCU_EXT_EVENT_FW_LOG_2_HOST ||
	    !rxd->seq)
		mt7996_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static struct tlv *
mt7996_mcu_add_uni_tlv(struct sk_buff *skb, u16 tag, u16 len)
{
	struct tlv *ptlv = skb_put_zero(skb, len);

	ptlv->tag = cpu_to_le16(tag);
	ptlv->len = cpu_to_le16(len);

	return ptlv;
}

static void
mt7996_mcu_bss_rfch_tlv(struct sk_buff *skb, struct mt7996_phy *phy)
{
	static const u8 rlm_ch_band[] = {
		[NL80211_BAND_2GHZ] = 1,
		[NL80211_BAND_5GHZ] = 2,
		[NL80211_BAND_6GHZ] = 3,
	};
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct bss_rlm_tlv *ch;
	struct tlv *tlv;
	int freq1 = chandef->center_freq1;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RLM, sizeof(*ch));

	ch = (struct bss_rlm_tlv *)tlv;
	ch->control_channel = chandef->chan->hw_value;
	ch->center_chan = ieee80211_frequency_to_channel(freq1);
	ch->bw = mt76_connac_chan_bw(chandef);
	ch->tx_streams = hweight8(phy->mt76->antenna_mask);
	ch->rx_streams = hweight8(phy->mt76->antenna_mask);
	ch->band = rlm_ch_band[chandef->chan->band];

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		ch->center_chan2 = ieee80211_frequency_to_channel(freq2);
	}
}

static void
mt7996_mcu_bss_ra_tlv(struct sk_buff *skb, struct mt7996_phy *phy)
{
	struct bss_ra_tlv *ra;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RA, sizeof(*ra));

	ra = (struct bss_ra_tlv *)tlv;
	ra->short_preamble = true;
}

static void
mt7996_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf,
		      struct mt7996_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_uni_he *he;
	struct tlv *tlv;

	cap = mt76_connac_get_he_phy_cap(phy->mt76, vif);

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_HE_BASIC, sizeof(*he));

	he = (struct bss_info_uni_he *)tlv;
	he->he_pe_duration = link_conf->htc_trig_based_pkt_ext;
	if (!he->he_pe_duration)
		he->he_pe_duration = DEFAULT_HE_PE_DURATION;

	he->he_rts_thres = cpu_to_le16(link_conf->frame_time_rts_th);
	if (!he->he_rts_thres)
		he->he_rts_thres = cpu_to_le16(DEFAULT_HE_DURATION_RTS_THRES);

	he->max_nss_mcs[CMD_HE_MCS_BW80] = cap->he_mcs_nss_supp.tx_mcs_80;
	he->max_nss_mcs[CMD_HE_MCS_BW160] = cap->he_mcs_nss_supp.tx_mcs_160;
	he->max_nss_mcs[CMD_HE_MCS_BW8080] = cap->he_mcs_nss_supp.tx_mcs_80p80;
}

static void
mt7996_mcu_bss_mbssid_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *link_conf,
			  int enable)
{
	struct bss_info_uni_mbssid *mbssid;
	struct tlv *tlv;

	if (link_conf && !link_conf->bssid_indicator && enable)
		return;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_11V_MBSSID, sizeof(*mbssid));

	mbssid = (struct bss_info_uni_mbssid *)tlv;

	if (enable && link_conf) {
		mbssid->max_indicator = link_conf->bssid_indicator;
		mbssid->mbss_idx = link_conf->bssid_index;
		mbssid->tx_bss_omac_idx = 0;
	}
}

static void
mt7996_mcu_bss_bmc_tlv(struct sk_buff *skb, struct mt76_vif_link *mlink,
		       struct mt7996_phy *phy)
{
	struct bss_rate_tlv *bmc;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct tlv *tlv;
	u8 idx = mlink->mcast_rates_idx ?
		 mlink->mcast_rates_idx : mlink->basic_rates_idx;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RATE, sizeof(*bmc));

	bmc = (struct bss_rate_tlv *)tlv;

	bmc->short_preamble = (band == NL80211_BAND_2GHZ);
	bmc->bc_fixed_rate = idx;
	bmc->mc_fixed_rate = idx;
}

static void
mt7996_mcu_bss_txcmd_tlv(struct sk_buff *skb, bool en)
{
	struct bss_txcmd_tlv *txcmd;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_TXCMD, sizeof(*txcmd));

	txcmd = (struct bss_txcmd_tlv *)tlv;
	txcmd->txcmd_mode = en;
}

static void
mt7996_mcu_bss_mld_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		       struct mt76_vif_link *mlink)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct bss_mld_tlv *mld;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_MLD, sizeof(*mld));
	mld = (struct bss_mld_tlv *)tlv;

	if (ieee80211_vif_is_mld(vif)) {
		mld->group_mld_id = mvif->group_mld_id;
		mld->remap_idx = mvif->mld_remap_id;
		memcpy(mld->mac_addr, vif->addr, ETH_ALEN);
	} else {
		mld->group_mld_id = 0xff;
		mld->remap_idx = 0xff;
	}

	mld->own_mld_id = link->own_mld_id;
	mld->link_id = link->msta_link.wcid.link_id;
	mt76_dbg(&mvif->dev->mt76, MT76_DBG_BSS,
		 "%s: group_mld_id=%d, own_mld_id=%d, remap_idx=%d, mld->addr[%pM]\n",
		 __func__, mld->group_mld_id,  mld->own_mld_id,
		 mld->remap_idx, mld->mac_addr);
}

static void
mt7996_mcu_bss_sec_tlv(struct sk_buff *skb, struct mt76_vif_link *mlink)
{
	struct bss_sec_tlv *sec;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_SEC, sizeof(*sec));

	sec = (struct bss_sec_tlv *)tlv;
	sec->cipher = mlink->cipher;
}

static int
mt7996_mcu_muar_config(struct mt7996_dev *dev, struct mt76_vif_link *mlink,
		       const u8 *addr, bool bssid, bool enable)
{
#define UNI_MUAR_ENTRY 2
	u32 idx = mlink->omac_idx - REPEATER_BSSID_START;
	struct {
		struct {
			u8 band;
			u8 __rsv[3];
		} hdr;

		__le16 tag;
		__le16 len;

		bool smesh;
		u8 bssid;
		u8 index;
		u8 entry_add;
		u8 addr[ETH_ALEN];
		u8 __rsv[2];
	} __packed req = {
		.hdr.band = mlink->band_idx,
		.tag = cpu_to_le16(UNI_MUAR_ENTRY),
		.len = cpu_to_le16(sizeof(req) - sizeof(req.hdr)),
		.smesh = false,
		.index = idx * 2 + bssid,
		.entry_add = true,
	};

	if (enable)
		memcpy(req.addr, addr, ETH_ALEN);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REPT_MUAR), &req,
				 sizeof(req), true);
}

static void
mt7996_mcu_bss_ifs_timing_tlv(struct sk_buff *skb, struct mt7996_phy *phy)
{
	struct bss_ifs_time_tlv *ifs_time;
	struct tlv *tlv;
	bool is_2ghz = phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_IFS_TIME, sizeof(*ifs_time));

	ifs_time = (struct bss_ifs_time_tlv *)tlv;
	ifs_time->slot_valid = true;
	ifs_time->sifs_valid = true;
	ifs_time->rifs_valid = true;
	ifs_time->eifs_valid = true;

	ifs_time->slot_time = cpu_to_le16(phy->slottime);
	ifs_time->sifs_time = cpu_to_le16(10);
	ifs_time->rifs_time = cpu_to_le16(2);
	ifs_time->eifs_time = cpu_to_le16(is_2ghz ? 78 : 84);

	if (is_2ghz) {
		ifs_time->eifs_cck_valid = true;
		ifs_time->eifs_cck_time = cpu_to_le16(314);
	}
}

static int
mt7996_mcu_bss_basic_tlv(struct mt7996_dev *dev,
			 struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf,
			 struct mt76_vif_link *mvif,
			 struct mt76_phy *phy, u16 wlan_idx,
			 bool enable)
{
	struct cfg80211_chan_def *chandef = &phy->chandef;
	struct mt76_connac_bss_basic_tlv *bss;
	u32 type = CONNECTION_INFRA_AP;
	u16 sta_wlan_idx = wlan_idx;
	struct ieee80211_sta *sta;
	struct tlv *tlv;
	int idx;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MONITOR:
		break;
	case NL80211_IFTYPE_STATION:
		if (enable) {
			rcu_read_lock();
			// TODO:  Potential bad BSSID area.
			sta = ieee80211_find_sta(vif, link_conf->bssid);
			mt76_dbg(&dev->mt76, MT76_DBG_BSS,
				 "%s: wlan_idx: %d find_sta returned: %p  link_conf->bssid: %pM\n",
				 __func__, wlan_idx, sta, link_conf->bssid);
			/* TODO: enable BSS_INFO_UAPSD & BSS_INFO_PM */
			if (sta) {
				struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
				struct mt7996_sta_link *msta_link;

				msta_link = rcu_dereference(msta->link[link_conf->link_id]);
				if (msta_link)
					sta_wlan_idx = msta_link->wcid.idx;
			}
			rcu_read_unlock();
		}
		type = CONNECTION_INFRA_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = CONNECTION_IBSS_ADHOC;
		break;
	default:
		WARN_ON(1);
		break;
	}

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_BASIC, sizeof(*bss));

	bss = (struct mt76_connac_bss_basic_tlv *)tlv;
	// TODO:  Investigate
	//bss->bcn_interval = cpu_to_le16(link_conf->beacon_int);
	//bss->dtim_period = link_conf->dtim_period;
	bss->bmc_tx_wlan_idx = cpu_to_le16(wlan_idx);
	bss->sta_idx = cpu_to_le16(sta_wlan_idx);
	bss->conn_type = cpu_to_le32(type);
	bss->omac_idx = mvif->omac_idx;
	bss->band_idx = mvif->band_idx;
	bss->wmm_idx = mvif->wmm_idx;
	bss->conn_state = !enable;
	bss->active = enable;

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	bss->hw_bss_idx = idx;

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		memcpy(bss->bssid, phy->macaddr, ETH_ALEN);
		return 0;
	}

	if (!link_conf)
		return 0;

	memcpy(bss->bssid, link_conf->bssid, ETH_ALEN);

	mt76_dbg(phy->dev, MT76_DBG_BSS,
		 "%s: band=%d, omac=%d, wmm_idx=%d, bssid=%pM, link=%d, en=%d sta_wlan_idx: %d\n",
		 __func__, bss->band_idx, bss->omac_idx,
		 bss->wmm_idx, bss->bssid, link_conf->link_id, enable, sta_wlan_idx);

	bss->bcn_interval = cpu_to_le16(link_conf->beacon_int);
	bss->dtim_period = link_conf->dtim_period;
	bss->phymode = mt76_connac_get_phy_mode(phy, vif,
						chandef->chan->band, NULL);
	bss->phymode_ext = mt76_connac_get_phy_mode_ext(phy, link_conf,
							chandef->chan->band);

	return 0;
}

static struct sk_buff *
__mt7996_mcu_alloc_bss_req(struct mt76_dev *dev, struct mt76_vif_link *mvif, int len)
{
	struct bss_req_hdr hdr = {
		.bss_idx = mvif->idx,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

int mt7996_mcu_add_bss_info(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink,
			    struct mt7996_sta_link *msta_link, int enable)
{
	struct mt7996_dev *dev = phy->dev;
	struct sk_buff *skb;

	if (mlink->omac_idx >= REPEATER_BSSID_START) {
		mt7996_mcu_muar_config(dev, mlink, link_conf->addr, false, enable);
		mt7996_mcu_muar_config(dev, mlink, link_conf->bssid, true, enable);
	}

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, mlink,
					 MT7996_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* bss_basic must be first */
	mt7996_mcu_bss_basic_tlv(dev, skb, vif, link_conf, mlink, phy->mt76,
				 msta_link->wcid.idx, enable);
	mt7996_mcu_bss_sec_tlv(skb, mlink);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		goto out;

	if (enable) {
		mt7996_mcu_bss_rfch_tlv(skb, phy);
		mt7996_mcu_bss_bmc_tlv(skb, mlink, phy);
		mt7996_mcu_bss_ra_tlv(skb, phy);
		mt7996_mcu_bss_txcmd_tlv(skb, true);
		mt7996_mcu_bss_ifs_timing_tlv(skb, phy);

		if (link_conf->he_support)
			mt7996_mcu_bss_he_tlv(skb, vif, link_conf, phy);

		/* this tag is necessary no matter if the vif is MLD */
		mt7996_mcu_bss_mld_tlv(skb, vif, mlink);
	}

	mt7996_mcu_bss_mbssid_tlv(skb, link_conf, enable);

out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7996_mcu_get_tsf_offset(struct mt7996_phy *phy,
			      struct mt7996_vif *mvif,
			      int rpting_link_id,
			      int rpted_link_id)
{
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif,
						 drv_priv);
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_vif_link *rpting_conf, *rpted_conf;
	struct mt7996_mcu_mac_info_tsf_diff *req;
	struct mt7996_mcu_mac_info_tsf_diff_resp *resp;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct {
		u8 __rsv[4];
		u8 buf[];
	} __packed hdr;
	int len = sizeof(hdr) + sizeof(*req), ret;
	int32_t tsf0_31, tsf32_63;
	int64_t tsf_rpted, tsf_rpting, tsf_offset;

	rpted_conf = mt7996_vif_link(dev, vif, rpted_link_id);
	rpting_conf = mt7996_vif_link(dev, vif, rpting_link_id);
	if (!rpted_conf || !rpting_conf)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_CMD_MAC_INFO_TSF_DIFF, sizeof(*req));

	req = (struct mt7996_mcu_mac_info_tsf_diff *)tlv;
	req->bss_idx0 = rpted_link_id;
	req->bss_idx1 = rpting_link_id;

	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(GET_MAC_INFO),
					    true, &rskb);
	if (ret)
		return ret;

	skb_pull(rskb, sizeof(struct mt7996_mcu_mac_info_event));
	resp = (struct mt7996_mcu_mac_info_tsf_diff_resp *)rskb->data;

	switch(le16_to_cpu(resp->tag)) {
	case UNI_EVENT_MAC_INFO_TSF_DIFF:
		tsf0_31 = le32_to_cpu(resp->tsf0_bit0_31);
		tsf32_63 = le32_to_cpu(resp->tsf0_bit32_63);
		tsf_rpted = (int64_t)tsf0_31 + ((int64_t)tsf32_63 << 32);
		tsf0_31 = le32_to_cpu(resp->tsf1_bit0_31);
		tsf32_63 = le32_to_cpu(resp->tsf1_bit32_63);
		tsf_rpting = (int64_t)tsf0_31 + ((int64_t)tsf32_63 << 32);
		tsf_offset = (tsf_rpted - tsf_rpting) / 2;
		rpted_conf->tsf_offset[rpting_link_id] = tsf_offset;
		break;
	default:
		break;
	}

	dev_kfree_skb(rskb);
	return ret;
}

int mt7996_mcu_set_timing(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_vif_link *mlink = mt76_vif_conf_link(&dev->mt76, vif, link_conf);
	struct sk_buff *skb;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, mlink,
					 MT7996_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7996_mcu_bss_ifs_timing_tlv(skb, phy);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int
mt7996_mcu_sta_ba(struct mt7996_dev *dev, struct mt76_vif_link *mvif,
		  struct mt76_wcid *wcid, struct ieee80211_ampdu_params *params,
		  bool enable, bool tx)
{
	struct sta_rec_ba_uni *ba;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mvif, wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba_uni *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
	ba->ba_rdd_rro = !tx && enable && dev->has_rro;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7996_mcu_sta_tx_cap(struct mt7996_dev *dev, struct mt76_vif_link *mvif,
		      struct mt76_wcid *wcid)
{
	struct sta_rec_tx_cap *tx_cap;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mvif, wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_TX_CAP, sizeof(*tx_cap));

	tx_cap = (struct sta_rec_tx_cap *)tlv;
	tx_cap->ampdu_limit_en = true;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(STA_REC_UPDATE), true);
}

static bool mt7996_check_limit_ampdu_en(struct ieee80211_ampdu_params *params) {
	struct ieee80211_sta *sta = params->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif,
						 drv_priv);
	unsigned long valid_links = sta->valid_links ?: BIT(0);
	unsigned int link_id;
	bool BW320 = false, BW160 = false;

	if (params->buf_size < 1024)
		return false;

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_link_sta __rcu *link =
			link_sta_dereference_protected(sta, link_id);
		struct mt7996_vif_link *mconf;
		struct mt76_phy *phy;
		struct ieee80211_eht_mcs_nss_supp_bw *ss = NULL;
		u8 sta_bw, ap_nss, sta_nss;

		if (!link) {
			pr_err("check-limit-ampdu-en, link is NULL. link_id: %d\n",
				link_id);
			continue;
		}
		mconf =	mt7996_vif_link(mvif->dev, vif, link_id);
		if (!mconf) {
			pr_err("check-limit-ampdu-en, mconf is NULL, link_id: %d\n",
				link_id);
			continue;
		}
		phy = mconf->phy->mt76;

		switch (phy->chandef.width) {
		case NL80211_CHAN_WIDTH_160:
			if (link->bandwidth >= IEEE80211_STA_RX_BW_160) {
				ss = &link->eht_cap.eht_mcs_nss_supp.bw._160;
				sta_bw = NL80211_CHAN_WIDTH_160;
			}
			break;
		case NL80211_CHAN_WIDTH_320:
			if (link->bandwidth == IEEE80211_STA_RX_BW_320) {
				ss = &link->eht_cap.eht_mcs_nss_supp.bw._320;
				sta_bw = NL80211_CHAN_WIDTH_320;
			}
			break;
		default:
			break;
		}

		if (!ss)
			continue;

		ap_nss = hweight8(phy->antenna_mask);
		sta_nss = max(u8_get_bits(ss->rx_tx_mcs11_max_nss, IEEE80211_EHT_MCS_NSS_RX),
			      u8_get_bits(ss->rx_tx_mcs13_max_nss, IEEE80211_EHT_MCS_NSS_RX));

		if (min(ap_nss, sta_nss) <= 2)
			continue;

		if (sta_bw == NL80211_CHAN_WIDTH_160)
			BW160 = true;
		else if (sta_bw == NL80211_CHAN_WIDTH_320)
			BW320 = true;
	}

	return BW320 && BW160;
}

/** starec & wtbl **/
int mt7996_mcu_add_tx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct mt7996_vif_link *link,
			 struct mt7996_sta_link *msta_link, bool enable)
{
	bool limit_ampdu_en = mt7996_check_limit_ampdu_en(params);
	int ret;

	if (enable && !params->amsdu)
		msta_link->wcid.amsdu = false;

	ret = mt7996_mcu_sta_ba(dev, &link->mt76, &msta_link->wcid, params,
				enable, true);
	if (ret)
		return ret;

	if (limit_ampdu_en)
		return mt7996_mcu_sta_tx_cap(dev, &link->mt76, &msta_link->wcid);

	return 0;
}

int mt7996_mcu_add_rx_ba(struct mt7996_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct mt7996_vif_link *link,
			 struct mt7996_sta_link *msta_link, bool enable)
{
	return mt7996_mcu_sta_ba(dev, &link->mt76, &msta_link->wcid, params,
				 enable, false);
}

static void
mt7996_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *conf,
		      struct ieee80211_link_sta *link_sta,
		      struct mt7996_vif_link *link)
{
	struct ieee80211_he_cap_elem *elem = &link_sta->he_cap.he_cap_elem;
	struct ieee80211_he_mcs_nss_supp mcs_map;
	struct sta_rec_he_v2 *he;
	struct tlv *tlv;
	int i = 0;

	if (!ok_he(link_sta))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_V2, sizeof(*he));

	he = (struct sta_rec_he_v2 *)tlv;
	for (i = 0; i < 11; i++) {
		if (i < 6)
			he->he_mac_cap[i] = elem->mac_cap_info[i];
		he->he_phy_cap[i] = elem->phy_cap_info[i];
	}

	if (conf->vif->type == NL80211_IFTYPE_AP &&
	    (elem->phy_cap_info[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		u8p_replace_bits(&he->he_phy_cap[1], conf->he_ldpc,
				 IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);

	mcs_map = link_sta->he_cap.he_mcs_nss_supp;
	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (elem->phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			mt7996_mcu_set_sta_he_mcs(link_sta, link,
						  &he->max_nss_mcs[CMD_HE_MCS_BW8080],
						  le16_to_cpu(mcs_map.rx_mcs_80p80));

		mt7996_mcu_set_sta_he_mcs(link_sta, link,
					  &he->max_nss_mcs[CMD_HE_MCS_BW160],
					  le16_to_cpu(mcs_map.rx_mcs_160));
		fallthrough;
	default:
		mt7996_mcu_set_sta_he_mcs(link_sta, link,
					  &he->max_nss_mcs[CMD_HE_MCS_BW80],
					  le16_to_cpu(mcs_map.rx_mcs_80));
		break;
	}

	he->pkt_ext = 2;
}

static void
mt7996_mcu_sta_he_6g_tlv(struct sk_buff *skb,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7996_dev *dev)
{
	struct sta_rec_he_6g_capa *he_6g;
	struct tlv *tlv;

	if (link_sta->conn_settings.mode < IEEE80211_CONN_MODE_HE ||
	    !link_sta->he_6ghz_capa.capa)
		return;

	dev_info(dev->mt76.dev, "sta-eht-tlv, marking 6ghz capable");

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_6G, sizeof(*he_6g));

	he_6g = (struct sta_rec_he_6g_capa *)tlv;
	he_6g->capa = link_sta->he_6ghz_capa.capa;
}

static void
mt7996_mcu_sta_eht_tlv(struct sk_buff *skb,
		       struct ieee80211_link_sta *link_sta,
		       struct mt7996_dev *dev)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)link_sta->sta->drv_priv;
	struct ieee80211_vif *vif = container_of((void *)msta->vif,
						 struct ieee80211_vif, drv_priv);
	struct ieee80211_eht_mcs_nss_supp *mcs_map;
	struct ieee80211_eht_cap_elem_fixed *elem;
	struct sta_rec_eht *eht;
	struct tlv *tlv;

	dev_info(dev->mt76.dev, "sta-eht-tlv, has-eht: %d  conn-settings mode: %d",
		 link_sta->eht_cap.has_eht,
		 link_sta->conn_settings.mode);

	if (!ok_eht(link_sta))
		return;

	mcs_map = &link_sta->eht_cap.eht_mcs_nss_supp;
	elem = &link_sta->eht_cap.eht_cap_elem;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT, sizeof(*eht));

	eht = (struct sta_rec_eht *)tlv;
	eht->tid_bitmap = 0xff;
	eht->mac_cap = cpu_to_le16(*(u16 *)elem->mac_cap_info);
	eht->phy_cap = cpu_to_le64(*(u64 *)elem->phy_cap_info);
	eht->phy_cap_ext = cpu_to_le64(elem->phy_cap_info[8]);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
	     (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)) == 0) {
		memcpy(eht->mcs_map_bw20, &mcs_map->only_20mhz,
		       sizeof(eht->mcs_map_bw20));
		return;
	}

	memcpy(eht->mcs_map_bw80, &mcs_map->bw._80, sizeof(eht->mcs_map_bw80));
	memcpy(eht->mcs_map_bw160, &mcs_map->bw._160, sizeof(eht->mcs_map_bw160));
	memcpy(eht->mcs_map_bw320, &mcs_map->bw._320, sizeof(eht->mcs_map_bw320));
}

static void
mt7996_mcu_sta_ht_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
		      struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_ht_uni *ht;
	struct tlv *tlv;

	mtk_dbg(&dev->mt76, CFG,
		"sta-ht-tlv, ok-ht: %d  ok_vht: %d  ok_he: %d ok_eht: %d deflink-mode: %d ht-supported: %d\n",
		ok_ht(link_sta), ok_vht(link_sta), ok_he(link_sta), ok_eht(link_sta),
		link_sta->conn_settings.mode, link_sta->ht_cap.ht_supported);

	if (!ok_ht(link_sta))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));

	ht = (struct sta_rec_ht_uni *)tlv;
	ht->ht_cap = cpu_to_le16(link_sta->ht_cap.cap);
	ht->ampdu_param = u8_encode_bits(link_sta->ht_cap.ampdu_factor,
					 IEEE80211_HT_AMPDU_PARM_FACTOR) |
			  u8_encode_bits(link_sta->ht_cap.ampdu_density,
					 IEEE80211_HT_AMPDU_PARM_DENSITY);
}

static void
mt7996_mcu_sta_vht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_vht *vht;
	struct tlv *tlv;

	/* For 6G band, this tlv is necessary to let hw work normally */
	if (!link_sta->he_6ghz_capa.capa && !ok_vht(link_sta))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));

	vht = (struct sta_rec_vht *)tlv;
	vht->vht_cap = cpu_to_le32(link_sta->vht_cap.cap);
	vht->vht_rx_mcs_map = link_sta->vht_cap.vht_mcs.rx_mcs_map;
	vht->vht_tx_mcs_map = link_sta->vht_cap.vht_mcs.tx_mcs_map;
}

static void
mt7996_mcu_sta_amsdu_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7996_sta_link *msta_link)
{
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	if (link_sta->conn_settings.mode < IEEE80211_CONN_MODE_HT ||
	    !link_sta->agg.max_amsdu_len)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;
	msta_link->wcid.amsdu = true;

	switch (link_sta->agg.max_amsdu_len) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		amsdu->max_mpdu_size =
			(msta_link->wcid.phy_idx == MT_BAND0 && !link_sta->sta->mlo) ?
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991 :
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		return;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		return;
	default:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		return;
	}
}

static void
mt7996_mcu_sta_muru_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct mt7996_vif_link *mconf,
			struct ieee80211_link_sta *link_sta)
{
	struct mt7996_phy *phy = mconf->phy;
	struct ieee80211_he_cap_elem *elem = &link_sta->he_cap.he_cap_elem;
	struct sta_rec_muru *muru;
	struct tlv *tlv;

	if (link_conf->vif->type != NL80211_IFTYPE_STATION &&
	    link_conf->vif->type != NL80211_IFTYPE_AP)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MURU, sizeof(*muru));

	muru = (struct sta_rec_muru *)tlv;
	if (ok_vht(link_sta) || ok_he(link_sta)) {
		muru->cfg.mimo_dl_en = (link_conf->eht_mu_beamformer ||
					link_conf->he_mu_beamformer ||
					link_conf->vht_mu_beamformer ||
					link_conf->vht_mu_beamformee) &&
			!!(phy->muru_onoff & MUMIMO_DL);
		muru->cfg.mimo_ul_en = !!(phy->muru_onoff & MUMIMO_UL);
	}
	if (ok_he(link_sta)) {
		muru->cfg.ofdma_dl_en = !!(phy->muru_onoff & OFDMA_DL);
		muru->cfg.ofdma_ul_en = !!(phy->muru_onoff & OFDMA_UL);
	}

	if (ok_vht(link_sta))
		muru->mimo_dl.vht_mu_bfee =
			!!(link_sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	if (!ok_he(link_sta))
		return;

	muru->mimo_dl.partial_bw_dl_mimo =
		HE_PHY(CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO, elem->phy_cap_info[6]);

	muru->mimo_ul.full_ul_mimo =
		HE_PHY(CAP2_UL_MU_FULL_MU_MIMO, elem->phy_cap_info[2]);
	muru->mimo_ul.partial_ul_mimo =
		HE_PHY(CAP2_UL_MU_PARTIAL_MU_MIMO, elem->phy_cap_info[2]);

	muru->ofdma_dl.punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);
	muru->ofdma_dl.he_20m_in_40m_2g =
		HE_PHY(CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_20m_in_160m =
		HE_PHY(CAP8_20MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_80m_in_160m =
		HE_PHY(CAP8_80MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);

	muru->ofdma_ul.t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	muru->ofdma_ul.mu_cascading =
		HE_MAC(CAP2_MU_CASCADING, elem->mac_cap_info[2]);
	muru->ofdma_ul.uo_ra =
		HE_MAC(CAP3_OFDMA_RA, elem->mac_cap_info[3]);
	muru->ofdma_ul.rx_ctrl_frame_to_mbss =
		HE_MAC(CAP3_RX_CTRL_FRAME_TO_MULTIBSS, elem->mac_cap_info[3]);
}

static inline bool
mt7996_is_ebf_supported(struct mt7996_phy *phy,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta, bool bfee)
{
	int sts = hweight16(phy->mt76->chainmask);

	if (link_conf->vif->type != NL80211_IFTYPE_STATION &&
	    link_conf->vif->type != NL80211_IFTYPE_AP)
		return false;

	if (!bfee && sts < 2)
		return false;

	if (ok_eht(link_sta)) {
		struct ieee80211_sta_eht_cap *pc = &link_sta->eht_cap;
		struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;

		if (bfee)
			return link_conf->eht_su_beamformee &&
			       EHT_PHY(CAP0_SU_BEAMFORMER, pe->phy_cap_info[0]);
		else
			return link_conf->eht_su_beamformer &&
			       EHT_PHY(CAP0_SU_BEAMFORMEE, pe->phy_cap_info[0]);
	}

	if (ok_he(link_sta)) {
		struct ieee80211_he_cap_elem *pe = &link_sta->he_cap.he_cap_elem;

		if (bfee)
			return link_conf->he_su_beamformee &&
			       HE_PHY(CAP3_SU_BEAMFORMER, pe->phy_cap_info[3]);
		else
			return link_conf->he_su_beamformer &&
			       HE_PHY(CAP4_SU_BEAMFORMEE, pe->phy_cap_info[4]);
	}

	if (ok_vht(link_sta)) {
		u32 cap = link_sta->vht_cap.cap;

		if (bfee)
			return link_conf->vht_su_beamformee &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		else
			return link_conf->vht_su_beamformer &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	}

	return false;
}

static void
mt7996_mcu_sta_sounding_rate(struct sta_rec_bf *bf, struct mt7996_phy *phy)
{
	bf->sounding_phy = MT_PHY_TYPE_OFDM;
	bf->ndp_rate = 0;				/* mcs0 */
	if (is_mt7996(phy->mt76->dev))
		bf->ndpa_rate = MT7996_CFEND_RATE_DEFAULT;	/* ofdm 24m */
	else
		bf->ndpa_rate = MT7992_CFEND_RATE_DEFAULT;	/* ofdm 6m */

	bf->rept_poll_rate = MT7996_CFEND_RATE_DEFAULT;	/* ofdm 24m */
}

static void
mt7996_mcu_sta_bfer_ht(struct ieee80211_link_sta *link_sta,
		       struct mt7996_phy *phy, struct sta_rec_bf *bf,
		       bool explicit)
{
	struct ieee80211_mcs_info *mcs = &link_sta->ht_cap.mcs;
	u8 n = 0;

	bf->tx_mode = MT_PHY_TYPE_HT;

	if ((mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF) &&
	    (mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED))
		n = FIELD_GET(IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK,
			      mcs->tx_params);
	else if (mcs->rx_mask[3])
		n = 3;
	else if (mcs->rx_mask[2])
		n = 2;
	else if (mcs->rx_mask[1])
		n = 1;

	bf->nrow = hweight8(phy->mt76->antenna_mask) - 1;
	bf->ncol = min_t(u8, bf->nrow, n);
	bf->ibf_ncol = explicit ? min_t(u8, MT7996_IBF_MAX_NC, bf->ncol) :
				  min_t(u8, MT7996_IBF_MAX_NC, n);
}

static void
mt7996_mcu_sta_bfer_vht(struct ieee80211_link_sta *link_sta,
			struct mt7996_phy *phy, struct sta_rec_bf *bf,
			bool explicit)
{
	struct ieee80211_sta_vht_cap *pc = &link_sta->vht_cap;
	struct ieee80211_sta_vht_cap *vc = &phy->mt76->sband_5g.sband.vht_cap;
	u16 mcs_map = le16_to_cpu(pc->vht_mcs.rx_mcs_map);
	u8 nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);
	u8 tx_ant = hweight8(phy->mt76->antenna_mask) - 1;

	bf->tx_mode = MT_PHY_TYPE_VHT;

	if (explicit) {
		u8 sts, snd_dim;

		mt7996_mcu_sta_sounding_rate(bf, phy);

		sts = FIELD_GET(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
				pc->cap);
		snd_dim = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				    vc->cap);
		bf->nrow = min_t(u8, min_t(u8, snd_dim, sts), tx_ant);
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = min_t(u8, MT7996_IBF_MAX_NC, bf->ncol);

		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->nrow = 1;
	} else {
		bf->nrow = tx_ant;
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = min_t(u8, MT7996_IBF_MAX_NC, nss_mcs);

		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->ibf_nrow = 1;
	}
}

static void
mt7996_mcu_sta_bfer_he(struct ieee80211_link_sta *link_sta,
		       struct ieee80211_vif *vif, struct mt7996_phy *phy,
		       struct sta_rec_bf *bf, bool explicit)
{
	struct ieee80211_sta_he_cap *pc = &link_sta->he_cap;
	struct ieee80211_he_cap_elem *pe = &pc->he_cap_elem;
	const struct ieee80211_sta_he_cap *vc =
		mt76_connac_get_he_phy_cap(phy->mt76, vif);
	const struct ieee80211_he_cap_elem *ve = &vc->he_cap_elem;
	u16 mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80);
	u8 nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);
	u8 snd_dim, sts;

	if (!vc)
		return;

	bf->tx_mode = MT_PHY_TYPE_HE_SU;

	mt7996_mcu_sta_sounding_rate(bf, phy);

	bf->trigger_su = HE_PHY(CAP6_TRIG_SU_BEAMFORMING_FB,
				pe->phy_cap_info[6]);
	bf->trigger_mu = HE_PHY(CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB,
				pe->phy_cap_info[6]);
	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK,
		     pe->phy_cap_info[4]);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = explicit ? min_t(u8, MT7996_IBF_MAX_NC, bf->ncol) :
				  min_t(u8, MT7996_IBF_MAX_NC, nss_mcs);

	if (link_sta->bandwidth != IEEE80211_STA_RX_BW_160)
		return;

	/* go over for 160MHz and 80p80 */
	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_160);
		nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);

		bf->ncol_gt_bw80 = nss_mcs;
	}

	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80p80);
		nss_mcs = mt7996_mcu_get_sta_nss(mcs_map);

		if (bf->ncol_gt_bw80)
			bf->ncol_gt_bw80 = min_t(u8, bf->ncol_gt_bw80, nss_mcs);
		else
			bf->ncol_gt_bw80 = nss_mcs;
	}

	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK,
		     pe->phy_cap_info[4]);

	bf->nrow_gt_bw80 = min_t(int, snd_dim, sts);
}

static void
mt7996_mcu_sta_bfer_eht(struct ieee80211_link_sta *link_sta,
			struct ieee80211_vif *vif, struct mt7996_phy *phy,
			struct sta_rec_bf *bf, bool explicit)
{
	struct ieee80211_sta_eht_cap *pc = &link_sta->eht_cap;
	struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss = &pc->eht_mcs_nss_supp;
	const struct ieee80211_sta_eht_cap *vc =
		mt76_connac_get_eht_phy_cap(phy->mt76, vif);
	const struct ieee80211_eht_cap_elem_fixed *ve = &vc->eht_cap_elem;
	u8 nss_mcs = u8_get_bits(eht_nss->bw._80.rx_tx_mcs9_max_nss,
				 IEEE80211_EHT_MCS_NSS_RX) - 1;
	u8 snd_dim, sts;

	bf->tx_mode = MT_PHY_TYPE_EHT_MU;

	mt7996_mcu_sta_sounding_rate(bf, phy);

	bf->trigger_su = EHT_PHY(CAP3_TRIG_SU_BF_FDBK, pe->phy_cap_info[3]);
	bf->trigger_mu = EHT_PHY(CAP3_TRIG_MU_BF_PART_BW_FDBK, pe->phy_cap_info[3]);
	snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_80MHZ_MASK, ve->phy_cap_info[2]);
	sts = EHT_PHY(CAP0_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[0]) +
	      (EHT_PHY(CAP1_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[1]) << 1);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = explicit ? min_t(u8, MT7996_IBF_MAX_NC, bf->ncol) :
				  min_t(u8, MT7996_IBF_MAX_NC, nss_mcs);

	if (link_sta->bandwidth < IEEE80211_STA_RX_BW_160)
		return;

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_160MHZ_MASK, ve->phy_cap_info[2]);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_160MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._160.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts);
		bf->ncol_gt_bw80 = nss_mcs;
		break;
	case IEEE80211_STA_RX_BW_320:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_320MHZ_MASK, ve->phy_cap_info[2]) +
			  (EHT_PHY(CAP3_SOUNDING_DIM_320MHZ_MASK,
				   ve->phy_cap_info[3]) << 1);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_320MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._320.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts) << 4;
		bf->ncol_gt_bw80 = nss_mcs << 4;
		break;
	default:
		break;
	}
}

static void
mt7996_mcu_sta_bfer_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta,
			struct mt7996_vif_link *link)
{
#define EBF_MODE	BIT(0)
#define IBF_MODE	BIT(1)
#define BF_MAT_ORDER	4
	struct ieee80211_vif *vif = link_conf->vif;
	struct mt7996_phy *phy = link->phy;
	int tx_ant = hweight16(phy->mt76->chainmask) - 1;
	struct sta_rec_bf *bf;
	struct tlv *tlv;
	static const u8 matrix[BF_MAT_ORDER][BF_MAT_ORDER] = {
		{0, 0, 0, 0},
		{1, 1, 0, 0},	/* 2x1, 2x2, 2x3, 2x4 */
		{2, 4, 4, 0},	/* 3x1, 3x2, 3x3, 3x4 */
		{3, 5, 6, 0}	/* 4x1, 4x2, 4x3, 4x4 */
	};
	bool ebf;

	if (!(ok_ht(link_sta) || ok_he(link_sta)))
		return;

	ebf = mt7996_is_ebf_supported(phy, link_conf, link_sta, false);
	if (!ebf && !dev->ibf)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BF, sizeof(*bf));
	bf = (struct sta_rec_bf *)tlv;

	/* he/eht: eBF only, except mt7992 that has 5T on 5GHz also supports iBF
	 * vht: support eBF and iBF
	 * ht: iBF only, since mac80211 lacks of eBF support
	 */
	if (ok_eht(link_sta))
		mt7996_mcu_sta_bfer_eht(link_sta, vif, phy, bf, ebf);
	else if (ok_he(link_sta))
		mt7996_mcu_sta_bfer_he(link_sta, vif, phy, bf, ebf);
	else if (ok_vht(link_sta))
		mt7996_mcu_sta_bfer_vht(link_sta, phy, bf, ebf);
	else if (ok_ht(link_sta))
		mt7996_mcu_sta_bfer_ht(link_sta, phy, bf, ebf);
	else
		return;

	bf->bf_cap = ebf ? EBF_MODE : (dev->ibf ? IBF_MODE : 0);
	if (is_mt7992(&dev->mt76) && tx_ant == 4)
		bf->bf_cap |= IBF_MODE;

	bf->bw = link_sta->bandwidth;
	bf->ibf_dbw = link_sta->bandwidth;
	bf->ibf_nrow = tx_ant;

	if (link_sta->eht_cap.has_eht || link_sta->he_cap.has_he)
		bf->ibf_timeout = is_mt7992(&dev->mt76) ? MT7992_IBF_TIMEOUT :
							  MT7996_IBF_TIMEOUT;
	else if (!ebf && link_sta->bandwidth <= IEEE80211_STA_RX_BW_40 && !bf->ncol)
		bf->ibf_timeout = MT7996_IBF_TIMEOUT_LEGACY;
	else
		bf->ibf_timeout = MT7996_IBF_TIMEOUT;

	if (bf->ncol < BF_MAT_ORDER) {
		if (ebf)
			bf->mem_20m = tx_ant < BF_MAT_ORDER ?
				      matrix[tx_ant][bf->ncol] : 0;
		else
			bf->mem_20m = bf->nrow < BF_MAT_ORDER ?
				      matrix[bf->nrow][bf->ncol] : 0;
	}

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
	case IEEE80211_STA_RX_BW_80:
		bf->mem_total = bf->mem_20m * 2;
		break;
	case IEEE80211_STA_RX_BW_40:
		bf->mem_total = bf->mem_20m;
		break;
	case IEEE80211_STA_RX_BW_20:
	default:
		break;
	}
}

static void
mt7996_mcu_sta_bfee_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta,
			struct mt7996_vif_link *link)
{
	struct mt7996_phy *phy = link->phy;
	int tx_ant = hweight8(phy->mt76->antenna_mask) - 1;
	struct sta_rec_bfee *bfee;
	struct tlv *tlv;
	u8 nrow = 0;

	if (!(ok_vht(link_sta) || ok_he(link_sta)))
		return;

	if (!mt7996_is_ebf_supported(phy, link_conf, link_sta, true))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BFEE, sizeof(*bfee));
	bfee = (struct sta_rec_bfee *)tlv;

	if (ok_he(link_sta)) {
		struct ieee80211_he_cap_elem *pe = &link_sta->he_cap.he_cap_elem;

		nrow = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			      pe->phy_cap_info[5]);
	} else {
		struct ieee80211_sta_vht_cap *pc = &link_sta->vht_cap;

		nrow = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				 pc->cap);
	}

	/* reply with identity matrix to avoid 2x2 BF negative gain */
	bfee->fb_identity_matrix = (nrow == 1 && tx_ant == 2);
}

static void
mt7996_mcu_sta_tx_proc_tlv(struct sk_buff *skb)
{
	struct sta_rec_tx_proc *tx_proc;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_TX_PROC, sizeof(*tx_proc));

	tx_proc = (struct sta_rec_tx_proc *)tlv;
	tx_proc->flag = cpu_to_le32(UNI_IPCSO | UNI_TCPUDPCSO);
}

static void
mt7996_mcu_sta_hdrt_tlv(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct sta_rec_hdrt *hdrt;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDRT, sizeof(*hdrt));

	hdrt = (struct sta_rec_hdrt *)tlv;
	hdrt->hdrt_mode = 1;
}

static void
mt7996_mcu_sta_hdr_trans_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			     struct ieee80211_vif *vif, struct mt76_wcid *wcid)
{
	struct sta_rec_hdr_trans *hdr_trans;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDR_TRANS, sizeof(*hdr_trans));
	hdr_trans = (struct sta_rec_hdr_trans *)tlv;
	hdr_trans->dis_rx_hdr_tran = true;

	if (!wcid->sta)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		hdr_trans->to_ds = true;
	else
		hdr_trans->from_ds = true;

	if (!wcid)
		return;

	hdr_trans->dis_rx_hdr_tran = !test_bit(MT_WCID_FLAG_HDR_TRANS, &wcid->flags);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags)) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
	}

	if (vif->type == NL80211_IFTYPE_MESH_POINT) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
		hdr_trans->mesh = true;
	}
}

static enum mcu_mmps_mode
mt7996_mcu_get_mmps_mode(enum ieee80211_smps_mode smps)
{
	switch (smps) {
	case IEEE80211_SMPS_OFF:
		return MCU_MMPS_DISABLE;
	case IEEE80211_SMPS_STATIC:
		return MCU_MMPS_STATIC;
	case IEEE80211_SMPS_DYNAMIC:
		return MCU_MMPS_DYNAMIC;
	default:
		return MCU_MMPS_DISABLE;
	}
}

int mt7996_mcu_set_fixed_rate_ctrl(struct mt7996_dev *dev,
				   void *data, u16 version)
{
	struct ra_fixed_rate *req;
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len;

	len = sizeof(hdr) + sizeof(*req);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_RA_FIXED_RATE, sizeof(*req));
	req = (struct ra_fixed_rate *)tlv;
	req->version = cpu_to_le16(version);
	memcpy(&req->rate, data, sizeof(req->rate));

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(RA), true);
}

int mt7996_mcu_set_fixed_field(struct mt7996_dev *dev, struct mt7996_sta *msta,
			       void *data, u8 link_id, u32 field)
{
	struct mt7996_vif *mvif = msta->vif;
	struct mt7996_sta_link *msta_link;
	struct sta_rec_ra_fixed_uni *ra;
	struct sta_phy_uni *phy = data;
	struct mt76_vif_link *mlink;
	struct sk_buff *skb;
	int err = -ENODEV;
	struct tlv *tlv;

	rcu_read_lock();

	mlink = rcu_dereference(mvif->mt76.link[link_id]);
	if (!mlink)
		goto error_unlock;

	msta_link = rcu_dereference(msta->link[link_id]);
	if (!msta_link)
		goto error_unlock;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mlink,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto error_unlock;
	}

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA_UPDATE, sizeof(*ra));
	ra = (struct sta_rec_ra_fixed_uni *)tlv;

	switch (field) {
	case RATE_PARAM_AUTO:
		break;
	case RATE_PARAM_FIXED:
	case RATE_PARAM_FIXED_MCS:
	case RATE_PARAM_FIXED_GI:
	case RATE_PARAM_FIXED_HE_LTF:
		if (phy)
			ra->phy = *phy;
		break;
	case RATE_PARAM_MMPS_UPDATE: {
		struct ieee80211_sta *sta = wcid_to_sta(&msta_link->wcid);
		struct ieee80211_link_sta *link_sta;

		link_sta = rcu_dereference(sta->link[link_id]);
		if (!link_sta) {
			dev_kfree_skb(skb);
			goto error_unlock;
		}

		ra->mmps_mode = mt7996_mcu_get_mmps_mode(link_sta->smps_mode);
		break;
	}
	case RATE_PARAM_VHT_OMN_UPDATE: {
		struct ieee80211_sta *sta = wcid_to_sta(&msta_link->wcid);
		struct ieee80211_link_sta *link_sta;

		link_sta = rcu_dereference(sta->link[link_id]);
		if (!link_sta) {
			dev_kfree_skb(skb);
			goto error_unlock;
		}

		ra->op_mode = true;
		switch (link_sta->bandwidth) {
			case IEEE80211_STA_RX_BW_20:
				ra->op_vht_chan_width =
					IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ;
				break;
			case IEEE80211_STA_RX_BW_40:
				ra->op_vht_chan_width =
					IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ;
				break;
			case IEEE80211_STA_RX_BW_80:
				ra->op_vht_chan_width =
					IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ;
				break;
			case IEEE80211_STA_RX_BW_160:
				ra->op_vht_chan_width =
					IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ;
				break;
			default:
				return 0;
		}
		ra->op_vht_rx_nss = link_sta->rx_nss > 0 ? link_sta->rx_nss - 1 : 0;
		ra->op_vht_rx_nss_type = 0;
		break;
	}
	default:
		break;
	}
	ra->field = cpu_to_le32(field);

	rcu_read_unlock();

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
error_unlock:
	rcu_read_unlock();

	return err;
}

static int
mt7996_mcu_add_rate_ctrl_fixed(struct mt7996_dev *dev, struct mt7996_sta *msta,
			       struct ieee80211_vif *vif, u8 link_id)
{
	struct ieee80211_link_sta *link_sta;
	struct cfg80211_bitrate_mask mask;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct sta_phy_uni phy = {};
	struct ieee80211_sta *sta;
	int ret, nrates = 0, idx;
	enum nl80211_band band;
	bool has_he;

#define __sta_phy_bitrate_mask_check(_mcs, _gi, _ht, _he)			\
	do {									\
		u8 i, gi = mask.control[band]._gi;				\
		gi = (_he) ? gi : gi == NL80211_TXRATE_FORCE_SGI;		\
		phy.sgi = gi;							\
		phy.he_ltf = mask.control[band].he_ltf;				\
		for (i = 0; i < ARRAY_SIZE(mask.control[band]._mcs); i++) {	\
			if (!mask.control[band]._mcs[i])			\
				continue;					\
			nrates += hweight16(mask.control[band]._mcs[i]);	\
			phy.mcs = ffs(mask.control[band]._mcs[i]) - 1;		\
			if (_ht)						\
				phy.mcs += 8 * i;				\
		}								\
	} while (0)

	rcu_read_lock();

	link = mt7996_vif_link(dev, vif, link_id);
	if (!link)
		goto error_unlock;

	msta_link = rcu_dereference(msta->link[link_id]);
	if (!msta_link)
		goto error_unlock;

	sta = wcid_to_sta(&msta_link->wcid);
	link_sta = rcu_dereference(sta->link[link_id]);
	if (!link_sta)
		goto error_unlock;

	band = link->phy->mt76->chandef.chan->band;
	has_he = link_sta->he_cap.has_he;
	mask = link->bitrate_mask;
	idx = msta_link->wcid.idx;

	// TODO:  EHT support?
	if (ok_he(link_sta)) {
		__sta_phy_bitrate_mask_check(he_mcs, he_gi, 0, 1);
	} else if (ok_vht(link_sta)) {
		__sta_phy_bitrate_mask_check(vht_mcs, gi, 0, 0);
	} else if (ok_ht(link_sta)) {
		__sta_phy_bitrate_mask_check(ht_mcs, gi, 1, 0);
	} else {
		nrates = hweight32(mask.control[band].legacy);
		phy.mcs = ffs(mask.control[band].legacy) - 1;
	}

	rcu_read_unlock();

#undef __sta_phy_bitrate_mask_check

	/* fall back to auto rate control */
	if (mask.control[band].gi == NL80211_TXRATE_DEFAULT_GI &&
	    mask.control[band].he_gi == GENMASK(7, 0) &&
	    mask.control[band].he_ltf == GENMASK(7, 0) &&
	    nrates != 1)
		return 0;

	/* fixed single rate */
	if (nrates == 1) {
		ret = mt7996_mcu_set_fixed_field(dev, msta, &phy, link_id,
						 RATE_PARAM_FIXED_MCS);
		if (ret)
			return ret;
	}

	/* fixed GI */
	if (mask.control[band].gi != NL80211_TXRATE_DEFAULT_GI ||
	    mask.control[band].he_gi != GENMASK(7, 0)) {
		u32 addr;

		/* firmware updates only TXCMD but doesn't take WTBL into
		 * account, so driver should update here to reflect the
		 * actual txrate hardware sends out.
		 */
		addr = mt7996_mac_wtbl_lmac_addr(dev, idx, 7);
		if (has_he)
			mt76_rmw_field(dev, addr, GENMASK(31, 24), phy.sgi);
		else
			mt76_rmw_field(dev, addr, GENMASK(15, 12), phy.sgi);

		ret = mt7996_mcu_set_fixed_field(dev, msta, &phy, link_id,
						 RATE_PARAM_FIXED_GI);
		if (ret)
			return ret;
	}

	/* fixed HE_LTF */
	if (mask.control[band].he_ltf != GENMASK(7, 0)) {
		ret = mt7996_mcu_set_fixed_field(dev, msta, &phy, link_id,
						 RATE_PARAM_FIXED_HE_LTF);
		if (ret)
			return ret;
	}

	return 0;

error_unlock:
	rcu_read_unlock();

	return -ENODEV;
}

static void
mt7996_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb, struct mt7996_dev *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf,
			     struct ieee80211_link_sta *link_sta,
			     struct mt7996_vif_link *link)
{
#define INIT_RCPI 180
	enum ieee80211_sta_rx_bandwidth cap_bw = ieee80211_link_sta_cap_bw(link_sta);
	struct mt76_phy *mphy = link->phy->mt76;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	struct cfg80211_bitrate_mask *mask = &link->bitrate_mask;
	u32 cap = link_sta->sta->wme ? STA_CAP_WMM : 0;
	u8 cap_nss = ieee80211_link_sta_cap_nss(link_sta);
	enum nl80211_band band = chandef->chan->band;
	struct sta_rec_ra_uni *ra;
	struct tlv *tlv;
	u32 supp_rate = link_sta->supp_rates[band];

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra));
	ra = (struct sta_rec_ra_uni *)tlv;

	ra->valid = true;
	ra->auto_rate = true;
	ra->phy_mode = mt76_connac_get_phy_mode(mphy, vif, band, link_sta);
	ra->channel = chandef->chan->hw_value;
	ra->bw = mt76_connac_chan_bw(chandef);
	ra->phy.bw = (cap_bw == IEEE80211_STA_RX_BW_320) ? CMD_CBW_320MHZ : cap_bw;
	ra->mmps_mode = mt7996_mcu_get_mmps_mode(link_sta->smps_mode);
	ra->op_mode = cap_bw != link_sta->bandwidth || cap_nss != link_sta->rx_nss;
	ra->op_vht_chan_width = link_sta->bandwidth;
	ra->op_vht_rx_nss = link_sta->rx_nss - 1;
	ra->op_vht_rx_nss_type = 0;

	if (supp_rate) {
		supp_rate &= mask->control[band].legacy;
		ra->rate_len = hweight32(supp_rate);

		if (band == NL80211_BAND_2GHZ) {
			ra->supp_mode = MODE_CCK;
			ra->supp_cck_rate = supp_rate & GENMASK(3, 0);

			if (ra->rate_len > 4) {
				ra->supp_mode |= MODE_OFDM;
				ra->supp_ofdm_rate = supp_rate >> 4;
			}
		} else {
			ra->supp_mode = MODE_OFDM;
			ra->supp_ofdm_rate = supp_rate;
		}
	}

	if (ok_ht(link_sta)) {
		ra->supp_mode |= MODE_HT;
		ra->af = link_sta->ht_cap.ampdu_factor;
		ra->ht_gf = !!(link_sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);

		cap |= STA_CAP_HT;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			cap |= STA_CAP_SGI_20;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			cap |= STA_CAP_SGI_40;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC)
			cap |= STA_CAP_TX_STBC;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			cap |= STA_CAP_RX_STBC;
		if (link_conf->ht_ldpc &&
		    (link_sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
			cap |= STA_CAP_LDPC;

		mt7996_mcu_set_sta_ht_mcs(link_sta, ra->ht_mcs,
					  mask->control[band].ht_mcs);
		ra->supp_ht_mcs = *(__le32 *)ra->ht_mcs;
	}

	if (ok_vht(link_sta)) {
		u8 af;

		ra->supp_mode |= MODE_VHT;
		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       link_sta->vht_cap.cap);
		ra->af = max_t(u8, ra->af, af);

		cap |= STA_CAP_VHT;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			cap |= STA_CAP_VHT_SGI_80;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			cap |= STA_CAP_VHT_SGI_160;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
			cap |= STA_CAP_VHT_TX_STBC;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_1)
			cap |= STA_CAP_VHT_RX_STBC;
		if ((vif->type != NL80211_IFTYPE_AP || link_conf->vht_ldpc) &&
		    (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC))
			cap |= STA_CAP_VHT_LDPC;

		mt7996_mcu_set_sta_vht_mcs(link_sta, ra->supp_vht_mcs,
					   mask->control[band].vht_mcs);
	}

	if (ok_he(link_sta)) {
		ra->supp_mode |= MODE_HE;
		cap |= STA_CAP_HE;

		if (link_sta->he_6ghz_capa.capa)
			ra->af = le16_get_bits(link_sta->he_6ghz_capa.capa,
					       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	}
	ra->sta_cap = cpu_to_le32(cap);

	memset(ra->rx_rcpi, INIT_RCPI, sizeof(ra->rx_rcpi));
}

int mt7996_mcu_add_rate_ctrl(struct mt7996_dev *dev, struct mt7996_sta *msta,
			     struct ieee80211_vif *vif, u8 link_id,
			     bool changed)
{
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_link_sta *link_sta;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct ieee80211_sta *sta;
	struct sk_buff *skb;
	int ret = -ENODEV;

	rcu_read_lock();

	link = mt7996_vif_link(dev, vif, link_id);
	if (!link)
		goto error_unlock;

	msta_link = rcu_dereference(msta->link[link_id]);
	if (!msta_link)
		goto error_unlock;

	sta = wcid_to_sta(&msta_link->wcid);
	link_sta = rcu_dereference(sta->link[link_id]);
	if (!link_sta)
		goto error_unlock;

	link_conf = rcu_dereference(vif->link_conf[link_id]);
	if (!link_conf)
		goto error_unlock;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		goto error_unlock;
	}

	/* firmware rc algorithm refers to sta_rec_he for HE control.
	 * once dev->rc_work changes the settings driver should also
	 * update sta_rec_he here.
	 */
	if (changed)
		mt7996_mcu_sta_he_tlv(skb, link_conf, link_sta, link);

	/* sta_rec_ra accommodates BW, NSS and only MCS range format
	 * i.e 0-{7,8,9} for VHT.
	 */
	mt7996_mcu_sta_rate_ctrl_tlv(skb, dev, vif, link_conf, link_sta, link);

	rcu_read_unlock();

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	return mt7996_mcu_add_rate_ctrl_fixed(dev, msta, vif, link_id);

error_unlock:
	rcu_read_unlock();

	return ret;
}

#if 0
static int
mt7996_mcu_sta_init_vow(struct mt7996_vif_link *mconf,
			struct mt7996_sta_link *msta_link)
{
	struct mt7996_phy *phy = mconf->phy;
	struct mt7996_vow_sta_ctrl *vow = &msta_link->vow;
	u8 omac_idx = mconf->mt76.omac_idx;
	int ret;

	/* Assignment of STA BSS group index aligns FW.
	 * Each band has its own BSS group bitmap space.
	 * 0: BSS 0
	 * 4..18: BSS 0x11..0x1f
	 */
	vow->bss_grp_idx = (omac_idx <= HW_BSSID_MAX)
	                   ? omac_idx
	                   : HW_BSSID_MAX + omac_idx - EXT_BSSID_START;
	vow->paused = false;
	vow->drr_quantum[IEEE80211_AC_VO] = VOW_DRR_QUANTUM_IDX0;
	vow->drr_quantum[IEEE80211_AC_VI] = VOW_DRR_QUANTUM_IDX1;
	vow->drr_quantum[IEEE80211_AC_BE] = VOW_DRR_QUANTUM_IDX2;
	vow->drr_quantum[IEEE80211_AC_BK] = VOW_DRR_QUANTUM_IDX2;

	ret = mt7996_mcu_set_vow_drr_ctrl(phy, mconf, msta_link, VOW_DRR_CTRL_STA_BSS_GROUP);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_vow_drr_ctrl(phy, mconf, msta_link, VOW_DRR_CTRL_STA_PAUSE);
	if (ret)
		return ret;

	return mt7996_mcu_set_vow_drr_ctrl(phy, mconf, msta_link, VOW_DRR_CTRL_STA_ALL);
}
#endif

static void
mt7996_mcu_sta_mld_setup_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			     struct ieee80211_sta *sta, unsigned long valid_links)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long links = sta->valid_links;
	unsigned int nlinks = hweight16(links);
	struct mld_setup_link *mld_setup_link;
	struct sta_rec_mld_setup *mld_setup;
	struct mt7996_sta_link *msta_link;
	struct ieee80211_vif *vif;
	unsigned int link_id;
	struct tlv *tlv;

	msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
	if (!msta_link)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MLD,
				      sizeof(struct sta_rec_mld_setup) +
				      sizeof(struct mld_setup_link) * nlinks);

	mld_setup = (struct sta_rec_mld_setup *)tlv;
	memcpy(mld_setup->mld_addr, sta->addr, ETH_ALEN);
	mld_setup->setup_wcid = cpu_to_le16(msta_link->wcid.idx);
	mld_setup->primary_id = cpu_to_le16(msta_link->wcid.idx);

	if (msta->sec_link != msta->deflink_id) {
		msta_link = mt76_dereference(msta->link[msta->sec_link],
					     &dev->mt76);
		if (!msta_link)
			return;
	}
	mld_setup->seconed_id = cpu_to_le16(msta_link->wcid.idx);
	mld_setup->link_num = nlinks;

	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	mld_setup_link = (struct mld_setup_link *)mld_setup->link_info;
	mt76_dbg(&dev->mt76, MT76_DBG_STA,
		 "%s: STA %pM pri_link=%u, pri_wcid=%u, sec_link=%u, sec_wcid=%u valid_links: 0x%lx\n",
		 __func__, sta->addr, msta->deflink_id,
		 le16_to_cpu(mld_setup->primary_id),
		 msta->sec_link, le16_to_cpu(mld_setup->seconed_id), links);
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *link;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link) {
			mt76_dbg(&dev->mt76, MT76_DBG_STA,
				 "%s: WARNING: STA %pM could not find msta_link for link_id: %d.\n",
				 __func__, sta->addr, link_id);
			continue;
		}

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link) {
			mt76_dbg(&dev->mt76, MT76_DBG_STA,
				 "%s: WARNING: STA %pM could not find msta_link for link_id: %d.\n",
				 __func__, sta->addr, link_id);
			continue;
		}

		mld_setup_link->wcid = cpu_to_le16(msta_link->wcid.idx);
		mld_setup_link->bss_idx = link->mt76.idx;

		mt76_dbg(&dev->mt76, MT76_DBG_STA,
			 "%s: link_id(%d) wcid(%d) bss_idx(%d)\n",
			 __func__, link_id, mld_setup_link->wcid,
			 mld_setup_link->bss_idx);

		mld_setup_link++;
	}
}

static void
mt7996_mcu_sta_eht_mld_tlv(struct mt7996_dev *dev, struct sk_buff *skb,
			   struct ieee80211_sta *sta)
{
	struct sta_rec_eht_mld *eht_mld;
	struct tlv *tlv;
	int i;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT_MLD, sizeof(*eht_mld));
	eht_mld = (struct sta_rec_eht_mld *)tlv;

	for (i = 0; i < ARRAY_SIZE(eht_mld->str_cap); i++)
		eht_mld->str_cap[i] = 0x7;
	// TODO:  Enable this after pulling in appropriate mac80211 patch.
	// autobuild/unified/filogic/mac80211/24.10/files/package/kernel/mac80211/patches/subsys/0055-mtk-mac80211-Add-support-for-EMLSR-support.patch
	// Looks like it would require hostapd patches too, so maybe no so important.
	//eht_mld->eml_cap = cpu_to_le16(sta->eml_capa);
}

int mt7996_mcu_add_sta(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_bss_conf *link_conf,
		       struct ieee80211_link_sta *link_sta,
		       struct mt7996_vif_link *link,
		       struct mt7996_sta_link *msta_link,
		       int conn_state, bool newly)
{
	struct mt76_wcid *wcid = msta_link ? &msta_link->wcid : link->mt76.wcid;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;
	struct sk_buff *skb;
	//int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76, wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec basic */
	mt76_connac_mcu_sta_basic_tlv(&dev->mt76, skb, vif, link_conf, link_sta,
				      conn_state, newly);
	mt76_dbg(&dev->mt76, MT76_DBG_DEV,
		 "%s: link=%u, wcid=%u, newly=%d, conn_state=%d\n",
		   __func__, wcid->link_id, wcid->idx, newly, conn_state);

	if (conn_state == CONN_STATE_DISCONNECT)
		goto out;

	/* starec hdr trans */
	mt7996_mcu_sta_hdr_trans_tlv(dev, skb, vif, wcid);
	/* starec tx proc */
	mt7996_mcu_sta_tx_proc_tlv(skb);

	/* tag order is in accordance with firmware dependency. */
	if (link_sta) {
		/* starec hdrt mode */
		mt7996_mcu_sta_hdrt_tlv(dev, skb);
		if (conn_state == CONN_STATE_CONNECT) {
			/* starec bfer */
			mt7996_mcu_sta_bfer_tlv(dev, skb, link_conf, link_sta,
						link);
			/* starec bfee */
			mt7996_mcu_sta_bfee_tlv(dev, skb, link_conf, link_sta,
						link);
		}
		/* starec ht */
		mt7996_mcu_sta_ht_tlv(dev, skb, link_sta);
		/* starec vht */
		mt7996_mcu_sta_vht_tlv(skb, link_sta);
		/* starec uapsd */
		mt76_connac_mcu_sta_uapsd(skb, link_conf->vif, sta);
		/* starec amsdu */
		mt7996_mcu_sta_amsdu_tlv(dev, skb, link_conf->vif, link_sta,
					 msta_link);
		/* starec he */
		mt7996_mcu_sta_he_tlv(skb, link_conf, link_sta, link);
		/* starec he 6g*/
		mt7996_mcu_sta_he_6g_tlv(skb, link_sta, dev);
		/* starec eht */
		mt7996_mcu_sta_eht_tlv(skb, link_sta, dev);
		/* starec muru */
		mt7996_mcu_sta_muru_tlv(dev, skb, link_conf, link, link_sta);

		if (sta->mlo) {
			mt7996_mcu_sta_mld_setup_tlv(dev, skb, sta, sta->valid_links);
			mt7996_mcu_sta_eht_mld_tlv(dev, skb, sta);
		}
	}

#if 0
	// NOTE:  Ifdef'd out in mtk upstream owrt tree too
	ret = mt7996_mcu_sta_init_vow(mconf, msta_link);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}
#endif
out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7996_mcu_teardown_mld_sta(struct mt7996_dev *dev,
				struct mt7996_vif_link *link,
				struct mt7996_sta_link *msta_link)
{
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_add_tlv(skb, STA_REC_MLD_OFF, sizeof(struct tlv));

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7996_mcu_sta_key_tlv(struct mt76_dev *dev,
		       struct mt76_wcid *wcid,
		       struct sk_buff *skb,
		       struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd,
		       u8 *pn)
{
	struct sta_rec_sec_uni *sec;
	struct sec_key_uni *sec_key;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_KEY_V2, sizeof(*sec));
	sec = (struct sta_rec_sec_uni *)tlv;
	sec->add = cmd;
	sec->n_cipher = 1;
	sec_key = &sec->key[0];
	sec_key->wlan_idx = cpu_to_le16(wcid->idx);
	sec_key->key_id = key->keyidx;

	if (cmd == SET_KEY) {
		u8 cipher;

		cipher = mt76_connac_mcu_get_cipher(key->cipher);
		if (cipher == MCU_CIPHER_NONE)
			return -EOPNOTSUPP;

		sec_key->mgmt_prot = 0;
		sec_key->cipher_id = cipher;
		sec_key->cipher_len = sizeof(*sec_key);
		sec_key->key_len = key->keylen;
		sec_key->need_resp = 0;
		memcpy(sec_key->key, key->key, key->keylen);
		if (sec_key->key_id == 6 || sec_key->key_id == 7) {
			switch (key->cipher) {
			case WLAN_CIPHER_SUITE_AES_CMAC:
				sec_key->cipher_id = MCU_CIPHER_BCN_PROT_CMAC_128;
				break;
			case WLAN_CIPHER_SUITE_BIP_GMAC_128:
				sec_key->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_128;
				break;
			case WLAN_CIPHER_SUITE_BIP_GMAC_256:
				sec_key->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_256;
				break;
			case WLAN_CIPHER_SUITE_BIP_CMAC_256:
				if (is_mt7990(dev)) {
					sec_key->cipher_id = MCU_CIPHER_BCN_PROT_CMAC_256;
					break;
				}
				fallthrough;
			default:
				dev_err(dev->dev, "Unsupported BIGTK cipher\n");
				return -EOPNOTSUPP;
			}
			sec_key->bcn_mode = BP_SW_MODE;
			if (is_mt7990(dev)) {
				sec_key->bcn_mode = BP_HW_MODE;
				wcid->hw_bcn_prot = true;
			}
			memcpy(sec_key->pn, pn, 6);
		}

		if (cipher == MCU_CIPHER_TKIP) {
			/* Rx/Tx MIC keys are swapped */
			memcpy(sec_key->key + 16, key->key + 24, 8);
			memcpy(sec_key->key + 24, key->key + 16, 8);
		}
	} else {
		/* connac3 fw use set key action to apply removing bigtk and other
		 * group keys should just use set key to overwrite the old ones. */
		sec->add = SET_KEY;
		if (is_mt7990(dev) && (sec_key->key_id == 6 || sec_key->key_id == 7))
			wcid->hw_bcn_prot = false;
	}

	return 0;
}

int mt7996_mcu_add_key(struct mt76_dev *dev, struct mt7996_vif_link *mconf,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd,
		       u8 *pn)
{
	struct sk_buff *skb;
	int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(dev, (struct mt76_vif_link *)mconf,
					      wcid, MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7996_mcu_sta_key_tlv(dev, wcid, skb, key, cmd, pn);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}

	return mt76_mcu_skb_send_msg(dev, skb, mcu_cmd, true);
}

int mt7996_mcu_get_pn(struct mt7996_dev *dev,
		      struct mt7996_vif_link *link,
		      struct mt7996_sta_link *msta_link, u8 *pn)
{
#define TSC_TYPE_BIGTK_PN 2
	struct sta_rec_pn_info *pn_info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					    &msta_link->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_PN_INFO, sizeof(*pn_info));
	pn_info = (struct sta_rec_pn_info *)tlv;

	pn_info->tsc_type = TSC_TYPE_BIGTK_PN;
	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD_QUERY(STA_REC_UPDATE),
					    true, &rskb);
	if (ret)
		return ret;

	skb_pull(rskb, 4);

	pn_info = (struct sta_rec_pn_info *)rskb->data;
	if (le16_to_cpu(pn_info->tag) == STA_REC_PN_INFO)
		memcpy(pn, pn_info->pn, 6);

	dev_kfree_skb(rskb);
	return 0;
}

int mt7996_mcu_add_dev_info(struct mt7996_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink, bool enable)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 band_idx;
			u8 __rsv[2];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 __rsv;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mlink->omac_idx,
			.band_idx = mlink->band_idx,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
		},
	};

	if (mlink->omac_idx >= REPEATER_BSSID_START)
		return mt7996_mcu_muar_config(dev, mlink, link_conf->addr, false, enable);

	if (link_conf) {
		memcpy(data.tlv.omac_addr, link_conf->addr, ETH_ALEN);
		mt76_dbg(&dev->mt76, MT76_DBG_DEV,
			 "%s: band=%u, omac=%u, addr=%pM, en=%d\n",
			 __func__, data.hdr.band_idx, data.hdr.omac_idx,
			 data.tlv.omac_addr, enable);
	}
	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static int
mt7996_mcu_mld_reconf(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		      u16 removed_links, u16 *removal_count)
{
	struct mld_req_hdr hdr = { .mld_idx = 0xff };
	struct mld_reconf_timer *rt;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*rt);
	unsigned long rem = removed_links;
	u8 link_id;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_CMD_MLD_RECONF_AP_REM_TIMER, sizeof(*rt));
	rt = (struct mld_reconf_timer *)tlv;
	rt->link_bitmap = cpu_to_le16(removed_links);

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);
		struct mt7996_vif_link *mconf =	mt7996_vif_link(dev, vif, link_id);
		u8 band_idx;
		u16 to_sec;

		if (!conf || !mconf)
			continue;

		band_idx = mconf->phy->mt76->band_idx;
		to_sec = conf->beacon_int * removal_count[link_id] / 1000;
		rt->to_sec[band_idx] = cpu_to_le16(to_sec);
		rt->bss_idx[band_idx] = mconf->mt76.idx;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD), true);
}

int mt7996_mcu_mld_reconf_stop_link(struct mt7996_dev *dev,
				    struct ieee80211_vif *vif, u16 removed_links)
{
	struct mld_req_hdr hdr = { .mld_idx = 0 };
	struct mld_reconf_stop_link *sl;
	struct sk_buff *skb;
	struct tlv *tlv;
	unsigned long rem = removed_links;
	int len = sizeof(hdr) + sizeof(*sl), link_id;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_CMD_MLD_RECONF_STOP_LINK, sizeof(*sl));
	sl = (struct mld_reconf_stop_link *)tlv;
	sl->link_bitmap = cpu_to_le16(removed_links);

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf =	mt7996_vif_link(dev, vif, link_id);

		if (!mconf)
			continue;

		sl->bss_idx[link_id] = mconf->mt76.idx;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD), true);
}

int mt7996_mcu_mld_link_oper(struct mt7996_phy *phy,
			     struct ieee80211_bss_conf *conf,
			     struct mt7996_vif_link *mconf, bool add)
{
	struct ieee80211_vif *vif = conf->vif;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = phy->dev;
	struct bss_mld_link_op_tlv *mld_op;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &mconf->mt76,
					 MT7996_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_BSS_INFO_MLD_LINK_OP, sizeof(*mld_op));
	mld_op = (struct bss_mld_link_op_tlv *)tlv;
	mld_op->link_operation = add;
	mld_op->own_mld_id = mconf->own_mld_id;
	mld_op->link_id = conf->link_id;
	memcpy(mld_op->mac_addr, vif->addr, ETH_ALEN);

	if (add) {
		mld_op->group_mld_id = mvif->group_mld_id;
		mld_op->remap_idx = mvif->mld_remap_id;
	} else {
		mld_op->group_mld_id = 0xff;
		mld_op->remap_idx = 0xff;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7996_mcu_mld_set_attlm(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			     u16 disabled_links, u16 switch_time, u32 duration)
{
	struct mld_req_hdr hdr = { .mld_idx = 0xff };
	struct mld_attlm_req *req;
	struct mt7996_mcu_mld_attlm_resp_event *resp;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*req), ret;
	unsigned long valid_disabled_links =
			(unsigned long) vif->valid_links & disabled_links;
	u8 link_id;
	bool bss_idx_set = false;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_CMD_MLD_ATTLM_RES_REQ, sizeof(*req));
	req = (struct mld_attlm_req *)tlv;

	req->attlm_idx = 0;
	req->mst_timer = 1;
	req->e_timer = 1;
	req->mst_timer_adv_time = cpu_to_le16(50);
	req->e_timer_adv_time = cpu_to_le16(0);
	req->mst_duration = cpu_to_le32(switch_time * USEC_PER_MSEC);
	req->e_duration = cpu_to_le32(duration * USEC_PER_MSEC);
	req->disabled_link_bitmap = cpu_to_le16(valid_disabled_links);
	for_each_set_bit(link_id, &valid_disabled_links,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf = mt7996_vif_link(dev, vif, link_id);

		if (!mconf)
			continue;

		if (!bss_idx_set) {
			req->bss_idx = mconf->mt76.idx;
			bss_idx_set = true;
		}

		req->disabled_bss_idx[link_id] = mconf->mt76.idx;
	}

	if (!bss_idx_set) {
		dev_kfree_skb(skb);
		return -ENOLINK;
	}

	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD),
					    true, &rskb);

	if (ret)
		return ret;

	skb_pull(rskb, sizeof(struct mt7996_mcu_mld_event) - sizeof(struct mt7996_mcu_rxd));
	resp = (struct mt7996_mcu_mld_attlm_resp_event *)rskb->data;
	switch(le16_to_cpu(resp->tag)) {
	case UNI_EVENT_MLD_ATTLM_RES_RSP: {
		u32 tsf_0, tsf_1;
		u64 switch_time_tsf;
		u16 switch_time_tsf_tu;

		tsf_0 = le32_to_cpu(resp->switch_time_tsf[0]);
		tsf_1 = le32_to_cpu(resp->switch_time_tsf[1]);
		switch_time_tsf = (u64)tsf_0 + ((u64)tsf_1 << 32);
		switch_time_tsf_tu = (u16)u64_get_bits(switch_time_tsf,
						   GENMASK_ULL(25, 10));
		ieee80211_attlm_notify(vif, switch_time_tsf_tu,
				       NL80211_ATTLM_STARTED, GFP_KERNEL);
		break;
	}
	default:
		break;
	}

	dev_kfree_skb(rskb);
	return ret;
}

int
mt7996_mcu_peer_mld_ttlm_req(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct ieee80211_neg_ttlm *neg_ttlm)
{
	struct mt7996_sta_link *msta_link;
	struct mt7996_sta *msta;
	struct peer_mld_req_hdr hdr = { .mld_idx = 0xff };
	struct peer_mld_ttlm_req *req;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*req);
	unsigned long valid_links = (unsigned long)vif->valid_links;
	u8 link_id, tid;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	if (!sta || !neg_ttlm)
		return -EINVAL;

	memcpy(hdr.peer_mld_addr, sta->addr, ETH_ALEN);
	msta = (struct mt7996_sta *)sta->drv_priv;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	dev_dbg(dev->mt76.dev, "Setup TTLM for %pM\n", sta->addr);
	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_CMD_PEER_MLD_TTLM_REQ,
				     sizeof(*req));
	req = (struct peer_mld_ttlm_req *)tlv;

	memcpy(req->mld_addr, vif->addr, ETH_ALEN);
	req->enabled_link_bitmap = cpu_to_le16(vif->valid_links);
	rcu_read_lock();
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		req->link_to_wcid[link_id] = cpu_to_le16(msta_link->wcid.idx);

		/* Skip all tid for tx paused link */
		if (msta->vif->tx_paused_links & BIT(link_id))
			continue;

		for (tid = 0; tid < IEEE80211_TTLM_NUM_TIDS; tid++) {
			if (neg_ttlm->downlink[tid] & BIT(link_id))
				req->dl_tid_map[link_id] |= BIT(tid);
			if (neg_ttlm->uplink[tid] & BIT(link_id))
				req->ul_tid_map[link_id] |= BIT(tid);
		}

		dev_dbg(dev->mt76.dev, "link_id=%u, dl_bitmap=%u, ul_bitmap=%u\n",
			link_id, req->dl_tid_map[link_id],
			req->ul_tid_map[link_id]);
	}

	rcu_read_unlock();

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(PEER_MLD),
				     true);
}

static void
mt7996_mcu_beacon_cntdwn(struct sk_buff *rskb, struct sk_buff *skb,
			 struct ieee80211_mutable_offsets *offs,
			 bool csa)
{
	struct bss_bcn_cntdwn_tlv *info;
	struct tlv *tlv;
	u16 tag;

	if (!offs->cntdwn_counter_offs[0])
		return;

	tag = csa ? UNI_BSS_INFO_BCN_CSA : UNI_BSS_INFO_BCN_BCC;

	tlv = mt7996_mcu_add_uni_tlv(rskb, tag, sizeof(*info));

	info = (struct bss_bcn_cntdwn_tlv *)tlv;
	info->cnt = skb->data[offs->cntdwn_counter_offs[0]];
}

static void
mt7996_mcu_beacon_mbss(struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs,
		       struct mt7996_mbssid_data *mbssid_data)
{
	struct bss_bcn_mbss_tlv *mbss;
	struct tlv *tlv;
	int i;

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_MBSSID, sizeof(*mbss));

	mbss = (struct bss_bcn_mbss_tlv *)tlv;
	mbss->offset[0] = cpu_to_le16(offs->tim_offset);
	mbss->bitmap = cpu_to_le32(1);

	for (i = 0; i < MAX_BEACON_NUM; i++) {
		if (!mbssid_data[i].mbssid_idx.valid)
			continue;

		mbss->offset[i] = cpu_to_le16(mbssid_data[i].mbssid_idx.offset);
		mbss->bitmap |= cpu_to_le32(BIT(i));
	}
}

static bool
mt7996_mcu_beacon_is_cu_link(struct sk_buff *skb, struct mt7996_vif_link *mconf,
			     u16 tail_offset)
{
	const struct element *elem;
	u8 *beacon_tail = skb->data + tail_offset;
	bool has_ml_ie = false;
	int bpcc;

	for_each_element_extid(elem, WLAN_EID_EXT_EHT_MULTI_LINK,
			       beacon_tail, skb->len - tail_offset)
		if (ieee80211_mle_type_ok(elem->data + 1,
					  IEEE80211_ML_CONTROL_TYPE_BASIC,
					  elem->datalen - 1)) {
			has_ml_ie = true;
			break;
		}

	if (!has_ml_ie)
		return false;

	bpcc = ieee80211_mle_get_bss_param_ch_cnt(elem->data + 1);
	if (bpcc < 0 || bpcc == mconf->bpcc)
		return false;

	mconf->bpcc = bpcc;

	return true;
}

static void
mt7996_mcu_beacon_crit_update(struct sk_buff *rskb, struct sk_buff *skb,
			      struct ieee80211_bss_conf *conf,
			      struct mt7996_vif_link *mconf,
			      struct ieee80211_mutable_offsets *offs,
			      struct mt7996_mbssid_data *mbssid_data)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct bss_bcn_crit_update_tlv *crit;
	struct tlv *tlv;
	u16 capab_info = le16_to_cpu(mgmt->u.beacon.capab_info);
	int i;

	if (!ieee80211_vif_is_mld(conf->vif) ||
	    !(capab_info & (WLAN_CAPABILITY_PBCC |
			    WLAN_CAPABILITY_NON_TX_BSSID_CU)))
		return;

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_CRIT_UPDATE, sizeof(*crit));

	crit = (struct bss_bcn_crit_update_tlv *)tlv;
	if (capab_info & WLAN_CAPABILITY_PBCC)
		crit->bss_bitmap = cpu_to_le32(BIT(0));
	/* The beacon of the CU link should be set in sequence
	 * to ensure it appears in the air before the beacon of
	 * the non-CU link.
	 */
	if (!mt7996_mcu_beacon_is_cu_link(skb, mconf, offs->tim_offset))
		crit->bypass_seq_bitmap = cpu_to_le32(BIT(0));
	crit->tim_ie_pos[0] = cpu_to_le16(offs->tim_offset);
	crit->cap_info_ie_pos[0] = cpu_to_le16(offsetof(struct ieee80211_mgmt,
							u.beacon.capab_info));

	for (i = 1; i < MAX_BEACON_NUM; i++) {
		u16 ntx_bss_capab_info, offset;

		if (!mbssid_data[i].ntx_bss_cap.valid)
			continue;

		offset = mbssid_data[i].ntx_bss_cap.offset;
		ntx_bss_capab_info = le16_to_cpu(*(skb->data + offset + 2));
		if (!(ntx_bss_capab_info & WLAN_CAPABILITY_PBCC))
			continue;

		crit->cap_info_ie_pos[i] = cpu_to_le16(offset);
		crit->bss_bitmap |= cpu_to_le32(BIT(i));
		if (!mbssid_data[i].is_cu_link)
			crit->bypass_seq_bitmap |= cpu_to_le32(BIT(i));
	}

	crit->require_event = true;
}

static void
mt7996_mcu_beacon_sta_prof_csa(struct sk_buff *rskb,
			       struct ieee80211_bss_conf *conf,
			       struct ieee80211_mutable_offsets *offs)
{
	struct ieee80211_vif *vif = conf->vif;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *cs_mconf;
	struct bss_bcn_sta_prof_cntdwn_tlv *sta_prof;
	struct tlv *tlv;
	u8 cs_band;

	if (!ieee80211_vif_is_mld(vif) || !offs->sta_prof_cntdwn_offs[0])
		return;

	cs_mconf = mt7996_vif_link(mvif->dev, conf->vif, mvif->cs_link_id);
	if (!cs_mconf)
		return;

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_STA_PROF_CSA, sizeof(*sta_prof));

	cs_band = cs_mconf->phy->mt76->band_idx;
	sta_prof = (struct bss_bcn_sta_prof_cntdwn_tlv *)tlv;
	sta_prof->sta_prof_csa_offs[cs_band] = cpu_to_le16(offs->sta_prof_cntdwn_offs[0] - 4);
	sta_prof->cs_bss_idx[cs_band] = cs_mconf->mt76.idx;
}

static void
mt7996_mcu_beacon_cont(struct mt7996_dev *dev,
		       struct ieee80211_bss_conf *link_conf,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs,
		       struct mt7996_vif_link *link)
{
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	u8 *buf;

	if (link->msta_link.wcid.hw_bcn_prot)
		wcid = &link->msta_link.wcid;

	bcn->pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	bcn->tim_ie_pos = cpu_to_le16(offs->tim_offset);

	if (offs->cntdwn_counter_offs[0]) {
		u16 offset = offs->cntdwn_counter_offs[0];

		if (link_conf->csa_active)
			bcn->csa_ie_pos = cpu_to_le16(offset - 4);
		if (link_conf->color_change_active)
			bcn->bcc_ie_pos = cpu_to_le16(offset - 3);
	}

	buf = (u8 *)bcn + sizeof(*bcn);
	mt7996_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0,
			      BSS_CHANGED_BEACON);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);
}

static void
mt7996_mcu_beacon_ml_reconf(struct mt7996_dev *dev,
			    struct ieee80211_bss_conf *conf,
			    struct sk_buff *rskb, struct sk_buff *skb,
			    struct ieee80211_mutable_offsets *offs)
{
	struct bss_bcn_ml_reconf_tlv *reconf;
	struct bss_bcn_ml_reconf_offset *reconf_offs;
	const struct element *elem, *sub;
	struct tlv *tlv;
	u16 removal_offs[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	u16 removal_count[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	u16 tail_offset = offs->tim_offset + offs->tim_length;
	unsigned long removed_links = 0;
	bool has_reconf = false;
	u8 link_id, *beacon_tail = skb->data + tail_offset;

	if (!ieee80211_vif_is_mld(conf->vif))
		return;

	/* TODO: currently manually parse reconf info directly from the IE, it
	 * is expected to be passed from upper layer in the future.
	 */
	for_each_element_extid(elem, WLAN_EID_EXT_EHT_MULTI_LINK,
			       beacon_tail, skb->len - tail_offset) {
		if (ieee80211_mle_type_ok(elem->data + 1,
					  IEEE80211_ML_CONTROL_TYPE_RECONF,
					  elem->datalen - 1)) {
			has_reconf = true;
			break;
		}
	}

	if (!has_reconf)
		return;

	for_each_mle_subelement(sub, elem->data + 1, elem->datalen - 1) {
		struct ieee80211_mle_per_sta_profile *prof = (void *)sub->data;
		u8 *pos = prof->variable;
		u16 control;

		if (sub->id != IEEE80211_MLE_SUBELEM_PER_STA_PROFILE)
			continue;

		if (!ieee80211_mle_reconf_sta_prof_size_ok(sub->data,
							   sub->datalen))
			return;

		control = le16_to_cpu(prof->control);
		link_id = control & IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID;

		removed_links |= BIT(link_id);

		if (control & IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT)
			pos += 6;

		if (control & IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT) {
			removal_offs[link_id] = pos - skb->data;
			removal_count[link_id] = le16_to_cpu(*(__le16 *)pos);
		}
	}

	if (!removed_links)
		return;

	/* the first link to be removed */
	if (conf->link_id == ffs(removed_links) - 1)
		mt7996_mcu_mld_reconf(dev, conf->vif, removed_links, removal_count);

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_ML_RECONF,
				     sizeof(*reconf) +
				     sizeof(*reconf_offs) * hweight16(removed_links));
	reconf = (struct bss_bcn_ml_reconf_tlv *)tlv;
	reconf->reconf_count = hweight16(removed_links);

	reconf_offs = (struct bss_bcn_ml_reconf_offset *)reconf->offset;
	for_each_set_bit(link_id, &removed_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf = mt7996_vif_link(dev, conf->vif, link_id);

		reconf_offs->ap_removal_timer_offs =
			cpu_to_le16(removal_offs[link_id]);
		reconf_offs->bss_idx = mconf->mt76.idx;
		reconf_offs++;
	}
}

static void
mt7996_mcu_beacon_ttlm(struct mt7996_dev *dev, struct ieee80211_bss_conf *conf,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct ieee80211_mutable_offsets *offs)
{
	u16 offset = 0, tail_offset = offs->tim_offset + offs->tim_length;
	struct bss_bcn_attlm_offset_tlv *attlm_offset;
	u8 *beacon_tail = skb->data + tail_offset;
	const struct element *elem;
	struct ieee80211_ttlm_elem *ttlm;
	bool cntdown_ttlm = false;
	struct tlv *tlv;

	if (!ieee80211_vif_is_mld(conf->vif))
		return;

	for_each_element_extid(elem, WLAN_EID_EXT_TID_TO_LINK_MAPPING,
			       beacon_tail, skb->len - tail_offset) {
		if (ieee80211_tid_to_link_map_size_ok(elem->data + 1,
						      elem->datalen - 1)) {
			ttlm = (struct ieee80211_ttlm_elem *)elem->data + 1;
			if (!(ttlm->control &
			      IEEE80211_TTLM_CONTROL_SWITCH_TIME_PRESENT) &&
			    (ttlm->control &
			     IEEE80211_TTLM_CONTROL_EXPECTED_DUR_PRESENT)) {
				offset = (u8 *)elem - skb->data;
				cntdown_ttlm = true;
				break;
			}
		}
	}

	if (!cntdown_ttlm)
		return;

	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_ATTLM,
				     sizeof(*attlm_offset));
	attlm_offset = (struct bss_bcn_attlm_offset_tlv *)tlv;
	attlm_offset->valid_id_bitmap = BIT(0);
	attlm_offset->offset = cpu_to_le16(offset);
}

static bool
mt7996_is_nontx_cu_link(struct mt7996_phy *phy,
			   const struct element *sub_elem, u8 bssid_idx)
{
	const struct element *ml_elem;
	struct mt7996_vif_link *mconf;
	int bpcc;

	ml_elem = cfg80211_find_ext_elem(WLAN_EID_EXT_EHT_MULTI_LINK,
					 sub_elem->data, sub_elem->datalen);
	if (!ml_elem || !ieee80211_mle_type_ok(ml_elem->data + 1,
					       IEEE80211_ML_CONTROL_TYPE_BASIC,
					       ml_elem->datalen - 1))
		return false;

	bpcc = ieee80211_mle_get_bss_param_ch_cnt(ml_elem->data + 1);
	if (bpcc < 0)
		return false;

	rcu_read_lock();
	mconf =	rcu_dereference(phy->mbssid_conf[bssid_idx]);
	if (!mconf || bpcc == mconf->bpcc) {
		rcu_read_unlock();
		return false;
	}

	mconf->bpcc = bpcc;

	rcu_read_unlock();
	return true;
}

static void
mt7996_parse_mbssid_elems(struct mt7996_phy *phy, struct sk_buff *skb,
			  u16 mbssid_off, struct mt7996_mbssid_data *mbssid_data)
{
	const struct element *elem;

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID,
			    &skb->data[mbssid_off],
			    skb->len - mbssid_off) {
		const struct element *sub_elem;

		if (elem->datalen < 2)
			continue;

		for_each_element(sub_elem, elem->data + 1, elem->datalen - 1) {
			const struct ieee80211_bssid_index *idx;
			const u8 *idx_ie, *ntx_bss_cap_ie;
			u8 bssid_idx;
			bool cu_flag = false;

			/* not a valid BSS profile */
			if (sub_elem->id || sub_elem->datalen < 4)
				continue;

			/* Find WLAN_EID_MULTI_BSSID_IDX
			 * in the merged nontransmitted profile
			 */
			idx_ie = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX,
						  sub_elem->data, sub_elem->datalen);

			/* At leat the BSSID idx should be preset and valid.
			 * Otherwise we do not know the idx.
			 * FIXME: Handle split subelements if other
			 * subelements need parsing
			 */
			if (!idx_ie || idx_ie[1] < sizeof(*idx))
				continue;

			idx = (void *)(idx_ie + 2);
			bssid_idx = idx->bssid_index;
			if (!bssid_idx || bssid_idx > MT7996_MAX_MBSSID - 1)
				continue;

			mbssid_data[bssid_idx].mbssid_idx.offset = idx_ie - skb->data;
			mbssid_data[bssid_idx].mbssid_idx.valid = true;

			/* Find WLAN_EID_NON_TX_BSSID_CAP
			 * in the merged nontransmitted profile
			 */
			ntx_bss_cap_ie = cfg80211_find_ie(WLAN_EID_NON_TX_BSSID_CAP,
							  sub_elem->data,
							  sub_elem->datalen);

			if (ntx_bss_cap_ie && ntx_bss_cap_ie[1] == sizeof(u16)) {
				mbssid_data[bssid_idx].ntx_bss_cap.offset =
					ntx_bss_cap_ie - skb->data;
				mbssid_data[bssid_idx].ntx_bss_cap.valid =
					true;
				cu_flag = le16_to_cpu(*(u16 *)(ntx_bss_cap_ie + 2)) &
					  WLAN_CAPABILITY_PBCC;
			}

			/* Find WLAN_EID_EXT_EHT_MULTI_LINK
			 * in the merged nontransmitted profile
			 */
			if (cu_flag)
				mbssid_data[bssid_idx].is_cu_link =
					mt7996_is_nontx_cu_link(phy, sub_elem,
								bssid_idx);
		}
	}
}

int mt7996_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf, int en)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt76_vif_link *mlink = mt76_vif_conf_link(&dev->mt76, vif, link_conf);
	struct mt7996_vif_link *link = mt7996_vif_conf_link(dev, vif, link_conf);
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct bss_bcn_content_tlv *bcn;
	struct mt7996_mbssid_data *mbssid_data;
	int len, extra_len = 0;

	if (link_conf->nontransmitted)
		return 0;

	if (!mlink)
		return -EINVAL;

	rskb = __mt7996_mcu_alloc_bss_req(&dev->mt76, mlink,
					  MT7996_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	skb = ieee80211_beacon_get_template(hw, vif, &offs, link_conf->link_id);
	if (en && !skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb) {
		if (skb->len > MT7996_MAX_BEACON_SIZE) {
			dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
			dev_kfree_skb(rskb);
			dev_kfree_skb(skb);
			return -EINVAL;
		}

		extra_len = skb->len;
	}

	len = ALIGN(sizeof(*bcn) + MT_TXD_SIZE + extra_len, 4);
	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_CONTENT, len);
	bcn = (struct bss_bcn_content_tlv *)tlv;
	bcn->enable = en;
	if (!en)
		goto out;

	info = IEEE80211_SKB_CB(skb);
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, mlink->band_idx);

	mbssid_data = kzalloc(sizeof(struct mt7996_mbssid_data) * MAX_BEACON_NUM, GFP_KERNEL);
	if (!mbssid_data) {
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	mt7996_parse_mbssid_elems(link->phy, skb, offs.mbssid_off, mbssid_data);
	mt7996_mcu_beacon_cont(dev, link_conf, rskb, skb, bcn, &offs, link);
	if (link_conf->bssid_indicator)
		mt7996_mcu_beacon_mbss(rskb, skb, bcn, &offs, mbssid_data);
	mt7996_mcu_beacon_cntdwn(rskb, skb, &offs, link_conf->csa_active);
	mt7996_mcu_beacon_sta_prof_csa(rskb, link_conf, &offs);
	mt7996_mcu_beacon_crit_update(rskb, skb, link_conf, link, &offs, mbssid_data);
	mt7996_mcu_beacon_ml_reconf(dev, link_conf, rskb, skb, &offs);
	mt7996_mcu_beacon_ttlm(dev, link_conf, rskb, skb, &offs);

	kfree(mbssid_data);
out:
	dev_kfree_skb(skb);
	return mt76_mcu_skb_send_msg(&dev->mt76, rskb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7996_mcu_beacon_inband_discov(struct mt7996_dev *dev,
				    struct ieee80211_bss_conf *link_conf,
				    struct mt7996_vif_link *link, u32 changed)
{
#define OFFLOAD_TX_MODE_SU	BIT(0)
#define OFFLOAD_TX_MODE_MU	BIT(1)
	struct ieee80211_vif *vif = link_conf->vif;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt7996_phy *phy = link->phy;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_inband_discovery_tlv *discov;
	struct ieee80211_tx_info *info;
	struct sk_buff *rskb, *skb = NULL;
	struct cfg80211_chan_def *chandef;
	enum nl80211_band band;
	struct tlv *tlv;
	u8 *buf, interval = 0;
	int len;

	if (!phy)
		return -EINVAL;

	chandef = &phy->mt76->chandef;
	band = chandef->chan->band;

	if (link_conf->nontransmitted)
		return 0;

	rskb = __mt7996_mcu_alloc_bss_req(&dev->mt76, &link->mt76,
					  MT7996_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	if (changed & BSS_CHANGED_FILS_DISCOVERY &&
	    link_conf->fils_discovery.max_interval) {
		interval = link_conf->fils_discovery.max_interval;
		skb = ieee80211_get_fils_discovery_tmpl(hw, vif);
	} else if (changed & BSS_CHANGED_UNSOL_BCAST_PROBE_RESP &&
		   link_conf->unsol_bcast_probe_resp_interval) {
		interval = link_conf->unsol_bcast_probe_resp_interval;
		skb = ieee80211_get_unsol_bcast_probe_resp_tmpl(hw, vif);
	}

	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb->len > MT7996_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "inband discovery size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->band = band;
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, phy->mt76->band_idx);

	len = ALIGN(sizeof(*discov) + MT_TXD_SIZE + skb->len, 4);
	tlv = mt7996_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_OFFLOAD, len);

	discov = (struct bss_inband_discovery_tlv *)tlv;
	discov->tx_mode = OFFLOAD_TX_MODE_SU;
	/* 0: UNSOL PROBE RESP, 1: FILS DISCOV */
	discov->tx_type = !!(changed & BSS_CHANGED_FILS_DISCOVERY);
	discov->tx_interval = interval;
	discov->prob_rsp_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	discov->enable = !!(interval);
	discov->wcid = cpu_to_le16(MT7996_WTBL_RESERVED);

	buf = (u8 *)tlv + sizeof(*discov);

	mt7996_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0, changed);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);

	dev_kfree_skb(skb);

	return mt76_mcu_skb_send_msg(&dev->mt76, rskb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int mt7996_driver_own(struct mt7996_dev *dev, u8 band)
{
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(band), MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_TOP_LPCR_HOST_BAND(band),
			    MT_TOP_LPCR_HOST_FW_OWN_STAT, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	/* clear irq when the driver own success */
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND_IRQ_STAT(band),
		MT_TOP_LPCR_HOST_BAND_STAT);

	return 0;
}

static u32 mt7996_patch_sec_mode(u32 key_info)
{
	u32 sec = u32_get_bits(key_info, MT7996_PATCH_SEC), key = 0;

	if (key_info == GENMASK(31, 0) || sec == MT7996_SEC_MODE_PLAIN)
		return 0;

	if (sec == MT7996_SEC_MODE_AES)
		key = u32_get_bits(key_info, MT7996_PATCH_AES_KEY);
	else
		key = u32_get_bits(key_info, MT7996_PATCH_SCRAMBLE_KEY);

	return MT7996_SEC_ENCRYPT | MT7996_SEC_IV |
	       u32_encode_bits(key, MT7996_SEC_KEY_IDX);
}

static int mt7996_load_patch(struct mt7996_dev *dev)
{
	const struct mt7996_patch_hdr *hdr;
	const struct firmware *fw = NULL;
	int i, ret, sem;

	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 1);
	switch (sem) {
	case PATCH_IS_DL:
		return 0;
	case PATCH_NOT_DL_SEM_SUCCESS:
		break;
	default:
		dev_err(dev->mt76.dev, "Failed to get patch semaphore\n");
		return -EAGAIN;
	}

	ret = request_firmware(&fw, fw_name(dev, ROM_PATCH), dev->mt76.dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const struct mt7996_patch_hdr *)(fw->data);

	dev_info(dev->mt76.dev, "HW/SW Version: 0x%x, Build Time: %.16s\n",
		 be32_to_cpu(hdr->hw_sw_ver), hdr->build_date);
	dev->mt76.fw.hw_sw_ver = be32_to_cpu(hdr->hw_sw_ver);
	strscpy(dev->mt76.fw.build_date, hdr->build_date, 15);

	for (i = 0; i < be32_to_cpu(hdr->desc.n_region); i++) {
		struct mt7996_patch_sec *sec;
		const u8 *dl;
		u32 len, addr, sec_key_idx, mode = DL_MODE_NEED_RSP;

		sec = (struct mt7996_patch_sec *)(fw->data + sizeof(*hdr) +
						  i * sizeof(*sec));
		if ((be32_to_cpu(sec->type) & PATCH_SEC_TYPE_MASK) !=
		    PATCH_SEC_TYPE_INFO) {
			ret = -EINVAL;
			goto out;
		}

		addr = be32_to_cpu(sec->info.addr);
		len = be32_to_cpu(sec->info.len);
		sec_key_idx = be32_to_cpu(sec->info.sec_key_idx);
		dl = fw->data + be32_to_cpu(sec->offs);

		mode |= mt7996_patch_sec_mode(sec_key_idx);

		ret = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (ret) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			goto out;
		}

		ret = __mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					       dl, len, 4096);
		if (ret) {
			dev_err(dev->mt76.dev, "Failed to send patch\n");
			goto out;
		}
	}

	ret = mt76_connac_mcu_start_patch(&dev->mt76);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to start patch\n");

out:
	sem = mt76_connac_mcu_patch_sem_ctrl(&dev->mt76, 0);
	switch (sem) {
	case PATCH_REL_SEM_SUCCESS:
		break;
	default:
		ret = -EAGAIN;
		dev_err(dev->mt76.dev, "Failed to release patch semaphore\n");
		break;
	}
	release_firmware(fw);

	return ret;
}

static int
mt7996_mcu_send_ram_firmware(struct mt7996_dev *dev,
			     const struct mt7996_fw_trailer *hdr,
			     const u8 *data, enum mt7996_ram_type type)
{
	int i, offset = 0;
	u32 override = 0, option = 0;

	for (i = 0; i < hdr->n_region; i++) {
		const struct mt7996_fw_region *region;
		int err;
		u32 len, addr, mode;

		region = (const struct mt7996_fw_region *)((const u8 *)hdr -
			 (hdr->n_region - i) * sizeof(*region));
		/* DSP and WA use same mode */
		mode = mt76_connac_mcu_gen_dl_mode(&dev->mt76,
						   region->feature_set,
						   type != MT7996_RAM_TYPE_WM);
		len = le32_to_cpu(region->len);
		addr = le32_to_cpu(region->addr);

		if (region->feature_set & FW_FEATURE_OVERRIDE_ADDR)
			override = addr;

		err = mt76_connac_mcu_init_download(&dev->mt76, addr, len,
						    mode);
		if (err) {
			dev_err(dev->mt76.dev, "Download request failed\n");
			return err;
		}

		err = __mt76_mcu_send_firmware(&dev->mt76, MCU_CMD(FW_SCATTER),
					       data + offset, len, 4096);
		if (err) {
			dev_err(dev->mt76.dev, "Failed to send firmware.\n");
			return err;
		}

		offset += len;
	}

	if (override)
		option |= FW_START_OVERRIDE;

	if (type == MT7996_RAM_TYPE_WA)
		option |= FW_START_WORKING_PDA_CR4;
	else if (type == MT7996_RAM_TYPE_DSP)
		option |= FW_START_WORKING_PDA_DSP;

	return mt76_connac_mcu_start_firmware(&dev->mt76, override, option);
}

static int __mt7996_load_ram(struct mt7996_dev *dev, const char *fw_type,
			     const char *fw_file, enum mt7996_ram_type ram_type)
{
	const struct mt7996_fw_trailer *hdr;
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, fw_file, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "Invalid firmware\n");
		ret = -EINVAL;
		goto out;
	}

	hdr = (const void *)(fw->data + fw->size - sizeof(*hdr));
	dev_info(dev->mt76.dev, "%s Firmware Version: %.10s, Build Time: %.15s\n",
		 fw_type, hdr->fw_ver, hdr->build_date);
	strncpy(dev->mt76.fw.wm_fw_ver, hdr->fw_ver, 10);
	strncpy(dev->mt76.fw.wm_build_date, hdr->build_date, 15);

	ret = mt7996_mcu_send_ram_firmware(dev, hdr, fw->data, ram_type);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to start %s firmware\n", fw_type);
		goto out;
	}

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%.10s-%.15s", hdr->fw_ver, hdr->build_date);

out:
	release_firmware(fw);

	return ret;
}

static int mt7996_load_ram(struct mt7996_dev *dev)
{
	int ret;

	if (dev->testmode_enable)
		ret = __mt7996_load_ram(dev, "WM_TM", fw_name(dev, FIRMWARE_WM_TM),
					MT7996_RAM_TYPE_WM_TM);
	else
		ret = __mt7996_load_ram(dev, "WM", fw_name(dev, FIRMWARE_WM),
					MT7996_RAM_TYPE_WM);
	if (ret)
		return ret;

	if (!mt7996_has_wa(dev))
		return 0;

	ret = __mt7996_load_ram(dev, "DSP", fw_name(dev, FIRMWARE_DSP),
				MT7996_RAM_TYPE_DSP);
	if (ret)
		return ret;

	return __mt7996_load_ram(dev, "WA", fw_name(dev, FIRMWARE_WA),
				 MT7996_RAM_TYPE_WA);
}

static int
mt7996_firmware_state(struct mt7996_dev *dev, u8 fw_state)
{
	u32 state = FIELD_PREP(MT_TOP_MISC_FW_STATE, fw_state);

	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    state, 1000)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}
	return 0;
}

static int
mt7996_mcu_restart(struct mt76_dev *dev)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_mode;
		u8 __rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_POWER_OFF),
		.len = cpu_to_le16(sizeof(req) - 4),
		.power_mode = 1,
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(POWER_CTRL), &req,
				 sizeof(req), false);
}

static int mt7996_load_firmware(struct mt7996_dev *dev)
{
	u8 fw_state;
	int ret;

	/* make sure fw is download state */
	if (mt7996_firmware_state(dev, FW_STATE_FW_DOWNLOAD)) {
		/* restart firmware once */
		mt7996_mcu_restart(&dev->mt76);
		ret = mt7996_firmware_state(dev, FW_STATE_FW_DOWNLOAD);
		if (ret) {
			dev_err(dev->mt76.dev,
				"Firmware is not ready for download\n");
			return ret;
		}
	}

	ret = mt7996_load_patch(dev);
	if (ret)
		return ret;

	ret = mt7996_load_ram(dev);
	if (ret)
		return ret;

	fw_state = mt7996_has_wa(dev) ? FW_STATE_RDY : FW_STATE_NORMAL_TRX;
	ret = mt7996_firmware_state(dev, fw_state);
	if (ret)
		return ret;

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7996_mcu_fw_log_2_host(struct mt7996_dev *dev, u8 type, u8 ctrl)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 ctrl;
		u8 interval;
		u8 _rsv2[2];
	} __packed data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_LOG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.ctrl = ctrl,
	};

	if (type == MCU_FW_LOG_WA)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(WSYS_CONFIG),
					 &data, sizeof(data), true);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), true);
}

int mt7996_mcu_fw_dbg_ctrl(struct mt7996_dev *dev, u32 module, u8 level)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 module_idx;
		u8 level;
		u8 _rsv2[3];
	} data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_DBG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.module_idx = cpu_to_le32(module),
		.level = level,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), false);
}

int mt7996_mcu_fw_time_sync(struct mt76_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 sec;
		__le32 usec;
	} data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_TIME_SYNC),
		.len = cpu_to_le16(sizeof(data) - 4),
	};
	struct timespec64 ts;
	struct tm tm;

	ktime_get_real_ts64(&ts);
	data.sec = cpu_to_le32((u32)ts.tv_sec);
	data.usec = cpu_to_le32((u32)(ts.tv_nsec / 1000));

	/* Dump synchronized time for ConsysPlanet to parse. */
	time64_to_tm(ts.tv_sec, 0, &tm);
	dev_info(dev->dev, "%ld-%02d-%02d %02d:%02d:%02d.%ld UTC\n",
	        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	        tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000);

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
	                         sizeof(data), true);
}

static int mt7996_mcu_set_mwds(struct mt7996_dev *dev, bool enabled)
{
	struct {
		u8 enable;
		u8 _rsv[3];
	} __packed req = {
		.enable = enabled
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(MWDS_SUPPORT), &req,
				 sizeof(req), false);
}

static void mt7996_add_rx_airtime_tlv(struct sk_buff *skb, u8 band_idx)
{
	struct vow_rx_airtime *req;
	struct tlv *tlv;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_CLR_EN, sizeof(*req));
	req = (struct vow_rx_airtime *)tlv;
	req->enable = true;
	req->band = band_idx;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_EN, sizeof(*req));
	req = (struct vow_rx_airtime *)tlv;
	req->enable = true;
	req->band = band_idx;
}

static int
mt7996_mcu_init_rx_airtime(struct mt7996_dev *dev)
{
	struct uni_header hdr = {};
	struct sk_buff *skb;
	int len, num, i;

	num = 2 + 2 * (mt7996_band_valid(dev, MT_BAND1) +
		       mt7996_band_valid(dev, MT_BAND2));
	len = sizeof(hdr) + num * sizeof(struct vow_rx_airtime);
	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (i = 0; i < __MT_MAX_BAND; i++) {
		if (mt7996_band_valid(dev, i))
			mt7996_add_rx_airtime_tlv(skb, i);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(VOW), true);
}

int mt7996_mcu_init_firmware(struct mt7996_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);

	ret = mt7996_driver_own(dev, 0);
	if (ret)
		return ret;
	/* set driver own for band1 when two hif exist */
	if (dev->hif2) {
		ret = mt7996_driver_own(dev, 1);
		if (ret)
			return ret;
	}

	ret = mt7996_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM, 1);
	if (ret)
		return ret;

	if (mt7996_has_wa(dev)) {
		ret = mt7996_mcu_fw_log_2_host(dev, MCU_FW_LOG_WA, 1);
		if (ret)
			return ret;

		ret = mt7996_mcu_set_mwds(dev, 1);
		if (ret)
			return ret;
	}

	ret = mt7996_mcu_init_rx_airtime(dev);
	if (ret)
		return ret;

	return mt7996_mcu_red_config(dev,
			mtk_wed_device_active(&dev->mt76.mmio.wed));
}

int mt7996_mcu_init(struct mt7996_dev *dev)
{
	static const struct mt76_mcu_ops mt7996_mcu_ops = {
		.max_retry = 2,
		.headroom = sizeof(struct mt76_connac2_mcu_txd), /* reuse */
		.mcu_skb_send_msg = mt7996_mcu_send_message,
		.mcu_parse_response = mt7996_mcu_parse_response,
	};

	dev->mt76.mcu_ops = &mt7996_mcu_ops;

	return mt7996_mcu_init_firmware(dev);
}

void mt7996_mcu_exit(struct mt7996_dev *dev)
{
	mt7996_mcu_restart(&dev->mt76);
	if (mt7996_firmware_state(dev, FW_STATE_FW_DOWNLOAD)) {
		dev_err(dev->mt76.dev, "Failed to exit mcu\n");
		goto out;
	}

	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(0), MT_TOP_LPCR_HOST_FW_OWN);
	if (dev->hif2)
		mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(1),
			MT_TOP_LPCR_HOST_FW_OWN);
out:
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

static int mt7996_mcu_wa_red_config(struct mt7996_dev *dev)
{
#define RED_TOKEN_CONFIG	2
#define RED_TOKEN_SRC_CNT	4
#define RED_MAX_BAND_CNT	4

	struct mt7996_wa_params {
		__le32 arg[3];
	} __packed;

	struct mt7996_red_config_hdr {
		u8 rsv[4];
		__le16 tag;
		__le16 len;
	} __packed;

	struct mt7996_red_config {
		u8 mode;
		u8 version;
		u8 _rsv[4];
		__le16 len;

		__le16 tcp_offset;
		__le16 priority_offset;
		__le16 token_per_src[RED_TOKEN_SRC_CNT];
		__le16 token_thr_per_src[RED_TOKEN_SRC_CNT];
	} __packed;
	struct mt7996_red_config *req;
	void *data;
	int ret, len = sizeof(struct mt7996_red_config);
	u8 i;

	len += is_mt7990(&dev->mt76) ?
		sizeof(struct mt7996_red_config_hdr) + 1120 :
		sizeof(struct mt7996_wa_params) + 604;

	data = kzalloc(len, GFP_KERNEL);

	if (is_mt7990(&dev->mt76)) {
		struct mt7996_red_config_hdr *hdr = (struct mt7996_red_config_hdr *)data;

		hdr->tag = cpu_to_le16(UNI_CMD_SDO_RED_SETTING);
		hdr->len = cpu_to_le16(len - 4);
		req = (struct mt7996_red_config *)(data + sizeof(*hdr));
		req->len = cpu_to_le16(len - sizeof(*hdr));
	} else {
		struct mt7996_wa_params *param = (struct mt7996_wa_params *)data;

		param->arg[0] = cpu_to_le32(MCU_WA_PARAM_RED_CONFIG);
		req = (struct mt7996_red_config *)(data + sizeof(*param));
		req->len = cpu_to_le16(len - sizeof(*param));
	}

	req->mode = RED_TOKEN_CONFIG;
	req->tcp_offset = cpu_to_le16(200);
	req->priority_offset = cpu_to_le16(255);
	for (i = 0; i < RED_TOKEN_SRC_CNT; i++) {
		req->token_per_src[i] = cpu_to_le16(MT7996_TOKEN_SIZE);
		req->token_thr_per_src[i] = cpu_to_le16(MT7996_TOKEN_SIZE);
	}

	req->token_per_src[RED_TOKEN_SRC_CNT - 1] = dev->mt76.token_size;

	if (is_mt7990(&dev->mt76))
		ret = mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO), data,
					len, false);
	else
		ret = mt76_mcu_send_msg(&dev->mt76, MCU_WA_PARAM_CMD(SET), data,
					len, false);

	kfree(data);

	return ret;
}

int mt7996_mcu_red_config(struct mt7996_dev *dev, bool enable)
{
#define RED_DISABLE		0
#define RED_BY_WA_ENABLE	2
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 __rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_RED_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable ? RED_BY_WA_ENABLE : RED_DISABLE,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(VOW), &req,
				 sizeof(req), true);

	if (ret)
		return ret;

	ret = mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET),
				MCU_WA_PARAM_RED_EN, enable, 0);

	if (ret || !enable)
		return ret;

	return mt7996_mcu_wa_red_config(dev);
}

int mt7996_mcu_set_hdr_trans(struct mt7996_dev *dev, bool hdr_trans)
{
	struct {
		u8 __rsv[4];
	} __packed hdr;
	struct hdr_trans_blacklist *req_blacklist;
	struct hdr_trans_en *req_en;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = MT7996_HDR_TRANS_MAX_SIZE + sizeof(hdr);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_EN, sizeof(*req_en));
	req_en = (struct hdr_trans_en *)tlv;
	req_en->enable = hdr_trans;

	tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_VLAN,
				     sizeof(struct hdr_trans_vlan));

	if (hdr_trans) {
		tlv = mt7996_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_BLACKLIST,
					     sizeof(*req_blacklist));
		req_blacklist = (struct hdr_trans_blacklist *)tlv;
		req_blacklist->enable = 1;
		req_blacklist->type = cpu_to_le16(ETH_P_PAE);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(RX_HDR_TRANS), true);
}

int mt7996_mcu_set_tx(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf)
{
#define MCU_EDCA_AC_PARAM	0
#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		(WMM_AIFS_SET | WMM_CW_MIN_SET | \
				 WMM_CW_MAX_SET | WMM_TXOP_SET)
	struct mt7996_vif_link *link = mt7996_vif_conf_link(dev, vif, link_conf);
	struct {
		u8 bss_idx;
		u8 __rsv[3];
	} __packed hdr = {
		.bss_idx = link->mt76.idx,
	};
	struct sk_buff *skb;
	int len = sizeof(hdr) + IEEE80211_NUM_ACS * sizeof(struct edca);
	int ac;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &link->queue_params[ac];
		struct edca *e;
		struct tlv *tlv;

		tlv = mt7996_mcu_add_uni_tlv(skb, MCU_EDCA_AC_PARAM, sizeof(*e));

		e = (struct edca *)tlv;
		e->set = WMM_PARAM_SET;
		e->queue = ac;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = fls(q->cw_max);
		else
			e->cw_max = 10;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(EDCA_UPDATE), true);
}

int mt7996_mcu_set_fcc5_lpn(struct mt7996_dev *dev, int val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le16 min_lpn;
		u8 rsv[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x1),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_pulse_th(struct mt7996_dev *dev,
			    const struct mt7996_dfs_pulse *pulse)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;

		__le32 max_width;		/* us */
		__le32 max_pwr;			/* dbm */
		__le32 min_pwr;			/* dbm */
		__le32 min_stgr_pri;		/* us */
		__le32 max_stgr_pri;		/* us */
		__le32 min_cr_pri;		/* us */
		__le32 max_cr_pri;		/* us */
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x3),

#define __req_field(field) .field = cpu_to_le32(pulse->field)
		__req_field(max_width),
		__req_field(max_pwr),
		__req_field(min_pwr),
		__req_field(min_stgr_pri),
		__req_field(max_stgr_pri),
		__req_field(min_cr_pri),
		__req_field(max_cr_pri),
#undef __req_field
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_radar_th(struct mt7996_dev *dev, int index,
			    const struct mt7996_dfs_pattern *pattern)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le16 radar_type;

		u8 enb;
		u8 stgr;
		u8 min_crpn;
		u8 max_crpn;
		u8 min_crpr;
		u8 min_pw;
		__le32 min_pri;
		__le32 max_pri;
		u8 max_pw;
		u8 min_crbn;
		u8 max_crbn;
		u8 min_stgpn;
		u8 max_stgpn;
		u8 min_stgpr;
		u8 rsv[2];
		__le32 min_stgpr_diff;
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(0x2),
		.radar_type = cpu_to_le16(index),

#define __req_field_u8(field) .field = pattern->field
#define __req_field_u32(field) .field = cpu_to_le32(pattern->field)
		__req_field_u8(enb),
		__req_field_u8(stgr),
		__req_field_u8(min_crpn),
		__req_field_u8(max_crpn),
		__req_field_u8(min_crpr),
		__req_field_u8(min_pw),
		__req_field_u32(min_pri),
		__req_field_u32(max_pri),
		__req_field_u8(max_pw),
		__req_field_u8(min_crbn),
		__req_field_u8(max_crbn),
		__req_field_u8(min_stgpn),
		__req_field_u8(max_stgpn),
		__req_field_u8(min_stgpr),
		__req_field_u32(min_stgpr_diff),
#undef __req_field_u8
#undef __req_field_u32
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_background_chain_ctrl(struct mt7996_phy *phy,
				 struct cfg80211_chan_def *chandef,
				 int cmd)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->chandef.chan;
	int freq = mphy->chandef.center_freq1;
	struct mt7996_mcu_background_chain_ctrl req = {
		.tag = cpu_to_le16(0),
		.len = cpu_to_le16(sizeof(req) - 4),
		.monitor_scan_type = 2, /* simple rx */
	};

	if (!chandef && cmd != CH_SWITCH_BACKGROUND_SCAN_STOP)
		return -EINVAL;

	if (!cfg80211_chandef_valid(&mphy->chandef))
		return -EINVAL;

	switch (cmd) {
	case CH_SWITCH_BACKGROUND_SCAN_START: {
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.monitor_bw = mt76_connac_chan_bw(chandef);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 1;
		break;
	}
	case CH_SWITCH_BACKGROUND_SCAN_RUNNING:
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 2;
		break;
	case CH_SWITCH_BACKGROUND_SCAN_STOP:
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.tx_stream = hweight8(mphy->antenna_mask);
		req.rx_stream = mphy->antenna_mask;
		break;
	default:
		return -EINVAL;
	}
	req.band = chandef ? chandef->chan->band == NL80211_BAND_5GHZ : 1;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(OFFCH_SCAN_CTRL),
				 &req, sizeof(req), false);
}

int mt7996_mcu_rdd_background_enable(struct mt7996_phy *phy,
				     struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = phy->dev;
	int err, region, rdd_idx = mt7996_get_rdd_idx(phy, true);

	if (!chandef) { /* disable offchain */
		err = mt7996_mcu_rdd_cmd(dev, RDD_STOP, rdd_idx, 0);
		if (err)
			return err;

		return mt7996_mcu_background_chain_ctrl(phy, NULL,
				CH_SWITCH_BACKGROUND_SCAN_STOP);
	}

	err = mt7996_mcu_background_chain_ctrl(phy, chandef,
					       CH_SWITCH_BACKGROUND_SCAN_START);
	if (err)
		return err;

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

	return mt7996_mcu_rdd_cmd(dev, RDD_START, rdd_idx, region);
}

int mt7996_mcu_set_chan_info(struct mt7996_phy *phy, u16 tag, bool sta)
{
	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 0,
		[NL80211_BAND_5GHZ] = 1,
		[NL80211_BAND_6GHZ] = 2,
	};
	struct mt7996_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	u8 band_idx = phy->mt76->band_idx;
	enum nl80211_iftype iftype = sta ? NL80211_IFTYPE_STATION :
					   NL80211_IFTYPE_AP;
	struct {
		/* fixed field */
		u8 __rsv[4];

		__le16 tag;
		__le16 len;
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_path_num;
		u8 rx_path;	/* mask or num */
		u8 switch_reason;
		u8 band_idx;
		u8 center_ch2;	/* for 80+80 only */
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 ap_bw;
		u8 ap_center_ch;
		u8 rsv1[53];
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt76_connac_chan_bw(chandef),
		.tx_path_num = hweight16(phy->mt76->chainmask),
		.rx_path = mt7996_rx_chainmask(phy) >> dev->chainshift[band_idx],
		.band_idx = band_idx,
		.channel_band = ch_band[chandef->chan->band],
	};

	if (phy->mt76->hw->conf.flags & IEEE80211_CONF_MONITOR)
		req.switch_reason = CH_SWITCH_NORMAL;
	else if (phy->mt76->offchannel ||
		 // TODO:  Offchannel code, maybe related to scanning issue.
		 phy->mt76->hw->conf.flags & IEEE80211_CONF_IDLE ||
		 test_bit(MT76_SCANNING, &phy->mt76->state))
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, chandef, iftype))
		req.switch_reason = CH_SWITCH_DFS;
	/* Set the switch reason to DFS if the ZWDFS is enabled and
	 * AP operates on ch 36 BW80 or ch 44 BW40. This avoids TX emission on ch 50.
	 */
	else if (dev->rdd2_phy && (chandef->chan->band == NL80211_BAND_5GHZ) &&
		 !cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, &dev->rdd2_chandef,
					  NL80211_IFTYPE_AP) &&
		 ((chandef->center_freq1 == 5210 && chandef->width == NL80211_CHAN_WIDTH_80) ||
		  (chandef->center_freq1 == 5230 && chandef->width == NL80211_CHAN_WIDTH_40)))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (tag == UNI_CHANNEL_SWITCH)
		req.rx_path = hweight8(req.rx_path);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	mtk_dbg(&phy->dev->mt76, CFG,
		"mcu-set-chan-info, ctrl: %d center: %d  bw: %d rx-path: 0x%x switch-reason: %d band-idx: %d center2: %d  channel-band: %d ap-bw: %d  ap-center: %d\n",
		req.control_ch, req.center_ch, req.bw, req.rx_path, req.switch_reason,
		req.band_idx, req.center_ch2, req.channel_band, req.ap_bw, req.ap_center_ch);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(CHANNEL_SWITCH),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_eeprom(struct mt7996_dev *dev)
{
#define MAX_PAGE_IDX_MASK	GENMASK(7, 5)
#define PAGE_IDX_MASK		GENMASK(4, 2)
#define PER_PAGE_SIZE		0x400
	struct mt7996_mcu_eeprom req = {
		.tag = cpu_to_le16(UNI_EFUSE_BUFFER_MODE),
		.buffer_mode = EE_MODE_BUFFER
	};
	u16 eeprom_size = MT7996_EEPROM_SIZE;
	u8 total = DIV_ROUND_UP(eeprom_size, PER_PAGE_SIZE);
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	int eep_len, i;

	for (i = 0; i < total; i++, eep += eep_len) {
		struct sk_buff *skb;
		int ret, msg_len;

		if (i == total - 1 && !!(eeprom_size % PER_PAGE_SIZE))
			eep_len = eeprom_size % PER_PAGE_SIZE;
		else
			eep_len = PER_PAGE_SIZE;

		msg_len = sizeof(req) + eep_len;
		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		req.len = cpu_to_le16(msg_len - 4);
		req.format = FIELD_PREP(MAX_PAGE_IDX_MASK, total - 1) |
			     FIELD_PREP(PAGE_IDX_MASK, i) | EE_FORMAT_WHOLE;
		req.buf_len = cpu_to_le16(eep_len);

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, eep, eep_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(EFUSE_CTRL), true);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7996_mcu_get_eeprom(struct mt7996_dev *dev, u32 offset, u8 *buf, u32 buf_len)
{
	struct mt7996_mcu_eeprom_info req = {
		.tag = cpu_to_le16(UNI_EFUSE_ACCESS),
		.len = cpu_to_le16(sizeof(req) - 4),
		.addr = cpu_to_le32(round_down(offset,
				    MT7996_EEPROM_BLOCK_SIZE)),
	};
	struct sk_buff *skb;
	bool valid;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	valid = le32_to_cpu(*(__le32 *)(skb->data + 16));
	if (valid) {
		u32 addr = le32_to_cpu(*(__le32 *)(skb->data + 12));

		if (!buf)
			buf = (u8 *)dev->mt76.eeprom.data + addr;
		if (!buf_len || buf_len > MT7996_EEPROM_BLOCK_SIZE)
			buf_len = MT7996_EEPROM_BLOCK_SIZE;

		skb_pull(skb, 48);
		memcpy(buf, skb->data, buf_len);
	} else {
		ret = -EINVAL;
	}

	dev_kfree_skb(skb);

	return ret;
}

int mt7996_mcu_get_eeprom_free_block(struct mt7996_dev *dev, u8 *block_num)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 num;
		u8 version;
		u8 die_idx;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_EFUSE_FREE_BLOCK),
		.len = cpu_to_le16(sizeof(req) - 4),
		.version = 2,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	*block_num = *(u8 *)(skb->data + 8);
	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_chip_config(struct mt7996_dev *dev, u32 *cap)
{
#define NIC_CAP	3
#define UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION	0x21
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(NIC_CAP),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct sk_buff *skb;
	u8 *buf;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(CHIP_CONFIG), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	/* fixed field */
	skb_pull(skb, 4);

	buf = skb->data;
	while (buf - skb->data < skb->len) {
		struct tlv *tlv = (struct tlv *)buf;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION:
			*cap = le32_to_cpu(*(__le32 *)(buf + sizeof(*tlv)));
			break;
		default:
			break;
		}

		buf += le16_to_cpu(tlv->len);
	}

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_chan_mib_info(struct mt7996_phy *phy, bool chan_switch)
{
	enum {
		IDX_TX_TIME,
		IDX_RX_TIME,
		IDX_OBSS_AIRTIME,
		IDX_NON_WIFI_TIME,
		IDX_NUM
	};
	struct {
		struct {
			u8 band;
			u8 __rsv[3];
		} hdr;
		struct {
			__le16 tag;
			__le16 len;
			__le32 offs;
		} data[IDX_NUM];
	} __packed req = {
		.hdr.band = phy->mt76->band_idx,
	};
	static const u32 offs[] = {
		[IDX_TX_TIME] = UNI_MIB_TX_TIME,
		[IDX_RX_TIME] = UNI_MIB_RX_TIME,
		[IDX_OBSS_AIRTIME] = UNI_MIB_OBSS_AIRTIME,
		[IDX_NON_WIFI_TIME] = UNI_MIB_NON_WIFI_TIME,
	};
	struct mt76_channel_state *state = phy->mt76->chan_state;
	struct mt76_channel_state *state_ts = &phy->state_ts;
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_mcu_mib *res;
	struct sk_buff *skb;
	int i, ret;

	for (i = 0; i < IDX_NUM; i++) {
		req.data[i].tag = cpu_to_le16(UNI_CMD_MIB_DATA);
		req.data[i].len = cpu_to_le16(sizeof(req.data[i]));
		req.data[i].offs = cpu_to_le32(offs[i]);
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(GET_MIB_INFO),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	skb_pull(skb, sizeof(req.hdr));

	res = (struct mt7996_mcu_mib *)(skb->data);

	if (chan_switch)
		goto out;

#define __res_u64(s) le64_to_cpu(res[s].data)
	state->cc_tx += __res_u64(IDX_TX_TIME) - state_ts->cc_tx;
	state->cc_bss_rx += __res_u64(IDX_RX_TIME) - state_ts->cc_bss_rx;
	state->cc_rx += __res_u64(IDX_RX_TIME) +
			__res_u64(IDX_OBSS_AIRTIME) -
			state_ts->cc_rx;
	state->cc_busy += __res_u64(IDX_TX_TIME) +
			  __res_u64(IDX_RX_TIME) +
			  __res_u64(IDX_OBSS_AIRTIME) +
			  __res_u64(IDX_NON_WIFI_TIME) -
			  state_ts->cc_busy;
out:
	state_ts->cc_tx = __res_u64(IDX_TX_TIME);
	state_ts->cc_bss_rx = __res_u64(IDX_RX_TIME);
	state_ts->cc_rx = __res_u64(IDX_RX_TIME) + __res_u64(IDX_OBSS_AIRTIME);
	state_ts->cc_busy = __res_u64(IDX_TX_TIME) +
			    __res_u64(IDX_RX_TIME) +
			    __res_u64(IDX_OBSS_AIRTIME) +
			    __res_u64(IDX_NON_WIFI_TIME);
#undef __res_u64

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_get_temperature(struct mt7996_phy *phy)
{
#define TEMPERATURE_QUERY 0
#define GET_TEMPERATURE 0
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 rsv1;
		u8 action;
		u8 band_idx;
		u8 rsv2;
	} req = {
		.tag = cpu_to_le16(TEMPERATURE_QUERY),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = GET_TEMPERATURE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt7996_mcu_thermal {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 rsv;
		__le32 temperature;
	} __packed * res;
	struct sk_buff *skb;
	int ret;
	u32 temp;

	ret = mt76_mcu_send_and_get_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	temp = le32_to_cpu(res->temperature);
	dev_kfree_skb(skb);

	return temp;
}

int mt7996_mcu_set_thermal_throttling(struct mt7996_phy *phy, u8 state)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		struct mt7996_mcu_thermal_ctrl ctrl;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_DUTY_CONFIG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
		},
	};
	int level, ret;

	/* set duty cycle and level */
	for (level = 0; level < 4; level++) {
		req.ctrl.duty.duty_level = level;
		req.ctrl.duty.duty_cycle = state;
		state /= 2;

		ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), false);
		if (ret)
			return ret;
	}

	phy->throttle_state = state;

	return 0;
}

int mt7996_mcu_set_thermal_protect(struct mt7996_phy *phy, bool enable)
{
#define SUSTAIN_PERIOD		10
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		struct mt7996_mcu_thermal_ctrl ctrl;
		struct mt7996_mcu_thermal_enable enable;
	} __packed req = {
		.len = cpu_to_le16(sizeof(req) - 4 - sizeof(req.enable)),
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
			.type.protect_type = 1,
			.type.trigger_type = 1,
		},
	};
	int ret;

	req.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_DISABLE);

	ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
				&req, sizeof(req) - sizeof(req.enable), false);
	if (ret || !enable)
		return ret;

	/* set high-temperature trigger threshold */
	req.tag = cpu_to_le16(UNI_CMD_THERMAL_PROTECT_ENABLE);
	req.enable.restore_temp = cpu_to_le32(phy->throttle_temp[0]);
	req.enable.trigger_temp = cpu_to_le32(phy->throttle_temp[1]);
	req.enable.sustain_time = cpu_to_le16(SUSTAIN_PERIOD);

	req.len = cpu_to_le16(sizeof(req) - 4);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_ser(struct mt7996_dev *dev, u8 action, u8 val, u8 band)
{
	struct {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		union {
			struct {
				__le32 mask;
			} __packed set;

			struct {
				u8 method;
				u8 band;
				u8 rsv2[2];
			} __packed trigger;
		};
	} __packed req = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (action) {
	case UNI_CMD_SER_QUERY:
		break;
	case UNI_CMD_SER_SET:
		req.set.mask = cpu_to_le32(val);
		break;
	case UNI_CMD_SER_TRIGGER:
		req.trigger.method = val;
		req.trigger.band = band;
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SER),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_txbf(struct mt7996_dev *dev, u8 action)
{
#define MT7996_BF_MAX_SIZE	sizeof(union bf_tag_tlv)
#define BF_PROCESSING	4
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	switch (action) {
	case BF_SOUNDING_ON: {
		struct bf_sounding_on *req_snd_on;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_snd_on));
		req_snd_on = (struct bf_sounding_on *)tlv;
		req_snd_on->snd_mode = BF_PROCESSING;
		break;
	}
	case BF_HW_EN_UPDATE: {
		struct bf_hw_en_status_update *req_hw_en;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_hw_en));
		req_hw_en = (struct bf_hw_en_status_update *)tlv;
		req_hw_en->ebf = true;
		req_hw_en->ibf = dev->ibf;
		break;
	}
	case BF_MOD_EN_CTRL: {
		struct bf_mod_en_ctrl *req_mod_en;

		tlv = mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req_mod_en));
		req_mod_en = (struct bf_mod_en_ctrl *)tlv;
		req_mod_en->bf_num = mt7996_band_valid(dev, MT_BAND2) ? 3 : 2;
		req_mod_en->bf_bitmap = mt7996_band_valid(dev, MT_BAND2) ?
					GENMASK(2, 0) : GENMASK(1, 0);
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(BF), true);
}

static int
mt7996_mcu_enable_obss_spr(struct mt7996_phy *phy, u16 action, u8 val)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		__le32 val;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
		.val = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_pd(struct mt7996_phy *phy,
			   struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_dev *dev = phy->dev;
	u8 max_th = 82, non_srg_max_th = 62;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 pd_th_non_srg;
		u8 pd_th_srg;
		u8 period_offs;
		u8 rcpi_src;
		__le16 obss_pd_min;
		__le16 obss_pd_min_srg;
		u8 resp_txpwr_mode;
		u8 txpwr_restrict_mode;
		u8 txpwr_ref;
		u8 __rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_PARAM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.obss_pd_min = cpu_to_le16(max_th),
		.obss_pd_min_srg = cpu_to_le16(max_th),
		.txpwr_restrict_mode = 2,
		.txpwr_ref = 21
	};
	int ret;

	/* disable firmware dynamical PD asjustment */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_DPD, false);
	if (ret)
		return ret;

	if (he_obss_pd->sr_ctrl &
	    IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED)
		req.pd_th_non_srg = max_th;
	else if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		req.pd_th_non_srg  = max_th - he_obss_pd->non_srg_max_offset;
	else
		req.pd_th_non_srg  = non_srg_max_th;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		req.pd_th_srg = max_th - he_obss_pd->max_offset;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_siga(struct mt7996_phy *phy,
			     struct mt7996_vif_link *link,
			     struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_dev *dev = phy->dev;
	u8 omac = link->mt76.omac_idx;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 omac;
		u8 __rsv2[3];
		u8 flag[20];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_SIGA),
		.len = cpu_to_le16(sizeof(req) - 4),
		.omac = omac > HW_BSSID_MAX ? omac - 12 : omac,
	};
	int ret;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED)
		req.flag[req.omac] = 0xf;
	else
		return 0;

	/* switch to normal AP mode */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_MODE, 0);
	if (ret)
		return ret;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7996_mcu_set_obss_spr_bitmap(struct mt7996_phy *phy,
			       struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		__le32 color_l[2];
		__le32 color_h[2];
		__le32 bssid_l[2];
		__le32 bssid_h[2];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SR_SET_SRG_BITMAP),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	u32 bitmap;

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap, sizeof(bitmap));
	req.color_l[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap + 4, sizeof(bitmap));
	req.color_h[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap, sizeof(bitmap));
	req.bssid_l[req.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap + 4, sizeof(bitmap));
	req.bssid_h[req.band_idx] = cpu_to_le32(bitmap);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR), &req,
				 sizeof(req), true);
}

int mt7996_mcu_add_obss_spr(struct mt7996_phy *phy,
			    struct mt7996_vif_link *link,
			    struct ieee80211_he_obss_pd *he_obss_pd)
{
	int ret;

	/* enable firmware scene detection algorithms */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_SD,
					 sr_scene_detect);
	if (ret)
		return ret;

	/* firmware dynamically adjusts PD threshold so skip manual control */
	if (sr_scene_detect && !he_obss_pd->enable)
		return 0;

	/* enable spatial reuse */
	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE,
					 he_obss_pd->enable);
	if (ret)
		return ret;

	if (sr_scene_detect || !he_obss_pd->enable)
		return 0;

	ret = mt7996_mcu_enable_obss_spr(phy, UNI_CMD_SR_ENABLE_TX, true);
	if (ret)
		return ret;

	/* set SRG/non-SRG OBSS PD threshold */
	ret = mt7996_mcu_set_obss_spr_pd(phy, he_obss_pd);
	if (ret)
		return ret;

	/* Set SR prohibit */
	ret = mt7996_mcu_set_obss_spr_siga(phy, link, he_obss_pd);
	if (ret)
		return ret;

	/* set SRG BSS color/BSSID bitmap */
	return mt7996_mcu_set_obss_spr_bitmap(phy, he_obss_pd);
}

int mt7996_mcu_update_bss_color(struct mt7996_dev *dev,
				struct mt76_vif_link *mlink,
				struct cfg80211_he_bss_color *he_bss_color)
{
	int len = sizeof(struct bss_req_hdr) + sizeof(struct bss_color_tlv);
	struct bss_color_tlv *bss_color;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt7996_mcu_alloc_bss_req(&dev->mt76, mlink, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BSS_COLOR,
				      sizeof(*bss_color));
	bss_color = (struct bss_color_tlv *)tlv;
	bss_color->enable = he_bss_color->enabled;
	bss_color->color = he_bss_color->color;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

#define TWT_AGRT_TRIGGER	BIT(0)
#define TWT_AGRT_ANNOUNCE	BIT(1)
#define TWT_AGRT_PROTECT	BIT(2)

int mt7996_mcu_twt_agrt_update(struct mt7996_dev *dev,
			       struct mt7996_vif_link *link,
			       struct mt7996_twt_flow *flow,
			       int cmd)
{
	struct {
		/* fixed field */
		u8 bss;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 tbl_idx;
		u8 cmd;
		u8 own_mac_idx;
		u8 flowid; /* 0xff for group id */
		__le16 peer_id; /* specify the peer_id (msb=0)
				 * or group_id (msb=1)
				 */
		u8 duration; /* 256 us */
		u8 bss_idx;
		__le64 start_tsf;
		__le16 mantissa;
		u8 exponent;
		u8 is_ap;
		u8 agrt_params;
		u8 __rsv2[23];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_TWT_ARGT_UPDATE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.tbl_idx = flow->table_id,
		.cmd = cmd,
		.own_mac_idx = link->mt76.omac_idx,
		.flowid = flow->id,
		.peer_id = cpu_to_le16(flow->wcid),
		.duration = flow->duration,
		.bss = link->mt76.idx,
		.bss_idx = link->mt76.idx,
		.start_tsf = cpu_to_le64(flow->tsf),
		.mantissa = flow->mantissa,
		.exponent = flow->exp,
		.is_ap = true,
	};

	if (flow->protection)
		req.agrt_params |= TWT_AGRT_PROTECT;
	if (!flow->flowtype)
		req.agrt_params |= TWT_AGRT_ANNOUNCE;
	if (flow->trigger)
		req.agrt_params |= TWT_AGRT_TRIGGER;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TWT),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_bssid_mapping_addr(struct mt76_dev *dev, u8 band_idx)
{
	enum {
		BSSID_MAPPING_ADDR1,
		BSSID_MAPPING_ADDR2,
		BSSID_MAPPING_ADDR3,
	};
	struct {
		u8 band_idx;
		u8 _rsv1[3];

		__le16 tag;
		__le16 len;
		u8 addr;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_BSSID_MAPPING_ADDR),
		.len = cpu_to_le16(sizeof(req) - 4),
		.addr = BSSID_MAPPING_ADDR1,
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_rts_thresh(struct mt7996_phy *phy, u32 val)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RTS_THRESHOLD),
		.len = cpu_to_le16(sizeof(req) - 4),
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int
mt7996_mcu_set_band_confg(struct mt7996_phy *phy, u16 option, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(option),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_radio_en(struct mt7996_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RADIO_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_rdd_cmd(struct mt7996_dev *dev, int cmd, u8 rdd_idx, u8 val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 ctrl;
		u8 rdd_idx;
		u8 rdd_rx_sel;
		u8 val;
		u8 rsv[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_PARM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = cmd,
		.rdd_idx = rdd_idx,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_wtbl_update_hdr_trans(struct mt7996_dev *dev,
				     struct ieee80211_vif *vif,
				     struct mt7996_vif_link *link,
				     struct mt7996_sta_link *msta_link)
{
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec hdr trans */
	mt7996_mcu_sta_hdr_trans_tlv(dev, skb, vif, &msta_link->wcid);
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7996_mcu_ps_leave(struct mt7996_dev *dev, struct mt7996_vif_link *mconf,
			struct mt7996_sta_link *msta_link)
{
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mconf->mt76,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_add_tlv(skb, STA_REC_PS_LEAVE,
				sizeof(struct sta_rec_ps_leave));

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7996_mcu_set_fixed_rate_table(struct mt7996_phy *phy, u8 table_idx,
				    u16 rate_idx, bool beacon)
{
#define UNI_FIXED_RATE_TABLE_SET	0
#define SPE_IXD_SELECT_TXD		0
#define SPE_IXD_SELECT_BMC_WTBL		1
	struct mt7996_dev *dev = phy->dev;
	struct fixed_rate_table_ctrl req = {
		.tag = cpu_to_le16(UNI_FIXED_RATE_TABLE_SET),
		.len = cpu_to_le16(sizeof(req) - 4),
		.table_idx = table_idx,
		.rate_idx = cpu_to_le16(rate_idx),
		.gi = 1,
		.he_ltf = 1,
	};
	u8 band_idx = phy->mt76->band_idx;

	if (beacon) {
		req.spe_idx_sel = SPE_IXD_SELECT_TXD;
		req.spe_idx = dev->mt76.mgmt_pwr_enhance ? 0 : 24 + band_idx;
		phy->beacon_rate = rate_idx;
	} else {
		req.spe_idx_sel = SPE_IXD_SELECT_BMC_WTBL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(FIXED_RATE_TABLE),
				 &req, sizeof(req), false);
}

int mt7996_mcu_rf_regval(struct mt7996_dev *dev, u32 regidx, u32 *val, bool set)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 idx;
		u8 __rsv2[2];
		__le32 ofs;
		__le32 data;
	} __packed *res, req = {
		.tag = cpu_to_le16(UNI_CMD_ACCESS_RF_REG_BASIC),
		.len = cpu_to_le16(sizeof(req) - 4),

		.idx = cpu_to_le16(u32_get_bits(regidx, GENMASK(31, 24))),
		.ofs = cpu_to_le32(u32_get_bits(regidx, GENMASK(23, 0))),
		.data = set ? cpu_to_le32(*val) : 0,
	};
	struct sk_buff *skb;
	int ret;

	if (set)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REG_ACCESS),
					 &req, sizeof(req), true);

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(REG_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	*val = le32_to_cpu(res->data);
	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_trigger_assert(struct mt7996_dev *dev)
{
	struct {
		__le16 tag;
		__le16 len;
		u8 enable;
		u8 rsv[3];
	} __packed req = {
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = true,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(ASSERT_DUMP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_rro(struct mt7996_dev *dev, u16 tag, u16 val)
{
	struct {
		u8 __rsv1[4];
		__le16 tag;
		__le16 len;
		union {
			struct {
				u8 type;
				u8 __rsv2[3];
			} __packed platform_type;
			struct {
				u8 type;
				u8 dest;
				u8 __rsv2[2];
			} __packed bypass_mode;
			struct {
				u8 path;
				u8 __rsv2[3];
			} __packed txfree_path;
			struct {
				__le16 flush_one;
				__le16 flush_all;
				u8 __rsv2[4];
			} __packed timeout;
		};
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (tag) {
	case UNI_RRO_SET_PLATFORM_TYPE:
		req.platform_type.type = val;
		break;
	case UNI_RRO_SET_BYPASS_MODE:
		req.bypass_mode.type = val;
		break;
	case UNI_RRO_SET_TXFREE_PATH:
		req.txfree_path.path = val;
		break;
	case UNI_RRO_SET_FLUSH_TIMEOUT:
		req.timeout.flush_one = cpu_to_le16(val);
		req.timeout.flush_all = cpu_to_le16(2 * val);
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7996_mcu_get_per_sta_info(struct mt76_dev *dev, u16 tag,
				u16 sta_num, u16 *sta_list)
{
#define PER_STA_INFO_MAX_NUM	90
	struct mt7996_mcu_per_sta_info_event *res;
	struct mt7996_sta_link *msta_link;
	struct mt76_wcid *wcid;
	struct sk_buff *skb;
	int i, j, ret;
	u16 wlan_idx;
	struct {
		u8 __rsv1;
		u8 unsolicit;
		u8 __rsv2[2];

		__le16 tag;
		__le16 len;
		__le16 sta_num;
		u8 __rsv3[2];
		__le16 sta_list[PER_STA_INFO_MAX_NUM];
	} __packed req = {
		.unsolicit = 0,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.sta_num = cpu_to_le16(sta_num)
	};

	if (sta_num > PER_STA_INFO_MAX_NUM)
		return -EINVAL;

	for (i = 0; i < sta_num; ++i)
		req.sta_list[i] = cpu_to_le16(sta_list[i]);

	ret = mt76_mcu_send_and_get_msg(dev, MCU_WM_UNI_CMD(PER_STA_INFO),
	                                &req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7996_mcu_per_sta_info_event *)skb->data;
	if (le16_to_cpu(res->tag) != tag) {
		ret = -EINVAL;
		goto out;
	}

	rcu_read_lock();
	switch (tag) {
	case UNI_PER_STA_RSSI:
		for (i = 0; i < sta_num; ++i) {
			wlan_idx = le16_to_cpu(res->rssi[i].wlan_idx);
			wcid = rcu_dereference(dev->wcid[wlan_idx]);
			msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
			if (msta_link) {
				struct mt76_phy *phy = dev->phys[wcid->phy_idx];
				u8 *rcpi = res->rssi[i].rcpi;

				for (j = 0; j < IEEE80211_MAX_CHAINS; ++j)
					msta_link->chain_ack_signal[j] = to_rssi(MT_PRXV_RCPI0, rcpi[j]);

				msta_link->ack_signal = mt76_rx_signal(phy->antenna_mask,
								       msta_link->chain_ack_signal);
				ewma_avg_signal_add(&msta_link->avg_ack_signal, -msta_link->ack_signal);
			}
		}
		break;
	case UNI_PER_STA_SNR:
		for (i = 0; i < sta_num; ++i) {
			wlan_idx = le16_to_cpu(res->snr[i].wlan_idx);
			wcid = rcu_dereference(dev->wcid[wlan_idx]);
			msta_link = container_of(wcid, struct mt7996_sta_link, wcid);
			if (msta_link)
				memcpy(msta_link->chain_ack_snr, res->snr[i].val,
				       IEEE80211_MAX_CHAINS);
		}
		break;
	case UNI_PER_STA_PKT_CNT:
#if 0
		for (i = 0; i < sta_num; ++i) {
			wlan_idx = le16_to_cpu(res->msdu_cnt[i].wlan_idx);
			wcid = rcu_dereference(dev->wcid[wlan_idx]);
			if (wcid) {
				u32 retries = le32_to_cpu(res->msdu_cnt[i].tx_retries),
				    drops = le32_to_cpu(res->msdu_cnt[i].tx_drops);

				// TODO:  Support these stats?
				//wcid->stats.tx_packets_retried += retries;
				//wcid->stats.tx_packets_failed += retries + drops;
			}
		}
#endif
		break;
	default:
		ret = -EINVAL;
		dev_err(dev->dev, "Unknown UNI_PER_STA_INFO_TAG: %d\n", tag);
	}
	rcu_read_unlock();
out:
	dev_kfree_skb(skb);
	return ret;
}


int mt7996_mcu_get_all_sta_info(struct mt76_dev *dev, u16 tag)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(ALL_STA_INFO),
				 &req, sizeof(req), false);
}

int mt7996_mcu_get_bss_acq_pkt_cnt(struct mt7996_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 bitmap[UNI_CMD_SDO_CFG_BSS_MAP_WORDLEN];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_SDO_GET_BSS_ACQ_PKT_NUM),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	int i = 0;

	if (mt7996_has_wa(dev))
		return mt7996_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(QUERY),
			MCU_WA_PARAM_BSS_ACQ_PKT_CNT,
			BSS_ACQ_PKT_CNT_BSS_BITMAP_ALL | BSS_ACQ_PKT_CNT_READ_CLR, 0);

	for (i = 0; i < UNI_CMD_SDO_CFG_BSS_MAP_WORDLEN; i++)
		req.bitmap[i] = cpu_to_le32(~0);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO), &req,
				 sizeof(req), false);
}

int mt7996_mcu_wed_rro_reset_sessions(struct mt7996_dev *dev, u16 id)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;
		__le16 session_id;
		u8 pad[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RRO_DEL_BA_SESSION),
		.len = cpu_to_le16(sizeof(req) - 4),
		.session_id = cpu_to_le16(id),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7996_mcu_rdd_background_disable_timer(struct mt7996_dev *dev, bool disable_timer)
{
	struct mt7996_rdd_ctrl req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_PARM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = RDD_DISABLE_ZW_TIMER,
		.rdd_idx = MT_RDD_IDX_BACKGROUND,
		.disable_timer = disable_timer,
	};

	if (!is_mt7996(&dev->mt76) ||
	    (mt76_get_field(dev, MT_PAD_GPIO, MT_PAD_GPIO_ADIE_COMB) % 2))
		return 0;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		req.val = 0;
		break;
	case NL80211_DFS_JP:
		req.val = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		req.val = 1;
		break;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7996_mcu_get_rssi(struct mt76_dev *dev)
{
	u16 sta_list[PER_STA_INFO_MAX_NUM];
	LIST_HEAD(sta_poll_list);
	struct mt7996_sta_link *msta_link;
	int i, ret;
	bool empty = false;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	while (!empty) {
		for (i = 0; i < PER_STA_INFO_MAX_NUM; ++i) {
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&sta_poll_list)) {
				spin_unlock_bh(&dev->sta_poll_lock);

				if (i == 0)
					return 0;

				empty = true;
				break;
			}
			msta_link = list_first_entry(&sta_poll_list,
			                        struct mt7996_sta_link,
			                        wcid.poll_list);
			list_del_init(&msta_link->wcid.poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);

			sta_list[i] = msta_link->wcid.idx;
		}

		ret = mt7996_mcu_get_per_sta_info(dev, UNI_PER_STA_RSSI,
		                                  i, sta_list);
		if (ret) {
			/* Add STAs, whose RSSI has not been updated,
			 * back to polling list.
			 */
			spin_lock_bh(&dev->sta_poll_lock);
			list_splice(&sta_poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
	}

	return ret;
}

int mt7996_mcu_set_sniffer_mode(struct mt7996_phy *phy, bool enabled)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 _rsv[3];
		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _pad[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = 0,
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enabled,
	};

	mtk_dbg(&phy->dev->mt76, CFG,
		"mcu-set-sniffer-mode, band-idx: %d enable: %d\n",
		req.band_idx, req.enable);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SNIFFER), &req,
				 sizeof(req), true);
}

static void
mt7996_update_max_txpower_cur(struct mt7996_phy *phy, int tx_power)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->main_chandef.chan;
	int e2p_power_limit = 0;

	if (chan == NULL) {
		mphy->txpower_cur = tx_power;
		return;
	}

	e2p_power_limit = mt7996_eeprom_get_target_power(phy->dev, chan);
	e2p_power_limit += mt7996_eeprom_get_power_delta(phy->dev, chan->band);

	if (phy->sku_limit_en)
		mphy->txpower_cur = min_t(int, e2p_power_limit, tx_power);
	else
		mphy->txpower_cur = e2p_power_limit;
}

static bool
mt7996_is_psd_country(char *country)
{
       char psd_country_list[][3] = {"US", "KR", "BR", "CL", "MY", ""};
       int i;

       if (strlen(country) != 2)
               return 0;

       for (i = 0; psd_country_list[i][0] != '\0'; i++) {
               if (!strncmp(country, psd_country_list[i], 2))
                       return 1;
       }

       return 0;
}

int mt7996_mcu_set_txpower_sku(struct mt7996_phy *phy)
{
#define TX_POWER_LIMIT_TABLE_RATE	0
#define TX_POWER_LIMIT_TABLE_PATH	1
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct tx_power_limit_table_ctrl {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_ctrl_id;
		u8 power_limit_type;
		u8 band_idx;
	} __packed req = {
		.tag = cpu_to_le16(UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL),
		.len = cpu_to_le16(sizeof(req) + MT7996_SKU_PATH_NUM - 4),
		.power_ctrl_id = UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL,
		.power_limit_type = TX_POWER_LIMIT_TABLE_RATE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt76_power_limits la = {};
	struct mt76_power_path_limits la_path = {};
	struct sk_buff *skb;
	int i, ret, txpower_limit;
	s8 single_nss_txpower;

	if (phy->txpower == INT_MIN || phy->txpower > 127)
		phy->txpower = 127;

	if (phy->adjust_txp_by_loss)
		txpower_limit = phy->txpower + phy->tx_front_end_loss;
	else
		txpower_limit = phy->txpower;

	txpower_limit = mt7996_get_power_bound(mphy, txpower_limit, &single_nss_txpower);

	if (phy->sku_limit_en) {
		txpower_limit = mt76_get_rate_power_limits(mphy, mphy->chandef.chan,
							   &la, &la_path, txpower_limit);
		mt7996_update_max_txpower_cur(phy, txpower_limit);
	} else {
		mt7996_update_max_txpower_cur(phy, txpower_limit);
		return 0;
	}

	//dev_info(dev->mt76.dev, "mt7996-mcu-set-txpower-sku, req power level: %d txpower_limit: %d\n",
	//	 phy->txpower, txpower_limit);

	if (!phy->default_txpower) {
		struct mt7996_mcu_txpower_event *event;
		u8 band_idx = phy->mt76->band_idx;
		int ret;

		event = kzalloc(sizeof(*event), GFP_KERNEL);
		if (!event)
			return 0;

		ret = mt7996_mcu_get_tx_power_info(phy, PHY_RATE_INFO, event);
		if (ret ||
		    le32_to_cpu(event->phy_rate_info.category) != UNI_TXPOWER_PHY_RATE_INFO) {
			kfree(event);
		}
		else {
			if (event->phy_rate_info.frame_power[4][band_idx] == 0) {
				/* sku is not initialized yet, I guess. */
				//dev_info(dev->mt76.dev, "mt7996-mcu-set-txpower-sku, sku returned 0 ofdm txpower.\n");
				kfree(event);
			} else {
				struct mt7996_mcu_txpower_event *e2;

				phy->default_txpower = event;

				if (phy->tx_front_end_loss_acquired)
					goto done_probe_mcu;

				/* Attempt to probe the front end loss. */
				e2 = kzalloc(sizeof(*e2), GFP_KERNEL);
				if (!e2)
					goto done_probe_mcu;

				ret = mt7996_mcu_get_tx_power_info(phy, BASIC_INFO, e2);

				if (!(ret ||
				      le32_to_cpu(e2->basic_info.category) != UNI_TXPOWER_BASIC_INFO)) {
					phy->tx_front_end_loss = e2->basic_info.front_end_loss_tx[0];
					for (i = 1; i<4; i++)
						phy->tx_front_end_loss = min(e2->basic_info.front_end_loss_tx[i],
									     phy->tx_front_end_loss);
					phy->tx_front_end_loss_acquired = 1;
				}
				kfree(e2);
			}
		done_probe_mcu:
			/* NOP */;
		}
		if (!phy->default_txpower)
			return 0; /* try again in a bit */
	}

	// CCK and OFDM should use single NSS power
	memset(&la.cck, single_nss_txpower, sizeof(la.cck));
	memset(&la.ofdm, single_nss_txpower, sizeof(la.ofdm));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(req) + MT7996_SKU_PATH_NUM);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req, sizeof(req));
	/* cck and ofdm */
	skb_put_data(skb, &la.cck, sizeof(la.cck));

       /* FW would compensate for PSD countries
        * driver doesn't need to do it
        */
	if (phy->mt76->cap.has_6ghz && mphy->dev->lpi_psd &&
	    !mt7996_is_psd_country(dev->mt76.alpha2)) {
		switch (mphy->chandef.width) {
		case NL80211_CHAN_WIDTH_20:
			skb_put_data(skb, &la.eht[3], sizeof(la.ofdm));
			break;
		case NL80211_CHAN_WIDTH_40:
			skb_put_data(skb, &la.eht[4], sizeof(la.ofdm));
			break;
		case NL80211_CHAN_WIDTH_80:
			skb_put_data(skb, &la.eht[5], sizeof(la.ofdm));
			break;
		case NL80211_CHAN_WIDTH_160:
			skb_put_data(skb, &la.eht[6], sizeof(la.ofdm));
			break;
		case NL80211_CHAN_WIDTH_320:
			skb_put_data(skb, &la.eht[7], sizeof(la.ofdm));
			break;
		default:
			skb_put_data(skb, &la.ofdm, sizeof(la.ofdm));
			break;
		}
	} else {
		skb_put_data(skb, &la.ofdm, sizeof(la.ofdm));
	}

	/* ht20 */
	skb_put_data(skb, &la.mcs[0], 8);
	/* ht40 */
	skb_put_data(skb, &la.mcs[1], 9);

	/* vht */
	for (i = 0; i < 4; i++) {
		skb_put_data(skb, &la.mcs[i], sizeof(la.mcs[i]));
		skb_put_zero(skb, 2);  /* padding */
	}

	/* he */
	skb_put_data(skb, &la.ru[0], sizeof(la.ru));
	/* eht */
	skb_put_data(skb, &la.eht[0], sizeof(la.eht));

	/* padding */
	skb_put_zero(skb, MT7996_SKU_PATH_NUM - MT7996_SKU_RATE_NUM);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WM_UNI_CMD(TXPOWER), true);
	if (ret)
		return ret;

	/* only set per-path power table when it's configured */
	if (!phy->sku_path_en)
		return 0;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
				 sizeof(req) + MT7996_SKU_PATH_NUM);
	if (!skb)
		return -ENOMEM;
	req.power_limit_type = TX_POWER_LIMIT_TABLE_PATH;

	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, &la_path.cck, sizeof(la_path.cck));

       /* FW would NOT compensate in the case of BF backoff table
        * driver needs to compensate for LPI PSD
        */
	if (phy->mt76->cap.has_6ghz && mphy->dev->lpi_psd) {
		switch (mphy->chandef.width) {
		case NL80211_CHAN_WIDTH_20:
			skb_put_data(skb, &la_path.ru[5], sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ru_bf[5], sizeof(la_path.ofdm_bf));
			break;
		case NL80211_CHAN_WIDTH_40:
			skb_put_data(skb, &la_path.ru[6], sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ru_bf[6], sizeof(la_path.ofdm_bf));
			break;
		case NL80211_CHAN_WIDTH_80:
			skb_put_data(skb, &la_path.ru[8], sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ru_bf[8], sizeof(la_path.ofdm_bf));
			break;
		case NL80211_CHAN_WIDTH_160:
			skb_put_data(skb, &la_path.ru[11], sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ru_bf[11], sizeof(la_path.ofdm_bf));
			break;
		case NL80211_CHAN_WIDTH_320:
			skb_put_data(skb, &la_path.ru[15], sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ru_bf[15], sizeof(la_path.ofdm_bf));
			break;
		default:
			skb_put_data(skb, &la_path.ofdm, sizeof(la_path.ofdm));
			skb_put_data(skb, &la_path.ofdm_bf, sizeof(la_path.ofdm_bf));
			break;
		}
	} else {
		skb_put_data(skb, &la_path.ofdm, sizeof(la_path.ofdm));
		skb_put_data(skb, &la_path.ofdm_bf, sizeof(la_path.ofdm_bf));
	}

	for (i = 0; i < 32; i++) {
		bool bf = i % 2;
		u8 idx = i / 2;
		s8 *buf = bf ? la_path.ru_bf[idx] : la_path.ru[idx];

		skb_put_data(skb, buf, sizeof(la_path.ru[0]));
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(TXPOWER), true);
}

int mt7996_mcu_set_lpi_psd(struct mt7996_phy *phy, u8 enable)
{
	struct mt7996_dev *dev = phy->dev;

	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 lpi_enable;
		u8 psd_limit;
		u8 _rsv2[2];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_LPI_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.lpi_enable = 0, //enable,
		.psd_limit = enable ? mt7996_is_psd_country(dev->mt76.alpha2) : 0,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), false);
}

int mt7996_mcu_cp_support(struct mt7996_dev *dev, u8 mode)
{
	__le32 cp_mode;

	if (mode < mt76_connac_lmac_mapping(IEEE80211_AC_BE) ||
	    mode > mt76_connac_lmac_mapping(IEEE80211_AC_VO))
		return -EINVAL;

	if (!mt7996_has_wa(dev)) {
		struct {
			u8 _rsv[4];

			__le16 tag;
			__le16 len;
			u8 cp_mode;
			u8 rsv[3];
		} __packed req = {
			.tag = cpu_to_le16(UNI_CMD_SDO_CP_MODE),
			.len = cpu_to_le16(sizeof(req) - 4),
			.cp_mode = mode,
		};

		return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO),
					 &req, sizeof(req), false);
	}

	cp_mode = cpu_to_le32(mode);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(CP_SUPPORT),
				 &cp_mode, sizeof(cp_mode), true);
}

int mt7996_mcu_set_pp_en(struct mt7996_phy *phy, u8 mode, u16 bitmap)
{
	struct mt7996_dev *dev = phy->dev;
	bool pp_auto = (mode == PP_FW_MODE);
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;
		u8 mgmt_mode;
		u8 band_idx;
		u8 force_bitmap_ctrl;
		u8 auto_mode;
		__le16 bitmap;
		u8 csa_enable;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_PP_EN_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.mgmt_mode = !pp_auto,
		.band_idx = phy->mt76->band_idx,
		.force_bitmap_ctrl = (mode == PP_USR_MODE) ? 2 : 0,
		.auto_mode = pp_auto,
		.bitmap = cpu_to_le16(bitmap),
		.csa_enable = false,
	};

	if (phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ ||
	    mode > PP_USR_MODE)
		return 0;

	if (bitmap && phy->punct_bitmap == bitmap)
		return 0;

	phy->punct_bitmap = bitmap;
	phy->pp_mode = mode;

#ifdef CONFIG_MTK_DEBUG
	/* Configuring PP would cause FW to disable MRU Probe by default. */
	if (!is_mt7996(&dev->mt76))
		phy->mru_probe_enable = false;
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_pp_sta_dscb(struct mt7996_phy *phy,
			       struct cfg80211_chan_def *chandef,
			       u8 omac_idx)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;
		u8 band_idx;
		u8 omac_idx;
		u8 eht_op_present;
		u8 dscb_present;
		__le16 dscb;
		u8 ctrl;
		u8 ccfs0;
		u8 ccfs1;
		u8 rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_PP_DSCB_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.band_idx = phy->mt76->band_idx,
		.omac_idx = omac_idx,
		.eht_op_present = true,
		.dscb_present = !!chandef->punctured,
		.dscb = cpu_to_le16(chandef->punctured),
		.ctrl = 0,
		.ccfs0 = ieee80211_frequency_to_channel(chandef->center_freq1),
		.ccfs1 = ieee80211_frequency_to_channel(chandef->center_freq1),
	};

	if (phy->mt76->chanctx->chandef.chan->band == NL80211_BAND_2GHZ ||
	    phy->punct_bitmap == chandef->punctured)
		return 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_320:
		req.ctrl |= IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ;
		if (chandef->chan->hw_value < req.ccfs1)
			req.ccfs0 -= 16;
		else
			req.ccfs0 += 16;
		break;
	case NL80211_CHAN_WIDTH_160:
		req.ctrl |= IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ;
		if (chandef->chan->hw_value < req.ccfs1)
			req.ccfs0 -= 8;
		else
			req.ccfs0 += 8;
		break;
	case NL80211_CHAN_WIDTH_80:
		req.ctrl |= IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ;
		req.ccfs0 = 0;
		break;
	default:
		return 0;
		break;
	}

	phy->punct_bitmap = cpu_to_le16(chandef->punctured);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

static int mt7996_mcu_set_scs_stats(struct mt7996_phy *phy)
{
	struct mt7996_scs_ctrl ctrl = phy->scs_ctrl;
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;

		u8 _rsv2[6];
		s8 min_rssi;
		u8 _rsv3;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SCS_SEND_DATA),
		.len = cpu_to_le16(sizeof(req) - 4),

		.min_rssi = ctrl.sta_min_rssi,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(SCS),
				 &req, sizeof(req), false);
}

static void mt7996_sta_rssi_work(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_phy *poll_phy = (struct mt7996_phy *)data;
	struct mt7996_vif *mvif = msta->vif;
	struct ieee80211_link_sta *link_sta;
	struct ieee80211_vif *vif;
	unsigned int link_id;

	vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	rcu_read_lock();
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;

		msta_link = rcu_dereference(msta->link[link_id]);
		if (!msta_link)
			continue;

		if (msta_link->wcid.phy_idx != poll_phy->mt76->band_idx)
			continue;

		if (poll_phy->scs_ctrl.sta_min_rssi > msta_link->ack_signal)
			poll_phy->scs_ctrl.sta_min_rssi = msta_link->ack_signal;

		break;
	}
	rcu_read_unlock();
}

void mt7996_mcu_scs_sta_poll(struct work_struct *work)
{
	struct mt7996_dev *dev = container_of(work, struct mt7996_dev,
				 scs_work.work);
	bool scs_enable_flag = false;
	u8 i;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		struct mt7996_phy *phy;

		switch (i) {
		case MT_BAND0:
			phy = dev->mphy.priv;
			break;
		case MT_BAND1:
			phy = mt7996_phy2(dev);
			break;
		case MT_BAND2:
			phy = mt7996_phy3(dev);
			break;
		default:
			phy = NULL;
			break;
		}

		if (!phy || !test_bit(MT76_STATE_RUNNING, &phy->mt76->state) ||
		    !phy->scs_ctrl.scs_enable)
			continue;

		ieee80211_iterate_stations_atomic(phy->mt76->hw,
						  mt7996_sta_rssi_work, phy);

		scs_enable_flag = true;
		if (mt7996_mcu_set_scs_stats(phy))
			dev_err(dev->mt76.dev, "Failed to send scs mcu cmd\n");
		phy->scs_ctrl.sta_min_rssi = 0;
	}

	if (scs_enable_flag)
		ieee80211_queue_delayed_work(mt76_hw(dev), &dev->scs_work, HZ);
}


int mt7996_mcu_set_scs(struct mt7996_phy *phy, u8 enable)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;

		u8 scs_enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_CMD_SCS_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.scs_enable = enable,
	};

	phy->scs_ctrl.scs_enable = enable;

	if (enable == SCS_ENABLE)
		ieee80211_queue_delayed_work(mt76_hw(dev), &dev->scs_work, HZ);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(SCS),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_pp_alg_ctrl(struct mt7996_phy *phy, u8 action)
{
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		__le32 pp_timer_intv;
		__le32 rsv2[14];
		u8 band_idx;
		u8 pp_action;
		u8 reset;
		u8 _rsv3;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_PP_ALG_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.pp_timer_intv = action == PP_ALG_SET_TIMER ? 2000 : 0,
		.band_idx = phy->mt76->band_idx,
		.pp_action = action,
		.reset = 0,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_sr_enable(struct mt7996_phy *phy, u8 action, u64 val, bool set)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;

		__le32 val;

	} __packed req = {
		.band_idx = phy->mt76->band_idx,

		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),

		.val = cpu_to_le32((u32) val),
	};

	if (WARN_ON_ONCE(((unsigned long)(phy)) < 4000)) {
		pr_err("sr-enable: phy invalid: %p\n", phy);
		return -EINVAL;
	}

	if (WARN_ON_ONCE((unsigned long)(phy->mt76) < 4000)) {
		pr_err("phy->mt76 invalid: %p\n", phy->mt76);
		return -EINVAL;
	}

	req.band_idx = phy->mt76->band_idx;

	mtk_dbg(&phy->dev->mt76, CFG, "mcu-set-sr-enable, action: 0x%x val: %d set: %d\n",
		action, (int)val, set);
	if (set)
		return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(SR), &req,
					 sizeof(req), false);
	else
		return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD_QUERY(SR), &req,
					 sizeof(req), false);
}

static void mt7996_mcu_rx_sr_swsd(struct mt7996_dev *dev, struct sk_buff *skb)
{
#define SR_SCENE_DETECTION_TIMER_PERIOD_MS 500
	struct mt7996_mcu_sr_swsd_event *event;
	static const char * const rules[] = {"1 - NO CONNECTED", "2 - NO CONGESTION",
					     "3 - NO INTERFERENCE", "4 - SR ON"};
	u8 idx;

	event = (struct mt7996_mcu_sr_swsd_event *)skb->data;
	idx = event->basic.band_idx;

	dev_info(dev->mt76.dev, "Band index = %u\n", le16_to_cpu(event->basic.band_idx));
	dev_info(dev->mt76.dev, "Hit Rule = %s\n", rules[event->tlv[idx].rule]);
	dev_info(dev->mt76.dev, "Timer Period = %d(us)\n"
		 "Congestion Ratio  = %d.%1d%%\n",
		 SR_SCENE_DETECTION_TIMER_PERIOD_MS * 1000,
		 le32_to_cpu(event->tlv[idx].total_airtime_ratio) / 10,
		 le32_to_cpu(event->tlv[idx].total_airtime_ratio) % 10);
	dev_info(dev->mt76.dev,
		 "Total Airtime = %d(us)\n"
		 "ChBusy = %d\n"
		 "SrTx = %d\n"
		 "OBSS = %d\n"
		 "MyTx = %d\n"
		 "MyRx = %d\n"
		 "Interference Ratio = %d.%1d%%\n",
		 le32_to_cpu(event->tlv[idx].total_airtime),
		 le32_to_cpu(event->tlv[idx].channel_busy_time),
		 le32_to_cpu(event->tlv[idx].sr_tx_airtime),
		 le32_to_cpu(event->tlv[idx].obss_airtime),
		 le32_to_cpu(event->tlv[idx].my_tx_airtime),
		 le32_to_cpu(event->tlv[idx].my_rx_airtime),
		 le32_to_cpu(event->tlv[idx].obss_airtime_ratio) / 10,
		 le32_to_cpu(event->tlv[idx].obss_airtime_ratio) % 10);
}

static void mt7996_mcu_rx_sr_hw_indicator(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_sr_hw_ind_event *event;

	event = (struct mt7996_mcu_sr_hw_ind_event *)skb->data;

	dev_info(dev->mt76.dev, "Inter PPDU Count = %u\n",
		 le16_to_cpu(event->inter_bss_ppdu_cnt));
	dev_info(dev->mt76.dev, "SR Valid Count = %u\n",
		 le16_to_cpu(event->non_srg_valid_cnt));
	dev_info(dev->mt76.dev, "SR Tx Count = %u\n",
		 le32_to_cpu(event->sr_ampdu_mpdu_cnt));
	dev_info(dev->mt76.dev, "SR Tx Acked Count = %u\n",
		 le32_to_cpu(event->sr_ampdu_mpdu_acked_cnt));
}

void mt7996_mcu_rx_sr_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7996_phy *phy;
	struct mt7996_mcu_sr_common_event *event;

	event = (struct mt7996_mcu_sr_common_event *)skb->data;
	mphy = dev->mt76.phys[event->basic.band_idx];
	if (!mphy)
		return;

	phy = (struct mt7996_phy *)mphy->priv;

	switch (le16_to_cpu(event->basic.tag)) {
	case UNI_EVENT_SR_CFG_SR_ENABLE:
		phy->sr_enable = le32_to_cpu(event->value) ? true : false;
		mtk_dbg(&phy->dev->mt76, CFG, "rx-sr-event, sr-enabled: %d\n",
			phy->sr_enable);
		break;
	case UNI_EVENT_SR_HW_ESR_ENABLE:
		phy->enhanced_sr_enable = le32_to_cpu(event->value) ? true : false;
		mtk_dbg(&phy->dev->mt76, CFG, "rx-sr-event, enhanced-sr-enabled: %d\n",
			phy->enhanced_sr_enable);
		break;
	case UNI_EVENT_SR_SW_SD:
		mt7996_mcu_rx_sr_swsd(dev, skb);
		break;
	case UNI_EVENT_SR_HW_IND:
		mt7996_mcu_rx_sr_hw_indicator(dev, skb);
		break;
	default:
		dev_info(dev->mt76.dev, "Unknown SR event tag %d\n",
			 le16_to_cpu(event->basic.tag));
	}
}

int mt7996_mcu_set_vow_drr_ctrl(struct mt7996_phy *phy,
				struct mt7996_vif_link *mconf,
				struct mt7996_sta_link *msta_link,
				enum vow_drr_ctrl_id id)
{
	struct mt7996_vow_sta_ctrl *vow = msta_link ? &msta_link->vow : NULL;
	u32 val = 0;
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 wlan_idx;
		u8 band_idx;
		u8 wmm_idx;
		__le32 ctrl_id;

		union {
			__le32 val;
			u8 drr_quantum[VOW_DRR_QUANTUM_NUM];
		};

		u8 __rsv2[3];
		u8 omac_idx;
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_DRR_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.wlan_idx = cpu_to_le16(msta_link ? msta_link->wcid.idx : 0),
		.band_idx = phy->mt76->band_idx,
		.wmm_idx = msta_link ? mconf->mt76.wmm_idx : 0,
		.ctrl_id = cpu_to_le32(id),
		.omac_idx = msta_link ? mconf->mt76.omac_idx : 0
	};

	switch (id) {
	case VOW_DRR_CTRL_STA_ALL:
		val |= FIELD_PREP(MT7996_DRR_STA_BSS_GRP_MASK, vow->bss_grp_idx);
		val |= FIELD_PREP(MT7996_DRR_STA_AC0_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_BK]);
		val |= FIELD_PREP(MT7996_DRR_STA_AC1_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_BE]);
		val |= FIELD_PREP(MT7996_DRR_STA_AC2_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_VI]);
		val |= FIELD_PREP(MT7996_DRR_STA_AC3_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_VO]);
		req.val = cpu_to_le32(val);
		break;
	case VOW_DRR_CTRL_STA_BSS_GROUP:
		req.val = cpu_to_le32(vow->bss_grp_idx);
		break;
	case VOW_DRR_CTRL_AIRTIME_DEFICIT_BOUND:
		req.val = cpu_to_le32(phy->dev->vow.max_deficit);
		break;
	case VOW_DRR_CTRL_AIRTIME_QUANTUM_ALL:
		memcpy(req.drr_quantum, phy->dev->vow.drr_quantum, VOW_DRR_QUANTUM_NUM);
		break;
	case VOW_DRR_CTRL_STA_PAUSE:
		req.val = cpu_to_le32(vow->paused);
		break;
	default:
		dev_err(phy->dev->mt76.dev, "Unknown VoW DRR Control ID: %u\n", id);
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(VOW),
	                         &req, sizeof(req), true);
}

int mt7996_mcu_set_vow_feature_ctrl(struct mt7996_phy *phy)
{
	struct mt7996_vow_ctrl *vow = &phy->dev->vow;
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;

		/* DW0 */
		__le16 apply_bwc_enable_per_grp;
		__le16 apply_bwc_refill_period		: 1;
		__le16 __rsv2				: 3;
		__le16 apply_band1_search_rule		: 1;
		__le16 apply_band0_search_rule		: 1;
		__le16 __rsv3				: 3;
		__le16 apply_watf_enable		: 1;
		__le16 __rsv4				: 2;
		__le16 apply_grp_no_change_in_txop	: 1;
		__le16 apply_atf_enable			: 1;
		__le16 apply_bwc_token_refill_enable	: 1;
		__le16 apply_bwc_enable			: 1;

		/* DW1 */
		__le16 apply_bwc_check_time_token_per_grp;
		__le16 __rsv5;

		/* DW2 */
		__le16 apply_bwc_check_len_token_per_grp;
		__le16 __rsv6;

		/* DW3 */
		u8 band_idx;
		u8 __rsv7[3];

		/* DW4 */
		__le32 __rsv8;

		/* DW5 */
		__le16 bwc_enable_per_grp;
		__le16 bwc_refill_period	: 3;
		__le16 __rsv9			: 1;
		__le16 band1_search_rule	: 1;
		__le16 band0_search_rule	: 1;
		__le16 __rsv10			: 3;
		__le16 watf_enable		: 1;
		__le16 __rsv11			: 2;
		__le16 grp_no_change_in_txop	: 1;
		__le16 atf_enable		: 1;
		__le16 bwc_token_refill_enable	: 1;
		__le16 bwc_enable		: 1;

		/* DW6 */
		__le16 bwc_check_time_token_per_grp;
		__le16 __rsv12;

		/* DW7 */
		__le16 bwc_check_len_token_per_grp;
		__le16 __rsv13;

		/* DW8 */
		__le32 apply_atf_rts_sta_lock		: 1;
		__le32 atf_rts_sta_lock			: 1;
		__le32 apply_atf_keep_quantum		: 1;
		__le32 atf_keep_quantum			: 1;
		__le32 apply_tx_cnt_mode_ctrl		: 1;
		__le32 tx_cnt_mode_ctrl			: 4;
		__le32 apply_tx_measure_mode_enable	: 1;
		__le32 tx_measure_mode_enable		: 1;
		__le32 apply_backoff_ctrl		: 1;
		__le32 backoff_bound_enable		: 1;
		__le32 backoff_bound			: 5;
		__le32 apply_atf_rts_fail_charge	: 1;
		__le32 atf_rts_fail_charge		: 1;
		__le32 apply_zero_eifs			: 1;
		__le32 zero_eifs			: 1;
		__le32 apply_rx_rifs_enable		: 1;
		__le32 rx_rifs_enable			: 1;
		__le32 apply_vow_ctrl			: 1;
		__le32 vow_ctrl_val			: 1;
		__le32 vow_ctrl_bit			: 5;
		__le32 __rsv14				: 1;

		/* DW9 */
		__le32 apply_spl_sta_num	: 1;
		__le32 spl_sta_num		: 3;
		__le32 dbg_lvl			: 2;
		__le32 apply_atf_sch_ctrl	: 1;
		__le32 atf_sch_type		: 2;
		__le32 atf_sch_policy		: 2;
		__le32 __rsv15			: 21;
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_FEATURE_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		/* DW0 */
		.apply_bwc_enable_per_grp = cpu_to_le16(0xffff),
		.apply_bwc_refill_period = true,
		.apply_band1_search_rule = true,
		.apply_band0_search_rule = true,
		.apply_watf_enable = true,
		.apply_grp_no_change_in_txop = true,
		.apply_atf_enable = true,
		.apply_bwc_token_refill_enable = true,
		.apply_bwc_enable = true,
		/* DW1 */
		.apply_bwc_check_time_token_per_grp = cpu_to_le16(0xffff),
		/* DW2 */
		.apply_bwc_check_len_token_per_grp = cpu_to_le16(0xffff),
		/* DW3 */
		.band_idx = phy->mt76->band_idx,
		/* DW5 */
		.bwc_enable_per_grp = cpu_to_le16(0xffff),
		.bwc_refill_period = VOW_REFILL_PERIOD_32US,
		.band1_search_rule = VOW_SEARCH_WMM_FIRST,
		.band0_search_rule = VOW_SEARCH_WMM_FIRST,
		.watf_enable = vow->watf_enable,
		.grp_no_change_in_txop = true,
		.atf_enable = vow->atf_enable,
		.bwc_token_refill_enable = true,
		.bwc_enable = false,
		/* DW6 */
		.bwc_check_time_token_per_grp = cpu_to_le16(0x0),
		/* DW7 */
		.bwc_check_len_token_per_grp = cpu_to_le16(0x0),
		/* DW8 */
		.apply_atf_rts_sta_lock = false,
		.apply_atf_keep_quantum = true,
		.atf_keep_quantum = true,
		.apply_tx_cnt_mode_ctrl = false,
		.apply_tx_measure_mode_enable = false,
		.apply_backoff_ctrl = false,
		.apply_atf_rts_fail_charge = false,
		.apply_zero_eifs = false,
		.apply_rx_rifs_enable = false,
		.apply_vow_ctrl = true,
		.vow_ctrl_val = true,
		/* Reset DRR table when SER occurs. */
		.vow_ctrl_bit = 26,
		/* DW9 */
		.apply_spl_sta_num = false,
		.dbg_lvl = 0,
		.apply_atf_sch_ctrl = true,
		.atf_sch_type = vow->sch_type,
		.atf_sch_policy = vow->sch_policy
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(VOW),
	                         &req, sizeof(req), true);
}

int mt7996_mcu_set_vow_drr_dbg(struct mt7996_dev *dev, u32 val)
{
#define MT7996_VOW_DEBUG_MODE	0xe
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 __rsv2[4];
		__le32 action;
		__le32 val;
		u8 __rsv3[8];
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_DRR_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = cpu_to_le32(MT7996_VOW_DEBUG_MODE),
		.val = cpu_to_le32(val),
	};

	if (val & ~VOW_DRR_DBG_FLAGS)
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(VOW), &req,
				 sizeof(req), true);
}

int mt7996_mcu_set_eml_omn(struct ieee80211_vif *vif,
			   u8 link_id,
			   struct ieee80211_sta *sta,
			   struct mt7996_dev *dev,
			   struct mt7996_eml_omn *eml_omn)
{
#define EML_OMN_CONTROL_EMLSR_MODE	0x01
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *mconf, *mconf_link;
	struct sta_rec_eml_op *eml_op;
	struct sk_buff *skb;
	struct tlv *tlv;

	msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	mconf = mt7996_vif_link(dev, vif, link_id);

	if (!msta_link || !mconf)
		return -EINVAL;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76,
					      &mconf->mt76,
					      &msta_link->wcid,
					      MT7996_STA_UPDATE_MAX_SIZE);

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EML_OP, sizeof(*eml_op));
	eml_op = (struct sta_rec_eml_op *) tlv;
	eml_op->bitmap = 0;

	if (eml_omn->control & EML_OMN_CONTROL_EMLSR_MODE) {
		unsigned long bitmap = (unsigned long) le16_to_cpu(eml_omn->bitmap);
		unsigned int linkid;

		for_each_set_bit(linkid, &bitmap, IEEE80211_MLD_MAX_NUM_LINKS) {
			mconf_link = mt7996_vif_link(dev, vif, linkid);

			if (!mconf_link)
				continue;

			eml_op->bitmap |= BIT(mconf_link->phy->mt76->band_idx);
		}
	}

	mt76_dbg(&dev->mt76, MT76_DBG_MLD,
		 "%s: link:%u, wcid:%d, control:%x, mode:%d, bmp:%x\n",
		 __func__, msta_link->wcid.link_id, msta_link->wcid.idx, eml_omn->control,
		 !!(eml_omn->control & EML_OMN_CONTROL_EMLSR_MODE), eml_op->bitmap);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
			MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7996_mcu_epcs_ctrl(u32 cmd, struct mt7996_dev *dev,
			 struct ieee80211_sta *sta, u8 link_id, bool enable,
			 u16 wmm_idx, struct mt7996_wmm_params *params)
{
	struct {
		u8 rsv1[4];

		__le16 tag;
		__le16 len;
		__le32 cmd;

		__le16 wlan_idx;
		__le16 wmm_idx;
		u8 enable;
		u8 rsv2[3];
		struct {
			struct {
				__le16 txop_limit;
				u8 ecwmin;
				u8 ecwmax;
				u8 aifsn;
				u8 rsv[3];
				u8 mu_ecwmin;
				u8 mu_ecwmax;
				u8 mu_aifsn;
				u8 mu_timer;
			} params[IEEE80211_NUM_ACS];
			__le16 flags;
			u8 rsv[2];
		} wmm;
	} req = {
		.tag = cpu_to_le16(UNI_CMD_EPCS_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.cmd = cpu_to_le32(cmd),
		.wmm_idx = cpu_to_le16(wmm_idx),
		.enable = enable,
		.wmm.flags = enable ? cpu_to_le16(EPCS_CTRL_WMM_FLAG_VALID) : 0,
	};
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	int i;

	msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	if (!msta_link)
		return -EINVAL;
	req.wlan_idx = cpu_to_le16(msta_link->wcid.idx);

	for (i = IEEE80211_AC_VO; i < IEEE80211_NUM_ACS; ++i) {
		u8 usr_ac = mt76_ac_to_hwq(i),
		   lmac_ac = mt76_connac_lmac_mapping(i);

		req.wmm.params[lmac_ac].txop_limit = cpu_to_le16(params[usr_ac].txop_limit);
		req.wmm.params[lmac_ac].ecwmin = params[usr_ac].ecwmin;
		req.wmm.params[lmac_ac].ecwmax = params[usr_ac].ecwmax;
		req.wmm.params[lmac_ac].aifsn = params[usr_ac].aifsn;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(EPCS), &req,
				 sizeof(req), false);
}

int mt7996_mcu_muru_dbg_info(struct mt7996_dev *dev, u16 item, u8 val)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;

		__le16 item;
		u8 __rsv2[2];
		__le32 value;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_MURU_DBG_INFO),
		.len = cpu_to_le16(sizeof(req) - 4),
		.item = cpu_to_le16(item),
		.value = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), true);
}

int mt7996_mcu_set_muru_fixed_rate_enable(struct mt7996_dev *dev, u8 action, int val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le16 value;
		__le16 rsv;
	} __packed data = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(data) - 4),
		.value = cpu_to_le16(!!val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &data, sizeof(data),
				 false);
}

int mt7996_mcu_set_muru_fixed_rate_parameter(struct mt7996_dev *dev, u8 action, void *para)
{
	char *buf = (char *)para;
	u8 num_user = 0, recv_arg = 0, max_mcs = 0, usr_mcs[4] = {0};
	__le16 bw;
	int i;
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 cmd_version;
		u8 cmd_revision;
		__le16 rsv;

		struct uni_muru_mum_set_group_tbl_entry entry;
	} __packed data = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(data) - 4),
	};

#define __RUALLOC_TYPE_CHECK_HE(BW) ((BW == RUALLOC_BW20) || (BW == RUALLOC_BW40) || (BW == RUALLOC_BW80) || (BW == RUALLOC_BW160))
#define __RUALLOC_TYPE_CHECK_EHT(BW) (__RUALLOC_TYPE_CHECK_HE(BW) || (BW == RUALLOC_BW320))
	/* [Num of user] - 1~4
	 * [RUAlloc] - BW320: 395, BW160: 137, BW80: 134, BW40: 130, BW20: 122
	 * [LTF/GI] - For VHT, short GI: 0, Long GI: 1; 	 *
	 * For HE/EHT, 4xLTF+3.2us: 0, 4xLTF+0.8us: 1, 2xLTF+0.8us:2
	 * [Phy/FullBW] - VHT: 0 / HEFullBw: 1 / HEPartialBw: 2 / EHTFullBW: 3, EHTPartialBW: 4
	 * [DL/UL] DL: 0, UL: 1, DL_UL: 2
	 * [Wcid User0] - WCID 0
	 * [MCS of WCID0] - For HE/VHT, 0-11: 1ss MCS0-MCS11, 12-23: 2SS MCS0-MCS11
	 * For EHT, 0-13: 1ss MCS0-MCS13, 14-27: 2SS MCS0-MCS13
	 * [WCID 1]
	 * [MCS of WCID1]
	 * [WCID 2]
	 * [MCS of WCID2]
	 * [WCID 3]
	 * [MCS of WCID3]
	 */

	recv_arg = sscanf(buf, "%hhu %hu %hhu %hhu %hhu %hu %hhu %hu %hhu %hu %hhu %hu %hhu",
			  &num_user, &bw, &data.entry.gi, &data.entry.capa, &data.entry.dl_ul,
			  &data.entry.wlan_idx0, &usr_mcs[0],
			  &data.entry.wlan_idx1, &usr_mcs[1],
			  &data.entry.wlan_idx2, &usr_mcs[2],
			  &data.entry.wlan_idx3, &usr_mcs[3]);

	if (recv_arg != (5 + (2 * num_user))) {
		dev_err(dev->mt76.dev, "The number of argument is invalid\n");
		goto error;
	}

	if (num_user > 0 && num_user < 5)
		data.entry.num_user = num_user - 1;
	else {
		dev_err(dev->mt76.dev, "The number of user count is invalid\n");
		goto error;
	}

	/**
	 * Older chip shall be set as HE. Refer to getHWSupportByChip() in Logan
	 * driver to know the value for differnt chips
	 */
	data.cmd_version = UNI_CMD_MURU_VER_EHT;

	if (data.cmd_version == UNI_CMD_MURU_VER_EHT)
		max_mcs = UNI_MAX_MCS_SUPPORT_EHT;
	else
		max_mcs = UNI_MAX_MCS_SUPPORT_HE;


	// Parameter Check
	if (data.cmd_version != UNI_CMD_MURU_VER_EHT) {
		if ((data.entry.capa > MAX_MODBF_HE) || (bw == RUALLOC_BW320))
			goto error;
	} else {
		if ((data.entry.capa <= MAX_MODBF_HE) && (bw == RUALLOC_BW320))
			goto error;
	}

	if (data.entry.capa <= MAX_MODBF_HE)
		max_mcs = UNI_MAX_MCS_SUPPORT_HE;

	if (__RUALLOC_TYPE_CHECK_EHT(bw)) {
		data.entry.ru_alloc = (u8)(bw & 0xFF);
		if (bw == RUALLOC_BW320)
			data.entry.ru_alloc_ext = (u8)(bw >> 8);
	} else {
		dev_err(dev->mt76.dev, "RU_ALLOC argument is invalid\n");
		goto error;
	}

	if ((data.entry.gi > 2) ||
	    ((data.entry.gi > 1) && (data.entry.capa == MAX_MODBF_VHT))) {
		dev_err(dev->mt76.dev, "GI argument is invalid\n");
		goto error;
	}

	if (data.entry.dl_ul > 2) {
		dev_err(dev->mt76.dev, "DL_UL argument is invalid\n");
		goto error;
	}

#define __mcs_handler(_n)							\
	do {									\
		if (usr_mcs[_n] > max_mcs) {					\
			usr_mcs[_n] -= (max_mcs + 1);				\
			data.entry.nss##_n = 1;					\
			if (usr_mcs[_n] > max_mcs)				\
				usr_mcs[_n] = max_mcs;				\
		}								\
		if ((data.entry.dl_ul & 0x1) == 0)				\
			data.entry.dl_mcs_user##_n = usr_mcs[_n];		\
		if ((data.entry.dl_ul & 0x3) > 0)				\
			data.entry.ul_mcs_user##_n = usr_mcs[_n];		\
	}									\
	while (0)

	for (i=0; i<= data.entry.num_user; i++) {
		switch (i) {
			case 0:
				__mcs_handler(0);
				break;
			case 1:
				__mcs_handler(1);
				break;
			case 2:
				__mcs_handler(2);
				break;
			case 3:
				__mcs_handler(3);
				break;
			default:
				break;
		}
	}
#undef __mcs_handler


	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &data,
				 sizeof(data), false);

error:
	dev_err(dev->mt76.dev, "Command failed!\n");
	return -EINVAL;
}

/**
 * This function can be used to build the following commands
 * MURU_SUTX_CTRL (0x10)
 * SET_FORCE_MU (0x33)
 * SET_MUDL_ACK_POLICY (0xC8)
 * SET_TRIG_TYPE (0xC9)
 * SET_20M_DYN_ALGO (0xCA)
 * SET_CERT_MU_EDCA_OVERRIDE (0xCD)
 */
int mt7996_mcu_set_muru_cmd(struct mt7996_dev *dev, u16 action, int val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 config;
		u8 rsv[3];
	} __packed data = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(data) - 4),
		.config = (u8) val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &data, sizeof(data),
				 false);
}

int mt7996_mcu_muru_set_prot_frame_thr(struct mt7996_dev *dev, u32 val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 prot_frame_thr;
	} __packed data = {
		.tag = cpu_to_le16(UNI_CMD_MURU_PROT_FRAME_THR),
		.len = cpu_to_le16(sizeof(data) - 4),
		.prot_frame_thr = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &data, sizeof(data),
				 false);
}

int mt7996_mcu_set_muru_cfg(struct mt7996_phy *phy, void *data)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_muru *muru;
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 version;
		u8 revision;
		u8 _rsv2[2];

		struct mt7996_muru muru;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_MURU_MUNUAL_CONFIG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.version = UNI_CMD_MURU_VER_EHT,
	};

	muru = (struct mt7996_muru *) data;
	memcpy(&req.muru, muru, sizeof(struct mt7996_muru));

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), false);
}

int mt7996_set_muru_cfg(struct mt7996_phy *phy, u8 action, u8 val)
{
	struct mt7996_muru *muru;
	struct mt7996_muru_dl *dl;
	struct mt7996_muru_ul *ul;
	struct mt7996_muru_comm *comm;
	int ret = 0;

	muru = kzalloc(sizeof(struct mt7996_muru), GFP_KERNEL);
	dl = &muru->dl;
	ul = &muru->ul;
	comm = &muru->comm;

	switch (action) {
	case MU_CTRL_DL_USER_CNT:
		dl->user_num = val;
		comm->ppdu_format = MURU_PPDU_HE_MU;
		comm->sch_type = MURU_OFDMA_SCH_TYPE_DL;
		muru->cfg_comm = cpu_to_le32(MURU_COMM_SET);
		muru->cfg_dl = cpu_to_le32(MURU_FIXED_DL_TOTAL_USER_CNT);
		ret = mt7996_mcu_set_muru_cfg(phy, muru);
		break;
	case MU_CTRL_UL_USER_CNT:
		ul->user_num = val;
		comm->ppdu_format = MURU_PPDU_HE_TRIG;
		comm->sch_type = MURU_OFDMA_SCH_TYPE_UL;
		muru->cfg_comm = cpu_to_le32(MURU_COMM_SET);
		muru->cfg_ul = cpu_to_le32(MURU_FIXED_UL_TOTAL_USER_CNT);
		ret = mt7996_mcu_set_muru_cfg(phy, muru);
		break;
	default:
		break;
	}

	kfree(muru);
	return ret;
}

static inline void
mt7996_ibf_phase_assign(struct mt7996_dev *dev,
			struct mt7996_ibf_cal_info *cal,
			struct mt7996_txbf_phase *phase)
{
	/* fw return ibf calibrated data with
	 * the mt7996_txbf_phase_info_5g struct for both 2G and 5G.
	 * (return struct mt7992_txbf_phase_info_5g for ibf 2.0)
	 * Therefore, memcpy cannot be used here.
	 */
	if (get_ibf_version(dev) != IBF_VER_2) {
		phase_assign(cal->group, v1, m_t0_h, true);
		phase_assign(cal->group, v1, m_t1_h, true);
		phase_assign(cal->group, v1, m_t2_h, true);
		phase_assign(cal->group, v1, m_t2_h_sx2, false);
		phase_assign_rx_v1(cal->group, v1, r0);
		phase_assign_rx_v1(cal->group, v1, r1);
		phase_assign_rx_v1(cal->group, v1, r2);
		phase_assign_rx_v1(cal->group, v1, r3);
		phase_assign_rx(cal->group, v1, r2_sx2, false);
		phase_assign_rx(cal->group, v1, r3_sx2, false);
		phase_assign(cal->group, v1, r0_reserved, false);
		phase_assign(cal->group, v1, r1_reserved, false);
		phase_assign(cal->group, v1, r2_reserved, false);
		phase_assign(cal->group, v1, r3_reserved, false);
		phase_assign(cal->group, v1, r2_sx2_reserved, false);
		phase_assign(cal->group, v1, r3_sx2_reserved, false);
	} else {
		phase_assign(cal->group, v2, m_t0_h, true);
		phase_assign(cal->group, v2, m_t1_h, true);
		phase_assign(cal->group, v2, m_t2_h, true);
		if (cal->group) {
			phase->v2.phase_5g.m_t3_h = cal->v2.phase_5g.m_t3_h;
			dev_info(dev->mt76.dev, "m_t3_h = %d\n", phase->v2.phase_5g.m_t3_h);
		}
		phase_assign_rx_ext(cal->group, v2, r0, true);
		phase_assign_rx_ext(cal->group, v2, r1, true);
		phase_assign_rx_ext(cal->group, v2, r2, true);
		phase_assign_rx_ext(cal->group, v2, r3, true);
		if (cal->group) {
			memcpy(&phase->v2.phase_5g.r4, &cal->v2.phase_5g.r4,
			       sizeof(struct txbf_rx_phase_ext));
			dev_info(dev->mt76.dev, "r4.rx_uh = %d\n", phase->v2.phase_5g.r4.rx_uh);
			dev_info(dev->mt76.dev, "r4.rx_h = %d\n", phase->v2.phase_5g.r4.rx_h);
			dev_info(dev->mt76.dev, "r4.rx_mh = %d\n", phase->v2.phase_5g.r4.rx_mh);
			dev_info(dev->mt76.dev, "r4.rx_m = %d\n", phase->v2.phase_5g.r4.rx_m);
			dev_info(dev->mt76.dev, "r4.rx_l = %d\n", phase->v2.phase_5g.r4.rx_l);
			dev_info(dev->mt76.dev, "r4.rx_ul = %d\n", phase->v2.phase_5g.r4.rx_ul);
		}
	}
}

void
mt7996_mcu_rx_bf_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_bf_basic_event *event;

	event = (struct mt7996_mcu_bf_basic_event *)skb->data;

	dev_info(dev->mt76.dev, " bf_event tag = %d\n", event->tag);

	switch (event->tag) {
	case UNI_EVENT_BF_PFMU_TAG: {

		struct mt7996_pfmu_tag_event *tag;
		u32 *raw_t1, *raw_t2;

		tag = (struct mt7996_pfmu_tag_event *) skb->data;

		raw_t1 = (u32 *)&tag->t1;
		raw_t2 = (u32 *)&tag->t2;

		dev_info(dev->mt76.dev, "=================== TXBf Profile Tag1 Info ==================\n");
		dev_info(dev->mt76.dev,
			 "DW0 = 0x%08x, DW1 = 0x%08x, DW2 = 0x%08x\n",
			 raw_t1[0], raw_t1[1], raw_t1[2]);
		dev_info(dev->mt76.dev,
			 "DW4 = 0x%08x, DW5 = 0x%08x, DW6 = 0x%08x\n\n",
			 raw_t1[3], raw_t1[4], raw_t1[5]);
		dev_info(dev->mt76.dev, "PFMU ID = %d              Invalid status = %d\n",
			 tag->t1.pfmu_idx, tag->t1.invalid_prof);
		dev_info(dev->mt76.dev, "iBf/eBf = %d\n\n", tag->t1.ebf);
		dev_info(dev->mt76.dev, "DBW   = %d\n", tag->t1.data_bw);
		dev_info(dev->mt76.dev, "SU/MU = %d\n", tag->t1.is_mu);
		dev_info(dev->mt76.dev,
			 "nrow = %d, ncol = %d, ng = %d, LM = %d, CodeBook = %d MobCalEn = %d\n",
			 tag->t1.nr, tag->t1.nc, tag->t1.ngroup, tag->t1.lm, tag->t1.codebook,
			 tag->t1.mob_cal_en);

		if (tag->t1.lm <= BF_LM_HE)
			dev_info(dev->mt76.dev, "RU start = %d, RU end = %d\n",
				 tag->t1.field.ru_start_id, tag->t1.field.ru_end_id);
		else
			dev_info(dev->mt76.dev, "PartialBW = %d\n",
				 tag->t1.bw_info.partial_bw_info);

		dev_info(dev->mt76.dev, "Mem Col1 = %d, Mem Row1 = %d, Mem Col2 = %d, Mem Row2 = %d\n",
			 tag->t1.col_id1, tag->t1.row_id1, tag->t1.col_id2, tag->t1.row_id2);
		dev_info(dev->mt76.dev, "Mem Col3 = %d, Mem Row3 = %d, Mem Col4 = %d, Mem Row4 = %d\n\n",
			 tag->t1.col_id3, tag->t1.row_id3, tag->t1.col_id4, tag->t1.row_id4);
		dev_info(dev->mt76.dev,
			 "STS0_SNR = 0x%02x, STS1_SNR = 0x%02x, STS2_SNR = 0x%02x, STS3_SNR = 0x%02x\n",
			 tag->t1.snr_sts0, tag->t1.snr_sts1, tag->t1.snr_sts2, tag->t1.snr_sts3);
		dev_info(dev->mt76.dev,
			 "STS4_SNR = 0x%02x, STS5_SNR = 0x%02x, STS6_SNR = 0x%02x, STS7_SNR = 0x%02x\n",
			 tag->t1.snr_sts4, tag->t1.snr_sts5, tag->t1.snr_sts6, tag->t1.snr_sts7);
		dev_info(dev->mt76.dev, "=============================================================\n");

		dev_info(dev->mt76.dev, "=================== TXBf Profile Tag2 Info ==================\n");
		dev_info(dev->mt76.dev,
			 "DW0 = 0x%08x, DW1 = 0x%08x, DW2 = 0x%08x\n",
			 raw_t2[0], raw_t2[1], raw_t2[2]);
		dev_info(dev->mt76.dev,
			 "DW3 = 0x%08x, DW4 = 0x%08x, DW5 = 0x%08x\n\n",
			 raw_t2[3], raw_t2[4], raw_t2[5]);
		dev_info(dev->mt76.dev, "Smart antenna ID = 0x%x,  SE index = %d\n",
			 tag->t2.smart_ant, tag->t2.se_idx);
		dev_info(dev->mt76.dev, "Timeout = 0x%x\n", tag->t2.ibf_timeout);
		dev_info(dev->mt76.dev, "Desired BW = %d, Desired Ncol = %d, Desired Nrow = %d\n",
			 tag->t2.ibf_data_bw, tag->t2.ibf_nc, tag->t2.ibf_nr);
		dev_info(dev->mt76.dev, "Desired RU Allocation = %d\n", tag->t2.ibf_ru);
		dev_info(dev->mt76.dev, "Mobility DeltaT = %d, Mobility LQ = %d\n",
			 tag->t2.mob_delta_t, tag->t2.mob_lq_result);
		dev_info(dev->mt76.dev, "=============================================================\n");
		break;
	}
	case UNI_EVENT_BF_STAREC: {

		struct mt7996_mcu_bf_starec_read *r;

		r = (struct mt7996_mcu_bf_starec_read *)skb->data;
		dev_info(dev->mt76.dev, "=================== BF StaRec ===================\n"
					"rStaRecBf.u2PfmuId      = %d\n"
					"rStaRecBf.fgSU_MU       = %d\n"
					"rStaRecBf.u1TxBfCap     = %d\n"
					"rStaRecBf.ucSoundingPhy = %d\n"
					"rStaRecBf.ucNdpaRate    = %d\n"
					"rStaRecBf.ucNdpRate     = %d\n"
					"rStaRecBf.ucReptPollRate= %d\n"
					"rStaRecBf.ucTxMode      = %d\n"
					"rStaRecBf.ucNc          = %d\n"
					"rStaRecBf.ucNr          = %d\n"
					"rStaRecBf.ucCBW         = %d\n"
					"rStaRecBf.ucMemRequire20M = %d\n"
					"rStaRecBf.ucMemRow0     = %d\n"
					"rStaRecBf.ucMemCol0     = %d\n"
					"rStaRecBf.ucMemRow1     = %d\n"
					"rStaRecBf.ucMemCol1     = %d\n"
					"rStaRecBf.ucMemRow2     = %d\n"
					"rStaRecBf.ucMemCol2     = %d\n"
					"rStaRecBf.ucMemRow3     = %d\n"
					"rStaRecBf.ucMemCol3     = %d\n",
					r->pfmu_id,
					r->is_su_mu,
					r->txbf_cap,
					r->sounding_phy,
					r->ndpa_rate,
					r->ndp_rate,
					r->rpt_poll_rate,
					r->tx_mode,
					r->nc,
					r->nr,
					r->bw,
					r->mem_require_20m,
					r->mem_row0,
					r->mem_col0,
					r->mem_row1,
					r->mem_col1,
					r->mem_row2,
					r->mem_col2,
					r->mem_row3,
					r->mem_col3);

		dev_info(dev->mt76.dev, "rStaRecBf.u2SmartAnt    = 0x%x\n"
					"rStaRecBf.ucSEIdx       = %d\n"
					"rStaRecBf.uciBfTimeOut  = 0x%x\n"
					"rStaRecBf.uciBfDBW      = %d\n"
					"rStaRecBf.uciBfNcol     = %d\n"
					"rStaRecBf.uciBfNrow     = %d\n"
					"rStaRecBf.nr_bw160      = %d\n"
					"rStaRecBf.nc_bw160 	  = %d\n"
					"rStaRecBf.ru_start_idx  = %d\n"
					"rStaRecBf.ru_end_idx 	  = %d\n"
					"rStaRecBf.trigger_su 	  = %d\n"
					"rStaRecBf.trigger_mu 	  = %d\n"
					"rStaRecBf.ng16_su 	  = %d\n"
					"rStaRecBf.ng16_mu 	  = %d\n"
					"rStaRecBf.codebook42_su = %d\n"
					"rStaRecBf.codebook75_mu = %d\n"
					"rStaRecBf.he_ltf 	      = %d\n"
					"======================================\n",
					r->smart_ant,
					r->se_idx,
					r->bf_timeout,
					r->bf_dbw,
					r->bf_ncol,
					r->bf_nrow,
					r->nr_lt_bw80,
					r->nc_lt_bw80,
					r->ru_start_idx,
					r->ru_end_idx,
					r->trigger_su,
					r->trigger_mu,
					r->ng16_su,
					r->ng16_mu,
					r->codebook42_su,
					r->codebook75_mu,
					r->he_ltf);
		break;
	}
	case UNI_EVENT_BF_FBK_INFO: {
		struct mt7996_mcu_txbf_fbk_info *info;
		__le32 total, i;

		info = (struct mt7996_mcu_txbf_fbk_info *)skb->data;

		total = info->u4PFMUWRDoneCnt + info->u4PFMUWRFailCnt;
		total += info->u4PFMUWRTimeoutFreeCnt + info->u4FbRptPktDropCnt;

		dev_info(dev->mt76.dev, "\n");
		dev_info(dev->mt76.dev, "\x1b[32m =================================\x1b[m\n");
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRDoneCnt              = %u\x1b[m\n",
			info->u4PFMUWRDoneCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRFailCnt              = %u\x1b[m\n",
			info->u4PFMUWRFailCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRTimeOutCnt           = %u\x1b[m\n",
			info->u4PFMUWRTimeOutCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRTimeoutFreeCnt       = %u\x1b[m\n",
			info->u4PFMUWRTimeoutFreeCnt);
		dev_info(dev->mt76.dev, "\x1b[32m FbRptPktDropCnt            = %u\x1b[m\n",
			info->u4FbRptPktDropCnt);
		dev_info(dev->mt76.dev, "\x1b[32m TotalFbRptPkt              = %u\x1b[m\n", total);
		dev_info(dev->mt76.dev, "\x1b[32m PollPFMUIntrStatTimeOut    = %u(micro-sec)\x1b[m\n",
			info->u4PollPFMUIntrStatTimeOut);
		dev_info(dev->mt76.dev, "\x1b[32m FbRptDeQInterval           = %u(milli-sec)\x1b[m\n",
			info->u4DeQInterval);
		dev_info(dev->mt76.dev, "\x1b[32m PktCntInFbRptTimeOutQ      = %u\x1b[m\n",
			info->u4RptPktTimeOutListNum);
		dev_info(dev->mt76.dev, "\x1b[32m PktCntInFbRptQ             = %u\x1b[m\n",
			info->u4RptPktListNum);

		// [ToDo] Check if it is valid entry
		for (i = 0; ((i < 5) && (i < CFG_BF_STA_REC_NUM)); i++) {

			// [ToDo] AID needs to be refined
			dev_info(dev->mt76.dev,"\x1b[32m AID%u  RxFbRptCnt           = %u\x1b[m\n"
				, i, info->au4RxPerStaFbRptCnt[i]);
		}

		break;
	}
	case UNI_EVENT_BF_TXSND_INFO: {
		struct mt7996_mcu_tx_snd_info *info;
		struct uni_event_bf_txsnd_sta_info *snd_sta_info;
		int Idx;
		int max_wtbl_size = mt7996_wtbl_size(dev);

		info = (struct mt7996_mcu_tx_snd_info *)skb->data;
		dev_info(dev->mt76.dev, "=================== Global Setting ===================\n");

		dev_info(dev->mt76.dev, "VhtOpt = 0x%02X, HeOpt = 0x%02X, GloOpt = 0x%02X\n",
			info->vht_opt, info->he_opt, info->glo_opt);

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "SuSta[%d] = 0x%08X,", Idx,
				 info->snd_rec_su_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");


		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "VhtMuSta[%d] = 0x%08X,", Idx, info->snd_rec_vht_mu_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "HeTBSta[%d] = 0x%08X,", Idx, info->snd_rec_he_tb_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "EhtTBSta[%d] = 0x%08X,", Idx, info->snd_rec_eht_tb_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < CFG_WIFI_RAM_BAND_NUM; Idx++) {
			dev_info(dev->mt76.dev, "Band%u:\n", Idx);
			dev_info(dev->mt76.dev, "	 Wlan Idx For VHT MC Sounding = %u\n", info->wlan_idx_for_mc_snd[Idx]);
			dev_info(dev->mt76.dev, "	 Wlan Idx For HE TB Sounding = %u\n", info->wlan_idx_for_he_tb_snd[Idx]);
			dev_info(dev->mt76.dev, "	 Wlan Idx For EHT TB Sounding = %u\n", info->wlan_idx_for_eht_tb_snd[Idx]);
		}

		dev_info(dev->mt76.dev, "ULLen = %d, ULMcs = %d, ULLDCP = %d\n",
			info->ul_length, info->mcs, info->ldpc);

		dev_info(dev->mt76.dev, "=================== STA Info ===================\n");

		for (Idx = 1; (Idx < 5 && (Idx < CFG_BF_STA_REC_NUM)); Idx++) {
			snd_sta_info = &info->snd_sta_info[Idx];
			dev_info(dev->mt76.dev, "Idx%2u Interval = %d, interval counter = %d, TxCnt = %d, StopReason = 0x%02X\n",
				Idx,
				snd_sta_info->snd_intv,
				snd_sta_info->snd_intv_cnt,
				snd_sta_info->snd_tx_cnt,
				snd_sta_info->snd_stop_reason);
		}

		dev_info(dev->mt76.dev, "=================== STA Info Connected ===================\n");
		// [ToDo] How to iterate and get AID info of station
		// Check UniEventBFCtrlTxSndHandle() on Logan

		//hardcode max_wtbl_size as 5
		max_wtbl_size = 5;
		for (Idx = 1; ((Idx < max_wtbl_size) && (Idx < CFG_BF_STA_REC_NUM)); Idx++) {

			// [ToDo] We do not show AID info here
			snd_sta_info = &info->snd_sta_info[Idx];
			dev_info(dev->mt76.dev, " Interval = %d (%u ms), interval counter = %d (%u ms), TxCnt = %d, StopReason = 0x%02X\n",
				snd_sta_info->snd_intv,
				snd_sta_info->snd_intv * 10,
				snd_sta_info->snd_intv_cnt,
				snd_sta_info->snd_intv_cnt * 10,
				snd_sta_info->snd_tx_cnt,
				snd_sta_info->snd_stop_reason);
		}

		dev_info(dev->mt76.dev, "======================================\n");

		break;
	}
	case UNI_EVENT_BF_CAL_PHASE: {
		struct mt7996_ibf_cal_info *cal;
		struct mt7996_txbf_phase *phase;
		union {
			struct mt7996_txbf_phase_out v1;
			struct mt7992_txbf_phase_out v2;
		} phase_out;
		int phase_out_size = sizeof(struct mt7996_txbf_phase_out);

		cal = (struct mt7996_ibf_cal_info *)skb->data;
		phase = (struct mt7996_txbf_phase *)dev->test.txbf_phase_cal;
		if (get_ibf_version(dev) == IBF_VER_2)
			phase_out_size = sizeof(struct mt7992_txbf_phase_out);
		memcpy(&phase_out, &cal->buf, phase_out_size);
		switch (cal->cal_type) {
		case IBF_PHASE_CAL_NORMAL:
		case IBF_PHASE_CAL_NORMAL_INSTRUMENT:
			/* Only calibrate group M */
			if (cal->group_l_m_n != GROUP_M)
				break;
			phase = &phase[cal->group];
			phase->status = cal->status;
			dev_info(dev->mt76.dev, "Calibrated result = %d\n", phase->status);
			dev_info(dev->mt76.dev, "Group %d and Group M\n", cal->group);
			mt7996_ibf_phase_assign(dev, cal, phase);
			break;
		case IBF_PHASE_CAL_VERIFY:
		case IBF_PHASE_CAL_VERIFY_INSTRUMENT:
			dev_info(dev->mt76.dev, "Verification result = %d\n", cal->status);
			break;
		default:
			break;
		}

		if (get_ibf_version(dev) == IBF_VER_2) {
			dev_info(dev->mt76.dev,
				 "c0_uh = %d, c1_uh = %d, c2_uh = %d, c3_uh = %d c4_uh = %d\n",
				 phase_out.v2.c0_uh, phase_out.v2.c1_uh, phase_out.v2.c2_uh,
				 phase_out.v2.c3_uh, phase_out.v2.c4_uh);
			dev_info(dev->mt76.dev,
				 "c0_h = %d, c1_h = %d, c2_h = %d, c3_h = %d c4_h = %d\n",
				 phase_out.v2.c0_h, phase_out.v2.c1_h, phase_out.v2.c2_h,
				 phase_out.v2.c3_h, phase_out.v2.c4_h);
			dev_info(dev->mt76.dev,
				 "c0_mh = %d, c1_mh = %d, c2_mh = %d, c3_mh = %d c4_mh = %d\n",
				 phase_out.v2.c0_mh, phase_out.v2.c1_mh, phase_out.v2.c2_mh,
				 phase_out.v2.c3_mh, phase_out.v2.c4_mh);
			dev_info(dev->mt76.dev,
				 "c0_m = %d, c1_m = %d, c2_m = %d, c3_m = %d c4_m = %d\n",
				 phase_out.v2.c0_m, phase_out.v2.c1_m, phase_out.v2.c2_m,
				 phase_out.v2.c3_m, phase_out.v2.c4_m);
			dev_info(dev->mt76.dev,
				 "c0_l = %d, c1_l = %d, c2_l = %d, c3_l = %d c4_l = %d\n",
				 phase_out.v2.c0_l, phase_out.v2.c1_l, phase_out.v2.c2_l,
				 phase_out.v2.c3_l, phase_out.v2.c4_l);
		} else {
			dev_info(dev->mt76.dev,
				 "c0_uh = %d, c1_uh = %d, c2_uh = %d, c3_uh = %d\n",
				 phase_out.v1.c0_uh, phase_out.v1.c1_uh,
				 phase_out.v1.c2_uh, phase_out.v1.c3_uh);
			dev_info(dev->mt76.dev,
				 "c0_h = %d, c1_h = %d, c2_h = %d, c3_h = %d\n",
				 phase_out.v1.c0_h, phase_out.v1.c1_h,
				 phase_out.v1.c2_h, phase_out.v1.c3_h);
			dev_info(dev->mt76.dev,
				 "c0_mh = %d, c1_mh = %d, c2_mh = %d, c3_mh = %d\n",
				 phase_out.v1.c0_mh, phase_out.v1.c1_mh,
				 phase_out.v1.c2_mh, phase_out.v1.c3_mh);
			dev_info(dev->mt76.dev,
				 "c0_m = %d, c1_m = %d, c2_m = %d, c3_m = %d\n",
				 phase_out.v1.c0_m, phase_out.v1.c1_m,
				 phase_out.v1.c2_m, phase_out.v1.c3_m);
			dev_info(dev->mt76.dev,
				 "c0_l = %d, c1_l = %d, c2_l = %d, c3_l = %d\n",
				 phase_out.v1.c0_l, phase_out.v1.c1_l,
				 phase_out.v1.c2_l, phase_out.v1.c3_l);
		}

		break;
	}
	default:
		dev_info(dev->mt76.dev, "%s: unknown bf event tag %d\n",
			 __func__, event->tag);
	}

}

int mt7996_mcu_edcca_enable(struct mt7996_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 std;
		u8 _rsv2[2];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_EDCCA_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
		.std = EDCCA_DEFAULT,
	};

	switch (phy->dev->mt76.region) {
	case NL80211_DFS_JP:
		req.std = EDCCA_JAPAN;
		break;
	case NL80211_DFS_FCC:
		req.std = EDCCA_FCC;
		break;
	case NL80211_DFS_ETSI:
		req.std = EDCCA_ETSI_2023;
		break;
	default:
		break;
	}

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7996_mcu_edcca_threshold_ctrl(struct mt7996_phy *phy, u8 *value, bool set)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 threshold[4];
		bool init;
		u8 _rsv2[3];
	} __packed *res, req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_EDCCA_THRESHOLD),
		.len = cpu_to_le16(sizeof(req) - 4),
		.init = false,
	};
	struct sk_buff *skb;
	int ret;
	int i;

	for (i = 0; i < EDCCA_MAX_BW_NUM; i++)
		req.threshold[i] = value[i];

	if (set)
		return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
					 &req, sizeof(req), true);

	ret = mt76_mcu_send_and_get_msg(&phy->dev->mt76,
					MCU_WM_UNI_CMD_QUERY(BAND_CONFIG),
					&req, sizeof(req), true, &skb);

	if (ret)
		return ret;

	res = (void *)skb->data;

	for (i = 0; i < EDCCA_MAX_BW_NUM; i++)
		value[i] = res->threshold[i];

	dev_kfree_skb(skb);

	return 0;
}

int mt7996_mcu_set_qos_map(struct mt7996_dev *dev, struct mt7996_vif_link *mconf,
			   struct cfg80211_qos_map *usr_qos_map)
{
	struct {
		u8 bss_idx;
		u8 qos_map_enable;
		u8 __rsv[2];
		s8 qos_map[IP_DSCP_NUM];
	} __packed req = {
		.bss_idx = mconf->mt76.idx,
		.qos_map_enable = true,
	};
	s8 i;

	/* Default QoS map, defined in section 2.3 of RFC8325.
	 * Three most significant bits of DSCP are used as UP.
	 */
	for (i = 0; i < IP_DSCP_NUM; ++i)
		req.qos_map[i] = i >> 3;

	/* Recommended QoS map, defined in section 4 of RFC8325.
	 * Used in cfg80211_classify8021d since kernel v6.8.
	 */
	req.qos_map[10] = req.qos_map[12] = req.qos_map[14] = req.qos_map[16] = 0;
	req.qos_map[18] = req.qos_map[20] = req.qos_map[22] = 3;
	req.qos_map[24] = 4;
	req.qos_map[40] = 5;
	req.qos_map[44] = req.qos_map[46] = 6;
	req.qos_map[48] = 7;

	/* User-defined QoS map */
	if (usr_qos_map) {
		for (i = 0; i < IEEE80211_NUM_UPS; ++i) {
			u8 low = usr_qos_map->up[i].low;
			u8 high = usr_qos_map->up[i].high;

			if (low < IP_DSCP_NUM && high < IP_DSCP_NUM && low <= high)
				memset(req.qos_map + low, i, high - low + 1);
		}

		for (i = 0; i < usr_qos_map->num_des; ++i) {
			u8 dscp = usr_qos_map->dscp_exception[i].dscp;
			u8 up = usr_qos_map->dscp_exception[i].up;

			if (dscp < IP_DSCP_NUM && up < IEEE80211_NUM_UPS)
				req.qos_map[dscp] = up;
		}
	}

	memcpy(mconf->msta_link.sta->vif->qos_map, req.qos_map, IP_DSCP_NUM);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(SET_QOS_MAP), &req,
				 sizeof(req), false);
}

int mt7996_mcu_set_muru_qos_cfg(struct mt7996_dev *dev, u16 wlan_idx, u8 dir,
				u8 scs_id, u8 req_type, u8 *qos_ie, u8 qos_ie_len)
{
#define QOS_FLAG_UPDATE 20
#define QOS_FLAG_DELETE 21

	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 qos_flag;
		__le16 wlan_idx;
		u8 __rsv2[12];
		u8 dir;
		u8 _rsv3[4];
		u8 scs_id;
		u8 qos_ie[44];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_MURU_SET_QOS_CFG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.wlan_idx = cpu_to_le16(wlan_idx),
		.scs_id = scs_id,
	};

	switch (req_type) {
	case SCS_REQ_TYPE_ADD:
	case SCS_REQ_TYPE_CHANGE:
		req.qos_flag = cpu_to_le32(QOS_FLAG_UPDATE);
		req.dir = dir;

		if (qos_ie_len > sizeof(req.qos_ie))
			return -EINVAL;

		memcpy(req.qos_ie, qos_ie, qos_ie_len);
		break;
	case SCS_REQ_TYPE_REMOVE:
		req.qos_flag = cpu_to_le32(QOS_FLAG_DELETE);
		break;
	default:
		dev_err(dev->mt76.dev, "Unsupported req_type %u\n", req_type);
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), true);
}

int mt7996_mcu_set_sr_pp_en(struct mt7996_dev *dev, u8 enable)
{
#define SR_PP_MODE 5
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		__le32 category;
		__le32 enable;
		u8 _rsv2[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_PP_CHANGE_CAP_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.category = cpu_to_le32(SR_PP_MODE),
		.enable = cpu_to_le32(enable),
	};

	if (is_mt7996(&dev->mt76))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_uba_en(struct mt7996_dev *dev, u8 enable)
{
#define UNI_CMD_RA_OPTION_CTRL 4
#define UBA_TYPE 3
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		u8 option_type;
		u8 val;
		u8 _rsv2[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_RA_OPTION_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.option_type = UBA_TYPE,
		.val = enable,
	};

	if (is_mt7996(&dev->mt76))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RA),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_mru_probe_en(struct mt7996_phy *phy)
{
#define MRU_PROBE_MODE 2
	struct mt7996_dev *dev = phy->dev;
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		u8 mgmt_mode;
		u8 band_idx;
		u8 force_bitmap_ctrl;
		u8 auto_mode;
		__le16 bitmap;
		u8 csa_enable;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_PP_EN_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.auto_mode = MRU_PROBE_MODE,
		.band_idx = phy->mt76->band_idx,
	};

	if (is_mt7996(&dev->mt76))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7996_mcu_set_tpo(struct mt7996_dev *dev, u8 type, u8 val)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		u8 type;
		u8 val;
		u8 __rsv2[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_CMD_TPO_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.type = type,
		.val = val,
	};

#ifdef CONFIG_MTK_DEBUG
	switch (type) {
	case MT7996_LP_TPO_ALL:
		dev->dbg.lp.tpo = val;
		break;
	case MT7996_LP_TPO_PB:
		dev->dbg.lp.pb_tpo = val;
		break;
	case MT7996_LP_TPO_LP:
		dev->dbg.lp.lp_tpo = val;
		break;
	case MT7996_LP_TPO_MIN_TX:
		dev->dbg.lp.min_tx_tpo = val;
		break;
	default:
		break;
	}
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TPO),
				 &req, sizeof(req), true);
}

int mt7996_mcu_set_lp_option(struct mt7996_dev *dev, u8 *arg)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		u8 arg[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_POWER_LOW_POWER),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	memcpy(&req.arg, arg, sizeof(req.arg));

#ifdef CONFIG_MTK_DEBUG
#define ULTRA_SAVE_FEATURE	1
#define ULTRA_SAVE_FEATURE_ALL	2
#define ULTRA_SAVE_FEATURE_DCM	3
#define ULTRA_SAVE_FEATURE_1RPD	4
#define ULTRA_SAVE_FEATURE_MMPS	5
#define ULTRA_SAVE_FEATURE_MDPC	6
	if (arg[0] == ULTRA_SAVE_FEATURE_ALL) {
		dev->dbg.lp.ultra_save = !!arg[1];
		dev->dbg.lp.one_rpd = !!arg[1];
		dev->dbg.lp.mmps = !!arg[1];
		dev->dbg.lp.mdpc = !!arg[1];
		dev->dbg.lp.dcm = !!arg[1];
		dev->dbg.lp.alpl = !!arg[1];
	} else if (arg[0] == ULTRA_SAVE_FEATURE) {
		switch (arg[1]) {
		case ULTRA_SAVE_FEATURE_DCM:
			dev->dbg.lp.dcm = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_1RPD:
			dev->dbg.lp.one_rpd = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_MMPS:
			dev->dbg.lp.mmps = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_MDPC:
			dev->dbg.lp.mdpc = !!arg[2];
			break;
		default:
			break;
		}
	}
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(POWER_CTRL), &req,
				 sizeof(req), false);
}

int mt7996_mcu_set_pst(struct mt7996_dev *dev, u32 band, u32 cmd, u32 val)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		__le32 band;
		__le32 cmd;
		__le32 val;
	} __packed req = {
		.tag = cpu_to_le16(UNI_MURU_PST_LOW_POWER),
		.len = cpu_to_le16(sizeof(req) - 4),

		.band = cpu_to_le32(band),
		.cmd = cpu_to_le32(cmd),
		.val = cpu_to_le32(val),
	};

#ifdef CONFIG_MTK_DEBUG
	if (!cmd) {
		if (band == 0xff) {
			dev->dbg.lp.pst = val ? GENMASK(2, 0) : 0;
		} else if (band < __MT_MAX_BAND) {
			dev->dbg.lp.pst &= ~BIT(band);
			dev->dbg.lp.pst |= val ? BIT(band) : 0;
		}

	}
#endif
	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), false);
}

#ifdef CONFIG_MTK_VENDOR
void mt7996_set_wireless_vif(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	u8 mode, val, band_idx;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;

	mode = FIELD_GET(RATE_CFG_MODE, *((u32 *)data));
	val = FIELD_GET(RATE_CFG_VAL, *((u32 *)data));
	band_idx = FIELD_GET(RATE_CFG_BAND_IDX, *((u32 *)data));

	if (!mt7996_band_valid(mvif->dev, band_idx))
		goto error;

	mphy = mvif->dev->mt76.phys[band_idx];
	if (!mphy)
		goto error;

	phy = (struct mt7996_phy *)mphy->priv;

	switch (mode) {
	case RATE_PARAM_FIXED_OFDMA:
		if (val == 3)
			phy->muru_onoff |= OFDMA_DL;
		else
			phy->muru_onoff |= val;
		break;
	case RATE_PARAM_FIXED_MIMO:
		if (val == 0)
			phy->muru_onoff |= MUMIMO_DL_CERT | MUMIMO_DL;
		else
			phy->muru_onoff |= MUMIMO_UL;
		break;
	case RATE_PARAM_AUTO_MU:
		phy->muru_onoff = val & GENMASK(3, 0);
		break;
	}

	return;
error:
	dev_err(mvif->dev->mt76.dev, "Invalid band_idx to config\n");
	return;
}

void mt7996_set_beacon_vif(struct ieee80211_vif *vif, u8 val)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct ieee80211_hw *hw = mvif->deflink.phy->mt76->hw;

	vif->bss_conf.enable_beacon = val;

	mt7996_mcu_add_beacon(hw, vif, &vif->bss_conf, val);
}

static int mt7996_mcu_set_csi_enable(struct mt7996_phy *phy, u16 tag)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7996_mcu_set_csi_frame_type(struct mt7996_phy *phy, u16 tag, u8 type_idx, u32 type)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 frame_type_idx;
		u8 frame_type;
		u8 rsv2[2];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.frame_type_idx = type_idx,
		.frame_type = type,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7996_mcu_set_csi_chain_filter(struct mt7996_phy *phy, u16 tag, u8 func, u32 value)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 function;
		u8 chain_value;
		u8 rsv2[2];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.function = func,
		.chain_value = value,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7996_mcu_set_csi_sta_filter(struct mt7996_phy *phy, u16 tag, u32 op, u8 *sta_mac)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 operation;
		u8 rsv2[1];
		u8 mac[6];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.operation = op,
	};

	memcpy(req.mac, sta_mac, ETH_ALEN);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7996_mcu_set_csi_active_mode(struct mt7996_phy *phy, u16 tag,
					  u32 interval, u8 frame_idx, u8 subframe_idx, u32 bitmap)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		__le16 interval; /* uint: ms */
		u8 frame_type_idx;
		u8 subframe_type_idx;
		__le32 bitmap; /* sta wcid bitmap */
		u8 rsv2[4];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.interval = cpu_to_le16(interval),
		.frame_type_idx = frame_idx,
		.subframe_type_idx = subframe_idx,
		.bitmap = cpu_to_le32(bitmap),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static
void mt7996_csi_wcid_bitmap_update(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_phy *phy = msta->vif->deflink.phy;
	struct csi_bitmap_info_update *sta_info = (struct csi_bitmap_info_update *)data;
	u16 wcid = 0;

#define CSI_ACTIVE_MODE_ADD 1
#define CSI_ACTIVE_MODE_REMOVE 0

	if (!memcmp(sta_info->addr, sta->addr, ETH_ALEN)) {
		wcid = msta->deflink.wcid.idx;

		/* active mode: only support station with wcid less than 32 */
		if (wcid > 32)
			return;

		if (sta_info->action == CSI_ACTIVE_MODE_ADD)
			phy->csi.active_bitmap |= BIT(wcid);
		else if (sta_info->action == CSI_ACTIVE_MODE_REMOVE)
			phy->csi.active_bitmap &= ~(BIT(wcid));
	}
}

int mt7996_mcu_set_csi(struct mt7996_phy *phy, u8 mode,
			u8 cfg, u8 v1, u32 v2, u8 *mac_addr)
{
	switch (mode) {
	case CSI_CONTROL_MODE_STOP:
		return mt7996_mcu_set_csi_enable(phy, UNI_CMD_CSI_STOP);
	case CSI_CONTROL_MODE_START:
		return mt7996_mcu_set_csi_enable(phy, UNI_CMD_CSI_START);
	case CSI_CONTROL_MODE_SET:
		switch (cfg) {
		case CSI_CONFIG_FRAME_TYPE:
			if (v2 > 255)
				return -EINVAL;

			return mt7996_mcu_set_csi_frame_type(phy,
					UNI_CMD_CSI_SET_FRAME_TYPE, v1, v2);
		case CSI_CONFIG_CHAIN_FILTER:
			if (v2 > 255)
				return -EINVAL;

			return mt7996_mcu_set_csi_chain_filter(phy,
					UNI_CMD_CSI_SET_CHAIN_FILTER, v1, v2);
		case CSI_CONFIG_STA_FILTER:
			if (!is_valid_ether_addr(mac_addr))
				return -EINVAL;

			if (v2 > 255)
				return -EINVAL;

			return mt7996_mcu_set_csi_sta_filter(phy,
					UNI_CMD_CSI_SET_STA_FILTER, v2, mac_addr);
		case CSI_CONFIG_ACTIVE_MODE:
			if (is_valid_ether_addr(mac_addr)) {
				struct csi_bitmap_info_update sta_info;

				if (v2 > 255)
					return -EINVAL;

				memcpy(sta_info.addr, mac_addr, ETH_ALEN);
				sta_info.action = v2;

				ieee80211_iterate_stations_atomic(phy->mt76->hw,
								  mt7996_csi_wcid_bitmap_update, &sta_info);
				return 0;
			} else {
				u8 frame_type = v1 & 0x3;
				u8 frame_subtype = (v1 & 0x3c) >> 2;

					/* active mode: max interval is 3000ms */
					if (v2 > 3000)
						return -EINVAL;

				return mt7996_mcu_set_csi_active_mode(phy, UNI_CMD_CSI_SET_ACTIVE_MODE,
						v2, frame_type, frame_subtype, phy->csi.active_bitmap);
			}
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}
#endif
