// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/nvmem-consumer.h>
#include <linux/etherdevice.h>
#include "mt76.h"
#include <linux/version.h>

static int mt76_get_of_eeprom_data(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	const void *data;
	int size;

	data = of_get_property(np, "mediatek,eeprom-data", &size);
	if (!data)
		return -ENOENT;

	if (size > len)
		return -EINVAL;

	memcpy(eep, data, size);

	return 0;
}

int mt76_get_of_data_from_mtd(struct mt76_dev *dev, void *eep, int offset, int len)
{
#ifdef CONFIG_MTD
	struct device_node *np = dev->dev->of_node;
	struct mtd_info *mtd;
	const __be32 *list;
	const char *part;
	phandle phandle;
	size_t retlen;
	int size;
	int ret;

	list = of_get_property(np, "mediatek,mtd-eeprom", &size);
	if (!list)
		return -ENOENT;

	phandle = be32_to_cpup(list++);
	if (!phandle)
		return -ENOENT;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return -EINVAL;

	part = of_get_property(np, "label", NULL);
	if (!part)
		part = np->name;

	mtd = get_mtd_device_nm(part);
	if (IS_ERR(mtd)) {
		ret =  PTR_ERR(mtd);
		goto out_put_node;
	}

	if (size <= sizeof(*list)) {
		ret = -EINVAL;
		goto out_put_node;
	}

	offset += be32_to_cpup(list);
	ret = mtd_read(mtd, offset, len, &retlen, eep);
	put_mtd_device(mtd);
	if (mtd_is_bitflip(ret))
		ret = 0;
	if (ret) {
		dev_err(dev->dev, "reading EEPROM from mtd %s failed: %i\n",
			part, ret);
		goto out_put_node;
	}

	if (retlen < len) {
		ret = -EINVAL;
		goto out_put_node;
	}

	if (of_property_read_bool(dev->dev->of_node, "big-endian")) {
		u8 *data = (u8 *)eep;
		int i;

		/* convert eeprom data in Little Endian */
		for (i = 0; i < round_down(len, 2); i += 2)
			put_unaligned_le16(get_unaligned_be16(&data[i]),
					   &data[i]);
	}

#ifdef CONFIG_NL80211_TESTMODE
	dev->test_mtd.name = devm_kstrdup(dev->dev, part, GFP_KERNEL);
	if (!dev->test_mtd.name) {
		ret = -ENOMEM;
		goto out_put_node;
	}
	dev->test_mtd.offset = offset;
#endif

out_put_node:
	of_node_put(np);
	return ret;
#else
	return -ENOENT;
#endif
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_mtd);

int mt76_get_of_data_from_nvmem(struct mt76_dev *dev, void *eep,
				const char *cell_name, int len)
{
	struct device_node *np = dev->dev->of_node;
	struct nvmem_cell *cell;
	const void *data;
	size_t retlen;
	int ret = 0;

	cell = of_nvmem_cell_get(np, cell_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	data = nvmem_cell_read(cell, &retlen);
	nvmem_cell_put(cell);

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (retlen < len) {
		ret = -EINVAL;
		goto exit;
	}

	memcpy(eep, data, len);

exit:
	kfree(data);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_nvmem);

static int _mt76_get_of_data_from_file(struct mt76_dev *dev, void *eep, u32 offset, int len,
				const char* fname)
{
#if defined(CONFIG_OF)
	int ret = 0;
	int retlen;

	char path[96];
	struct file *fp;
	loff_t pos = 0;
	struct inode *inode = NULL;
	loff_t f_size;

	mtk_dbg(dev, CFG, "Attempting to load eeprom %s\n", fname);
	retlen = snprintf(path, sizeof(path), fname);
	if (retlen < 0) {
		mtk_dbg(dev, CFG, "ERROR:  Could not find eeprom file-name %s\n", fname);
		return -EINVAL;
	}

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		dev_warn(dev->dev, "open eeprom file failed: %s\n", path);
		return -ENOENT;
	}

	inode = file_inode(fp);
	if ((!S_ISREG(inode->i_mode) && !S_ISBLK(inode->i_mode))) {
		dev_warn(dev->dev, "invalid eeprom file type: %s\n", path);
		ret = -ENOENT;
		goto out_put_node;
	}

	f_size = i_size_read(inode->i_mapping->host);
	if (f_size < 0)
	{
		dev_warn(dev->dev, "failed getting eeprom size of %s size:%lld \n", path, f_size);
		ret = -ENOENT;
		goto out_put_node;
	}

	pos = offset;

	mtk_dbg(dev, CFG, "reading eeprom file: len %d, pos %lld \n", len, pos);
	retlen = kernel_read(fp, eep, len, &pos);
	if (retlen != len) {
		ret = -EINVAL;
		dev_warn(dev->dev, "load eeprom ERROR, count %d byte (len:%d)\n", ret, len);
		goto out_put_node;
	}

	if (of_property_read_bool(dev->dev->of_node, "big-endian")) {
		int i;
		u8 *data = (u8 *)eep;

		/* convert eeprom data in Little Endian */
		for (i = 0; i < round_down(len, 2); i += 2)
			put_unaligned_le16(get_unaligned_be16(&data[i]),
					   &data[i]);
	}

	mtk_dbg(dev, CFG, "load eeprom from %s OK, count %d, pos %lld ret: %d\n",
		path, retlen, pos, ret);

out_put_node:
	filp_close(fp, 0);
	return ret;
#else
	return -ENOENT;
#endif
}

int mt76_get_of_data_from_file(struct mt76_dev *dev, void *eep, u32 offset,
			       int len)
{
	char buf[96];
	int ret;

	// Try with bus-specific FW file first
	snprintf(buf, sizeof(buf), "/lib/firmware/mediatek/rf-%s.bin",
		 dev_name(dev->dev));
	ret = _mt76_get_of_data_from_file(dev, eep, offset, len, buf);
	if (ret >= 0)
		return ret;

	return _mt76_get_of_data_from_file(dev, eep, offset, len, "/lib/firmware/mediatek/rf.bin");
}
EXPORT_SYMBOL_GPL(mt76_get_of_data_from_file);

static int mt76_get_of_eeprom(struct mt76_dev *dev, void *eep, int len)
{
	struct device_node *np = dev->dev->of_node;
	int ret;

	if (!np)
		return -ENOENT;

	ret = mt76_get_of_eeprom_data(dev, eep, len);
	if (!ret)
		return 0;

	ret = mt76_get_of_data_from_mtd(dev, eep, 0, len);
	if (!ret)
		return 0;

	return mt76_get_of_data_from_nvmem(dev, eep, "eeprom", len);
}

void
mt76_eeprom_override(struct mt76_phy *phy)
{
	struct mt76_dev *dev = phy->dev;
	struct device_node *np = dev->dev->of_node, *band_np;
	bool found_mac = false;
	u32 reg;
	int ret;

	for_each_child_of_node(np, band_np) {
		ret = of_property_read_u32(band_np, "reg", &reg);
		if (ret)
			continue;

		if (reg == phy->band_idx) {
			found_mac = !of_get_mac_address(band_np, phy->macaddr);
			of_node_put(band_np);
			break;
		}
	}

	if (!found_mac)
		of_get_mac_address(np, phy->macaddr);

	if (!is_valid_ether_addr(phy->macaddr)) {
		eth_random_addr(phy->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 phy->macaddr);
	}
}
EXPORT_SYMBOL_GPL(mt76_eeprom_override);

static bool mt76_string_prop_find(struct property *prop, const char *str)
{
	const char *cp = NULL;

	if (!prop || !str || !str[0])
		return false;

	while ((cp = of_prop_next_string(prop, cp)) != NULL)
		if (!strcasecmp(cp, str))
			return true;

	return false;
}

struct device_node *
mt76_find_power_limits_node(struct mt76_dev *dev)
{
	struct device_node *np = dev->dev->of_node;
	const char *const region_names[] = {
		[NL80211_DFS_UNSET] = "ww",
		[NL80211_DFS_ETSI] = "etsi",
		[NL80211_DFS_FCC] = "fcc",
		[NL80211_DFS_JP] = "jp",
	};
	struct device_node *cur, *fallback = NULL;
	const char *region_name = NULL;

	if (dev->region < ARRAY_SIZE(region_names))
		region_name = region_names[dev->region];

	np = of_get_child_by_name(np, "power-limits");
	if (!np)
		return NULL;

	for_each_child_of_node(np, cur) {
		struct property *country = of_find_property(cur, "country", NULL);
		struct property *regd = of_find_property(cur, "regdomain", NULL);

		if (!country && !regd) {
			fallback = cur;
			continue;
		}

		if (mt76_string_prop_find(country, dev->alpha2) ||
		    mt76_string_prop_find(regd, region_name)) {
			of_node_put(np);
			return cur;
		}
	}

	of_node_put(np);
	return fallback;
}
EXPORT_SYMBOL_GPL(mt76_find_power_limits_node);

static const __be32 *
mt76_get_of_array(struct device_node *np, char *name, size_t *len, int min)
{
	struct property *prop = of_find_property(np, name, NULL);

	if (!prop || !prop->value || prop->length < min * 4)
		return NULL;

	*len = prop->length;

	return prop->value;
}

struct device_node *
mt76_find_channel_node(struct device_node *np, struct ieee80211_channel *chan)
{
	struct device_node *cur;
	const __be32 *val;
	size_t len;

	for_each_child_of_node(np, cur) {
		val = mt76_get_of_array(cur, "channels", &len, 2);
		if (!val)
			continue;

		while (len >= 2 * sizeof(*val)) {
			if (chan->hw_value >= be32_to_cpu(val[0]) &&
			    chan->hw_value <= be32_to_cpu(val[1]))
				return cur;

			val += 2;
			len -= 2 * sizeof(*val);
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(mt76_find_channel_node);


static s8
mt76_get_txs_delta(struct device_node *np, u8 nss)
{
	const __be32 *val;
	size_t len;

	val = mt76_get_of_array(np, "txs-delta", &len, nss);
	if (!val)
		return 0;

	return be32_to_cpu(val[nss - 1]);
}

static void
mt76_apply_array_limit(s8 *pwr, size_t pwr_len, const __be32 *data,
		       s8 target_power, s8 nss_delta, s8 *max_power)
{
	int i;

	if (!data)
		return;

	for (i = 0; i < pwr_len; i++) {
		pwr[i] = min_t(s8, target_power,
			       be32_to_cpu(data[i]) + nss_delta);
		*max_power = max(*max_power, pwr[i]);
	}
}

static void
mt76_apply_multi_array_limit(s8 *pwr, size_t pwr_len, s8 pwr_num,
			     const __be32 *data, size_t len, s8 target_power,
			     s8 nss_delta)
{
	int i, cur;
	s8 max_power = -128;

	if (!data)
		return;

	len /= 4;
	cur = be32_to_cpu(data[0]);
	for (i = 0; i < pwr_num; i++) {
		if (len < pwr_len + 1)
			break;

		mt76_apply_array_limit(pwr + pwr_len * i, pwr_len, data + 1,
				       target_power, nss_delta, &max_power);
		if (--cur > 0)
			continue;

		data += pwr_len + 1;
		len -= pwr_len + 1;
		if (!len)
			break;

		cur = be32_to_cpu(data[0]);
	}
}

s8 mt76_get_rate_power_limits(struct mt76_phy *phy,
			      struct ieee80211_channel *chan,
			      struct mt76_power_limits *dest,
			      struct mt76_power_path_limits *dest_path,
			      s8 target_power)
{
	struct mt76_dev *dev = phy->dev;
	//struct txpower_phy_rate_info *txp;
	struct device_node *np;
	const __be32 *val;
	char name[16];
	u32 mcs_rates = dev->drv->mcs_rates;
	char band;
	size_t len;
	s8 max_power = -127;
	s8 max_power_backoff = -127;
	s8 txs_delta;
	int n_chains = hweight16(phy->chainmask);
	s8 target_power_combine = target_power + mt76_tx_power_path_delta(n_chains);

	if (!mcs_rates)
		mcs_rates = 12;

	memset(dest, target_power, sizeof(*dest));
	if (dest_path != NULL)
		memset(dest_path, 0, sizeof(*dest_path));

	if (!IS_ENABLED(CONFIG_OF))
		goto try_sku;

	np = mt76_find_power_limits_node(dev);
	if (!np)
		goto try_sku;

	switch (chan->band) {
	case NL80211_BAND_2GHZ:
		band = '2';
		break;
	case NL80211_BAND_5GHZ:
		band = '5';
		break;
	case NL80211_BAND_6GHZ:
		band = '6';
		break;
	default:
		return target_power;
	}

	snprintf(name, sizeof(name), "txpower-%cg", band);
	np = of_get_child_by_name(np, name);
	if (!np) {
		mtk_dbg(dev, CFG, "get-rate-power-limits:  Could not find band node: %s\n",
			name);
		return target_power;
	}

	np = mt76_find_channel_node(np, chan);
	if (!np) {
		mtk_dbg(dev, CFG, "get-rate-power-limits:  Could not find chan node\n");
		return target_power;
	}

	txs_delta = mt76_get_txs_delta(np, hweight16(phy->chainmask));

	val = mt76_get_of_array(np, "rates-cck", &len, ARRAY_SIZE(dest->cck));
	mt76_apply_array_limit(dest->cck, ARRAY_SIZE(dest->cck), val,
			       target_power, txs_delta, &max_power);

	val = mt76_get_of_array(np, "rates-ofdm",
				&len, ARRAY_SIZE(dest->ofdm));
	mt76_apply_array_limit(dest->ofdm, ARRAY_SIZE(dest->ofdm), val,
			       target_power, txs_delta, &max_power);

	val = mt76_get_of_array(np, "rates-mcs", &len, mcs_rates + 1);
	mt76_apply_multi_array_limit(dest->mcs[0], ARRAY_SIZE(dest->mcs[0]),
				     ARRAY_SIZE(dest->mcs), val, len,
				     target_power, txs_delta);

	val = mt76_get_of_array(np, "rates-ru", &len, ARRAY_SIZE(dest->ru[0]) + 1);
	mt76_apply_multi_array_limit(dest->ru[0], ARRAY_SIZE(dest->ru[0]),
				     ARRAY_SIZE(dest->ru), val, len,
				     target_power, txs_delta);

	val = mt76_get_of_array(np, "rates-eht", &len, ARRAY_SIZE(dest->eht[0]) + 1);
	mt76_apply_multi_array_limit(dest->eht[0], ARRAY_SIZE(dest->eht[0]),
				     ARRAY_SIZE(dest->eht), val, len,
				     target_power, txs_delta);

	if (dest_path == NULL)
		return max_power;

	max_power_backoff = max_power;

	val = mt76_get_of_array(np, "paths-cck", &len, ARRAY_SIZE(dest_path->cck));
	mt76_apply_array_limit(dest_path->cck, ARRAY_SIZE(dest_path->cck), val,
			       target_power_combine, txs_delta, &max_power_backoff);

	val = mt76_get_of_array(np, "paths-ofdm", &len, ARRAY_SIZE(dest_path->ofdm));
	mt76_apply_array_limit(dest_path->ofdm, ARRAY_SIZE(dest_path->ofdm), val,
			       target_power_combine, txs_delta, &max_power_backoff);

	val = mt76_get_of_array(np, "paths-ofdm-bf", &len, ARRAY_SIZE(dest_path->ofdm_bf));
	mt76_apply_array_limit(dest_path->ofdm_bf, ARRAY_SIZE(dest_path->ofdm_bf), val,
			       target_power_combine, txs_delta, &max_power_backoff);

	val = mt76_get_of_array(np, "paths-ru", &len, ARRAY_SIZE(dest_path->ru[0]) + 1);
	mt76_apply_multi_array_limit(dest_path->ru[0], ARRAY_SIZE(dest_path->ru[0]),
				     ARRAY_SIZE(dest_path->ru), val, len,
				     target_power_combine, txs_delta);

	val = mt76_get_of_array(np, "paths-ru-bf", &len, ARRAY_SIZE(dest_path->ru_bf[0]) + 1);
	mt76_apply_multi_array_limit(dest_path->ru_bf[0], ARRAY_SIZE(dest_path->ru_bf[0]),
				     ARRAY_SIZE(dest_path->ru_bf), val, len,
				     target_power_combine, txs_delta);

	return max_power;

try_sku:
	max_power = target_power;
#if 0
	dev_info(mdev->mt76.dev, "try sku, default_txpower: %p  max-power: %d band-idx: %d\n",
		 phy96->default_txpower, max_power, band_idx);
	// TODO:  Calculate default_txpower by taking max txpower plus eeprom offsets
	// into account.  Always NULL at the moment.
	if (!phy96->default_txpower)
		return max_power;

	//txp = &phy96->default_txpower->phy_rate_info;

	// TODO:  Adjust requested txpower to negate the offsets in order to get
	// as close to requested txpower as possible in cases where offsets are decreasing
	// txpower from maximum.
#endif

	return max_power;
}
EXPORT_SYMBOL_GPL(mt76_get_rate_power_limits);

int
mt76_eeprom_init(struct mt76_dev *dev, int len)
{
#if defined(CONFIG_OF)
	u32 offset;
	struct device_node *np = dev->dev->of_node;
#endif

	dev->eeprom.size = len;
	dev->eeprom.data = devm_kzalloc(dev->dev, len, GFP_KERNEL);
	if (!dev->eeprom.data)
		return -ENOMEM;

#if defined(CONFIG_OF)
	if (np && of_property_read_u32(np, "mediatek,eeprom-file-offset", &offset) == 0) {
		return !mt76_get_of_data_from_file(dev, dev->eeprom.data, offset, len);
	} else {
		int rv;

		offset = 0;
		rv = mt76_get_of_data_from_file(dev, dev->eeprom.data, offset, len);

		if (!rv)
			return !rv;
	}
#endif

	return !mt76_get_of_eeprom(dev, dev->eeprom.data, len);
}
EXPORT_SYMBOL_GPL(mt76_eeprom_init);
