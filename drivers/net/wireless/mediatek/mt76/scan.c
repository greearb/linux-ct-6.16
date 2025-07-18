// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static void mt76_scan_complete(struct mt76_dev *dev, bool abort)
{
	struct mt76_phy *phy = dev->scan.phy;
	struct ieee80211_vif *vif = dev->scan.vif;
	struct cfg80211_scan_info info = {
		.aborted = abort,
	};

	if (!phy || !vif)
		return;

	if (ieee80211_vif_is_mld(vif))
		if (dev->scan.mlink == (struct mt76_vif_link *)vif->drv_priv)
			dev->drv->vif_link_remove(phy, vif, NULL, dev->scan.mlink);

	ieee80211_scan_completed(phy->hw, &info);
	memset(&dev->scan, 0, sizeof(dev->scan));
	clear_bit(MT76_SCANNING, &phy->state);
	clear_bit(MT76_SCANNING_WAIT_BEACON, &phy->state);
	clear_bit(MT76_SCANNING_BEACON_DONE, &phy->state);
}

void mt76_abort_scan(struct mt76_dev *dev)
{
	cancel_delayed_work_sync(&dev->scan_work);
	mutex_lock(&dev->mutex);
	mt76_scan_complete(dev, true);
	mutex_unlock(&dev->mutex);
}

static void
mt76_scan_send_probe(struct mt76_dev *dev, struct cfg80211_ssid *ssid)
{
	struct cfg80211_scan_request *req = dev->scan.req;
	struct ieee80211_vif *vif = dev->scan.vif;
	struct mt76_vif_link *mvif = dev->scan.mlink;
	enum nl80211_band band = dev->scan.chan->band;
	struct mt76_phy *phy = dev->scan.phy;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	if (WARN_ON_ONCE((unsigned long)(mvif) < 4000)) {
		pr_err("scan-send-probe: mvif: %p\n", mvif);
		return;
	}
	if (WARN_ON_ONCE((unsigned long)(mvif->wcid) < 4000)) {
		pr_err("scan-send-probe: mvif->wcid: %p\n", mvif->wcid);
		return;
	}

	skb = ieee80211_probereq_get(phy->hw, vif->addr, ssid->ssid,
				     ssid->ssid_len, req->ie_len);
	if (!skb)
		return;

	hdr = (struct ieee80211_hdr *)skb->data;
	if (is_unicast_ether_addr(req->bssid)) {
		ether_addr_copy(hdr->addr1, req->bssid);
		ether_addr_copy(hdr->addr3, req->bssid);
	}

	if (req->ie_len)
		skb_put_data(skb, req->ie, req->ie_len);

	skb->priority = 7;
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);

	rcu_read_lock();

	if (!ieee80211_tx_prepare_skb(phy->hw, vif, skb, band, NULL)) {
		ieee80211_free_txskb(phy->hw, skb);
		goto out;
	}

	info = IEEE80211_SKB_CB(skb);
	if (req->no_cck)
		info->flags |= IEEE80211_TX_CTL_NO_CCK_RATE;
	info->control.flags |= IEEE80211_TX_CTRL_DONT_USE_RATE_MASK;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: scan probe req, vif->addr: %pM  addr1: %pM addr2: %pM  addr3: %pM\n",
		 __func__, vif->addr, hdr->addr1, hdr->addr2, hdr->addr3);

	mt76_tx(phy, NULL, mvif->wcid, skb);

out:
	rcu_read_unlock();
}

void mt76_scan_work(struct work_struct *work)
{
	struct mt76_dev *dev = container_of(work, struct mt76_dev,
					    scan_work.work);
	struct cfg80211_scan_request *req = dev->scan.req;
	struct cfg80211_chan_def chandef = {};
	struct mt76_phy *phy = dev->scan.phy;
	int duration = HZ / 9; /* ~110 ms */
	int i;

	clear_bit(MT76_SCANNING_WAIT_BEACON, &phy->state);

	if (dev->scan.chan_idx >= req->n_channels) {
		mutex_lock(&dev->mutex);
		mt76_scan_complete(dev, false);
		mutex_unlock(&dev->mutex);

		mt76_set_channel(phy, &phy->main_chandef, false);

		return;
	}

	/* move to active scan for the current scanning channel */
	if (test_and_clear_bit(MT76_SCANNING_BEACON_DONE, &phy->state)) {
		local_bh_disable();
		for (i = 0; i < req->n_ssids; i++)
			mt76_scan_send_probe(dev, &req->ssids[i]);
		local_bh_enable();
		ieee80211_queue_delayed_work(phy->hw, &dev->scan_work, HZ / 16);
		mt76_dbg(dev, MT76_DBG_SCAN,
			 "%s: move to active scan on channel %d\n",
			 __func__, phy->chanctx ? phy->chanctx->chandef.center_freq1 :
						  phy->chandef.center_freq1);
		return;
	}

	if (dev->scan.chan && phy->num_sta) {
		mt76_dbg(dev, MT76_DBG_SCAN, "%s: moving back to main chandef freq: %d\n",
			 __func__, phy->main_chandef.chan ? phy->main_chandef.chan->center_freq : -1);
		dev->scan.chan = NULL;
		mt76_set_channel(phy, &phy->main_chandef, false);
		goto out;
	}

	dev->scan.chan = req->channels[dev->scan.chan_idx++];

	/* If we are scanning on current mgt channel, we do not need
	 * to go off-channel.
	 */
	if (!dev->scan.chan || !phy->main_chandef.chan ||
	    dev->scan.chan->center_freq != phy->main_chandef.chan->center_freq) {
		cfg80211_chandef_create(&chandef, dev->scan.chan, NL80211_CHAN_HT20);
		mt76_set_channel(phy, &chandef, true);
		mt76_dbg(dev, MT76_DBG_SCAN, "%s: moving freq: %d for chan_idx: %d\n",
			 __func__, dev->scan.chan ? dev->scan.chan->center_freq : -1,
			 dev->scan.chan_idx);

		if (chandef.chan->flags & (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_RADAR))
			goto out;

	} else {
		mt76_dbg(dev, MT76_DBG_SCAN, "%s: scanning on-channel for freq: %d for chan_idx: %d\n",
			 __func__, dev->scan.chan->center_freq, dev->scan.chan_idx);
	}

	if (!req->n_ssids)
		goto out;

	duration = HZ / 16; /* ~60 ms */
	local_bh_disable();
	for (i = 0; i < req->n_ssids; i++) {
		mt76_dbg(dev, MT76_DBG_SCAN, "%s: Sending probe request to %s\n",
			 __func__, req->ssids[i].ssid);
		mt76_scan_send_probe(dev, &req->ssids[i]);
	}
	local_bh_enable();

out:
	if (dev->scan.chan)
		duration = max_t(int, duration,
			         msecs_to_jiffies(req->duration +
						  (req->duration >> 5)));

	ieee80211_queue_delayed_work(dev->phy.hw, &dev->scan_work, duration);
}

int mt76_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_scan_request *req)
{
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	int ret = 0;

	if (hw->wiphy->n_radio > 1) {
		phy = dev->band_phys[req->req.channels[0]->band];
		if (!phy)
			return -EINVAL;
	}

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: trigger scan on mt76 band %u\n",
		 __func__, phy->band_idx);

	mutex_lock(&dev->mutex);

	if (dev->scan.req || phy->roc_vif) {
		ret = -EBUSY;
		goto out;
	}

	if (!ieee80211_vif_is_mld(vif)) {
		mlink = mt76_vif_link(dev, vif, 0);

		if (mlink && mlink->band_idx != phy->band_idx) {
			mt76_dbg(dev, MT76_DBG_CHAN, "%s: non-mld: remove mlink to change to band: %d\n",
				 __func__, phy->band_idx);
			dev->drv->vif_link_remove(phy, vif, NULL, mlink);
			mlink = NULL;
		} else {
			mt76_dbg(dev, MT76_DBG_CHAN, "%s: non-mld: use existing mlink for band: %d\n",
				 __func__, phy->band_idx);
		}

		if (!mlink) {
			mlink = (struct mt76_vif_link *)vif->drv_priv;
			ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, NULL);
			mt76_dbg(dev, MT76_DBG_CHAN, "%s: non-mld: add mlink on band: %d, ret: %d\n",
				 __func__, phy->band_idx, ret);
			if (ret)
				goto out;
		}
	} else {
		struct ieee80211_bss_conf *link_conf;
		unsigned long valid_links = vif->valid_links;
		unsigned int link_id;
		bool found = false;

		for_each_set_bit(link_id, &valid_links,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			mlink = mt76_vif_link(dev, vif, link_id);
			if (mlink && mlink->band_idx == phy->band_idx) {
				found = true;
				break;
			}

			link_conf = link_conf_dereference_protected(vif, link_id);
			if (link_conf && !mlink) {
				/* The link is added in mac80211, but not yet
				 * initialized and assigned to a chanctx.
				 * Here we use the default link to perform scan.
				 */
				mlink = (struct mt76_vif_link *)vif->drv_priv;
				memcpy(&vif->bss_conf, link_conf, sizeof(struct ieee80211_bss_conf));
				ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, NULL);
				mt76_dbg(dev, MT76_DBG_CHAN, "%s: mld: added default link for scanning, ret: %d\n",
					 __func__, ret);
				if (ret)
					goto out;
				found = true;
				break;
			}
		}

		if (!found) {
			if (vif->type != NL80211_IFTYPE_STATION) {
				/* Only allowed STA MLD to scan full-band when
				 * there is no valid link on the band.
				 * (For example, when connecting by 2 links
				 * (2+5 GHz), an AP MLD is not allowed to scan
				 * full-band (2+5+6 GHz), while a STA MLD is.)
				 */
				mt76_scan_complete(dev, 0);
				goto out;
			}

			/* Try to find an empty link, which is later used to scan. */
			for (link_id = 0;
			     link_id < IEEE80211_MLD_MAX_NUM_LINKS;
			     link_id++) {
				if (!rcu_access_pointer(mvif->link[link_id]))
					break;
			}

			if (link_id == IEEE80211_MLD_MAX_NUM_LINKS) {
				ret = -ENOLINK;
				goto out;
			}

			vif->bss_conf.link_id = link_id;
			mlink = (struct mt76_vif_link *)vif->drv_priv;
			ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, NULL);
			mt76_dbg(dev, MT76_DBG_CHAN, "%s: mld: added link for scanning, ret: %d\n",
				 __func__, ret);
			if (ret)
				goto out;
		}
	}

	memset(&dev->scan, 0, sizeof(dev->scan));
	dev->scan.req = &req->req;
	dev->scan.vif = vif;
	dev->scan.phy = phy;
	dev->scan.mlink = mlink;
	set_bit(MT76_SCANNING, &phy->state);

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: queing scan, mlink idx: %d  link_idx: %d  omac_idx: %d  wmm_idx: %d offchannel: %d\n",
		 __func__, mlink->idx, mlink->link_idx, mlink->omac_idx, mlink->band_idx, mlink->wmm_idx);
	ieee80211_queue_delayed_work(dev->phy.hw, &dev->scan_work, 0);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_hw_scan);

void mt76_cancel_hw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_phy *phy = hw->priv;

	mt76_abort_scan(phy->dev);
}
EXPORT_SYMBOL_GPL(mt76_cancel_hw_scan);
