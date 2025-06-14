// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include "mt7996.h"
#include "mcu.h"
#include "mac.h"

u32 debug_lvl = MTK_DEBUG_FATAL | MTK_DEBUG_WRN;
module_param(debug_lvl, uint, 0644);
MODULE_PARM_DESC(debug_lvl,
		 "Enable debugging messages\n"
		 "0x00001	tx path\n"
		 "0x00002	tx path verbose\n"
		 "0x00004	fatal/very-important messages\n"
		 "0x00008	warning messages\n"
		 "0x00010	Info about messages to/from firmware\n"
		 "0x00020	Configuration related\n"
		 "0x00040	block-ack and aggregation related\n"
		 "0x00080	verbose rx path\n"
		 "0x00100	Last n messages to MCU when something goes wrong\n"
		 "0x00200       MLD related debugging\n"
		 "0x00400       STA related debugging\n"
		 "0x00800       BSS related debugging\n"
		 "0x01000       DEV related debugging\n"
		 "0x02000       Scan related debugging\n"
		 "0x04000       Channel related debugging\n"
		 "0xffffffff	any/all\n"
	);

#if 0
static void mt7996_testmode_disable_all(struct mt7996_dev *dev)
{
	struct mt7996_phy *phy;
	int i;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		phy = __mt7996_phy(dev, i);
		if (phy)
			mt76_testmode_set_state(phy->mt76, MT76_TM_STATE_OFF);
	}
}
#endif

int mt7996_run(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	int ret;

	phy->sr_enable = true;
	phy->enhanced_sr_enable = true;

	//mt7996_testmode_disable_all(dev);

	phy->sr_enable = true;
	phy->enhanced_sr_enable = true;

	/* needed to re-apply power tables after SER */
	ret = mt7996_mcu_set_txpower_sku(phy);
	if (ret)
		return ret;

	mt7996_mac_enable_nf(dev, phy->mt76->band_idx);

	ret = mt7996_mcu_set_rts_thresh(phy, 0x92b);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_radio_en(phy, true);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH, false);
	if (ret)
		return ret;

	/* set a parking channel */
	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_throttling(phy, MT7996_THERMAL_THROTTLE_MAX);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_protect(phy, true);
	if (ret)
		return ret;

#ifdef CONFIG_MTK_DEBUG
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
						dev->dbg.sku_disable ? 0 : phy->sku_limit_en);

	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
						dev->dbg.sku_disable ? 0 : phy->sku_path_en);
#else
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
						phy->sku_limit_en);
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
						phy->sku_path_en);
#endif
	ret = mt7996_mcu_set_scs(phy, SCS_ENABLE);
	if (ret)
		return ret;

	phy->sr_enable = true;
	phy->enhanced_sr_enable = true;
	phy->thermal_protection_enable = true;

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	ieee80211_queue_delayed_work(dev->mphy.hw, &phy->mt76->mac_work,
				     MT7996_WATCHDOG_TIME);

	if (!phy->counter_reset) {
		mt7996_mac_reset_counters(phy);
		phy->counter_reset = true;
	}

	return 0;
}

static int mt7996_start(struct ieee80211_hw *hw)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	dev->mt76.debug_lvl = debug_lvl;

	flush_work(&dev->init_work);

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_set_hdr_trans(dev, true);
	if (!ret && !is_mt7996(&dev->mt76)) {
		u8 queue = mt76_connac_lmac_mapping(IEEE80211_AC_VI);

		ret = mt7996_mcu_cp_support(dev, queue);
		dev->sr_pp_enable = true;
		dev->uba_enable = true;
	}
	mutex_unlock(&dev->mt76.mutex);

	ieee80211_queue_delayed_work(hw, &dev->scs_work, HZ);

	return ret;
}

static void mt7996_stop_phy(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev;

	if (!phy || !test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	dev = phy->dev;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mutex_lock(&dev->mt76.mutex);

	mt7996_mcu_set_radio_en(phy, false);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	cancel_delayed_work_sync(&dev->scs_work);
}

static bool
mt7996_has_repeater_stations(struct mt7996_phy *phy)
{
	return !!(phy->omac_mask & GENMASK_ULL(REPEATER_BSSID_MAX, REPEATER_BSSID_START));
}

static inline int get_free_idx(u64 mask, u8 start, u8 end)
{
	if (~mask & GENMASK_ULL(end, start))
		return __ffs64(~mask & GENMASK_ULL(end, start)) + 1;
	return 0;
}

static int get_omac_idx(enum nl80211_iftype type, struct mt7996_phy *phy)
{
	int i;
	struct mt7996_dev *dev = phy->dev;
	u64 mask = phy->omac_mask;

	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
		/* prefer hw bssid slot 1-3 */
		i = get_free_idx(mask, HW_BSSID_1, HW_BSSID_3);
		if (i)
			return i - 1;

		if (type != NL80211_IFTYPE_STATION)
			break;

		if (dev->sta_omac_repeater_bssid_enable) {
			i = get_free_idx(mask, REPEATER_BSSID_START, REPEATER_BSSID_MAX);
			if (i)
				return i - 1;
		}

		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		break;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		/* ap uses hw bssid 0 and ext bssid */
		if (dev->sta_omac_repeater_bssid_enable) {
			/* Use HW_BSSID_0 last, as bouncing that link will disrupt repeater
			 * interfaces.
			 */
			i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
			if (i)
				return i - 1;

			/* NOTE: Does MLD need this interface? */
			if (~mask & BIT(HW_BSSID_0))
				return HW_BSSID_0;
		} else {
			if (~mask & BIT(HW_BSSID_0))
				return HW_BSSID_0;

			i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
			if (i)
				return i - 1;
		}

		break;
	default:
		WARN_ON(1);
		break;
	}

	return -1;
}

static int get_own_mld_idx(u64 mask, bool group_mld)
{
	u8 start, end;
	int i;

	if (group_mld) {
		start = 0;
		end = 15;
	} else {
		start = 16;
		end = 63;
	}

	i = get_free_idx(mask, start, end);
	if (i)
		return i - 1;

	/* if 16-63 are fully used, find again from 0-15 */
	if (!group_mld && !i) {
		i = get_free_idx(mask, 0, 15);
		if (i)
			return i - 1;
	}

	return -1;
}

static int get_mld_remap_idx(u64 mask)
{
	u8 start = 0, end = 15;
	int i;

	i = get_free_idx(mask, start, end);
	if (i)
		return i - 1;

	return -1;
}

static void
mt7996_init_bitrate_mask(struct ieee80211_vif *vif, struct mt7996_vif_link *mlink)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlink->bitrate_mask.control); i++) {
		mlink->bitrate_mask.control[i].gi = NL80211_TXRATE_DEFAULT_GI;
		mlink->bitrate_mask.control[i].he_gi = 0xff;
		mlink->bitrate_mask.control[i].he_ltf = 0xff;
		mlink->bitrate_mask.control[i].legacy = GENMASK(31, 0);
		memset(mlink->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].ht_mcs));
		memset(mlink->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].vht_mcs));
		memset(mlink->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].he_mcs));
	}
}

/* Remove the headless link, see add_headless_vif for more details. */
static void
mt7996_remove_headless_vif(struct mt7996_phy *phy)
{
	struct mt76_phy *mphy;
	struct mt7996_dev *dev;
	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *link_conf;
	struct mt7996_vif *mvif;
	struct mt7996_vif_link *link;
	struct mt7996_sta_link *msta_link;
	struct mt76_vif_link *mlink;
	int idx;
	int link_id;

	if (!phy)
		return;

	mphy = phy->mt76;
	dev = phy->dev;
	vif = phy->headless_vif;

	if (!vif)
		return;

	link_conf = &vif->bss_conf;
	mvif = (struct mt7996_vif *)vif->drv_priv;
	link = &mvif->deflink;
	msta_link = &link->msta_link;
	mlink = &link->mt76;

	if (!mlink->wcid)
		goto out;

	idx = msta_link->wcid.idx;
	link_id = msta_link->wcid.link_id;

	dev = phy->dev;

	mt76_dbg(&dev->mt76, MT76_DBG_BSS,
		 "%s: band=%u, bss_idx=%u, link_id=%u, wcid=%u\n",
		 __func__, phy->mt76->band_idx, mlink->idx, link_id, idx);

	cancel_delayed_work(&link->sta_chsw_work);

	mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, NULL,
			   CONN_STATE_DISCONNECT, false);
	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, false);

	mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	rcu_assign_pointer(mvif->mt76.link[link_id], NULL);
	rcu_assign_pointer(mvif->sta.link[link_id], NULL);
	if (link->mbssid_idx != 0 && link->mbssid_idx < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link->mbssid_idx], NULL);
		link->mbssid_idx = 0;
	}

	if (mlink->idx > 63)
		dev->mt76.vif_mask[1] &= ~BIT_ULL(mlink->idx - 64);
	else
		dev->mt76.vif_mask[0] &= ~BIT_ULL(mlink->idx);

	phy->omac_mask &= ~BIT_ULL(mlink->omac_idx);

	mvif->mt76.valid_links &= ~BIT(link_id);
	dev->mld_id_mask &= ~BIT_ULL(link->own_mld_id);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);

out:
	phy->headless_vif = NULL;

	kfree(vif);
}

/* Adds a "headless" VIF for the band. Repeater stations need one HW_BSSID link to be active in
 * order to work. In order to provide the illusion of not needing this master interface to users,
 * we manually set up that link if a repeater station is added. To minimize the impact of this, this
 * code uses mac80211 structs, so that no other API needs to change. Only the fields needed to set
 * up this minimal link configuration are set in those structs, so they should be treated with much
 * care.
 */
static int
mt7996_add_headless_vif(struct mt7996_phy *phy)
{
	struct mt7996_vif_link *link;
	struct mt76_vif_link *mlink;
	struct mt7996_vif *mvif;
	struct mt7996_sta_link *msta_link;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	struct ieee80211_hw *hw = phy->mt76->hw;
	int idx, ret;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_vif *vif = NULL;
	u8 link_id = 0;

	if (phy->headless_vif)
		return 0;

	vif = kzalloc(struct_size(vif, drv_priv, hw->vif_data_size),
		      GFP_KERNEL);
	if (!vif)
		return -ENOMEM;

	mt76_dbg(&dev->mt76, MT76_DBG_BSS, "%s: Adding headless link\n", __func__);

	phy->headless_vif = vif;

	link_conf = &vif->bss_conf;
	link_conf->vif = vif;
	mvif = (struct mt7996_vif *)vif->drv_priv;
	link = &mvif->deflink;
	mlink = &link->mt76;
	msta_link = &link->msta_link;

	/* Only fields that matter are edited below */
	memcpy(&link_conf->addr, &phy->mt76->macaddr, sizeof(link_conf->addr));
	/* Enable local-administered bit, to make this address unique */
	link_conf->addr[0] |= 0x2;
	link_conf->bssid = link_conf->addr;
	link_conf->link_id = link_id;

	vif->type = NL80211_IFTYPE_AP;

	if (rcu_access_pointer(mvif->mt76.link[link_id]))
		return 0;

	if (dev->mt76.vif_mask[0] == 0xffffffffffffffff)
		mlink->idx = __ffs64(~dev->mt76.vif_mask[1]) + 64;
	else
		mlink->idx = __ffs64(~dev->mt76.vif_mask[0]);

	if (mlink->idx >= mt7996_max_interface_num(dev)) {
		ret = -ENOSPC;
		goto error;
	}

	if (phy->omac_mask & HW_BSSID_0) {
		ret = -ENOSPC;
		goto error;
	}
	idx = HW_BSSID_0;

	link->phy = phy;
	mlink->omac_idx = idx;

	mlink->band_idx = band_idx;
	mlink->wmm_idx = 0;
	mlink->wcid = &msta_link->wcid;

	mlink->bss_idx = mlink->idx + 3;
	mlink->bss_idx += 1;

	ret = mt7996_mcu_add_dev_info(phy, NULL /* unused */,
				      link_conf /* needed for MAC addr */,
				      mlink, true);
	if (ret)
		goto error;

	if (mlink->idx > 63)
		dev->mt76.vif_mask[1] |= BIT_ULL(mlink->idx - 64);
	else
		dev->mt76.vif_mask[0] |= BIT_ULL(mlink->idx);

	idx = MT7996_WTBL_RESERVED - mlink->idx;

	INIT_LIST_HEAD(&msta_link->rc_list);
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_conf->link_id;
	msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&msta_link->wcid, band_idx);

	link->bpcc = 0;
	memset(link->tsf_offset, 0, sizeof(link->tsf_offset));
	mlink->mvif = &mvif->mt76;
	mvif->mt76.valid_links |= BIT(link_id);
	mvif->dev = dev;
	INIT_DELAYED_WORK(&link->sta_chsw_work, mt7996_sta_chsw_work);

	dev->mld_id_mask |= BIT_ULL(link->own_mld_id);

	rcu_assign_pointer(msta_link->wcid.def_wcid, &mvif->sta.deflink.wcid);
	msta_link->wcid.link_valid = true;
	msta_link->sta = &mvif->sta;
	msta_link->sta->vif = mvif;
	msta_link->sta = NULL;

	mt7996_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (phy->mt76->chandef.chan->band != NL80211_BAND_2GHZ)
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL + 4;
	else
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL;

	mt7996_init_bitrate_mask(vif, link);

	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, true);

	mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, msta_link,
			   CONN_STATE_PORT_SECURE, true);
	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
	rcu_assign_pointer(mvif->mt76.link[link_id], &link->mt76);
	rcu_assign_pointer(mvif->sta.link[link_id], msta_link);

	if (link_conf->nontransmitted && link_conf->bssid_index != 0 &&
	    link_conf->bssid_index < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link_conf->bssid_index], link);
		link->mbssid_idx = link_conf->bssid_index;
	}

	mt76_dbg(&dev->mt76, MT76_DBG_BSS,
		 "%s: band=%u, bss_idx=%u, link_id=%u, wcid=%u\n",
		 __func__, phy->mt76->band_idx, mlink->idx,
		 link_id, msta_link->wcid.idx);

	return 0;
error:
	mt7996_remove_headless_vif(phy);
	return ret;
}

int mt7996_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf,
			struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	struct mt76_txq *mtxq;
	int idx, ret;
	u8 link_id = link_conf->link_id;

	mt76_dbg(&dev->mt76, MT76_DBG_BSS,
		 "%s:  vif_link_add called, link_id: %d.\n",
		 __func__, link_id);

	if (rcu_access_pointer(mvif->mt76.link[link_id]))
		return 0;

	if (link_conf != &vif->bss_conf) {
		link = kzalloc(sizeof(*link), GFP_KERNEL);
		if (!link)
			return -ENOMEM;
	} else {
		link = &mvif->deflink;
	}

	mlink = &link->mt76;
	msta_link = &link->msta_link;

	if (dev->mt76.vif_mask[0] == 0xffffffffffffffff)
		mlink->idx = __ffs64(~dev->mt76.vif_mask[1]) + 64;
	else
		mlink->idx = __ffs64(~dev->mt76.vif_mask[0]);

	if (mlink->idx >= mt7996_max_interface_num(dev)) {
		ret = -ENOSPC;
		goto error;
	}

	if (phy->omac_mask == 0xFFFFFFFF)
		phy->omac_mask = 0;

	idx = get_omac_idx(vif->type, phy);
	if (idx < 0) {
		ret = -ENOSPC;
		goto error;
	}

	mt76_dbg(&dev->mt76, MT76_DBG_BSS, "%s: Selected OMAC index %d\n",
		 __func__, idx);

	if (dev->sta_omac_repeater_bssid_enable) {
		if (BIT_ULL(idx) & __GENMASK_ULL(REPEATER_BSSID_MAX, REPEATER_BSSID_START)) {
			ret = mt7996_add_headless_vif(phy);
			mt76_dbg(&dev->mt76, MT76_DBG_BSS, "%s: called add_headless_vif, ret %d\n",
				 __func__, ret);
		} else if (idx == HW_BSSID_0) {
			mt76_dbg(&dev->mt76, MT76_DBG_BSS,
				 "%s: omac_idx shared with headless, calling remove_headless_vif\n",
				 __func__);
			mt7996_remove_headless_vif(phy);
		}
	}

	/* Below code seems to be testmode only, re-enable when we bring that patch in */
	///* bss idx & omac idx should be set to band idx for ibf cal */
	//if (dev->mt76.vif_mask[0] & BIT_ULL(band_idx) ||
	//    (dev->mt76.vif_mask[1] & BIT_ULL(band_idx-64)) ||
	//    phy->omac_mask & BIT_ULL(band_idx)) {
	//	ret = -ENOSPC;
	//	goto error;
	//}
	//mlink->idx = band_idx;
	//idx = band_idx;

	link->own_mld_id = get_own_mld_idx(dev->mld_id_mask, false);
	if (link->own_mld_id < 0) {
		ret = -ENOSPC;
		goto error;
	}
	link->phy = phy;
	mlink->omac_idx = idx;

	mlink->band_idx = band_idx;
	mlink->wmm_idx = vif->type == NL80211_IFTYPE_AP ? 0 : 3;
	mlink->wcid = &msta_link->wcid;

	if (dev->sta_omac_repeater_bssid_enable) {
		if (idx >= REPEATER_BSSID_START && band_idx == 0)
			mlink->bss_idx = 0;
		else if (idx >= REPEATER_BSSID_START && band_idx == 1)
			mlink->bss_idx = 1;
		else if (idx >= REPEATER_BSSID_START && band_idx == 2)
			mlink->bss_idx = 2;
		else
			mlink->bss_idx = mlink->idx + 3;

		mlink->bss_idx += 1;
	}

	ret = mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, true);
	if (ret)
		goto error;

	if (mlink->idx > 63)
		dev->mt76.vif_mask[1] |= BIT_ULL(mlink->idx - 64);
	else
		dev->mt76.vif_mask[0] |= BIT_ULL(mlink->idx);

	phy->omac_mask |= BIT_ULL(mlink->omac_idx);

	idx = MT7996_WTBL_RESERVED - mlink->idx;

	INIT_LIST_HEAD(&msta_link->rc_list);
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_conf->link_id;
	msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&msta_link->wcid, band_idx);

	link->bpcc = 0;
	memset(link->tsf_offset, 0, sizeof(link->tsf_offset));
	mlink->mvif = &mvif->mt76;
	mvif->mt76.valid_links |= BIT(link_id);
	INIT_DELAYED_WORK(&link->sta_chsw_work, mt7996_sta_chsw_work);

	dev->mld_id_mask |= BIT_ULL(link->own_mld_id);

	rcu_assign_pointer(msta_link->wcid.def_wcid, &mvif->sta.deflink.wcid);
	msta_link->wcid.link_valid = ieee80211_vif_is_mld(vif);
	msta_link->sta = &mvif->sta;
	msta_link->sta->vif = mvif;

	mt7996_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->txq && hweight16(vif->valid_links) <= 1) {
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		mtxq->wcid = idx;
	}

	if (vif->type != NL80211_IFTYPE_AP &&
	    (!mlink->omac_idx || mlink->omac_idx > 3))
		vif->offload_flags = 0;

	if (phy->mt76->chandef.chan->band != NL80211_BAND_2GHZ)
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL + 4;
	else
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL;

	mt7996_init_bitrate_mask(vif, link);

	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, true);
	/* defer the first STA_REC of BMC entry to BSS_CHANGED_BSSID for STA
	 * interface, since firmware only records BSSID when the entry is new
	 */
	if (vif->type != NL80211_IFTYPE_STATION)
		mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, msta_link,
				   CONN_STATE_PORT_SECURE, true);
	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
	rcu_assign_pointer(mvif->mt76.link[link_id], &link->mt76);
	rcu_assign_pointer(mvif->sta.link[link_id], msta_link);

	if (link_conf->nontransmitted && link_conf->bssid_index != 0 &&
	    link_conf->bssid_index < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link_conf->bssid_index], link);
		link->mbssid_idx = link_conf->bssid_index;
	}

	mt76_dbg(&dev->mt76, MT76_DBG_BSS,
		 "%s: band=%u, bss_idx=%u, link_id=%u, wcid=%u\n",
		 __func__, phy->mt76->band_idx, mlink->idx,
		 link_id, msta_link->wcid.idx);

	return 0;
error:
	mt7996_vif_link_remove(mphy, vif, link_conf, mlink);
	return ret;
}

void mt7996_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link = &link->msta_link;
	struct mt7996_phy *phy = link->phy;
	struct mt7996_dev *dev;
	int idx = msta_link->wcid.idx;
	int link_id = msta_link->wcid.link_id;
	int ret;

	if (!phy || !mlink->wcid)
		goto out;

	dev = phy->dev;

	mt76_dbg(&dev->mt76, MT76_DBG_BSS,
		 "%s: band=%u, bss_idx=%u, link_id=%u, wcid=%u\n",
		 __func__, phy->mt76->band_idx, mlink->idx, link_id, idx);

	cancel_delayed_work(&link->sta_chsw_work);

	mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, NULL,
			   CONN_STATE_DISCONNECT, false);
	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, false);

	mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	rcu_assign_pointer(mvif->mt76.link[link_id], NULL);
	rcu_assign_pointer(mvif->sta.link[link_id], NULL);
	if (link->mbssid_idx != 0 && link->mbssid_idx < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link->mbssid_idx], NULL);
		link->mbssid_idx = 0;
	}

	if (mlink->idx > 63)
		dev->mt76.vif_mask[1] &= ~BIT_ULL(mlink->idx - 64);
	else
		dev->mt76.vif_mask[0] &= ~BIT_ULL(mlink->idx);

	phy->omac_mask &= ~BIT_ULL(mlink->omac_idx);

	mvif->mt76.valid_links &= ~BIT(link_id);
	dev->mld_id_mask &= ~BIT_ULL(link->own_mld_id);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);

	if (dev->sta_omac_repeater_bssid_enable) {
		if (!mt7996_has_repeater_stations(phy)) {
			mt7996_remove_headless_vif(phy);
			mt76_dbg(&dev->mt76, MT76_DBG_STA, "%s: Removing headless VIF\n",
				 __func__);
		} else if (mlink->omac_idx == HW_BSSID_0) {
			ret = mt7996_add_headless_vif(phy);
			mt76_dbg(&dev->mt76, MT76_DBG_STA, "%s: Re-started headless VIF, ret: %d\n",
				 __func__, ret);
		}
	}

out:
	if (link != &mvif->deflink)
		kfree_rcu(link, mt76.rcu_head);
}

static void mt7996_phy_set_rxfilter(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	u32 supported_flags = 0;

	/* Initially reset the filter */
	phy->rxfilter.cr = 0;
	phy->rxfilter.cr1 = 0;

	/* The following HW flags should never be set here:
	 * MT_WF_RFCR_DROP_OTHER_BSS
	 * MT_WF_RFCR_DROP_OTHER_BEACON
	 * MT_WF_RFCR_DROP_FRAME_REPORT
	 * MT_WF_RFCR_DROP_PROBEREQ
	 * MT_WF_RFCR_DROP_MCAST_FILTERED
	 * MT_WF_RFCR_DROP_MCAST
	 * MT_WF_RFCR_DROP_BCAST
	 * MT_WF_RFCR_DROP_DUPLICATE
	 * MT_WF_RFCR_DROP_A2_BSSID
	 * MT_WF_RFCR_DROP_UNWANTED_CTL
	 * MT_WF_RFCR_DROP_STBC_MULTI
	 */

	supported_flags |= FIF_OTHER_BSS;
	if (!(phy->mac80211_rxfilter_flags & FIF_OTHER_BSS))
		phy->rxfilter.cr |= MT_WF_RFCR_DROP_OTHER_TIM |
				    MT_WF_RFCR_DROP_A3_MAC |
				    MT_WF_RFCR_DROP_A3_BSSID;

	supported_flags |= FIF_FCSFAIL;
	if (!(phy->mac80211_rxfilter_flags & FIF_FCSFAIL))
		phy->rxfilter.cr |= MT_WF_RFCR_DROP_FCSFAIL;

	supported_flags |= FIF_CONTROL;
	if (!(phy->mac80211_rxfilter_flags & FIF_CONTROL))
		phy->rxfilter.cr |= MT_WF_RFCR_DROP_CTS |
				    MT_WF_RFCR_DROP_RTS |
				    MT_WF_RFCR_DROP_CTL_RSV;

	if (!phy->monitor_enabled)
		phy->rxfilter.cr |= MT_WF_RFCR_DROP_CTS |
				    MT_WF_RFCR_DROP_RTS |
				    MT_WF_RFCR_DROP_CTL_RSV |
				    MT_WF_RFCR_DROP_FCSFAIL |
				    MT_WF_RFCR_DROP_OTHER_UC;

	if (!((phy->mac80211_rxfilter_flags & FIF_CONTROL) || phy->monitor_enabled))
		phy->rxfilter.cr1 |= MT_WF_RFCR1_DROP_ACK |
				     MT_WF_RFCR1_DROP_BF_POLL |
				     MT_WF_RFCR1_DROP_BA |
				     MT_WF_RFCR1_DROP_CFEND |
				     MT_WF_RFCR1_DROP_CFACK;

	phy->mac80211_rxfilter_flags &= supported_flags;

	mt76_wr(dev, MT_WF_RFCR(phy->mt76->band_idx), phy->rxfilter.cr);
	mt76_wr(dev, MT_WF_RFCR1(phy->mt76->band_idx), phy->rxfilter.cr1);
}

static void mt7996_set_monitor(struct mt7996_phy *phy, bool enabled)
{
	struct mt7996_dev *dev;

	if (!phy)
		return;

	dev = phy->dev;

	if (enabled == phy->monitor_enabled)
		return;

	phy->monitor_enabled = enabled;

	mt76_rmw_field(dev, MT_DMA_DCR0(phy->mt76->band_idx),
		       MT_DMA_DCR0_RXD_G5_EN, enabled || dev->rx_group_5_enable);
	mt7996_phy_set_rxfilter(phy);
	mt7996_mcu_set_sniffer_mode(phy, enabled);
}

static int mt7996_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, err = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (phy && !mvif->deflink.phy && (wdev->radio_mask & BIT(i)))
			mvif->deflink.phy = phy;

		if (!phy)
			continue;

		if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state)) {
			err = mt7996_run(phy);
			if (err)
				goto out;
		}

		if (vif->type == NL80211_IFTYPE_MONITOR)
			mt7996_set_monitor(phy, true);
		else if (dev->rx_group_5_enable)
			mt76_rmw_field(dev, MT_DMA_DCR0(phy->mt76->band_idx),
				       MT_DMA_DCR0_RXD_G5_EN, true);
	}

	vif->offload_flags |= IEEE80211_OFFLOAD_ENCAP_4ADDR;

	/* TODO: This may not be related to the 31 station patch. Investigate further.
	 *       OWRT tree seems to experience beacon loss regularly, and uses Nullfunc frames
	 *       to stay associated. Maybe this is a temporary fix from their tree for that...
	 */
	if (!dev->sta_omac_repeater_bssid_enable)
		INIT_DELAYED_WORK(&mvif->beacon_mon_work, mt7996_beacon_mon_work);

	mvif->dev = dev;
	mvif->sta.vif = mvif;
	/* TODO: temporaily set this to prevent some crashes */
	mvif->deflink.mt76.mvif = &mvif->mt76;
	memset(mvif->mt76.band_to_link, IEEE80211_LINK_UNSPECIFIED,
	       sizeof(mvif->mt76.band_to_link));

	if (vif->type == NL80211_IFTYPE_STATION && mvif->deflink.phy)
		err = mt7996_vif_link_add(mvif->deflink.phy->mt76, vif, &vif->bss_conf, NULL);

out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

struct mt7996_radio_data {
	u32 active_mask;
	u32 monitor_mask;
};

static void mt7996_remove_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_radio_data *rdata = data;

	rdata->active_mask |= wdev->radio_mask;
	if (vif->type == NL80211_IFTYPE_MONITOR)
		rdata->monitor_mask |= wdev->radio_mask;
}

static void mt7996_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct ieee80211_bss_conf *conf;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = &dev->phy;
	struct mt7996_radio_data rdata = {};
	int i;

	ieee80211_iterate_active_interfaces_mtx(hw, 0, mt7996_remove_iter,
						&rdata);

	cancel_delayed_work(&mvif->beacon_mon_work);

	mutex_lock(&dev->mt76.mutex);

	conf = link_conf_dereference_protected(vif, 0);
	mconf = mt7996_vif_link(dev, vif, 0);
	if (!mconf || !conf)
		goto out;

	mt7996_vif_link_remove(mconf->phy->mt76, vif, conf, &mconf->mt76);

out:
	mutex_unlock(&dev->mt76.mutex);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		phy = dev->radio_phy[i];

		if (!phy)
			continue;
		if (!(rdata.monitor_mask & BIT(i)))
			mt7996_set_monitor(phy, false);
		if (!(rdata.active_mask & BIT(i)))
			mt7996_stop_phy(phy);
	}
}

int mt7996_set_channel(struct mt76_phy *mphy)
{
	struct mt7996_phy *phy = mphy->priv;
	int ret = 0;

	if (mphy->chanctx && mphy->chanctx->state == MT76_CHANCTX_STATE_ADD) {
		if (!mt76_testmode_enabled(phy->mt76) /* && !phy->mt76->test.bf_en*/) {
			ret = mt7996_mcu_edcca_enable(phy, true);
			if (ret)
				goto out;
		}

		ret = mt7996_mcu_set_pp_en(phy, PP_USR_MODE,
					   mphy->chanctx->chandef.punctured);
		if (ret)
			goto out;
	} else if (mphy->chanctx && mphy->chanctx->state == MT76_CHANCTX_STATE_SWITCH) {
		if (mphy->chanctx->has_ap && phy->pp_mode == PP_USR_MODE) {
			ret = mt7996_mcu_set_pp_en(phy, PP_USR_MODE,
						   mphy->main_chandef.punctured);
		} else if (mphy->chanctx->has_sta) {
			u8 omac_idx = get_omac_idx(NL80211_IFTYPE_STATION, phy);
			ret = mt7996_mcu_set_pp_sta_dscb(phy, &mphy->main_chandef,
							 omac_idx);
		}
		if (ret)
			goto out;

		ret = mt7996_mcu_set_txpower_sku(phy);
		if (ret)
			goto out;
	}

#if 0
	// TODO:  Re-enable if/when adding dpd code
	if (phy->dev->cal) {
		ret = mt7996_mcu_apply_tx_dpd(phy);
		if (ret)
			goto out;
	}
#endif

#if 0
	// TODO:  Enable if/when we add bf_en testmode support.
	if (mt76_testmode_enabled(phy->mt76) || phy->mt76->test.bf_en) {
		mt7996_tm_update_channel(phy);
		goto out;
	}
#endif

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);
	if (ret)
		goto out;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH, false);
	if (ret)
		goto out;

	ret = mt7996_mcu_set_txpower_sku(phy);
	if (ret) {
		mtk_dbg(mphy->dev, CFG, "mcu-parse-response, firmware returned failure code: 0x%x.\n",
			ret);
		goto out;
	}

	ret = mt7996_dfs_init_radar_detector(phy);
	mt7996_mac_cca_stats_reset(phy);

	mt7996_mac_reset_counters(phy);
	phy->noise = 0;

out:
	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7996_WATCHDOG_TIME);

	return ret;
}

static int mt7996_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = sta ? (struct mt7996_sta *)sta->drv_priv :
				  &mvif->sta;
	struct mt7996_vif_link *mconf;
	struct mt7996_sta_link *msta_link;
	struct ieee80211_bss_conf *conf;
	u8 *wcid_keyidx;
	int idx = key->keyidx;
	int err = 0;
	unsigned long add;
	unsigned int link_id;
	u8 pn[6] = {};
	bool is_bigtk = idx == 6 || idx == 7;

	if (cmd != SET_KEY && cmd != DISABLE_KEY)
		return -EINVAL;

	if (key->link_id >= 0) {
		add = BIT(key->link_id);
	} else {
		if (sta)
			add = sta->valid_links ?: BIT(0);
		else
			add = vif->valid_links ?: BIT(0);
	}

	if (sta)
		mt76_dbg(&dev->mt76, MT76_DBG_STA,
			 "%s: keyidx=%d, link_bitmap=0x%lx (STA %pM)\n",
			 __func__, key->keyidx, add, sta->addr);
	else
		mt76_dbg(&dev->mt76, MT76_DBG_BSS,
			 "%s: keyidx=%d, link_bitmap=0x%lx\n",
			 __func__, key->keyidx, add);

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		conf = link_conf_dereference_protected(vif, link_id);
		mconf = mt7996_vif_link(dev, vif, link_id);
		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);

		if ((cmd == SET_KEY && !conf) || !mconf || !msta_link)
			continue;

		wcid_keyidx = &msta_link->wcid.hw_key_idx;

		/* fall back to sw encryption for unsupported ciphers */
		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_TKIP:
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
		case WLAN_CIPHER_SUITE_SMS4:
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
		case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		case WLAN_CIPHER_SUITE_BIP_GMAC_256:
			if (is_bigtk) {
				wcid_keyidx = &msta_link->wcid.hw_key_idx2;
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
				err = mt7996_mcu_get_pn(dev, mconf, msta_link, pn);
				if (err)
					goto out;
				break;
			}
			fallthrough;
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		default:
			mutex_unlock(&dev->mt76.mutex);
			return -EOPNOTSUPP;
		}

		/* Necessary for fw cipher check */
		if (cmd == SET_KEY && !sta && !mconf->mt76.cipher) {
			mconf->mt76.cipher = mt76_connac_mcu_get_cipher(key->cipher);
			mt7996_mcu_add_bss_info(mconf->phy, vif, conf, &mconf->mt76, msta_link, true);
		}

		if (cmd == SET_KEY) {
			*wcid_keyidx = idx;
		} else if (idx == *wcid_keyidx) {
			*wcid_keyidx = -1;
		}

		/* To remove BIGTK independently, FW needs an extra inband command */
		if (cmd == DISABLE_KEY && !is_bigtk)
			goto out;

		mt76_wcid_key_setup(&dev->mt76, &msta_link->wcid, key);

		err = mt7996_mcu_add_key(&dev->mt76, mconf, key,
					 MCU_WMWA_UNI_CMD(STA_REC_UPDATE),
					 &msta_link->wcid, cmd, pn);

		if (cmd == SET_KEY && is_bigtk && conf && conf->enable_beacon) {
			/* Remove beacon first to update beacon Txd for beacon protection */
			mt7996_mcu_add_beacon(hw, vif, conf, false);
			mt7996_mcu_add_beacon(hw, vif, conf, true);
		}
	}
out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7996_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

static int
mt7996_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *mlink;
	static const u8 mq_to_aci[] = {
		[IEEE80211_AC_VO] = 3,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
	};

	mutex_lock(&dev->mt76.mutex);
	mlink = mt7996_vif_link(dev, vif, link_id);
	if (!mlink) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	/* firmware uses access class index */
	mlink->queue_params[mq_to_aci[queue]] = *params;
	/* no need to update right away, we'll get BSS_CHANGED_QOS */

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7996_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	u32 flags = 0;

	mutex_lock(&dev->mt76.mutex);

	mt7996_for_each_phy(dev, phy) {
		phy->mac80211_rxfilter_flags = *total_flags;
		mt7996_phy_set_rxfilter(phy);
		flags |= phy->mac80211_rxfilter_flags;
	}

	*total_flags = flags;

	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   unsigned int link_id, int *dbm)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mt7996_vif_link_phy(&mvif->deflink);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct wireless_dev *wdev;
	int n_chains, delta, i;

	if (!phy) {
		wdev = ieee80211_vif_to_wdev(vif);
		for (i = 0; i < hw->wiphy->n_radio; i++)
			if (wdev->radio_mask & BIT(i))
				phy = dev->radio_phy[i];

		if (!phy)
			return -EINVAL;
	}

	n_chains = hweight16(phy->mt76->chainmask);
	delta = mt76_tx_power_path_delta(n_chains);
	*dbm = DIV_ROUND_UP(phy->mt76->txpower_cur + delta, 2);

	return 0;
}

static u8
mt7996_get_rates_table(struct mt7996_phy *phy, struct ieee80211_bss_conf *conf,
		       bool beacon, bool mcast)
{
	#define FR_RATE_IDX_OFDM_6M 0x004b
	struct mt7996_dev *dev = phy->dev;
	struct mt76_vif_link *mvif = mt76_vif_conf_link(&dev->mt76, conf->vif, conf);
	struct mt76_phy *mphy = mt76_vif_link_phy(mvif);
	u16 rate;
	u8 i, idx;

	rate = mt76_connac2_mac_tx_rate_val(phy->mt76, conf, beacon, mcast);

	if (beacon) {

		if (mphy->dev->lpi_bcn_enhance)
			rate = FR_RATE_IDX_OFDM_6M;

		/* odd index for driver, even index for firmware */
		idx = MT7996_BEACON_RATES_TBL + 2 * phy->mt76->band_idx;
		if (phy->beacon_rate != rate)
			mt7996_mcu_set_fixed_rate_table(phy, idx, rate, beacon);

		return idx;
	}

	idx = FIELD_GET(MT_TX_RATE_IDX, rate);
	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++)
		if ((mt76_rates[i].hw_value & GENMASK(7, 0)) == idx)
			return MT7996_BASIC_RATES_TBL + 2 * i;

	return mvif->basic_rates_idx;
}

static void
mt7996_update_mu_group(struct ieee80211_hw *hw, struct mt7996_vif_link *link,
		       struct ieee80211_bss_conf *info)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	u8 band = link->mt76.band_idx;
	u32 *mu;

	mu = (u32 *)info->mu_group.membership;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD1(band), mu[1]);

	mu = (u32 *)info->mu_group.position;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS1(band), mu[1]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS2(band), mu[2]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS3(band), mu[3]);
}

static void
mt7996_vif_cfg_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);

	if ((changed & BSS_CHANGED_ASSOC) && vif->cfg.assoc) {
		struct ieee80211_bss_conf *link_conf;
		unsigned long link_id;

		for_each_vif_active_link(vif, link_conf, link_id) {
			struct mt7996_vif_link *link;

			link = mt7996_vif_link(dev, vif, link_id);
			if (!link)
				continue;

			if (!link->phy)
				continue;

			mt7996_mcu_add_bss_info(link->phy, vif, link_conf,
						&link->mt76, &link->msta_link,
						true);
			mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, NULL,
					   CONN_STATE_PORT_SECURE,
					   !!(changed & BSS_CHANGED_BSSID));
		}
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_get_tsf_offset(struct ieee80211_vif *vif,
				  struct mt7996_phy *phy,
				  struct mt7996_dev *dev)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *rpted_mconf;
	unsigned long valid_links = vif->valid_links;
	unsigned int rpting_linkid, rpted_linkid;

	for_each_set_bit(rpted_linkid, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {

		rpted_mconf = mt7996_vif_link(dev, vif, rpted_linkid);
		if (!rpted_mconf)
			return;

		for_each_set_bit(rpting_linkid, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {

			if (rpted_linkid == rpting_linkid)
				continue;
			mt7996_mcu_get_tsf_offset(phy, mvif, rpting_linkid, rpted_linkid);
		}

		ieee80211_tsf_offset_notify(vif, rpted_linkid, rpted_mconf->tsf_offset,
					    sizeof(rpted_mconf->tsf_offset), GFP_KERNEL);
	}
}

static void
mt7996_link_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *info, u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_conf_link(dev, vif, info);
	if (!link)
		goto out;

	mphy = mt76_vif_link_phy(&link->mt76);
	if (!mphy)
		goto out;

	phy = mphy->priv;

	/* station mode uses BSSID to map the wlan entry to a peer,
	 * and then peer references bss_info_rfch to set bandwidth cap.
	 */
	if ((changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid)) ||
	    (changed & BSS_CHANGED_BEACON_ENABLED)) {
		mt7996_mcu_add_bss_info(phy, vif, info, &link->mt76,
					&link->msta_link, true);
		mt7996_mcu_add_sta(dev, vif, info, NULL, link, &link->msta_link,
				   CONN_STATE_PORT_SECURE,
				   !!(changed & BSS_CHANGED_BSSID));
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7996_mcu_set_timing(phy, vif, info);
		}
	}

	if (changed & BSS_CHANGED_MCAST_RATE)
		link->mt76.mcast_rates_idx =
			mt7996_get_rates_table(phy, info, false, true);

	if (changed & BSS_CHANGED_BASIC_RATES)
		link->mt76.basic_rates_idx =
			mt7996_get_rates_table(phy, info, false, false);

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7996_mcu_set_tx(dev, vif, info);

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		mt7996_mcu_add_obss_spr(phy, link, &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR) {
		if ((vif->type == NL80211_IFTYPE_AP &&
		    link->mt76.omac_idx <= HW_BSSID_MAX) ||
		   vif->type == NL80211_IFTYPE_STATION)
			mt7996_mcu_update_bss_color(dev, &link->mt76,
						    &info->he_bss_color);
	}

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED)) {
		link->mt76.beacon_rates_idx =
			mt7996_get_rates_table(phy, info, true, false);

		/* The CSA beacon will be set in channel_switch_beacon,
		 * but beacon can be disabled during CSA for DFS channel.
		 */
		if (!info->enable_beacon || !info->csa_active)
			mt7996_mcu_add_beacon(hw, vif, info, info->enable_beacon);

		if (!info->enable_beacon && hweight16(vif->valid_links) > 1)
			mt7996_get_tsf_offset(vif, phy, dev);
	}

	if (changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
		       BSS_CHANGED_FILS_DISCOVERY))
		mt7996_mcu_beacon_inband_discov(dev, info, link, changed);

	if (changed & BSS_CHANGED_MU_GROUPS)
		mt7996_update_mu_group(hw, link, info);

	if (changed & BSS_CHANGED_TXPOWER &&
	    info->txpower != phy->txpower) {
		phy->txpower = info->txpower;
		mt7996_mcu_set_txpower_sku(phy);
	}

out:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mt7996_band_phy(dev, chandef->chan->band);
	struct ieee80211_bss_conf *conf;
	struct mt7996_vif_link *mconf;
	u16 valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);
	link_id = mvif->mt76.band_to_link[phy->mt76->band_idx];
	if (link_id == IEEE80211_LINK_UNSPECIFIED)
		goto out;

	if (!mvif->cs_ready_links)
		mvif->cs_link_id = link_id;

	mvif->cs_ready_links |= BIT(link_id);
	if (mvif->cs_ready_links != valid_links)
		goto out;

	link_id = mvif->cs_link_id;
	do {
		valid_links &= ~BIT(link_id);
		mconf = mt7996_vif_link(dev, vif, link_id);
		conf = link_conf_dereference_protected(vif, link_id);
		if (!conf || !mconf)
			goto fail;

		/* Reset the beacon when switching channels during CAC */
		if (link_id == mvif->cs_link_id &&
		    !cfg80211_reg_can_beacon(hw->wiphy, &phy->mt76->chandef, vif->type))
			mt7996_mcu_add_beacon(hw, vif, conf, false);

		mt7996_mcu_add_beacon(hw, vif, conf, true);
		link_id = ffs(valid_links) - 1;
	} while (valid_links);

out:
	mutex_unlock(&dev->mt76.mutex);
	return;
fail:
	mvif->cs_ready_links = 0;
	mvif->cs_link_id = IEEE80211_LINK_UNSPECIFIED;
	dev_err(dev->mt76.dev, "link %d: failed to switch beacon\n", link_id);
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_post_channel_switch(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_bss_conf *link_conf)
{
	struct cfg80211_chan_def *chandef = &link_conf->chanreq.oper;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = mt7996_band_phy(dev, chandef->chan->band);
	int ret;

	ret = cfg80211_chandef_dfs_required(hw->wiphy, chandef, NL80211_IFTYPE_AP);
	if (ret <= 0)
		return ret;

	return mt76_set_channel(phy->mt76, chandef, false);
}

static int
mt7996_mac_sta_init_link(struct mt7996_dev *dev,
			 struct ieee80211_bss_conf *link_conf,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7996_vif_link *link, bool assoc)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct mt7996_vif *mvif = (struct mt7996_vif *)link_conf->vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	u8 link_id = link_sta->link_id;
	struct mt7996_sta_link *msta_link = NULL;
	int idx, ret, i;

	if (!rcu_access_pointer(msta->link[link_id])) {
		idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7996_WTBL_STA);
		if (idx < 0)
			return -ENOSPC;

		if (sta->mlo) {
			msta_link = kzalloc(sizeof(*msta_link), GFP_KERNEL);
			if (!msta_link)
				return -ENOMEM;
		} else {
			msta_link = &msta->deflink;
		}

		INIT_LIST_HEAD(&msta_link->rc_list);
		msta->vif = mvif;
		msta->valid_links |= BIT(link_id);
		msta_link->wcid.sta = 1;
		msta_link->wcid.idx = idx;
		rcu_assign_pointer(msta_link->wcid.def_wcid, &msta->deflink.wcid);
		msta_link->sta = msta;
		if (sta->valid_links) {
			msta_link->wcid.link_valid = true;
			msta_link->wcid.link_id = link_id;
			if (sta->valid_links & ~(BIT(msta->deflink_id)))
				msta->sec_link = __ffs(sta->valid_links &
						       ~(BIT(msta->deflink_id)));
			else
				msta->sec_link = msta->deflink_id;
		}

		rcu_assign_pointer(msta->link[link_id], msta_link);

		ewma_signal_init(&msta_link->wcid.rssi);
		rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
		mt76_wcid_init(&msta_link->wcid, link->phy->mt76->band_idx);

		mt76_dbg(&dev->mt76, MT76_DBG_STA,
			 "%s: STA %pM, wcid=%u, link_id=%u (%pM), pri_link=%u, sec_link=%u\n",
			 __func__, sta->addr, msta_link->wcid.idx, link_id,
			 link_sta->addr, msta->deflink_id, msta->sec_link);
	}

	if (!assoc)
		return 0;

	if (!msta_link)
		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	mt7996_mac_wtbl_update(dev, msta_link->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	ret = mt7996_mcu_add_sta(dev, link_conf->vif, link_conf, link_sta, link, msta_link,
				 CONN_STATE_CONNECT, true);
	if (ret)
		goto error;

	ret = mt7996_mcu_add_rate_ctrl(dev, msta_link->sta, link_conf->vif, link_id, false);
	if (ret)
		goto error;

	if (link_sta->eht_cap.has_eht && link_conf->vif->type == NL80211_IFTYPE_STATION) {
		ret = mt7996_mcu_set_pp_sta_dscb(link->phy, &link_conf->chanreq.oper, link->mt76.omac_idx);
		if (ret)
			goto error;
	}

	for (i = 0; i < IEEE80211_MAX_CHAINS; ++i)
		ewma_avg_signal_init(msta_link->chain_signal_avg + i);
	ewma_avg_signal_init(&msta_link->signal_avg);
	ewma_avg_signal_init(&msta_link->avg_ack_signal);

#ifdef CONFIG_MTK_VENDOR
	mt7996_vendor_amnt_sta_remove(link->phy, sta);
#endif

error:
	return ret;
}

static void
mt7996_mac_sta_deinit_link(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_bss_conf *conf,
			   struct mt7996_vif_link *mconf,
			   struct ieee80211_sta *sta,
			   struct ieee80211_link_sta *link_sta,
			   struct mt7996_sta_link *msta_link,
			   bool last_link)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	int i;

	if (!msta_link)
		return;

	for (i = 0; i < ARRAY_SIZE(msta_link->wcid.aggr); i++)
			mt76_rx_aggr_stop(&dev->mt76, &msta_link->wcid, i);

	if (!sta->mlo)
		mt7996_mcu_add_sta(dev, vif, conf, link_sta, mconf, msta_link,
				   CONN_STATE_DISCONNECT, false);
	else if (sta->mlo && last_link)
		mt7996_mcu_teardown_mld_sta(dev, mconf, msta_link);

	mt7996_mac_wtbl_update(dev, msta_link->wcid.idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	for (i = 0; i < ARRAY_SIZE(msta_link->twt.flow); i++)
		mt7996_mac_twt_teardown_flow(dev, mconf, msta_link, i);

	rcu_assign_pointer(msta_link->sta->link[msta_link->wcid.link_id], NULL);
	msta_link->sta->valid_links &= ~BIT(msta_link->wcid.link_id);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	if (!list_empty(&msta_link->rc_list))
		list_del_init(&msta_link->rc_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);
	mt76_wcid_mask_clear(dev->mt76.wcid_mask, msta_link->wcid.idx);

	if (msta_link != &msta->deflink)
		kfree_rcu(msta_link, rcu_head);
}

static void
mt7996_mac_sta_remove_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, unsigned long rem)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned int link_id;

	mt76_dbg(&dev->mt76, MT76_DBG_STA, "%s: removed_links=0x%lx\n", __func__, rem);
	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf =
			mt7996_vif_link(dev, vif, link_id);
		struct mt7996_sta_link *msta_link =
			mt76_dereference(msta->link[link_id], &dev->mt76);
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_protected(sta, link_id);
		struct mt7996_phy *phy;

		if (!mconf || !msta_link)
			continue;

		phy = mconf->phy;
		mt7996_mac_sta_deinit_link(dev, vif, conf, mconf, sta, link_sta,
					   msta_link, msta->valid_links == BIT(link_id));
		if (phy->mt76->num_sta > 0)
			phy->mt76->num_sta--;
	}
}

static int
mt7996_mac_sta_add_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, unsigned long add,
			 bool assoc)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	unsigned int link_id;
	int i, ret;

	mt76_dbg(&dev->mt76, MT76_DBG_STA,
		 "%s: add_links=0x%lx, assoc=%d\n", __func__, add, assoc);
	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf =
			mt7996_vif_link(dev, vif, link_id);
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_protected(sta, link_id);

		if (!mconf || !conf || !link_sta) {
			mt76_dbg(&dev->mt76, MT76_DBG_STA,
				 "%s: WARNING: STA %pM link_id: %d could not find one of: mconf: %p conf: %p link_sta: %p.\n",
				 __func__, sta->addr, link_id, mconf, conf, link_sta);
			continue;
		}

		ret = mt7996_mac_sta_init_link(dev, conf, link_sta, mconf, assoc);
		if (ret)
			goto error;

		if (assoc)
			mconf->phy->mt76->num_sta++;
	}

	if (!assoc)
		return 0;

	msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct mt76_txq *mtxq;

		if (!sta->txq[i])
			continue;
		mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
		mtxq->wcid = msta_link->wcid.idx;
	}

	return 0;
error:
	mt7996_mac_sta_remove_links(dev, vif, sta, add);
	return ret;
}

static int
mt7996_mac_sta_change_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 old_links,
			    u16 new_links)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long add = new_links & ~old_links;
	unsigned long rem = old_links & ~new_links;
	int ret = 0, link_id;
	bool assoc;

	mt76_dbg(&dev->mt76, MT76_DBG_STA, "%s: STA %pM old=0x%x, new=0x%x\n",
		 __func__, sta->addr, old_links, new_links);
	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link =
			mt76_dereference(msta->link[link_id], &dev->mt76);
		struct mt7996_sta_link *msta_link_new;
		int i;

		if (!msta_link)
			continue;

		msta_link->wcid.sta_disabled = 1;

		if (link_id != msta->deflink_id)
			continue;

		msta->deflink_id = msta->sec_link;
		msta_link_new = msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
		msta_link_new->wcid.ampdu_state = msta_link->wcid.ampdu_state;

		for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
			struct mt76_txq *mtxq;
			if (!sta->txq[i])
				continue;
			mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
			mtxq->wcid = msta_link_new->wcid.idx;
		}
	}

	assoc = vif->type == NL80211_IFTYPE_STATION ? vif->cfg.assoc : false;
	ret = mt7996_mac_sta_add_links(dev, vif, sta, add, assoc);
	if (ret)
		goto remove;

	goto out;
remove:
	mt7996_mac_sta_remove_links(dev, vif, sta, add);
out:
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static int
mt7996_mac_sta_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct mt76_dev *mdev = mphy->dev;
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	u8 link_id = sta->valid_links ? __ffs(sta->valid_links) : 0;
	unsigned long add = BIT(link_id);
	int ret;

	mutex_lock(&mdev->mutex);

	msta->deflink_id = link_id;
	ret = mt7996_mac_sta_add_links(dev, vif, sta, add, false);

	mutex_unlock(&mdev->mutex);

	return ret;
}

static void mt7996_check_mld_sta(void *data, struct ieee80211_sta *sta)
{
	bool *has_mld_sta = data;

	if (*has_mld_sta)
		return;
	if (sta->mlo)
		*has_mld_sta = true;
}

static int
mt7996_mac_sta_event(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long add = sta->valid_links ?: BIT(0);
	int ret = 0, link_id;
	bool has_mld_sta = false;

#ifdef CONFIG_MTK_VENDOR
	struct mt7996_vif_link *mconf;
#endif

	mutex_lock(&dev->mt76.mutex);

	switch (ev) {
	case MT76_STA_EVENT_ASSOC:
		ret = mt7996_mac_sta_add_links(dev, vif, sta, add, true);
		if (!ret && sta->mlo)
			mt7996_set_pse_drop(dev, 0);

#ifdef CONFIG_MTK_VENDOR
		link_id = sta->valid_links ? __ffs(sta->valid_links) : 0;
		mconf = mt7996_vif_link(dev, vif, link_id);
		if (mconf && mconf->phy && mconf->phy->muru_onoff & MUMIMO_DL_CERT)
			mt7996_mcu_set_mimo(mconf->phy);
#endif

		break;
	case MT76_STA_EVENT_AUTHORIZE:
		for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
			struct mt7996_vif_link *mconf =
				mt7996_vif_link(dev, vif, link_id);
			struct mt7996_sta_link *msta_link =
				mt76_dereference(msta->link[link_id], &dev->mt76);
			struct ieee80211_bss_conf *conf =
				link_conf_dereference_protected(vif, link_id);
			struct ieee80211_link_sta *link_sta =
				link_sta_dereference_protected(sta, link_id);

			if (!mconf || !msta_link || !conf || !link_sta)
				continue;

			ret = mt7996_mcu_add_sta(dev, vif, conf, link_sta, mconf, msta_link,
						 CONN_STATE_PORT_SECURE, false);
			if (ret)
				goto out;

			msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
		}
		break;
	case MT76_STA_EVENT_DISASSOC:
		ieee80211_iterate_stations_atomic(dev->mt76.hw, mt7996_check_mld_sta,
						  &has_mld_sta);
		if (!has_mld_sta)
			mt7996_set_pse_drop(dev, 1);
		break;
	default:
		break;
	}

out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_mac_sta_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct mt76_dev *mdev = mphy->dev;
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long rem = sta->valid_links ?: BIT(0);

	cancel_delayed_work(&msta->vif->beacon_mon_work);
	mutex_lock(&mdev->mutex);

#ifdef CONFIG_MTK_VENDOR
	mt7996_mcu_set_csi(&dev->phy, 2, 8, 1, 0, sta->addr);
#endif
	mt7996_mac_sta_remove_links(dev, vif, sta, rem | msta->valid_links);

	mutex_unlock(&mdev->mutex);
}

static int
mt7996_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, enum ieee80211_sta_state old_state,
		 enum ieee80211_sta_state new_state)
{
	struct mt76_phy *mphy = mt76_vif_phy(hw, vif);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	enum mt76_sta_event ev;

	if (!mphy)
		mphy = hw->priv;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE)
		return mt7996_mac_sta_add(mphy, vif, sta);

	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST)
		mt7996_mac_sta_remove(mphy, vif, sta);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC)
		ev = MT76_STA_EVENT_ASSOC;
	else if (old_state == IEEE80211_STA_ASSOC &&
		 new_state == IEEE80211_STA_AUTHORIZED)
		ev = MT76_STA_EVENT_AUTHORIZE;
	else if (old_state == IEEE80211_STA_ASSOC &&
		 new_state == IEEE80211_STA_AUTH)
		ev = MT76_STA_EVENT_DISASSOC;
	else
		return 0;

	return mt7996_mac_sta_event(dev, vif, sta, ev);
}

static void
mt7996_sta_pre_rcu_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long rem = msta->valid_links ?: BIT(0);
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);
	spin_lock_bh(&dev->mt76.status_lock);
	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link =
			mt76_dereference(msta->link[link_id], &dev->mt76);

		if (!msta_link)
			continue;
		rcu_assign_pointer(dev->mt76.wcid[msta_link->wcid.idx], NULL);
	}
	spin_unlock_bh(&dev->mt76.status_lock);
	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt76_phy *mphy;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid;
	struct mt7996_vif *mvif;
	struct mt7996_sta *msta;

	if (control->sta) {
		msta = (struct mt7996_sta *)control->sta->drv_priv;
		mvif = msta->vif;
	} else if (vif) {
		mvif = (struct mt7996_vif *)vif->drv_priv;
		msta = &mvif->sta;
	}

	rcu_read_lock();
	if (mvif && msta) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		struct mt7996_vif_link *mconf;
		struct mt7996_sta_link *msta_link;
		u8 link_id = u32_get_bits(info->control.flags,
					  IEEE80211_TX_CTRL_MLO_LINK);
		struct ieee80211_sta *sta = ieee80211_find_sta(vif, hdr->addr1);

		if (link_id >= IEEE80211_LINK_UNSPECIFIED) {
			if (sta) {
				struct mt7996_sta *peer;

				peer = (struct mt7996_sta *)sta->drv_priv;
				link_id = peer->deflink_id;
			} else {
				link_id = mvif->mt76.deflink_id;
			}
		}

		/* translate mld addr to link addr */
		if (ieee80211_vif_is_mld(vif)) {
			struct ieee80211_bss_conf *conf;
			if (sta) {
				struct ieee80211_link_sta *link_sta =
					rcu_dereference(sta->link[link_id]);

				if (!link_sta) {
					mtk_dbg(&dev->mt76, TX,
						"%s, request TX on invalid link_id=%u, use primary link (id=%u) instead.\n",
						__func__, link_id, msta->deflink_id);
					link_id = msta->deflink_id;
					link_sta = rcu_dereference(sta->link[link_id]);

					if (!link_sta) {
						mtk_dbg(&dev->mt76, TX,
							"%s, primary link became invalid, give up the TX\n",
							__func__);
						goto unlock;
					}
				}

				memcpy(hdr->addr1, link_sta->addr, ETH_ALEN);
				if (ether_addr_equal(sta->addr, hdr->addr3))
					memcpy(hdr->addr3, link_sta->addr, ETH_ALEN);
			}

			conf = rcu_dereference(vif->link_conf[link_id]);
			if (unlikely(!conf))
				goto unlock;

			memcpy(hdr->addr2, conf->addr, ETH_ALEN);
			if (ether_addr_equal(vif->addr, hdr->addr3))
				memcpy(hdr->addr3, conf->addr, ETH_ALEN);
		}

		mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
		msta_link = rcu_dereference(msta->link[link_id]);

		if (!mconf || !msta_link)
			goto unlock;

		mphy = mconf->phy->mt76;
		wcid = &msta_link->wcid;
	} else {
		mphy = hw->priv;
		wcid = &dev->mt76.global_wcid;
	}

	mtk_dbg(&dev->mt76, TXV, "mt7996-tx, wcid: %p wcid->idx: %d skb: %p, call mt76_tx\n",
		wcid, wcid->idx, skb);

	mt76_tx(mphy, control->sta, wcid, skb);
unlock:
	rcu_read_unlock();
}

static int mt7996_set_rts_threshold(struct ieee80211_hw *hw, s8 radio_id, u32 val)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, ret = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		ret = mt7996_mcu_set_rts_thresh(phy, val);
		if (ret)
			break;
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	struct ieee80211_link_sta *link_sta;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	unsigned int link_id;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		switch (action) {
		case IEEE80211_AMPDU_RX_START:
			mt76_rx_aggr_start(&dev->mt76, &msta_link->wcid, tid,
					   ssn, params->buf_size);
			ret = mt7996_mcu_add_rx_ba(dev, params, link,
						   msta_link, true);
			mtk_dbg(&dev->mt76, BA, "ampdu-action, RX_START, tid: %d ssn: %d ret: %d\n",
				tid, ssn, ret);
			break;
		case IEEE80211_AMPDU_RX_STOP:
			mt76_rx_aggr_stop(&dev->mt76, &msta_link->wcid, tid);
			ret = mt7996_mcu_add_rx_ba(dev, params, link,
						   msta_link, false);
			mtk_dbg(&dev->mt76, BA, "ampdu-action, RX_STOP, tid: %d ssn: %d ret: %d\n",
				tid, ssn, ret);
			break;
		case IEEE80211_AMPDU_TX_OPERATIONAL:
			mtxq->aggr = true;
			mtxq->send_bar = false;
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, true);
			if (ret)
				dev_err(dev->mt76.dev, "TX AGG operation failed\n");
			else
				mtk_dbg(&dev->mt76, BA, "ampdu-action, TX_OPERATIONAL, tid: %d ssn: %d ret: %d\n",
					tid, ssn, ret);
			break;
		case IEEE80211_AMPDU_TX_STOP_FLUSH:
		case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
			mtxq->aggr = false;
			clear_bit(tid, &msta_link->wcid.ampdu_state);
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, false);
			mtk_dbg(&dev->mt76, BA, "ampdu-action, TX_AMPDU_STOP_FLUSH(%d), tid: %d ssn: %d ret: %d\n",
				action, tid, ssn, ret);
			break;
		case IEEE80211_AMPDU_TX_START:
			set_bit(tid, &msta_link->wcid.ampdu_state);
			ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
			mtk_dbg(&dev->mt76, BA, "ampdu-action, TX_START, tid: %d ssn: %d ret: %d\n",
				tid, ssn, ret);
			break;
		case IEEE80211_AMPDU_TX_STOP_CONT:
			mtxq->aggr = false;
			clear_bit(tid, &msta_link->wcid.ampdu_state);
			ret = mt7996_mcu_add_tx_ba(dev, params, link,
						   msta_link, false);
			mtk_dbg(&dev->mt76, BA, "ampdu-action, AMPDU_TX_STOP_CONT, tid: %d ssn: %d ret: %d\n",
				tid, ssn, ret);
			break;
		}

		if (ret && ret != IEEE80211_AMPDU_TX_START_IMMEDIATE)
			break;
	}

	if (action == IEEE80211_AMPDU_TX_STOP_CONT)
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i;

	mutex_lock(&dev->mt76.mutex);

	memset(stats, 0, sizeof(*stats));
	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];
		struct mt76_mib_stats *mib = &phy->mib;

		stats->dot11RTSSuccessCount += mib->rts_cnt;
		stats->dot11RTSFailureCount += mib->rts_retries_cnt;
		stats->dot11FCSErrorCount += mib->fcs_err_cnt;
		stats->dot11ACKFailureCount += mib->ack_fail_cnt;
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

u64 __mt7996_get_tsf(struct ieee80211_hw *hw, struct mt7996_vif_link *link)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = link->phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	if (!phy)
		return 0;

	lockdep_assert_held(&dev->mt76.mutex);

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	/* TSF software read */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_READ);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(phy->mt76->band_idx));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(phy->mt76->band_idx));

	return tsf.t64;
}

static u64
mt7996_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *mconf;
	u64 ret = -1ULL;
	int i;

	mutex_lock(&dev->mt76.mutex);
	/* FIXME workaround for preventing kernel crash during ACS
	 * (i.e., link 0 is doing ACS while link 1 queries tsf)
	 */
	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		mconf = mt7996_vif_link(dev, vif, i);
		if (mconf)
			break;
	}
	if (mconf)
		ret = __mt7996_get_tsf(hw, mconf);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	phy = link->phy;
	if (!phy)
		goto unlock;

	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software overwrite */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_WRITE);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_offset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  s64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	phy = link->phy;
	if (!phy)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software adjust*/
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_ADJUST);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;

	mutex_lock(&dev->mt76.mutex);
	mt7996_for_each_phy(dev, phy) {
		phy->coverage_class = max_t(s16, coverage_class, 0);
		mt7996_mac_set_coverage_class(phy);
	}
	mutex_unlock(&dev->mt76.mutex);
}

static int mt7996_do_set_tx_ant(s8 cfg_radio_id, s8 actual_radio_id,
				struct mt7996_dev *dev,
				struct mt7996_phy *phy, u32 _tx_ant, u32 rx_ant)
{
	u8 band_idx = phy->mt76->band_idx;
	u8 shift = dev->chainshift[band_idx];
	u32 tx_ant = _tx_ant;

	if (cfg_radio_id == -1) {
		/* Setting all radios.  If user specified only antennas for band 0,
		 * then assume they want same for each band.
		 */
		if (tx_ant <= 0xf)
			tx_ant = tx_ant << shift;
	} else {
		tx_ant = tx_ant << shift;
	}

	if (!(tx_ant & phy->orig_chainmask)) {
		pr_info("ERROR: mt7996-set-antenna,  cfg-radio-id: %d actual-id: %d tx_ant: 0x%x  orig-chainmask: 0x%x band-idx: %d shift: %d\n",
			cfg_radio_id, actual_radio_id, tx_ant, phy->orig_chainmask, band_idx, shift);
		return -EINVAL;
	}

	phy->mt76->chainmask = tx_ant & phy->orig_chainmask;
	phy->mt76->antenna_mask = (phy->mt76->chainmask >> shift) &
				   phy->orig_antenna_mask;

	mt76_set_stream_caps(phy->mt76, true);
	mt7996_set_stream_vht_txbf_caps(phy);
	mt7996_set_stream_he_eht_caps(phy);
	mt7996_mcu_set_txpower_sku(phy);

	return 0;
}

static int
mt7996_set_antenna(struct ieee80211_hw *hw, s8 radio_id, u32 tx_ant, u32 rx_ant)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i;
	int rv = 0;

	if (tx_ant != rx_ant)
		return -EINVAL;

	mutex_lock(&dev->mt76.mutex);

	if (radio_id == -1) {
		for (i = 0; i < hw->wiphy->n_radio; i++) {
			struct mt7996_phy *phy = dev->radio_phy[i];

			rv |= mt7996_do_set_tx_ant(radio_id, i, dev, phy, tx_ant, rx_ant);
		}
	} else {
		struct mt7996_phy *phy = dev->radio_phy[radio_id];

		rv |= mt7996_do_set_tx_ant(radio_id, radio_id, dev, phy, tx_ant, rx_ant);
	}

	mutex_unlock(&dev->mt76.mutex);

	return rv;
}

static void mt7996_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct rate_info *txrate;

	/* TODO: support per-link rate report */
	mutex_lock(&dev->mt76.mutex);
	msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
	if (!msta_link)
		goto out;

	txrate = &msta_link->wcid.rate;

	if (txrate->legacy || txrate->flags) {
		if (txrate->legacy) {
			sinfo->txrate.legacy = txrate->legacy;
		} else {
			sinfo->txrate.mcs = txrate->mcs;
			sinfo->txrate.nss = txrate->nss;
			sinfo->txrate.bw = txrate->bw;
			sinfo->txrate.he_gi = txrate->he_gi;
			sinfo->txrate.he_dcm = txrate->he_dcm;
			sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
			sinfo->txrate.eht_gi = txrate->eht_gi;
		}
		sinfo->txrate.flags = txrate->flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}
	sinfo->txrate.flags = txrate->flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	sinfo->tx_failed = msta_link->wcid.stats.tx_failed;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->tx_retries = msta_link->wcid.stats.tx_retries;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	sinfo->ack_signal = (s8)msta_link->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal =
		-(s8)ewma_avg_signal_read(&msta_link->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		sinfo->tx_bytes = msta_link->wcid.stats.tx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

		sinfo->rx_bytes = msta_link->wcid.stats.rx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

		sinfo->tx_packets = msta_link->wcid.stats.tx_mpdu_ok;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);

		sinfo->rx_packets = msta_link->wcid.stats.rx_packets;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
	}
out:
	mutex_unlock(&dev->mt76.mutex);
}

#if 0
// TODO:  Needs mac80211 patch, maybe upstream will do it differently.
//autobuild/unified/filogic/mac80211/24.10/files/package/kernel/mac80211/patches/subsys/0062-mtk-mac80211-add-link-information-when-dump-station.patch
static void mt7996_sta_link_statistics(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       unsigned int link_id,
				       struct station_link_info *linfo)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *mconf;
	struct mt76_sta_stats *stats;
	int i;

	mutex_lock(&dev->mt76.mutex);
	msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	if (!msta_link)
		goto out;
	stats = &msta_link->wcid.stats;

	mconf = mt7996_vif_link(dev, vif, link_id);
	if (!mconf)
		goto out;

	linfo->signal = (s8)msta_link->signal;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);

	linfo->chains = mconf->phy->mt76->antenna_mask;
	memcpy(linfo->chain_signal, msta_link->chain_signal, IEEE80211_MAX_CHAINS);
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL);

	linfo->signal_avg = -(s8)ewma_avg_signal_read(&msta_link->signal_avg);
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);

	for (i = 0; i < IEEE80211_MAX_CHAINS; ++i)
		linfo->chain_signal_avg[i] = -(s8)ewma_avg_signal_read(msta_link->chain_signal_avg + i);
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL_AVG);

	linfo->ack_signal = (s8)msta_link->ack_signal;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	linfo->avg_ack_signal = -(s8)ewma_avg_signal_read(&msta_link->avg_ack_signal);
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);

	linfo->txrate = msta_link->wcid.rate;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	linfo->rxrate = msta_link->wcid.rx_rate;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);

	linfo->tx_bytes = stats->tx_bytes;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

	linfo->rx_bytes = stats->rx_bytes;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

	linfo->tx_failed = stats->tx_failed;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	linfo->tx_retries = stats->tx_retries;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	linfo->rx_mpdu_count = stats->rx_mpdus;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_MPDUS);

	linfo->fcs_err_count = stats->rx_fcs_err;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_FCS_ERROR_COUNT);

	linfo->tx_duration = stats->tx_airtime;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_DURATION);

	linfo->rx_duration = stats->rx_airtime;
	linfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_DURATION);
out:
	mutex_unlock(&dev->mt76.mutex);
}
#endif

static void mt7996_sta_rc_work(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_dev *dev = msta->vif->dev;
	struct mt7996_sta_rc_work_data *wd = data;

	rcu_read_lock();

	msta_link = rcu_dereference(msta->link[wd->link_id]);
	if (!msta_link)
		goto out;

	spin_lock_bh(&dev->mt76.sta_poll_lock);

	msta_link->changed |= wd->changed;
	if (list_empty(&msta_link->rc_list))
		list_add_tail(&msta_link->rc_list, &dev->sta_rc_list);

	spin_unlock_bh(&dev->mt76.sta_poll_lock);
out:
	rcu_read_unlock();
}

static void mt7996_link_sta_rc_update(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_link_sta *link_sta,
				      u32 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = link_sta->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_rc_work_data data = {
		.link_id = msta->deflink_id,
		.changed = changed,
	};

	if (!msta->vif) {
		dev_warn(dev->mt76.dev, "Un-initialized STA %pM wcid %d in rc_work\n",
			 sta->addr, msta->deflink.wcid.idx);
		return;
	}
	mt7996_sta_rc_work(&data, sta);
	ieee80211_queue_work(hw, &dev->rc_work);
}

static int
mt7996_set_bitrate_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			const struct cfg80211_bitrate_mask *mask)
{
	// TODO:  mac80211 patch to pass in link-id.
	unsigned int link_id = 0;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *mconf;
	struct mt7996_sta_rc_work_data data = {
		.link_id = link_id,
		.changed = IEEE80211_RC_SUPP_RATES_CHANGED,
	};

	mutex_lock(&dev->mt76.mutex);
	mconf = mt7996_vif_link(dev, vif, link_id);

	if (!mconf) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	mconf->bitrate_mask = *mask;
	mutex_unlock(&dev->mt76.mutex);

	/* if multiple rates across different preambles are given we can
	 * reconfigure this info with all peers using sta_rec command with
	 * the below exception cases.
	 * - single rate : if a rate is passed along with different preambles,
	 * we select the highest one as fixed rate. i.e VHT MCS for VHT peers.
	 * - multiple rates: if it's not in range format i.e 0-{7,8,9} for VHT
	 * then multiple MCS setting (MCS 4,5,6) is not supported.
	 */
	ieee80211_iterate_stations_atomic(hw, mt7996_sta_rc_work, &data);
	ieee80211_queue_work(hw, &dev->rc_work);

	return 0;
}

static void mt7996_sta_set_4addr(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);

		if (msta->deflink_id != link_id && is_mt7996(&dev->mt76))
			mt7996_mcu_ps_leave(dev, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_sta_set_decap_offload(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_HDR_TRANS,
				&msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_HDR_TRANS,
				  &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static const char mt7996_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_pkts_nic", /* from driver, phy tx-ok skb */
	"tx_bytes_nic", /* from driver, phy tx-ok bytes */
	"rx_pkts_nic", /* from driver, phy rx OK skb */
	"rx_bytes_nic", /* from driver, phy rx OK bytes */
	"tx_ampdu_cnt",
	"tx_stop_q_empty_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
	"tx_rwp_fail_cnt",
	"tx_rwp_need_cnt",
	"tx_pkt_ebf_cnt",
	"tx_pkt_ibf_cnt",
	"tx_ampdu_len:0-1",
	"tx_ampdu_len:2-10",
	"tx_ampdu_len:11-19",
	"tx_ampdu_len:20-28",
	"tx_ampdu_len:29-37",
	"tx_ampdu_len:38-46",
	"tx_ampdu_len:47-55",
	"tx_ampdu_len:56-79",
	"tx_ampdu_len:80-103",
	"tx_ampdu_len:104-127",
	"tx_ampdu_len:128-151",
	"tx_ampdu_len:152-175",
	"tx_ampdu_len:176-199",
	"tx_ampdu_len:200-223",
	"tx_ampdu_len:224-247",
	"ba_miss_count",
	"tx_bfer_ppdu_iBF",
	"tx_bfer_ppdu_eBF",
	"tx_bfer_rx_feedback_all",
	"tx_bfer_rx_feedback_he",
	"tx_bfer_rx_feedback_vht",
	"tx_bfer_rx_feedback_ht",
	"tx_bfer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"tx_bfer_rx_feedback_nc",
	"tx_bfer_rx_feedback_nr",
	"tx_bfee_ok_feedback_pkts",
	"tx_bfee_feedback_trig",
	"tx_mu_beamforming",
	"tx_mu_mpdu",
	"tx_mu_successful_mpdu",
	"tx_su_successful_mpdu",
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",

	/* rx counters */
	"rx_fifo_full_cnt",
	"rx_mpdu_cnt",
	"channel_idle_cnt",
	"rx_vector_mismatch_cnt",
	"rx_delimiter_fail_cnt",
	"rx_len_mismatch_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
	"rx_ampdu_valid_subframe_cnt",
	"rx_ampdu_valid_subframe_b_cnt",
	"rx_pfdrop_cnt",
	"rx_vec_q_overflow_drop_cnt",
	"rx_ba_cnt",

	/* driver rx counters */
	"d_rx_skb",
	"d_rx_rxd2_amsdu_err",
	"d_rx_null_channels",
	"d_rx_max_len_err",
	"d_rx_too_short",
	"d_rx_bad_ht_rix",
	"d_rx_bad_vht_rix",
	"d_rx_bad_mode",
	"d_rx_bad_bw",

	/* phy 1 stats */
	"P1:tx_pkts_nic", /* from driver, phy tx-ok skb */
	"P1:tx_bytes_nic", /* from driver, phy tx-ok bytes */
	"P1:rx_pkts_nic", /* from driver, phy rx OK skb */
	"P1:rx_bytes_nic", /* from driver, phy rx OK bytes */
	"P1:tx_ampdu_cnt",
	"P1:tx_stop_q_empty_cnt",
	"P1:tx_mpdu_attempts",
	"P1:tx_mpdu_success",
	"P1:tx_rwp_fail_cnt",
	"P1:tx_rwp_need_cnt",
	"P1:tx_pkt_ebf_cnt",
	"P1:tx_pkt_ibf_cnt",
	"P1:tx_ampdu_len:0-1",
	"P1:tx_ampdu_len:2-10",
	"P1:tx_ampdu_len:11-19",
	"P1:tx_ampdu_len:20-28",
	"P1:tx_ampdu_len:29-37",
	"P1:tx_ampdu_len:38-46",
	"P1:tx_ampdu_len:47-55",
	"P1:tx_ampdu_len:56-79",
	"P1:tx_ampdu_len:80-103",
	"P1:tx_ampdu_len:104-127",
	"P1:tx_ampdu_len:128-151",
	"P1:tx_ampdu_len:152-175",
	"P1:tx_ampdu_len:176-199",
	"P1:tx_ampdu_len:200-223",
	"P1:tx_ampdu_len:224-247",
	"P1:ba_miss_count",
	"P1:tx_bfer_ppdu_iBF",
	"P1:tx_bfer_ppdu_eBF",
	"P1:tx_bfer_rx_feedback_all",
	"P1:tx_bfer_rx_feedback_he",
	"P1:tx_bfer_rx_feedback_vht",
	"P1:tx_bfer_rx_feedback_ht",
	"P1:tx_bfer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"P1:tx_bfer_rx_feedback_nc",
	"P1:tx_bfer_rx_feedback_nr",
	"P1:tx_bfee_ok_feedback_pkts",
	"P1:tx_bfee_feedback_trig",
	"P1:tx_mu_beamforming",
	"P1:tx_mu_mpdu",
	"P1:tx_mu_successful_mpdu",
	"P1:tx_su_successful_mpdu",
	"P1:tx_msdu_pack_1",
	"P1:tx_msdu_pack_2",
	"P1:tx_msdu_pack_3",
	"P1:tx_msdu_pack_4",
	"P1:tx_msdu_pack_5",
	"P1:tx_msdu_pack_6",
	"P1:tx_msdu_pack_7",
	"P1:tx_msdu_pack_8",

	/* rx counters */
	"P1:rx_fifo_full_cnt",
	"P1:rx_mpdu_cnt",
	"P1:channel_idle_cnt",
	"P1:rx_vector_mismatch_cnt",
	"P1:rx_delimiter_fail_cnt",
	"P1:rx_len_mismatch_cnt",
	"P1:rx_ampdu_cnt",
	"P1:rx_ampdu_bytes_cnt",
	"P1:rx_ampdu_valid_subframe_cnt",
	"P1:rx_ampdu_valid_subframe_b_cnt",
	"P1:rx_pfdrop_cnt",
	"P1:rx_vec_q_overflow_drop_cnt",
	"P1:rx_ba_cnt",

	/* driver rx counters */
	"P1:d_rx_skb",
	"P1:d_rx_rxd2_amsdu_err",
	"P1:d_rx_null_channels",
	"P1:d_rx_max_len_err",
	"P1:d_rx_too_short",
	"P1:d_rx_bad_ht_rix",
	"P1:d_rx_bad_vht_rix",
	"P1:d_rx_bad_mode",
	"P1:d_rx_bad_bw",

	/* phy 2 stats*/
	"P2:tx_pkts_nic", /* from driver, phy tx-ok skb */
	"P2:tx_bytes_nic", /* from driver, phy tx-ok bytes */
	"P2:rx_pkts_nic", /* from driver, phy rx OK skb */
	"P2:rx_bytes_nic", /* from driver, phy rx OK bytes */
	"P2:tx_ampdu_cnt",
	"P2:tx_stop_q_empty_cnt",
	"P2:tx_mpdu_attempts",
	"P2:tx_mpdu_success",
	"P2:tx_rwp_fail_cnt",
	"P2:tx_rwp_need_cnt",
	"P2:tx_pkt_ebf_cnt",
	"P2:tx_pkt_ibf_cnt",
	"P2:tx_ampdu_len:0-1",
	"P2:tx_ampdu_len:2-10",
	"P2:tx_ampdu_len:11-19",
	"P2:tx_ampdu_len:20-28",
	"P2:tx_ampdu_len:29-37",
	"P2:tx_ampdu_len:38-46",
	"P2:tx_ampdu_len:47-55",
	"P2:tx_ampdu_len:56-79",
	"P2:tx_ampdu_len:80-103",
	"P2:tx_ampdu_len:104-127",
	"P2:tx_ampdu_len:128-151",
	"P2:tx_ampdu_len:152-175",
	"P2:tx_ampdu_len:176-199",
	"P2:tx_ampdu_len:200-223",
	"P2:tx_ampdu_len:224-247",
	"P2:ba_miss_count",
	"P2:tx_bfer_ppdu_iBF",
	"P2:tx_bfer_ppdu_eBF",
	"P2:tx_bfer_rx_feedback_all",
	"P2:tx_bfer_rx_feedback_he",
	"P2:tx_bfer_rx_feedback_vht",
	"P2:tx_bfer_rx_feedback_ht",
	"P2:tx_bfer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"P2:tx_bfer_rx_feedback_nc",
	"P2:tx_bfer_rx_feedback_nr",
	"P2:tx_bfee_ok_feedback_pkts",
	"P2:tx_bfee_feedback_trig",
	"P2:tx_mu_beamforming",
	"P2:tx_mu_mpdu",
	"P2:tx_mu_successful_mpdu",
	"P2:tx_su_successful_mpdu",
	"P2:tx_msdu_pack_1",
	"P2:tx_msdu_pack_2",
	"P2:tx_msdu_pack_3",
	"P2:tx_msdu_pack_4",
	"P2:tx_msdu_pack_5",
	"P2:tx_msdu_pack_6",
	"P2:tx_msdu_pack_7",
	"P2:tx_msdu_pack_8",

	/* rx counters */
	"P2:rx_fifo_full_cnt",
	"P2:rx_mpdu_cnt",
	"P2:channel_idle_cnt",
	"P2:rx_vector_mismatch_cnt",
	"P2:rx_delimiter_fail_cnt",
	"P2:rx_len_mismatch_cnt",
	"P2:rx_ampdu_cnt",
	"P2:rx_ampdu_bytes_cnt",
	"P2:rx_ampdu_valid_subframe_cnt",
	"P2:rx_ampdu_valid_subframe_b_cnt",
	"P2:rx_pfdrop_cnt",
	"P2:rx_vec_q_overflow_drop_cnt",
	"P2:rx_ba_cnt",

	/* driver rx counters */
	"P2:d_rx_skb",
	"P2:d_rx_rxd2_amsdu_err",
	"P2:d_rx_null_channels",
	"P2:d_rx_max_len_err",
	"P2:d_rx_too_short",
	"P2:d_rx_bad_ht_rix",
	"P2:d_rx_bad_vht_rix",
	"P2:d_rx_bad_mode",
	"P2:d_rx_bad_bw",

	/* per vif counters */
	"v_tx_mpdu_attempts", /* counting any retries (all frames) */
	"v_tx_mpdu_fail",  /* frames that failed even after retry (all frames) */
	"v_tx_mpdu_retry", /* number of times frames were retried (all frames) */
	"v_tx_mpdu_ok", /* frames that succeeded, perhaps after retry (all frames) */

	"v_txo_tx_mpdu_attempts", /* counting any retries, txo frames */
	"v_txo_tx_mpdu_fail",  /* frames that failed even after retry, txo frames */
	"v_txo_tx_mpdu_retry", /* number of times frames were retried, txo frames */
	"v_txo_tx_mpdu_ok", /* frames that succeeded, perhaps after retry, txo frames */

	"v_tx_mode_cck",
	"v_tx_mode_ofdm",
	"v_tx_mode_ht",
	"v_tx_mode_ht_gf",
	"v_tx_mode_vht",
	"v_tx_mode_he_su",
	"v_tx_mode_he_ext_su",
	"v_tx_mode_he_tb",
	"v_tx_mode_he_mu",
	"v_tx_mode_eht_su",
	"v_tx_mode_eht_trig",
	"v_tx_mode_eht_mu",
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
	"v_tx_bw_320",
	"v_tx_mcs_0",
	"v_tx_mcs_1",
	"v_tx_mcs_2",
	"v_tx_mcs_3",
	"v_tx_mcs_4",
	"v_tx_mcs_5",
	"v_tx_mcs_6",
	"v_tx_mcs_7",
	"v_tx_mcs_8",
	"v_tx_mcs_9",
	"v_tx_mcs_10",
	"v_tx_mcs_11",
	"v_tx_mcs_12",
	"v_tx_mcs_13",
	"v_tx_nss_1",
	"v_tx_nss_2",
	"v_tx_nss_3",
	"v_tx_nss_4",

	/* per-vif rx counters */
	"v_rx_nss_1",
	"v_rx_nss_2",
	"v_rx_nss_3",
	"v_rx_nss_4",
	"v_rx_mode_cck",
	"v_rx_mode_ofdm",
	"v_rx_mode_ht",
	"v_rx_mode_ht_gf",
	"v_rx_mode_vht",
	"v_rx_mode_he_su",
	"v_rx_mode_he_ext_su",
	"v_rx_mode_he_tb",
	"v_rx_mode_he_mu",
	"v_rx_mode_eht_su",
	"v_rx_mode_eht_trig",
	"v_rx_mode_eht_mu",
	"v_rx_bw_20",
	"v_rx_bw_40",
	"v_rx_bw_80",
	"v_rx_bw_160",
	"v_rx_bw_320",
	"v_rx_bw_he_ru",
	"v_rx_ru_106",

	"v_rx_mcs_0",
	"v_rx_mcs_1",
	"v_rx_mcs_2",
	"v_rx_mcs_3",
	"v_rx_mcs_4",
	"v_rx_mcs_5",
	"v_rx_mcs_6",
	"v_rx_mcs_7",
	"v_rx_mcs_8",
	"v_rx_mcs_9",
	"v_rx_mcs_10",
	"v_rx_mcs_11",
	"v_rx_mcs_12",
	"v_rx_mcs_13",

	"rx_ampdu_len:0-1",
	"rx_ampdu_len:2-10",
	"rx_ampdu_len:11-19",
	"rx_ampdu_len:20-28",
	"rx_ampdu_len:29-37",
	"rx_ampdu_len:38-46",
	"rx_ampdu_len:47-55",
	"rx_ampdu_len:56-79",
	"rx_ampdu_len:80-103",
	"rx_ampdu_len:104-127",
	"rx_ampdu_len:128-151",
	"rx_ampdu_len:152-175",
	"rx_ampdu_len:176-199",
	"rx_ampdu_len:200-223",
	"rx_ampdu_len:224-247",
};

#define MT7996_SSTATS_LEN ARRAY_SIZE(mt7996_gstrings_stats)

/* Ethtool related API */
static
void mt7996_get_et_strings(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, mt7996_gstrings_stats,
		       sizeof(mt7996_gstrings_stats));
}

static
int mt7996_get_et_sset_count(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return MT7996_SSTATS_LEN;

	return 0;
}

static void mt7996_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt76_ethtool_worker_info *wi = wi_data;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *mconf;
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	msta_link = mt76_dereference(msta->link[msta->deflink_id], &mvif->dev->mt76);
	mconf = mt7996_vif_link(mvif->dev, vif, msta->deflink_id);

	if (mconf->mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta_link->wcid.stats, true);
}

static
void mt7996_get_et_stats(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = &dev->phy;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
		.has_eht = true,
	};
	/* See mt7996_ampdu_stat_read_phy, etc */
	int i, j, ei = 0;

	mutex_lock(&dev->mt76.mutex);

	/* mvif may not be set up when this is called */
	if (((struct mt76_vif_link *)(vif->drv_priv))->mvif) {
		mconf = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
		if (mconf)
			wi.idx = mconf->mt76.idx;
	}

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		phy = dev->radio_phy[i];

		if (!phy) {
			ei += 73;
			continue;
		}

		mib = &phy->mib;


		mt7996_mac_update_stats(phy);

		/* driver phy-wide stats */
		data[ei++] = mib->tx_pkts_nic;
		data[ei++] = mib->tx_bytes_nic;
		data[ei++] = mib->rx_pkts_nic;
		data[ei++] = mib->rx_bytes_nic;

		/* MIB stats from FW/HW */
		data[ei++] = mib->tx_ampdu_cnt;
		data[ei++] = mib->tx_stop_q_empty_cnt;
		data[ei++] = mib->tx_mpdu_attempts_cnt;
		data[ei++] = mib->tx_mpdu_success_cnt;
		data[ei++] = mib->tx_rwp_fail_cnt;
		data[ei++] = mib->tx_rwp_need_cnt;
		data[ei++] = mib->tx_bf_ebf_ppdu_cnt;
		data[ei++] = mib->tx_bf_ibf_ppdu_cnt;

		/* Tx ampdu stat */
		for (j = 0; j < 15 /*ARRAY_SIZE(bound)*/; j++)
			data[ei++] = phy->mt76->aggr_stats[j];
		data[ei++] = mib->ba_miss_cnt;

		/* Tx Beamformer monitor */
		data[ei++] = mib->tx_bf_ibf_ppdu_cnt;
		data[ei++] = mib->tx_bf_ebf_ppdu_cnt;

		/* Tx Beamformer Rx feedback monitor */
		data[ei++] = mib->tx_bf_rx_fb_all_cnt;
		data[ei++] = mib->tx_bf_rx_fb_he_cnt;
		data[ei++] = mib->tx_bf_rx_fb_vht_cnt;
		data[ei++] = mib->tx_bf_rx_fb_ht_cnt;

		data[ei++] = mib->tx_bf_rx_fb_bw;
		data[ei++] = mib->tx_bf_rx_fb_nc_cnt;
		data[ei++] = mib->tx_bf_rx_fb_nr_cnt;

		/* Tx Beamformee Rx NDPA & Tx feedback report */
		data[ei++] = mib->tx_bf_fb_cpl_cnt;
		data[ei++] = mib->tx_bf_fb_trig_cnt;

		/* Tx SU & MU counters */
		data[ei++] = mib->tx_mu_bf_cnt;
		data[ei++] = mib->tx_mu_mpdu_cnt;
		data[ei++] = mib->tx_mu_acked_mpdu_cnt;
		data[ei++] = mib->tx_su_acked_mpdu_cnt;

		/* Tx amsdu info (pack-count histogram) */
		for (j = 0; j < ARRAY_SIZE(mib->tx_amsdu); j++)
			data[ei++] = mib->tx_amsdu[j];

		/* rx counters */
		data[ei++] = mib->rx_fifo_full_cnt;
		data[ei++] = mib->rx_mpdu_cnt;
		data[ei++] = mib->channel_idle_cnt;
		data[ei++] = mib->rx_vector_mismatch_cnt;
		data[ei++] = mib->rx_delimiter_fail_cnt;
		data[ei++] = mib->rx_len_mismatch_cnt;
		data[ei++] = mib->rx_ampdu_cnt;
		data[ei++] = mib->rx_ampdu_bytes_cnt;
		data[ei++] = mib->rx_ampdu_valid_subframe_cnt;
		data[ei++] = mib->rx_ampdu_valid_subframe_bytes_cnt;
		data[ei++] = mib->rx_pfdrop_cnt;
		data[ei++] = mib->rx_vec_queue_overflow_drop_cnt;
		data[ei++] = mib->rx_ba_cnt;

		/* rx stats from driver */
		data[ei++] = mib->rx_d_skb;
		data[ei++] = mib->rx_d_rxd2_amsdu_err;
		data[ei++] = mib->rx_d_null_channels;
		data[ei++] = mib->rx_d_max_len_err;
		data[ei++] = mib->rx_d_too_short;
		data[ei++] = mib->rx_d_bad_ht_rix;
		data[ei++] = mib->rx_d_bad_vht_rix;
		data[ei++] = mib->rx_d_bad_mode;
		data[ei++] = mib->rx_d_bad_bw;

	}

	/* Add values for all stations owned by this vif */
	/* CT NOTE: Someday we may want to attempt breaking sta stats out
	 *	    by phy...
	 */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt7996_ethtool_worker, &wi);

	mutex_unlock(&dev->mt76.mutex);

	if (wi.sta_count == 0)
		return;

	ei += wi.worker_stat_count;
	if (ei != MT7996_SSTATS_LEN)
		dev_err(dev->mt76.dev, "ei: %d  MT7996_SSTATS_LEN: %d",
			ei, (int)MT7996_SSTATS_LEN);
}

static void
mt7996_twt_teardown_request(struct ieee80211_hw *hw,
			    struct ieee80211_sta *sta,
			    u8 flowid)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct ieee80211_vif *vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	msta_link = mt76_dereference(msta->link[0], &dev->mt76);
	if (msta_link) {
		link = mt7996_vif_link(dev, vif, 0);
		mt7996_mac_twt_teardown_flow(dev, link, msta_link, flowid);
	}
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_set_radar_background(struct ieee80211_hw *hw,
			    struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	int ret = -EINVAL;
	bool running;

	if (chandef)
		phy = mt7996_band_phy(dev, chandef->chan->band);
	else
		phy = dev->rdd2_phy;
	if (!phy)
	    return -EINVAL;

	mutex_lock(&dev->mt76.mutex);

	if (dev->mt76.region == NL80211_DFS_UNSET)
		goto out;

	if (dev->rdd2_phy && dev->rdd2_phy != phy) {
		/* rdd2 is already locked */
		ret = -EBUSY;
		goto out;
	}

	/* rdd2 already configured on a radar channel */
	running = dev->rdd2_phy &&
		  cfg80211_chandef_valid(&dev->rdd2_chandef) &&
		  !!(dev->rdd2_chandef.chan->flags & IEEE80211_CHAN_RADAR);

	if (!chandef || running ||
	    !(chandef->chan->flags & IEEE80211_CHAN_RADAR)) {
		ret = mt7996_mcu_rdd_background_enable(phy, NULL);
		if (ret)
			goto out;

		if (!running)
			goto update_phy;
	}

	ret = mt7996_mcu_rdd_background_enable(phy, chandef);
	if (ret)
		goto out;

update_phy:
	dev->rdd2_phy = chandef ? phy : NULL;
	if (chandef)
		dev->rdd2_chandef = *chandef;
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static int
mt7996_net_fill_forward_path(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct net_device_path_ctx *ctx,
			     struct net_device_path *path)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct mt76_vif_link *mlink;
	struct mt7996_phy *phy;
	u8 dscp = path->mtk_wdma.tid >> 2;

	mlink = rcu_dereference(mvif->mt76.link[msta->deflink_id]);
	if (!mlink)
		return -EIO;

	msta_link = rcu_dereference(msta->link[msta->deflink_id]);
	if (!msta_link)
		return -EIO;

	if (!msta_link->wcid.sta || msta_link->wcid.idx > MT7996_WTBL_STA)
		return -EIO;

	link = (struct mt7996_vif_link *)mlink;
	phy = mt7996_vif_link_phy(link);
	if (!phy)
		return -ENODEV;

	if (phy != &dev->phy && phy->mt76->band_idx == MT_BAND2)
		wed = &dev->mt76.mmio.wed_hif2;

	if (!mtk_wed_device_active(wed))
		return -ENODEV;

	path->type = DEV_PATH_MTK_WDMA;
	path->dev = ctx->dev;
	path->mtk_wdma.wdma_idx = wed->wdma_idx;
	path->mtk_wdma.bss = mlink->idx;
	path->mtk_wdma.queue = 0;
	path->mtk_wdma.wcid = msta_link->wcid.idx;

	path->mtk_wdma.amsdu = mtk_wed_is_amsdu_supported(wed);
	ctx->dev = NULL;

	if (path->mtk_wdma.amsdu)
		path->mtk_wdma.tid = mvif->qos_map[dscp];

	return 0;
}

#endif

static int
mt7996_change_vif_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			u16 old_links, u16 new_links,
			struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mvif->deflink.phy;
	struct mt7996_vif_link *mconf;
	unsigned long rem = old_links & ~new_links & ~vif->dormant_links &
			    mvif->mt76.valid_links;
	unsigned long add = new_links & ~old_links;
	int link_id, ret = 0;

	mt76_dbg(&dev->mt76, MT76_DBG_MLD,
		 "%s: old=0x%x, new=0x%x, dormant=0x%x\n",
		 __func__, old_links, new_links, vif->dormant_links);

	if (old_links == new_links)
		return 0;

	mutex_lock(&dev->mt76.mutex);

	/* remove first */
	if (rem && vif->type == NL80211_IFTYPE_AP) {
		ret = mt7996_mcu_mld_reconf_stop_link(dev, vif, rem);
		if (ret)
			goto out;

		for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
			mconf = mt7996_vif_link(dev, vif, link_id);
			if (!mconf)
				continue;

			ret = mt7996_mcu_mld_link_oper(mconf->phy, old[link_id],
						       mconf, false);
			if (ret)
				goto out;
		}
	}

	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);

		mconf =	mt7996_vif_link(dev, vif, link_id);
		if (!mconf)
			continue;

		ret = mt7996_mcu_mld_link_oper(mconf->phy, conf, mconf, true);
		if (ret)
			goto out;
	}

	if (!old_links) {
		int idx;

		mconf = mt7996_vif_link(dev, vif, 0);

		/* switch from non-MLD vif to MLD vif, remove default bss conf */
		if (ieee80211_vif_is_mld(vif) && mconf == &mvif->deflink)
			mt7996_vif_link_remove(mconf->phy->mt76, vif, NULL, &mconf->mt76);

		idx = get_own_mld_idx(dev->mld_id_mask, true);
		if (idx < 0) {
			ret = -ENOSPC;
			goto out;
		}
		mvif->group_mld_id = idx;
		dev->mld_id_mask |= BIT_ULL(mvif->group_mld_id);

		idx = get_mld_remap_idx(dev->mld_remap_id_mask);
		if (idx < 0) {
			ret = -ENOSPC;
			goto out;
		}
		mvif->mld_remap_id = idx;
		dev->mld_remap_id_mask |= BIT_ULL(mvif->mld_remap_id);
	}

	if (new_links)
		goto out;

	/* remove all links and fallback to non-MLO interface */
	rem = mvif->mt76.valid_links;
	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = mt7996_vif_link(dev, vif, link_id);
		if (!mconf)
			continue;

		mt7996_vif_link_remove(phy->mt76, vif, NULL, &mconf->mt76);
	}

	ret = mt7996_vif_link_add(phy->mt76, vif, &vif->bss_conf, NULL);
	dev->mld_id_mask &= ~BIT_ULL(mvif->group_mld_id);
	dev->mld_remap_id_mask &= ~BIT_ULL(mvif->mld_remap_id);

out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_set_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ieee80211_neg_ttlm merged_ttlm;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta;
	int ret;

	/* TODO check the intersection between Adv-TTLM and Neg-TTLM */
	if (vif->type != NL80211_IFTYPE_STATION ||
	    (vif->adv_ttlm.active && vif->neg_ttlm.valid))
		return -EOPNOTSUPP;

	mutex_lock(&dev->mt76.mutex);
	sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!sta) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	mt7996_get_merged_ttlm(vif, &merged_ttlm);

	ret = mt7996_mcu_peer_mld_ttlm_req(dev, vif, sta, &merged_ttlm);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static int
mt7996_set_sta_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta, struct ieee80211_neg_ttlm *neg_ttlm)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_peer_mld_ttlm_req(dev, vif, sta, neg_ttlm);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static int
mt7996_set_attlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 u16 disabled_links, u16 switch_time, u32 duration)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_mld_set_attlm(dev, vif, disabled_links, switch_time, duration);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static enum ieee80211_neg_ttlm_res
mt7996_can_neg_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_neg_ttlm *neg_ttlm)
{
	/* TODO check intersection between adv-TTLM and neg-TTLM
	 * For now, we reject all possible overlapping cases of Adv-TTLM and
	 * Neg-TTLM
	 */
	return vif->adv_ttlm.active ? NEG_TTLM_RES_REJECT : NEG_TTLM_RES_ACCEPT;
}

static void
mt7996_event_callback(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      const struct ieee80211_event *event)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	int i;

	switch (event->type) {
	case MLME_EVENT:
		if (event->u.mlme.data == ASSOC_EVENT &&
		    event->u.mlme.status == MLME_SUCCESS) {
			struct ieee80211_bss_conf *conf;
			struct mt7996_vif_link *mconf;
			struct mt7996_phy *phy;
			unsigned long cur, valid_links = vif->valid_links ?: BIT(0);
			unsigned int link_id;
			int next_time = INT_MAX;

			mutex_lock(&dev->mt76.mutex);
			cur = jiffies;
			for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
				conf = link_conf_dereference_protected(vif, link_id);
				mconf = mt7996_vif_link(dev, vif, link_id);

				if (!conf || !mconf)
					continue;

				phy = mconf->phy;
				mvif->beacon_received_time[phy->mt76->band_idx] = cur;
				next_time = min(next_time,
						MT7996_MAX_BEACON_LOSS *
						conf->beacon_int);

				/* trigger calibration for DFS link */
				if (!cfg80211_reg_can_beacon(hw->wiphy,
							     &phy->mt76->chanctx->chandef,
							     NL80211_IFTYPE_AP))
					mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH,
								 true);
			}

			/* TODO: This may not be related to the 31 station patch. Investigate
			 *       further. OWRT tree seems to experience beacon loss regularly, and
			 *       uses Nullfunc frames to stay associated. Maybe this is a temporary
			 *       fix from their tree for that...
			 */
			if (!dev->sta_omac_repeater_bssid_enable)
				ieee80211_queue_delayed_work(hw, &mvif->beacon_mon_work,
							     msecs_to_jiffies(next_time));
			mutex_unlock(&dev->mt76.mutex);
			break;
		}

		mutex_lock(&dev->mt76.mutex);
		memset(mvif->probe_send_count, 0, sizeof(mvif->probe_send_count));
		for (i = 0; i < __MT_MAX_BAND; i++)
			mvif->probe[i] = NULL;
		mvif->lost_links = 0;
		mutex_unlock(&dev->mt76.mutex);

		cancel_delayed_work(&mvif->beacon_mon_work);
		break;
	default:
		break;
	}
}

static int
mt7996_set_qos_map(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct cfg80211_qos_map *qos_map)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf =
			mt7996_vif_link(dev, vif, link_id);

		if (!mconf)
			continue;

		ret = mt7996_mcu_set_qos_map(dev, mconf, qos_map);
		if(ret)
			break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_sta_channel_switch(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_channel_switch *ch_switch)
{
#define TX_PAUSED_GRACE_PERIOD		2000
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_bss_conf *conf;
	struct mt7996_vif_link *mconf;
	unsigned int link_id = ch_switch->link_id;
	int csa_time;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	mutex_lock(&dev->mt76.mutex);

	conf = link_conf_dereference_protected(vif, link_id);
	mconf = mt7996_vif_link(dev, vif, link_id);
	if (!conf || !mconf) {
		mutex_unlock(&dev->mt76.mutex);
		return;
	}

	/* a new csa occurred while the original one was still in progress */
	if (mconf->state != MT7996_STA_CHSW_IDLE)
		mvif->tx_paused_links &= ~BIT(link_id);

	csa_time = (max_t(u8, ch_switch->count, 1) - 1) * conf->beacon_int;
	mconf->pause_timeout = TX_PAUSED_GRACE_PERIOD +
			       MT7996_MAX_BEACON_LOSS * conf->beacon_int +
			       cfg80211_chandef_dfs_cac_time(hw->wiphy,
							     &ch_switch->chandef);
	mconf->next_state = MT7996_STA_CHSW_PAUSE_TX;
	mutex_unlock(&dev->mt76.mutex);

	cancel_delayed_work(&mconf->sta_chsw_work);
	ieee80211_queue_delayed_work(hw, &mconf->sta_chsw_work,
				     msecs_to_jiffies(csa_time));
}


const struct ieee80211_ops mt7996_ops = {
	.add_chanctx = mt76_add_chanctx,
	.remove_chanctx = mt76_remove_chanctx,
	.change_chanctx = mt76_change_chanctx,
	.assign_vif_chanctx = mt76_assign_vif_chanctx,
	.unassign_vif_chanctx = mt76_unassign_vif_chanctx,
	.switch_vif_chanctx = mt76_switch_vif_chanctx,
	.tx = mt7996_tx,
	.start = mt7996_start,
	.stop = mt7996_stop,
	.add_interface = mt7996_add_interface,
	.remove_interface = mt7996_remove_interface,
	.config = mt7996_config,
	.conf_tx = mt7996_conf_tx,
	.configure_filter = mt7996_configure_filter,
	.vif_cfg_changed = mt7996_vif_cfg_changed,
	.link_info_changed = mt7996_link_info_changed,
	.sta_state = mt7996_sta_state,
	.sta_pre_rcu_remove = mt7996_sta_pre_rcu_remove,
	.link_sta_rc_update = mt7996_link_sta_rc_update,
	.set_key = mt7996_set_key,
	.ampdu_action = mt7996_ampdu_action,
	.set_rts_threshold = mt7996_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.hw_scan = mt76_hw_scan,
	.cancel_hw_scan = mt76_cancel_hw_scan,
	.remain_on_channel = mt76_remain_on_channel,
	.cancel_remain_on_channel = mt76_cancel_remain_on_channel,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt7996_get_txpower,
	.channel_switch_beacon = mt7996_channel_switch_beacon,
	.post_channel_switch = mt7996_post_channel_switch,
	.get_stats = mt7996_get_stats,
	.get_et_sset_count = mt7996_get_et_sset_count,
	.get_et_stats = mt7996_get_et_stats,
	.get_et_strings = mt7996_get_et_strings,
	.get_tsf = mt7996_get_tsf,
	.set_tsf = mt7996_set_tsf,
	.offset_tsf = mt7996_offset_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7996_set_antenna,
	.set_bitrate_mask = mt7996_set_bitrate_mask,
	.set_coverage_class = mt7996_set_coverage_class,
	.sta_statistics = mt7996_sta_statistics,
	//.sta_link_statistics = mt7996_sta_link_statistics,
	.sta_set_4addr = mt7996_sta_set_4addr,
	.sta_set_decap_offload = mt7996_sta_set_decap_offload,
	.add_twt_setup = mt7996_mac_add_twt_setup,
	.twt_teardown_request = mt7996_twt_teardown_request,
	CFG80211_TESTMODE_CMD(mt76_testmode_cmd)
	CFG80211_TESTMODE_DUMP(mt76_testmode_dump)
#ifdef CONFIG_MAC80211_DEBUGFS
	.sta_add_debugfs = mt7996_sta_add_debugfs,
	.link_sta_add_debugfs = mt7996_link_sta_add_debugfs,
	.link_add_debugfs = mt7996_link_add_debugfs,
	.vif_add_debugfs = mt7996_vif_add_debugfs,
#endif
	.set_radar_background = mt7996_set_radar_background,
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	.net_fill_forward_path = mt7996_net_fill_forward_path,
	.net_setup_tc = mt76_wed_net_setup_tc,
#endif
	.event_callback = mt7996_event_callback,
	.change_vif_links = mt7996_change_vif_links,
	.change_sta_links = mt7996_mac_sta_change_links,
	.set_qos_map = mt7996_set_qos_map,
	.set_attlm = mt7996_set_attlm,
	.set_sta_ttlm = mt7996_set_sta_ttlm,
	.can_neg_ttlm = mt7996_can_neg_ttlm,
	.set_ttlm = mt7996_set_ttlm,
	.channel_switch = mt7996_sta_channel_switch,
};
