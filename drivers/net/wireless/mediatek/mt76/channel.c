// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static struct mt76_vif_link *
mt76_alloc_mlink(struct mt76_dev *dev, struct mt76_vif_data *mvif)
{
	struct mt76_vif_link *mlink;

	mlink = kzalloc(dev->drv->link_data_size, GFP_KERNEL);
	if (!mlink)
		return NULL;

	mlink->mvif = mvif;

	return mlink;
}

static int
mt76_phy_update_channel(struct mt76_phy *phy,
			struct ieee80211_chanctx_conf *conf)
{
	phy->radar_enabled = conf->radar_enabled;
	phy->main_chandef = conf->def;
	phy->chanctx = (struct mt76_chanctx *)conf->drv_priv;

	return __mt76_set_channel(phy, &phy->main_chandef, false);
}

int mt76_add_chanctx(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	int ret = -EINVAL;

	phy = ctx->phy = dev->band_phys[conf->def.chan->band];
	if (WARN_ON_ONCE(!phy))
		return ret;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: add %u on mt76 band %d\n",
		 __func__, conf->def.chan->hw_value, phy->band_idx);

	if (dev->scan.phy == phy)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	ctx->assigned = true;
	ctx->chandef = conf->def;
	ctx->state = MT76_CHANCTX_STATE_ADD;
	if (!phy->chanctx)
		ret = mt76_phy_update_channel(phy, conf);
	else
		ret = 0;
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_add_chanctx);

void mt76_remove_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;

	phy = ctx->phy;
	if (WARN_ON_ONCE(!phy))
		return;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: remove %u\n",
		 __func__, conf->def.chan->hw_value);
	cancel_delayed_work_sync(&phy->mac_work);

	if (dev->scan.phy == phy)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);
	ctx->assigned = false;
	if (phy->chanctx == ctx) {
		phy->chanctx = NULL;
		phy->radar_enabled = false;
	}
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_remove_chanctx);

void mt76_change_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf,
			 u32 changed)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;

	if (!(changed & (IEEE80211_CHANCTX_CHANGE_WIDTH |
			 IEEE80211_CHANCTX_CHANGE_RADAR)))
		return;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: change to %u, 0x%x\n",
		 __func__, conf->def.chan->hw_value, changed);

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mutex);
	ctx->chandef = conf->def;
	ctx->state = MT76_CHANCTX_STATE_CHANGE;
	mt76_phy_update_channel(phy, conf);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_change_chanctx);


int mt76_assign_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	int link_id = link_conf->link_id;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;
	int ret = 0;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s: assign link_id %u to %d MHz\n",
		 __func__, link_id, conf->def.chan->center_freq);

	if (dev->scan.vif == vif)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = mt76_vif_link(dev, vif, link_id);
	/* Remove bss conf when change non-MLO interface to MLO interface */
	if (ieee80211_vif_is_mld(vif) && mlink == (struct mt76_vif_link *)vif->drv_priv)
		dev->drv->vif_link_remove(phy, vif, NULL, mlink);

	ret = dev->drv->vif_link_add(phy, vif, link_conf, NULL);
	if (ret)
		goto out;

	mlink = mt76_vif_link(dev, vif, link_id);
	mlink->ctx = conf;
	ctx->nbss_assigned++;
	mvif->band_to_link[phy->band_idx] = link_id;

	if (hw->priv == phy)
		mvif->deflink_id = link_id;

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_assign_vif_chanctx);

void mt76_unassign_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf,
			       struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_vif_link *mlink;
	int link_id = link_conf->link_id;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;

	mt76_dbg(dev, MT76_DBG_CHAN, "%s, remove link %u from %d MHz\n",
		 __func__, link_id, conf->def.chan->center_freq);

	if (dev->scan.vif == vif)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = mt76_vif_conf_link(dev, vif, link_conf);
	if (!mlink)
		goto out;

	mlink->ctx = NULL;
	ctx->nbss_assigned--;

out:
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_unassign_vif_chanctx);

int mt76_switch_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif_chanctx_switch *vifs,
			    int n_vifs,
			    enum ieee80211_chanctx_switch_mode mode)
{
	struct mt76_chanctx *old_ctx = (struct mt76_chanctx *)vifs->old_ctx->drv_priv;
	struct mt76_chanctx *new_ctx = (struct mt76_chanctx *)vifs->new_ctx->drv_priv;
	struct ieee80211_chanctx_conf *conf = vifs->new_ctx;
	struct mt76_phy *old_phy = old_ctx->phy;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_link *mlink;
	int i, ret = 0;

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS)
		phy = new_ctx->phy = dev->band_phys[conf->def.chan->band];
	else
		phy = new_ctx->phy;
	if (!phy)
		return -EINVAL;

	if (dev->scan.phy == phy)
		mt76_abort_scan(dev);

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mutex);

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS &&
	    phy != old_phy && old_phy->chanctx == old_ctx)
		old_phy->chanctx = NULL;

	for (i = 0; i < n_vifs; i++) {
		if (vifs[i].old_ctx == vifs[i].new_ctx)
			continue;

		mt76_dbg(dev, MT76_DBG_CHAN,
			 "%s: chan=%d->%d, width=%d->%d, punct_bitmap=0x%04x->0x%04x, link=%u\n",
			 __func__,
			 vifs[i].old_ctx->def.chan->hw_value,
			 vifs[i].new_ctx->def.chan->hw_value,
			 vifs[i].old_ctx->def.width,
			 vifs[i].new_ctx->def.width,
			 vifs[i].old_ctx->def.punctured,
			 vifs[i].new_ctx->def.punctured,
			 vifs[i].link_conf->link_id);

		old_ctx = (struct mt76_chanctx *)vifs[i].old_ctx->drv_priv;
		new_ctx = (struct mt76_chanctx *)vifs[i].new_ctx->drv_priv;
		if (new_ctx->nbss_assigned && phy->chanctx == new_ctx) {
			new_ctx->nbss_assigned++;
			continue;
		}

		new_ctx->phy = phy;
		new_ctx->nbss_assigned++;
		new_ctx->assigned = true;
		new_ctx->chandef = vifs[i].new_ctx->def;
		new_ctx->state = MT76_CHANCTX_STATE_SWITCH;

		if (vifs[i].vif->type == NL80211_IFTYPE_AP)
			new_ctx->has_ap = true;
		else if (vifs[i].vif->type == NL80211_IFTYPE_STATION)
			new_ctx->has_sta = true;
	}

	ret = mt76_phy_update_channel(phy, vifs->new_ctx);
	if (ret)
		goto out;

	if (old_phy == phy)
		goto skip_link_replace;

	for (i = 0; i < n_vifs; i++) {
		mlink = mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;
		dev->drv->vif_link_remove(old_phy, vifs[i].vif,
					  vifs[i].link_conf, mlink);
		ret = dev->drv->vif_link_add(phy, vifs[i].vif,
					     vifs[i].link_conf, mlink);
		if (ret)
			break;
	}

skip_link_replace:
	for (i = 0; i < n_vifs; i++) {
		mlink = mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;

		mlink->ctx = vifs->new_ctx;
	}

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_switch_vif_chanctx);

struct mt76_vif_link *mt76_get_vif_phy_link(struct mt76_phy *phy,
					    struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	struct mt76_dev *dev = phy->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(mvif->link); i++) {
		mlink = mt76_dereference(mvif->link[i], dev);
		if (!mlink)
			continue;

		if (mt76_vif_link_phy(mlink) == phy)
			return mlink;
	}

	if (!dev->drv->vif_link_add)
		return ERR_PTR(-EINVAL);

	mlink = mt76_alloc_mlink(dev, mvif);
	if (!mlink)
		return ERR_PTR(-ENOMEM);

	mlink->offchannel = true;
	ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, mlink);
	if (ret) {
		kfree(mlink);
		return ERR_PTR(ret);
	}
	rcu_assign_pointer(mvif->offchannel_link, mlink);

	return mlink;
}

void mt76_put_vif_phy_link(struct mt76_phy *phy, struct ieee80211_vif *vif,
			   struct mt76_vif_link *mlink)
{
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_data *mvif;

	if (IS_ERR_OR_NULL(mlink) || !mlink->offchannel)
		return;

	mvif = mlink->mvif;

	rcu_assign_pointer(mvif->offchannel_link, NULL);
	dev->drv->vif_link_remove(phy, vif, &vif->bss_conf, mlink);
	kfree(mlink);
}

static void mt76_roc_complete(struct mt76_phy *phy)
{
	struct ieee80211_vif *vif = phy->roc_vif;
	struct mt76_vif_link *mlink = phy->roc_link;
	struct mt76_dev *dev = phy->dev;

	if (!vif)
		return;

	if (mlink)
		mlink->mvif->roc_phy = NULL;
	if (phy->main_chandef.chan) {
		mutex_unlock(&dev->mutex);
		mt76_set_channel(phy, &phy->main_chandef, false);
		mutex_lock(&dev->mutex);
	}

	if (ieee80211_vif_is_mld(phy->roc_vif)) {
		if (mlink && mlink == (struct mt76_vif_link *)vif->drv_priv)
			dev->drv->vif_link_remove(phy, vif, NULL, mlink);
	}

	phy->roc_vif = NULL;
	phy->roc_link = NULL;
	ieee80211_remain_on_channel_expired(phy->hw);

	mt76_dbg(dev, MT76_DBG_CHAN, "finish roc work, go back to freq=%u\n",
		 phy->main_chandef.chan->center_freq);
}

void mt76_roc_complete_work(struct work_struct *work)
{
	struct mt76_phy *phy = container_of(work, struct mt76_phy, roc_work.work);
	struct mt76_dev *dev = phy->dev;

	mutex_lock(&dev->mutex);
	mt76_roc_complete(phy);
	mutex_unlock(&dev->mutex);
}

void mt76_abort_roc(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;

	cancel_delayed_work_sync(&phy->roc_work);

	mutex_lock(&dev->mutex);
	mt76_roc_complete(phy);
	mutex_unlock(&dev->mutex);
}

int mt76_remain_on_channel(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_channel *chan, int duration,
			   enum ieee80211_roc_type type)
{
	struct cfg80211_chan_def chandef = {};
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	int ret = 0;

	phy = dev->band_phys[chan->band];
	if (!phy)
		return -EINVAL;

	mt76_dbg(dev, MT76_DBG_CHAN, "start roc work on freq=%u\n",
		 chan->center_freq);

	mutex_lock(&dev->mutex);

	if (phy->roc_vif || dev->scan.phy == phy) {
		ret = -EBUSY;
		goto out;
	}

	if (!ieee80211_vif_is_mld(vif)) {
		mlink = mt76_vif_link(dev, vif, 0);
		if (!mlink || mlink->band_idx != phy->band_idx) {
			ret = -EINVAL;
			goto out;
		}
	} else {
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
		}

		if (!found) {
			if (vif->type != NL80211_IFTYPE_STATION) {
				ret = -ENOLINK;
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
			ret = dev->drv->vif_link_add(phy, vif, &vif->bss_conf, NULL);
			if (ret)
				goto out;
		}

	}

	mlink->mvif->roc_phy = phy;
	phy->roc_vif = vif;
	phy->roc_link = mlink;
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_HT20);
	mutex_unlock(&dev->mutex);

	mt76_set_channel(phy, &chandef, true);
	ieee80211_ready_on_channel(hw);
	ieee80211_queue_delayed_work(phy->hw, &phy->roc_work,
				     msecs_to_jiffies(duration));
	return 0;

out:
	mutex_unlock(&dev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(mt76_remain_on_channel);

int mt76_cancel_remain_on_channel(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	struct mt76_phy *phy = mvif->roc_phy;

	if (!phy)
		return 0;

	mt76_abort_roc(phy);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76_cancel_remain_on_channel);
