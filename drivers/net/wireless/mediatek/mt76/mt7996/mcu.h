/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef __MT7996_MCU_H
#define __MT7996_MCU_H

#include "../mt76_connac_mcu.h"

struct txpower_basic_info {
	u8 category;
	u8 rsv1;

	/* basic info */
	u8 band_idx;
	u8 band;

	/* board type info */
	bool is_epa;
	bool is_elna;

	/* power percentage info */
	bool percentage_ctrl_enable;
	s8 power_drop_level;

	/* frond-end loss TX info */
	s8 front_end_loss_tx[4];

	/* frond-end loss RX info */
	s8 front_end_loss_rx[4];

	/* thermal info */
	bool thermal_compensate_enable;
	s8 thermal_compensate_value;
	u8 rsv2;

	/* TX power max/min limit info */
	s8 max_power_bound;
	s8 min_power_bound;

	/* power limit info */
	bool sku_enable;
	bool bf_backoff_enable;

	/* MU TX power info */
	bool mu_tx_power_manual_enable;
	s8 mu_tx_power_auto;
	s8 mu_tx_power_manual;
	u8 rsv3;
};

struct txpower_phy_rate_info {
	u8 category;
	u8 band_idx;
	u8 band;
	u8 epa_gain;

	/* rate power info [dBm] */
	s8 frame_power[MT7996_SKU_RATE_NUM][__MT_MAX_BAND];

	/* TX power max/min limit info */
	s8 max_power_bound;
	s8 min_power_bound;
	u8 rsv1;
};

struct txpower_backoff_table_info {
	u8 category;
	u8 band_idx;
	u8 band;
	u8 backoff_en;

	s8 frame_power[MT7996_SKU_PATH_NUM];
	u8 rsv[3];
};

struct mt7996_mcu_txpower_event {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;

	union {
		struct txpower_basic_info basic_info;
		struct txpower_phy_rate_info phy_rate_info;
		struct txpower_backoff_table_info backoff_table_info;
	};
};

enum txpower_category {
	BASIC_INFO,
	BACKOFF_TABLE_INFO,
	PHY_RATE_INFO,
};

enum txpower_event {
	UNI_TXPOWER_BASIC_INFO = 0,
	UNI_TXPOWER_BACKOFF_TABLE_SHOW_INFO = 3,
	UNI_TXPOWER_PHY_RATE_INFO = 5,
};

struct mt7996_mcu_rxd {
	__le32 rxd[8];

	__le16 len;
	__le16 pkt_type_id;

	u8 eid;
	u8 seq;
	u8 option;
	u8 __rsv;

	u8 ext_eid;
	u8 __rsv1[2];
	u8 s2d_index;
};

enum {
	UNI_MEC_READ_INFO = 0,
	UNI_MEC_AMSDU_ALGO_EN_STA,
	UNI_MEC_AMSDU_PARA_STA,
	UNI_MEC_AMSDU_ALGO_THRESHOLD,
	UNI_MEC_IFAC_SPEED,
};

enum {
	UNI_EVENT_SR_CFG_SR_ENABLE = 0x1,
	UNI_EVENT_SR_SW_SD = 0x83,
	UNI_EVENT_SR_HW_IND = 0xC9,
	UNI_EVENT_SR_HW_ESR_ENABLE = 0xD8,
};
enum {
	UNI_CMD_SR_CFG_SR_ENABLE = 0x1,
	UNI_CMD_SR_SW_SD = 0x84,
	UNI_CMD_SR_HW_IND = 0xCB,
	UNI_CMD_SR_HW_ENHANCE_SR_ENABLE = 0xDA,
};

struct mt7996_mcu_sr_basic_event {
	struct mt7996_mcu_rxd rxd;

	u8 band_idx;
	u8 _rsv[3];

	__le16 tag;
	__le16 len;
};

struct sr_sd_tlv {
	u8 _rsv[16];
	__le32 sr_tx_airtime;
	__le32 obss_airtime;
	__le32 my_tx_airtime;
	__le32 my_rx_airtime;
	__le32 channel_busy_time;
	__le32 total_airtime;
	__le32 total_airtime_ratio;
	__le32 obss_airtime_ratio;
	u8 rule;
	u8 _rsv2[59];
} __packed;

struct mt7996_mcu_sr_swsd_event {
	struct mt7996_mcu_sr_basic_event basic;
	struct sr_sd_tlv tlv[3];
} __packed;

struct mt7996_mcu_sr_common_event {
	struct mt7996_mcu_sr_basic_event basic;
	__le32 value;
};

struct mt7996_mcu_sr_hw_ind_event {
	struct mt7996_mcu_sr_basic_event basic;
	__le16 non_srg_valid_cnt;
	u8 _rsv[4];
	__le16 inter_bss_ppdu_cnt;
	u8 _rsv2[4];
	__le32 sr_ampdu_mpdu_cnt;
	__le32 sr_ampdu_mpdu_acked_cnt;
};

struct mt7996_mcu_uni_event {
	u8 cid;
	u8 __rsv[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

struct mt7996_mcu_thermal_ctrl {
	u8 ctrl_id;
	u8 band_idx;
	union {
		struct {
			u8 protect_type; /* 1: duty admit, 2: radio off */
			u8 trigger_type; /* 0: low, 1: high */
		} __packed type;
		struct {
			u8 duty_level;	/* level 0~3 */
			u8 duty_cycle;
		} __packed duty;
	};
} __packed;

struct mt7996_mcu_thermal_enable {
	__le32 trigger_temp;
	__le32 restore_temp;
	__le16 sustain_time;
	u8 rsv[2];
} __packed;

struct mt7996_mcu_csa_notify {
	struct mt7996_mcu_rxd rxd;

	u8 omac_idx;
	u8 csa_count;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7996_mcu_rdd_report {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;

	u8 rdd_idx;
	u8 long_detected;
	u8 constant_prf_detected;
	u8 staggered_prf_detected;
	u8 radar_type_idx;
	u8 periodic_pulse_num;
	u8 long_pulse_num;
	u8 hw_pulse_num;

	u8 out_lpn;
	u8 out_spn;
	u8 out_crpn;
	u8 out_crpw;
	u8 out_crbn;
	u8 out_stgpn;
	u8 out_stgpw;

	u8 __rsv2;

	__le32 out_pri_const;
	__le32 out_pri_stg[3];
	__le32 out_pri_stg_dmin;

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} long_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} periodic_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 sc_pass;
		u8 sw_reset;
		u8 mdrdy_flag;
		u8 tx_active;
	} hw_pulse[32];
} __packed;

struct mt7996_rdd_ctrl {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;

	u8 ctrl;
	u8 rdd_idx;
	u8 rdd_rx_sel;
	u8 val;
	u8 disable_timer;
	u8 rsv[3];
} __packed;

struct mt7996_mcu_background_chain_ctrl {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;

	u8 chan;		/* primary channel */
	u8 central_chan;	/* central channel */
	u8 bw;
	u8 tx_stream;
	u8 rx_stream;

	u8 monitor_chan;	/* monitor channel */
	u8 monitor_central_chan;/* monitor central channel */
	u8 monitor_bw;
	u8 monitor_tx_stream;
	u8 monitor_rx_stream;

	u8 scan_mode;		/* 0: ScanStop
				 * 1: ScanStart
				 * 2: ScanRunning
				 */
	u8 band_idx;		/* DBDC */
	u8 monitor_scan_type;
	u8 band;		/* 0: 2.4GHz, 1: 5GHz */
	u8 rsv[2];
} __packed;

struct mt7996_mcu_eeprom {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;
	u8 buffer_mode;
	u8 format;
	__le16 buf_len;
} __packed;

struct mt7996_mcu_eeprom_info {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;
	__le32 addr;
	__le32 valid;
	u8 data[MT7996_EEPROM_BLOCK_SIZE];
} __packed;

struct mt7996_mcu_phy_rx_info {
	u8 category;
	u8 rate;
	u8 mode;
	u8 nsts;
	u8 gi;
	u8 coding;
	u8 stbc;
	u8 bw;
};

struct mt7996_mcu_mib {
	__le16 tag;
	__le16 len;
	__le32 offs;
	__le64 data;
} __packed;

struct per_sta_rssi {
	__le16 wlan_idx;
	u8 __rsv[2];
	u8 rcpi[4];
} __packed;

struct per_sta_snr {
	__le16 wlan_idx;
	u8 __rsv[2];
	s8 val[IEEE80211_MAX_CHAINS];
} __packed;

struct per_sta_msdu_cnt {
	__le16 wlan_idx;
	u8 __rsv[2];
	__le32 tx_drops;
	__le32 tx_retries;
} __packed;

struct mt7996_mcu_per_sta_info_event {
	u8 __rsv[4];

	__le16 tag;
	__le16 len;

	union {
		struct per_sta_rssi rssi[0];
		struct per_sta_snr snr[0];
		struct per_sta_msdu_cnt msdu_cnt[0];
	};
} __packed;

struct all_sta_trx_rate {
	__le16 wlan_idx;
	u8 __rsv1[2];
	u8 tx_mode;
	u8 flags;
	u8 tx_stbc;
	u8 tx_gi;
	u8 tx_bw;
	u8 tx_ldpc;
	u8 tx_mcs;
	u8 tx_nss;
	u8 rx_rate;
	u8 rx_mode;
	u8 rx_nsts;
	u8 rx_gi;
	u8 rx_coding;
	u8 rx_stbc;
	u8 rx_bw;
	u8 __rsv2;
} __packed;

#define UNI_EVENT_SIZE_ADM_STAT_V1	1452

struct mt7996_mcu_all_sta_info_event {
	u8 rsv[4];
	__le16 tag;
	__le16 len;
	u8 more;
	u8 rsv2;
	__le16 sta_num;
	u8 rsv3[4];

	union {
		DECLARE_FLEX_ARRAY(struct all_sta_trx_rate, rate);
		DECLARE_FLEX_ARRAY(struct {
			__le16 wlan_idx;
			u8 rsv[2];
			__le32 tx_bytes[IEEE80211_NUM_ACS];
			__le32 rx_bytes[IEEE80211_NUM_ACS];
		} __packed, adm_stat_v1);

		DECLARE_FLEX_ARRAY(struct {
			__le16 wlan_idx;
			u8 rsv[2];
			__le32 tx_bytes[IEEE80211_NUM_ACS];
			__le32 rx_bytes[IEEE80211_NUM_ACS];
			__le32 tx_bytes_failed[IEEE80211_NUM_ACS];
		} __packed, adm_stat_v2);

		DECLARE_FLEX_ARRAY(struct {
			__le16 wlan_idx;
			u8 rsv[2];
			__le32 tx_msdu_cnt;
			__le32 rx_msdu_cnt;
		} __packed, msdu_cnt);

		DECLARE_FLEX_ARRAY(struct {
			__le16 wlan_idx;
			u8 rsv[2];
			__le32 tx[IEEE80211_NUM_ACS];
			__le32 rx[IEEE80211_NUM_ACS];
		} __packed, airtime);
	} __packed;
} __packed;

struct mt7996_mcu_wed_rro_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;
} __packed;

struct mt7996_mcu_wed_rro_ba_event {
	__le16 tag;
	__le16 len;

	__le16 wlan_id;
	u8 tid;
	u8 __rsv1;
	__le32 status;
	__le16 id;
	u8 __rsv2[2];
} __packed;

struct mt7996_mcu_wed_rro_ba_delete_event {
	__le16 tag;
	__le16 len;

	__le16 session_id;
	__le16 mld_id;
	u8 tid;
	u8 __rsv[3];
} __packed;

enum  {
	UNI_WED_RRO_BA_SESSION_STATUS,
	UNI_WED_RRO_BA_SESSION_TBL,
	UNI_WED_RRO_BA_SESSION_DELETE,
};

struct mt7996_mcu_thermal_notify {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;

	u8 event_id;
	u8 band_idx;
	u8 level_idx;
	u8 duty_percent;
	__le32 restore_temp;
	u8 __rsv2[4];
} __packed;

enum mt7996_chan_mib_offs {
	UNI_MIB_OBSS_AIRTIME = 26,
	UNI_MIB_NON_WIFI_TIME = 27,
	UNI_MIB_TX_TIME = 28,
	UNI_MIB_RX_TIME = 29
};

struct edca {
	__le16 tag;
	__le16 len;

	u8 queue;
	u8 set;
	u8 cw_min;
	u8 cw_max;
	__le16 txop;
	u8 aifs;
	u8 __rsv;
};

#define MCU_PQ_ID(p, q)			(((p) << 15) | ((q) << 10))
#define MCU_PKT_ID			0xa0

enum {
	MCU_FW_LOG_WM,
	MCU_FW_LOG_WA,
	MCU_FW_LOG_TO_HOST,
	MCU_FW_LOG_RELAY = 16,
	MCU_FW_LOG_RELAY_IDX = 40
};

struct bin_debug_hdr {
	__le32 magic_num;
	__le16 serial_id;
	__le16 msg_type;
	__le16 len;
	__le16 des_len;	/* descriptor len for rxd */
} __packed;

enum {
	MCU_TWT_AGRT_ADD,
	MCU_TWT_AGRT_MODIFY,
	MCU_TWT_AGRT_DELETE,
	MCU_TWT_AGRT_TEARDOWN,
	MCU_TWT_AGRT_GET_TSF,
};

enum {
	MCU_WA_PARAM_CMD_QUERY,
	MCU_WA_PARAM_CMD_SET,
	MCU_WA_PARAM_CMD_CAPABILITY,
	MCU_WA_PARAM_CMD_DEBUG,
};

#define BSS_ACQ_PKT_CNT_BSS_NUM		24
#define BSS_ACQ_PKT_CNT_BSS_BITMAP_ALL	0x00ffffff
#define BSS_ACQ_PKT_CNT_READ_CLR	BIT(31)
#define WMM_PKT_THRESHOLD		100

struct mt7996_mcu_bss_acq_pkt_cnt_event {
	struct mt7996_mcu_rxd rxd;

	__le32 bss_bitmap;
	struct {
		__le32 cnt[IEEE80211_NUM_ACS];
	} __packed bss[BSS_ACQ_PKT_CNT_BSS_NUM];
} __packed;

enum {
	MCU_WA_PARAM_PDMA_RX = 0x04,
	MCU_WA_PARAM_CPU_UTIL = 0x0b,
	MCU_WA_PARAM_RED_EN = 0x0e,
	MCU_WA_PARAM_BSS_ACQ_PKT_CNT = 0x12,
	MCU_WA_PARAM_HW_PATH_HIF_VER = 0x2f,
	MCU_WA_PARAM_RED_CONFIG = 0x40,
};

enum mcu_mmps_mode {
	MCU_MMPS_STATIC,
	MCU_MMPS_DYNAMIC,
	MCU_MMPS_RSV,
	MCU_MMPS_DISABLE,
};

struct bss_rate_tlv {
	__le16 tag;
	__le16 len;
	u8 __rsv1[4];
	__le16 bc_trans;
	__le16 mc_trans;
	u8 short_preamble;
	u8 bc_fixed_rate;
	u8 mc_fixed_rate;
	u8 __rsv2[9];
} __packed;

enum {
	BP_DISABLE,
	BP_SW_MODE,
	BP_HW_MODE,
};

struct bss_ra_tlv {
	__le16 tag;
	__le16 len;
	u8 short_preamble;
	u8 force_sgi;
	u8 force_gf;
	u8 ht_mode;
	u8 se_off;
	u8 antenna_idx;
	__le16 max_phyrate;
	u8 force_tx_streams;
	u8 __rsv[3];
} __packed;

struct bss_rlm_tlv {
	__le16 tag;
	__le16 len;
	u8 control_channel;
	u8 center_chan;
	u8 center_chan2;
	u8 bw;
	u8 tx_streams;
	u8 rx_streams;
	u8 ht_op_info;
	u8 sco;
	u8 band;
	u8 __rsv[3];
} __packed;

struct bss_color_tlv {
	__le16 tag;
	__le16 len;
	u8 enable;
	u8 color;
	u8 rsv[2];
} __packed;

struct bss_inband_discovery_tlv {
	__le16 tag;
	__le16 len;
	u8 tx_type;
	u8 tx_mode;
	u8 tx_interval;
	u8 enable;
	__le16 wcid;
	__le16 prob_rsp_len;
} __packed;

struct bss_bcn_content_tlv {
	__le16 tag;
	__le16 len;
	__le16 tim_ie_pos;
	__le16 csa_ie_pos;
	__le16 bcc_ie_pos;
	u8 enable;
	u8 type;
	__le16 pkt_len;
} __packed;

struct bss_bcn_cntdwn_tlv {
	__le16 tag;
	__le16 len;
	u8 cnt;
	u8 rsv[3];
} __packed;

struct bss_bcn_mbss_tlv {
	__le16 tag;
	__le16 len;
	__le32 bitmap;
#define MAX_BEACON_NUM	32
	__le16 offset[MAX_BEACON_NUM];
} __packed __aligned(4);

struct bss_bcn_crit_update_tlv {
	__le16 tag;
	__le16 len;
	__le32 bss_bitmap;
	/* Bypass the beacon sequence handling in firmware for the
	 * BSSes in the bitmap. If the flag is set for a BSS, then the
	 * firmware will not set the beacon of the BSS in sequence.
	 */
	__le32 bypass_seq_bitmap;
	__le16 tim_ie_pos[32];
	__le16 cap_info_ie_pos[32];
	bool require_event;
	u8 rsv[3];
} __packed;

struct bss_bcn_sta_prof_cntdwn_tlv {
	__le16 tag;
	__le16 len;
	__le16 sta_prof_csa_offs[__MT_MAX_BAND];
	u8 cs_bss_idx[__MT_MAX_BAND];
	u8 pkt_content[3];
} __packed;

struct bss_bcn_ml_reconf_tlv {
	__le16 tag;
	__le16 len;
	u8 reconf_count;
	u8 rsv[3];
	u8 offset[];
} __packed;

struct bss_bcn_ml_reconf_offset {
	__le16 ap_removal_timer_offs;
	u8 bss_idx;
	u8 rsv;
} __packed;

struct bss_bcn_attlm_offset_tlv {
	__le16 tag;
	__le16 len;
	u8 valid_id_bitmap;
	u8 rsv;
	__le16 offset;
} __packed;

struct bss_txcmd_tlv {
	__le16 tag;
	__le16 len;
	u8 txcmd_mode;
	u8 __rsv[3];
} __packed;

struct bss_sec_tlv {
	__le16 tag;
	__le16 len;
	u8 __rsv1[2];
	u8 cipher;
	u8 __rsv2[1];
} __packed;

struct bss_ifs_time_tlv {
	__le16 tag;
	__le16 len;
	u8 slot_valid;
	u8 sifs_valid;
	u8 rifs_valid;
	u8 eifs_valid;
	__le16 slot_time;
	__le16 sifs_time;
	__le16 rifs_time;
	__le16 eifs_time;
	u8 eifs_cck_valid;
	u8 rsv;
	__le16 eifs_cck_time;
} __packed;

struct bss_power_save {
	__le16 tag;
	__le16 len;
	u8 profile;
	u8 _rsv[3];
} __packed;

struct bss_mld_tlv {
	__le16 tag;
	__le16 len;
	u8 group_mld_id;
	u8 own_mld_id;
	u8 mac_addr[ETH_ALEN];
	u8 remap_idx;
	u8 link_id;
	u8 __rsv[2];
} __packed;

struct bss_mld_link_op_tlv {
	__le16 tag;
	__le16 len;
	u8 group_mld_id;
	u8 own_mld_id;
	u8 mac_addr[ETH_ALEN];
	u8 remap_idx;
	u8 link_operation;
	u8 link_id;
	u8 rsv[2];
} __packed;

struct sta_rec_ht_uni {
	__le16 tag;
	__le16 len;
	__le16 ht_cap;
	__le16 ht_cap_ext;
	u8 ampdu_param;
	u8 _rsv[3];
} __packed;

struct bssinfo_force_cmd_tlv {
	__le16 tag;
	__le16 len;
	u8 own_mld_id;
	u8 own_mld_addr[ETH_ALEN];
	u8 __rsv[1]; /* Actually a one element array, not a flexible array member. */
} __packed;

struct sta_rec_muar_idx {
	__le16 tag;
	__le16 len;
	u8 muar_idx;
	u8 __rsv[3];
} __packed;

struct sta_rec_ba_uni {
	__le16 tag;
	__le16 len;
	u8 tid;
	u8 ba_type;
	u8 amsdu;
	u8 ba_en;
	__le16 ssn;
	__le16 winsize;
	u8 ba_rdd_rro;
	u8 __rsv[3];
} __packed;

struct sta_rec_tx_cap {
	__le16 tag;
	__le16 len;
	u8 ampdu_limit_en;
	u8 rsv[3];
} __packed;

struct sta_rec_eht {
	__le16 tag;
	__le16 len;
	u8 tid_bitmap;
	u8 _rsv;
	__le16 mac_cap;
	__le64 phy_cap;
	__le64 phy_cap_ext;
	u8 mcs_map_bw20[4];
	u8 mcs_map_bw80[3];
	u8 mcs_map_bw160[3];
	u8 mcs_map_bw320[3];
	u8 _rsv2[3];
} __packed;

struct sec_key_uni {
	__le16 wlan_idx;
	u8 mgmt_prot;
	u8 cipher_id;
	u8 cipher_len;
	u8 key_id;
	u8 key_len;
	u8 need_resp;
	u8 key[32];
	u8 pn[6];
	u8 bcn_mode;
	u8 _rsv;
} __packed;

struct sta_rec_sec_uni {
	__le16 tag;
	__le16 len;
	u8 add;
	u8 n_cipher;
	u8 rsv[2];

	struct sec_key_uni key[2];
} __packed;

struct sta_phy_uni {
	u8 type;
	u8 flag;
	u8 stbc;
	u8 sgi;
	u8 bw;
	u8 ldpc;
	u8 mcs;
	u8 nss;
	u8 he_ltf;
	u8 rsv[3];
};

struct sta_rec_ra_uni {
	__le16 tag;
	__le16 len;

	u8 valid;
	u8 auto_rate;
	u8 phy_mode;
	u8 channel;
	u8 bw;
	u8 disable_cck;
	u8 ht_mcs32;
	u8 ht_gf;
	u8 ht_mcs[4];
	u8 mmps_mode;
	u8 gband_256;
	u8 af;
	u8 auth_wapi_mode;
	u8 rate_len;

	u8 supp_mode;
	u8 supp_cck_rate;
	u8 supp_ofdm_rate;
	__le32 supp_ht_mcs;
	__le16 supp_vht_mcs[4];

	u8 op_mode;
	u8 op_vht_chan_width;
	u8 op_vht_rx_nss;
	u8 op_vht_rx_nss_type;

	__le32 sta_cap;

	struct sta_phy_uni phy;
	u8 rx_rcpi[4];
} __packed;

struct sta_rec_ra_fixed_uni {
	__le16 tag;
	__le16 len;

	__le32 field;
	u8 op_mode;
	u8 op_vht_chan_width;
	u8 op_vht_rx_nss;
	u8 op_vht_rx_nss_type;

	struct sta_phy_uni phy;

	u8 spe_idx;
	u8 short_preamble;
	u8 is_5g;
	u8 mmps_mode;
} __packed;

struct sta_rec_hdrt {
	__le16 tag;
	__le16 len;
	u8 hdrt_mode;
	u8 rsv[3];
} __packed;

struct sta_rec_hdr_trans {
	__le16 tag;
	__le16 len;
	u8 from_ds;
	u8 to_ds;
	u8 dis_rx_hdr_tran;
	u8 mesh;
} __packed;

struct sta_rec_ps_leave {
	__le16 tag;
	__le16 len;
	u8 __rsv[4];
} __packed;

struct sta_rec_mld_setup {
	__le16 tag;
	__le16 len;
	u8 mld_addr[ETH_ALEN];
	__le16 primary_id;
	__le16 seconed_id;
	__le16 setup_wcid;
	u8 link_num;
	u8 info;
	u8 __rsv[2];
	u8 link_info[];
} __packed;

struct sta_rec_eht_mld {
	__le16 tag;
	__le16 len;
	u8 nsep;
	u8 __rsv1[2];
	u8 str_cap[__MT_MAX_BAND];
	__le16 eml_cap;
	u8 __rsv2[4];
} __packed;

struct mld_setup_link {
	__le16 wcid;
	u8 bss_idx;
	u8 __rsv;
} __packed;

struct hdr_trans_en {
	__le16 tag;
	__le16 len;
	u8 enable;
	u8 check_bssid;
	u8 mode;
	u8 __rsv;
} __packed;

struct hdr_trans_vlan {
	__le16 tag;
	__le16 len;
	u8 insert_vlan;
	u8 remove_vlan;
	u8 tid;
	u8 __rsv;
} __packed;

struct hdr_trans_blacklist {
	__le16 tag;
	__le16 len;
	u8 idx;
	u8 enable;
	__le16 type;
} __packed;

struct uni_header {
	u8 __rsv[4];
} __packed;

struct vow_rx_airtime {
	__le16 tag;
	__le16 len;

	u8 enable;
	u8 band;
	u8 __rsv[2];
} __packed;

struct bf_sounding_on {
	__le16 tag;
	__le16 len;

	u8 snd_mode;
	u8 sta_num;
	u8 __rsv[2];
	__le16 wlan_id[4];
	__le32 snd_period;
} __packed;

struct bf_hw_en_status_update {
	__le16 tag;
	__le16 len;

	bool ebf;
	bool ibf;
	u8 __rsv[2];
} __packed;

struct bf_mod_en_ctrl {
	__le16 tag;
	__le16 len;

	u8 bf_num;
	u8 bf_bitmap;
	u8 bf_sel[8];
	u8 __rsv[2];
} __packed;

union bf_tag_tlv {
	struct bf_sounding_on bf_snd;
	struct bf_hw_en_status_update bf_hw_en;
	struct bf_mod_en_ctrl bf_mod_en;
};

enum {
	BF_SOUNDING_OFF = 0,
	BF_SOUNDING_ON = 1,
	BF_DATA_PACKET_APPLY = 2,
	BF_PFMU_TAG_READ = 5,
	BF_PFMU_TAG_WRITE = 6,
	BF_STA_REC_READ = 11,
	BF_PHASE_CALIBRATION = 12,
	BF_IBF_PHASE_COMP = 13,
	BF_PROFILE_WRITE_20M_ALL = 15,
	BF_HW_EN_UPDATE = 17,
	BF_MOD_EN_CTRL = 20,
	BF_FBRPT_DBG_INFO_READ = 23,
	BF_TXSND_INFO = 24,
	BF_CMD_TXCMD = 27,
	BF_CFG_PHY = 28,
	BF_PROFILE_WRITE_20M_ALL_5X5 = 30,
};

struct ra_rate {
	__le16 wlan_idx;
	u8 mode;
	u8 stbc;
	__le16 gi;
	u8 bw;
	u8 ldpc;
	u8 mcs;
	u8 nss;
	__le16 ltf;
	u8 spe;
	u8 preamble;
	u8 __rsv[2];
} __packed;

struct ra_fixed_rate {
	__le16 tag;
	__le16 len;

	__le16 version;
	struct ra_rate rate;
} __packed;

enum {
	UNI_RA_FIXED_RATE = 0xf,
};

enum {
	UNI_MURU_PST_LOW_POWER = 0x6e,
};

#define MT7996_HDR_TRANS_MAX_SIZE	(sizeof(struct hdr_trans_en) +	 \
					 sizeof(struct hdr_trans_vlan) + \
					 sizeof(struct hdr_trans_blacklist))

enum {
	UNI_HDR_TRANS_EN,
	UNI_HDR_TRANS_VLAN,
	UNI_HDR_TRANS_BLACKLIST,
};

enum {
	RATE_PARAM_VHT_OMN_UPDATE = 1,
	RATE_PARAM_FIXED = 3,
	RATE_PARAM_MMPS_UPDATE = 5,
	RATE_PARAM_FIXED_HE_LTF = 7,
	RATE_PARAM_FIXED_MCS,
	RATE_PARAM_FIXED_GI = 11,
	RATE_PARAM_FIXED_ENCODING,
	RATE_PARAM_AUTO = 20,
#ifdef CONFIG_MTK_VENDOR
	RATE_PARAM_FIXED_MIMO = 30,
	RATE_PARAM_FIXED_OFDMA = 31,
	RATE_PARAM_AUTO_MU = 32,
#endif
};

#define RATE_CFG_BAND_IDX	GENMASK(17, 16)
#define RATE_CFG_MODE	GENMASK(15, 8)
#define RATE_CFG_VAL	GENMASK(7, 0)

/* MURU */
#define OFDMA_DL                       BIT(0)
#define OFDMA_UL                       BIT(1)
#define MUMIMO_DL                      BIT(2)
#define MUMIMO_UL                      BIT(3)
#define MUMIMO_DL_CERT                 BIT(4)

enum {
	CMD_BAND_NONE,
	CMD_BAND_24G,
	CMD_BAND_5G,
	CMD_BAND_6G,
};

struct bss_req_hdr {
	u8 bss_idx;
	u8 __rsv[3];
} __packed;

enum {
	UNI_CHANNEL_SWITCH,
	UNI_CHANNEL_RX_PATH,
};

#define MT7996_BSS_UPDATE_MAX_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct mt76_connac_bss_basic_tlv) +	\
					 sizeof(struct bss_rlm_tlv) +		\
					 sizeof(struct bss_ra_tlv) +		\
					 sizeof(struct bss_info_uni_he) +	\
					 sizeof(struct bss_rate_tlv) +		\
					 sizeof(struct bss_txcmd_tlv) +		\
					 sizeof(struct bss_power_save) +	\
					 sizeof(struct bss_sec_tlv) +		\
					 sizeof(struct bss_ifs_time_tlv) +	\
					 sizeof(struct bss_mld_tlv) +           \
					 sizeof(struct bssinfo_force_cmd_tlv))

#define MT7996_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +		\
					 sizeof(struct sta_rec_basic) +		\
					 sizeof(struct sta_rec_bf) +		\
					 sizeof(struct sta_rec_ht_uni) +	\
					 sizeof(struct sta_rec_he_v2) +		\
					 sizeof(struct sta_rec_ba_uni) +	\
					 sizeof(struct sta_rec_vht) +		\
					 sizeof(struct sta_rec_uapsd) + 	\
					 sizeof(struct sta_rec_amsdu) +		\
					 sizeof(struct sta_rec_bfee) +		\
					 sizeof(struct sta_rec_ra_uni) +	\
					 sizeof(struct sta_rec_sec) +		\
					 sizeof(struct sta_rec_ra_fixed_uni) +	\
					 sizeof(struct sta_rec_he_6g_capa) +	\
					 sizeof(struct sta_rec_eht) +		\
					 sizeof(struct sta_rec_hdrt) +		\
					 sizeof(struct sta_rec_hdr_trans) +	\
					 sizeof(struct sta_rec_mld_setup) +	\
					 sizeof(struct mld_setup_link) * 3 +	\
					 sizeof(struct sta_rec_tx_cap) +	\
					 sizeof(struct sta_rec_eht_mld) +	\
					 sizeof(struct sta_rec_muar_idx) +	\
					 sizeof(struct tlv))

#define MT7996_BEACON_UPDATE_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct bss_bcn_content_tlv) +	\
					 4 + MT_TXD_SIZE +			\
					 sizeof(struct bss_bcn_cntdwn_tlv) +	\
					 sizeof(struct bss_bcn_mbss_tlv) +	\
					 sizeof(struct bss_bcn_crit_update_tlv) +	\
					 sizeof(struct bss_bcn_sta_prof_cntdwn_tlv) +	\
					 sizeof(struct bss_bcn_ml_reconf_tlv) +	\
					 3 * sizeof(struct bss_bcn_ml_reconf_offset))
#define MT7996_MAX_BSS_OFFLOAD_SIZE	2048
#define MT7996_MAX_BEACON_SIZE		(MT7996_MAX_BSS_OFFLOAD_SIZE - \
					 MT7996_BEACON_UPDATE_SIZE)

static inline s8
mt7996_get_power_bound(struct mt76_phy *mphy, s8 txpower, s8 *single_nss_txpower_half_db_ret)
{
	int n_chains = hweight16(mphy->chainmask);

	txpower = mt76_get_sar_power(mphy, mphy->chandef.chan, txpower * 2);
	*single_nss_txpower_half_db_ret = txpower;
	txpower -= mt76_tx_power_path_delta(n_chains);

	return txpower;
}

enum {
	UNI_BAND_CONFIG_RADIO_ENABLE,
	UNI_BAND_CONFIG_EDCCA_ENABLE = 0x05,
        UNI_BAND_CONFIG_EDCCA_THRESHOLD = 0x06,
	UNI_BAND_CONFIG_RTS_THRESHOLD = 0x08,
	UNI_BAND_CONFIG_RTS_SIGTA_EN = 0x09,
	UNI_BAND_CONFIG_DIS_SECCH_CCA_DET = 0x0a,
	UNI_BAND_CONFIG_LPI_CTRL = 0x0d,
	UNI_BAND_CONFIG_BSSID_MAPPING_ADDR = 0x12,
};

enum {
	UNI_WSYS_CONFIG_FW_LOG_CTRL,
	UNI_WSYS_CONFIG_FW_DBG_CTRL,
	UNI_CMD_CERT_CFG = 6,
	UNI_WSYS_CONFIG_FW_TIME_SYNC, /* UNI_CMD_FW_TIME_SYNC in FW */
};

enum {
	UNI_RDD_CTRL_PARM,
	UNI_RDD_CTRL_SET_TH = 0x3,
};

enum {
	UNI_EFUSE_ACCESS = 1,
	UNI_EFUSE_BUFFER_MODE,
	UNI_EFUSE_FREE_BLOCK,
	UNI_EFUSE_BUFFER_RD,
};

enum {
	UNI_VOW_DRR_CTRL,
	UNI_VOW_FEATURE_CTRL,
	UNI_VOW_RX_AT_AIRTIME_EN = 0x0b,
	UNI_VOW_RX_AT_AIRTIME_CLR_EN = 0x0e,
	UNI_VOW_RED_ENABLE = 0x18,
};

enum {
	VOW_DRR_DBG_DUMP_BMP = BIT(0),
	VOW_DRR_DBG_EST_AT_PRINT = BIT(1),
	VOW_DRR_DBG_ADJ_GLOBAL_THLD = BIT(21),
	VOW_DRR_DBG_PRN_LOUD = BIT(22),
	VOW_DRR_DBG_PRN_ADJ_STA = BIT(23),
	VOW_DRR_DBG_FIX_CR = GENMASK(27, 24),
	VOW_DRR_DBG_CLR_FIX_CR = BIT(28),
	VOW_DRR_DBG_DISABLE = BIT(29),
	VOW_DRR_DBG_DUMP_CR = BIT(30),
	VOW_DRR_DBG_PRN = BIT(31)
};

#define VOW_DRR_DBG_FLAGS (VOW_DRR_DBG_DUMP_BMP |	\
			  VOW_DRR_DBG_EST_AT_PRINT |	\
			  VOW_DRR_DBG_ADJ_GLOBAL_THLD |	\
			  VOW_DRR_DBG_PRN_LOUD |	\
			  VOW_DRR_DBG_PRN_ADJ_STA |	\
			  VOW_DRR_DBG_FIX_CR |		\
			  VOW_DRR_DBG_CLR_FIX_CR |	\
			  VOW_DRR_DBG_DISABLE |		\
			  VOW_DRR_DBG_DUMP_CR |		\
			  VOW_DRR_DBG_PRN)

enum {
	UNI_CMD_MIB_DATA,
};

enum {
	UNI_POWER_OFF,
	UNI_POWER_LOW_POWER = 0x6,
};

enum {
	UNI_CMD_TWT_ARGT_UPDATE = 0x0,
	UNI_CMD_TWT_MGMT_OFFLOAD,
};

enum {
	UNI_RRO_DEL_ENTRY = 0x1,
	UNI_RRO_SET_PLATFORM_TYPE,
	UNI_RRO_GET_BA_SESSION_TABLE,
	UNI_RRO_SET_BYPASS_MODE,
	UNI_RRO_SET_TXFREE_PATH,
	UNI_RRO_DEL_BA_SESSION,
	UNI_RRO_SET_FLUSH_TIMEOUT
};

enum{
	UNI_CMD_SR_ENABLE = 0x1,
	UNI_CMD_SR_ENABLE_SD,
	UNI_CMD_SR_ENABLE_MODE,
	UNI_CMD_SR_ENABLE_DPD = 0x12,
	UNI_CMD_SR_ENABLE_TX,
	UNI_CMD_SR_SET_SRG_BITMAP = 0x80,
	UNI_CMD_SR_SET_PARAM = 0xc1,
	UNI_CMD_SR_SET_SIGA = 0xd0,
};

enum {
	UNI_CMD_THERMAL_PROTECT_ENABLE = 0x6,
	UNI_CMD_THERMAL_PROTECT_DISABLE,
	UNI_CMD_THERMAL_PROTECT_DUTY_CONFIG,
};

enum {
	UNI_CMD_MAC_INFO_TSF_DIFF = 2,
};

enum {
	UNI_EVENT_MAC_INFO_TSF_DIFF = 2,
};

struct mt7996_mcu_mac_info_tsf_diff {
	__le16 tag;
	__le16 len;
	u8 bss_idx0;
	u8 bss_idx1;
	u8 rsv[2];
} __packed;

struct mt7996_mcu_mac_info_event {
	u8 rsv[4];
	u8 buf[];
} __packed;

struct mt7996_mcu_mac_info_tsf_diff_resp {
	__le16 tag;
	__le16 len;
	__le32 tsf0_bit0_31;
	__le32 tsf0_bit32_63;
	__le32 tsf1_bit0_31;
	__le32 tsf1_bit32_63;
} __packed;

struct mld_req_hdr {
	u8 ver;
	u8 mld_addr[ETH_ALEN];
	u8 mld_idx;
	u8 flag;
	u8 rsv[3];
	u8 buf[];
} __packed;

struct mld_attlm_req {
	__le16 tag;
	__le16 len;
	u8 attlm_idx;
	u8 bss_idx;
	u8 mst_timer;
	u8 e_timer;
	__le16 mst_timer_adv_time;
	__le16 e_timer_adv_time;
	__le32 mst_duration;
	__le32 e_duration;
	__le16 disabled_link_bitmap;
	u8 disabled_bss_idx[16];
	u8 rsv[2];
} __packed;

struct mld_reconf_timer {
	__le16 tag;
	__le16 len;
	__le16 link_bitmap;
	__le16 to_sec[__MT_MAX_BAND]; /* timeout of reconf (second) */
	u8 bss_idx[__MT_MAX_BAND];
	u8 rsv;
} __packed;

struct mld_reconf_stop_link {
	__le16 tag;
	__le16 len;
	__le16 link_bitmap;
	u8 rsv[2];
	u8 bss_idx[16];
} __packed;

enum {
	UNI_CMD_MLD_ATTLM_RES_REQ = 0x02,
	UNI_CMD_MLD_RECONF_AP_REM_TIMER = 0x03,
	UNI_CMD_MLD_RECONF_STOP_LINK = 0x04,
};

struct mt7996_mcu_mld_event {
	struct mt7996_mcu_rxd rxd;

	/* fixed field */
	u8 ver;
	u8 mld_addr[ETH_ALEN];
	u8 mld_idx;
	u8 rsv[4];
	/* tlv */
	u8 buf[];
} __packed;

struct mt7996_mld_event_data {
	u8 mld_addr[ETH_ALEN];
	u8 *data;
};

struct mt7996_mcu_mld_attlm_resp_event {
	__le16 tag;
	__le16 len;
	u8 status;
	u8 attlm_idx;
	u8 bss_idx;
	u8 rsv;
	__le32 switch_time_tsf[2];
	__le32 end_tsf[2];
} __packed;

struct mt7996_mcu_mld_attlm_timeout_event {
	__le16 tag;
	__le16 len;
	u8 attlm_idx;
	u8 event_type;
	u8 rsv[2];
} __packed;

struct mt7996_mcu_mld_ap_reconf_event {
	__le16 tag;
	__le16 len;
	__le16 link_bitmap;
	u8 bss_idx[3];
	u8 rsv[3];
} __packed;

enum {
	UNI_EVENT_MLD_ATTLM_RES_RSP = 0x02,
	UNI_EVENT_MLD_ATTLM_TIMEOUT = 0x03,
	UNI_EVENT_MLD_RECONF_AP_REM_TIMER = 0x04,
};

struct peer_mld_req_hdr {
	u8 ver;
	u8 peer_mld_addr[ETH_ALEN];
	u8 mld_idx;
	u8 rsv[4];
	u8 buf[];
} __packed;

struct peer_mld_ttlm_req {
	__le16 tag;
	__le16 len;
	u8 mld_addr[ETH_ALEN];
	__le16 enabled_link_bitmap;
	__le16 link_to_wcid[IEEE80211_MLD_MAX_NUM_LINKS + 1];
	u8 dl_tid_map[IEEE80211_MLD_MAX_NUM_LINKS + 1];
	u8 ul_tid_map[IEEE80211_MLD_MAX_NUM_LINKS + 1];
} __packed;

enum {
	UNI_CMD_PEER_MLD_TTLM_REQ = 0x0,
};

#define UNI_CMD_SDO_CFG_BSS_NUM 96
#define UNI_CMD_SDO_CFG_BSS_MAP_WORDLEN ((UNI_CMD_SDO_CFG_BSS_NUM) / 32)

struct mt7996_mcu_bss_event {
	struct mt7996_mcu_rxd rxd;

	/* fixed field */
	u8 bss_idx;
	u8 __rsv[3];
	/* tlv */
	u8 buf[];
} __packed;

struct mt7996_mcu_bss_bcn_crit_update_event {
	__le16 tag;
	__le16 len;
	u8 rsv[4];
} __packed;

enum {
	UNI_EVENT_BSS_BCN_CRIT_UPDATE = 0x01,
};

struct tx_power_ctrl {
        u8 _rsv[4];

        __le16 tag;
        __le16 len;

        u8 power_ctrl_id;
        union {
                bool sku_enable;
                bool ate_mode_enable;
                bool percentage_ctrl_enable;
                bool bf_backoff_enable;
                u8 show_info_category;
                u8 power_drop_level;
        };
        u8 band_idx;
        u8 rsv[1];
} __packed;

enum {
        UNI_TXPOWER_SKU_POWER_LIMIT_CTRL = 0,
        UNI_TXPOWER_PERCENTAGE_CTRL = 1,
        UNI_TXPOWER_PERCENTAGE_DROP_CTRL = 2,
        UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL = 3,
        UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL = 4,
        UNI_TXPOWER_ATE_MODE_CTRL = 6,
        UNI_TXPOWER_SHOW_INFO = 7,
};

enum {
	UNI_CMD_ACCESS_REG_BASIC = 0x0,
	UNI_CMD_ACCESS_RF_REG_BASIC,
};

enum {
	UNI_CMD_SER_QUERY,
	/* recovery */
	UNI_CMD_SER_SET_RECOVER_L1,
	UNI_CMD_SER_SET_RECOVER_L2,
	UNI_CMD_SER_SET_RECOVER_L3_RX_ABORT,
	UNI_CMD_SER_SET_RECOVER_L3_TX_ABORT,
	UNI_CMD_SER_SET_RECOVER_L3_TX_DISABLE,
	UNI_CMD_SER_SET_RECOVER_L3_BF,
	UNI_CMD_SER_SET_RECOVER_L4_MDP,
	UNI_CMD_SER_SET_RECOVER_FROM_ETH,
	UNI_CMD_SER_SET_RECOVER_FULL = 8,
	/* fw assert */
	UNI_CMD_SER_SET_SYSTEM_ASSERT,
	/* coredump */
	UNI_CMD_SER_FW_COREDUMP_WA,
	UNI_CMD_SER_FW_COREDUMP_WM,
	/*hw bit detect only*/
	UNI_CMD_SER_SET_HW_BIT_DETECT_ONLY,
	/* action */
	UNI_CMD_SER_ENABLE = 1,
	UNI_CMD_SER_SET,
	UNI_CMD_SER_TRIGGER
};

enum {
	UNI_CMD_SDO_SET = 1,
	UNI_CMD_SDO_QUERY,
	UNI_CMD_SDO_AUTO_BA,
	UNI_CMD_SDO_SET_QOS_MAP,
	UNI_CMD_SDO_HOTSPOT,
	UNI_CMD_SDO_CP_MODE,
	UNI_CMD_SDO_RED_SETTING,
	UNI_CMD_SDO_PKT_BUDGET_CTRL_CFG,
	UNI_CMD_SDO_GET_BSS_ACQ_PKT_NUM,
	UNI_CMD_SDO_OVERRIDE_CTRL
};

enum {
	MT7996_SEC_MODE_PLAIN,
	MT7996_SEC_MODE_AES,
	MT7996_SEC_MODE_SCRAMBLE,
	MT7996_SEC_MODE_MAX,
};

enum {
	UNI_CMD_MLO_AGC_TX = 4,
	UNI_CMD_MLO_AGC_TRIG = 5,
};

struct mt7996_mlo_agc_set {
	u8 rsv[4];

	__le16 tag;
	__le16 len;

	u8 mld_id;
	u8 link_id;
	u8 ac;
	u8 disp_pol;
	u8 ratio;
	u8 order;
	__le16 mgf;
} __packed;

enum {
	UNI_CMD_PP_EN_CTRL,
	UNI_CMD_PP_ALG_CTRL,
	UNI_CMD_PP_DSCB_CTRL,
	UNI_CMD_PP_CHANGE_CAP_CTRL = 4,
};

enum pp_mode {
	PP_DISABLE = 0,
	PP_FW_MODE,
	PP_USR_MODE,
};

enum pp_alg_action {
	PP_ALG_SET_TIMER,
	PP_ALG_GET_STATISTICS = 2,
};

enum {
	UNI_EVENT_PP_TAG_ALG_CTRL = 1,
	UNI_EVENT_STATIC_PP_TAG_DSCB_IE,
	UNI_EVENT_STATIC_PP_TAG_CSA_DSCB_IE,
	UNI_EVENT_PP_SHOW_INFO,
};

struct mt7996_mcu_pp_basic_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;
	u8 band_idx;
	u8 __rsv2[3];
} __packed;

struct mt7996_mcu_pp_dscb_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;
	u8 band_idx;
	u8 omac_idx;
	u8 new_dscb;
	u8 __rsv2;
	__le16 punct_bitmap;
	u8 __rsv3[2];
} __packed;

struct mt7996_mcu_pp_alg_ctrl_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;

	__le32 pp_timer_intv;
	__le32 thr_x2_value;
	__le32 thr_x2_shift;
	__le32 thr_x3_value;
	__le32 thr_x3_shift;
	__le32 thr_x4_value;
	__le32 thr_x4_shift;
	__le32 thr_x5_value;
	__le32 thr_x5_shift;
	__le32 thr_x6_value;
	__le32 thr_x6_shift;
	__le32 thr_x7_value;
	__le32 thr_x7_shift;
	__le32 thr_x8_value;
	__le32 thr_x8_shift;
	__le32 sw_pp_time;
	__le32 hw_pp_time;
	__le32 no_pp_time;
	__le32 auto_bw_time;
	u8 band_idx;
	u8 __rsv2;
	__le16 punct_bitmap;
} __packed;

enum {
	UNI_CMD_SCS_SEND_DATA,
	UNI_CMD_SCS_SET_PD_THR_RANGE = 2,
	UNI_CMD_SCS_ENABLE,
};

enum {
	UNI_CMD_TPO_CTRL,
};

enum {
	MT7996_LP_TPO_ALL,
	MT7996_LP_TPO_PB,
	MT7996_LP_TPO_LP,
	MT7996_LP_TPO_MIN_TX,
};

#define MT7996_PATCH_SEC		GENMASK(31, 24)
#define MT7996_PATCH_SCRAMBLE_KEY	GENMASK(15, 8)
#define MT7996_PATCH_AES_KEY		GENMASK(7, 0)

#define MT7996_SEC_ENCRYPT		BIT(0)
#define MT7996_SEC_KEY_IDX		GENMASK(2, 1)
#define MT7996_SEC_IV			BIT(3)

struct fixed_rate_table_ctrl {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;

	u8 table_idx;
	u8 antenna_idx;
	__le16 rate_idx;
	u8 spe_idx_sel;
	u8 spe_idx;
	u8 gi;
	u8 he_ltf;
	bool ldpc;
	bool txbf;
	bool dynamic_bw;

	u8 _rsv2;
} __packed;

#define EPCS_MAX_WMM_PARAMS	16
#define EPCS_CTRL_WMM_FLAG_VALID	BIT(0)

enum {
	UNI_CMD_EPCS_CTRL = 1,
};

enum {
	UNI_EPCS_CTRL_ENABLE = 1,
	UNI_EPCS_CTRL_SET_WMM_PARAMS,
	UNI_EPCS_CTRL_SET_WMM_IDX,
	UNI_EPCS_CTRL_GET_WMM_PARAMS,
	UNI_EPCS_CTRL_GET_STA,
	UNI_EPCS_CTRL_RESET_STA
};

enum {
	UNI_CMD_MURU_DBG_INFO = 0x18,
};

enum {
	UNI_CMD_MURU_BSRP_CTRL = 0x01,
	UNI_CMD_MURU_SUTX_CTRL = 0x10,
	UNI_CMD_MURU_FIXED_RATE_CTRL = 0x11,
	UNI_CMD_MURU_FIXED_GROUP_RATE_CTRL = 0x12,
	UNI_CMD_MURU_SET_FORCE_MU = 0x33,
	UNI_CMD_MURU_MUNUAL_CONFIG = 0x64,
	UNI_CMD_MURU_SET_MUDL_ACK_POLICY = 0xC8,
	UNI_CMD_MURU_SET_TRIG_TYPE = 0xC9,
	UNI_CMD_MURU_SET_20M_DYN_ALGO = 0xCA,
	UNI_CMD_MURU_PROT_FRAME_THR = 0xCC,
	UNI_CMD_MURU_SET_CERT_MU_EDCA_OVERRIDE,
	UNI_CMD_MURU_SET_TRIG_VARIANT = 0xD5,
	UNI_CMD_MURU_SET_QOS_CFG = 0xFE,
};

enum {
	SCS_REQ_TYPE_ADD,
	SCS_REQ_TYPE_REMOVE,
	SCS_REQ_TYPE_CHANGE,
};

struct uni_muru_mum_set_group_tbl_entry {
	__le16 wlan_idx0;
	__le16 wlan_idx1;
	__le16 wlan_idx2;
	__le16 wlan_idx3;

	u8 dl_mcs_user0:4;
	u8 dl_mcs_user1:4;
	u8 dl_mcs_user2:4;
	u8 dl_mcs_user3:4;
	u8 ul_mcs_user0:4;
	u8 ul_mcs_user1:4;
	u8 ul_mcs_user2:4;
	u8 ul_mcs_user3:4;

	u8 num_user:2;
	u8 rsv:6;
	u8 nss0:2;
	u8 nss1:2;
	u8 nss2:2;
	u8 nss3:2;
	u8 ru_alloc;
	u8 ru_alloc_ext;

	u8 capa;
	u8 gi;
	u8 dl_ul;
	u8 _rsv2;
};

enum UNI_CMD_MURU_VER_T {
	UNI_CMD_MURU_VER_LEG = 0,
	UNI_CMD_MURU_VER_HE,
	UNI_CMD_MURU_VER_EHT,
	UNI_CMD_MURU_VER_MAX
};

struct mt7996_muru_comm {
	u8 pda_pol;
	u8 band;
	u8 spe_idx;
	u8 proc_type;

	__le16 mlo_ctrl;
	u8 sch_type;
	u8 ppdu_format;
	u8 ac;
	u8 _rsv[3];
};

struct mt7996_muru_dl {
	u8 user_num;
	u8 tx_mode;
	u8 bw;
	u8 gi;

	u8 ltf;
	u8 mcs;
	u8 dcm;
	u8 cmprs;

	__le16 ru[16];

	u8 c26[2];
	u8 ack_policy;
	u8 tx_power;

	__le16 mu_ppdu_duration;
	u8 agc_disp_order;
	u8 _rsv1;

	u8 agc_disp_pol;
	u8 agc_disp_ratio;
	__le16 agc_disp_linkMFG;

	__le16 prmbl_punc_bmp;
	u8 _rsv2[2];

	struct {
		__le16 wlan_idx;
		u8 ru_alloc_seg;
		u8 ru_idx;
		u8 ldpc;
		u8 nss;
		u8 mcs;
		u8 mu_group_idx;
		u8 vht_groud_id;
		u8 vht_up;
		u8 he_start_stream;
		u8 he_mu_spatial;
		__le16 tx_power_alpha;
		u8 ack_policy;
		u8 ru_allo_ps160;
	} usr[16];
};

struct mt7996_muru_ul {
	u8 user_num;
	u8 tx_mode;

	u8 ba_type;
	u8 _rsv;

	u8 bw;
	u8 gi_ltf;
	__le16 ul_len;

	__le16 trig_cnt;
	u8 pad;
	u8 trig_type;

	__le16 trig_intv;
	u8 trig_ta[ETH_ALEN];
	__le16 ul_ru[16];

	u8 c26[2];
	__le16 agc_disp_linkMFG;

	u8 agc_disp_mu_len;
	u8 agc_disp_pol;
	u8 agc_disp_ratio;
	u8 agc_disp_pu_idx;

	struct {
		__le16 wlan_idx;
		u8 ru_alloc_seg;
		u8 ru_idx;
		u8 ldpc;
		u8 nss;
		u8 mcs;
		u8 target_rssi;
		__le32 trig_pkt_size;
		u8 ru_allo_ps160;
		u8 _rsv2[3];
	} usr[16];
};

struct mt7996_muru_dbg {
	/* HE TB RX Debug */
	__le32 rx_hetb_nonsf_en_bitmap;
	__le32 rx_hetb_cfg[2];
};

struct mt7996_muru {
	__le32 cfg_comm;
	__le32 cfg_dl;
	__le32 cfg_ul;
	__le32 cfg_dbg;

	struct mt7996_muru_comm comm;
	struct mt7996_muru_dl dl;
	struct mt7996_muru_ul ul;
	struct mt7996_muru_dbg dbg;
};


#define MURU_PPDU_HE_TRIG	BIT(2)
#define MURU_PPDU_HE_MU		BIT(3)

#define MURU_OFDMA_SCH_TYPE_DL	BIT(0)
#define MURU_OFDMA_SCH_TYPE_UL	BIT(1)

/* Common Config */
#define MURU_COMM_PPDU_FMT	BIT(0)
#define MURU_COMM_SCH_TYPE	BIT(1)
#define MURU_COMM_BAND		BIT(2)
#define MURU_COMM_WMM		BIT(3)
#define MURU_COMM_SPE_IDX	BIT(4)
#define MURU_COMM_PROC_TYPE	BIT(5)
#define MURU_COMM_SET		(MURU_COMM_PPDU_FMT | MURU_COMM_SCH_TYPE)
#define MURU_COMM_SET_TM	(MURU_COMM_PPDU_FMT | MURU_COMM_BAND | \
				 MURU_COMM_WMM | MURU_COMM_SPE_IDX)

/* DL Common config */
#define MURU_FIXED_DL_TOTAL_USER_CNT	BIT(4)

/* UL Common Config */
#define MURU_FIXED_UL_TOTAL_USER_CNT	BIT(4)

enum muru_vendor_ctrl {
	MU_CTRL_UPDATE,
	MU_CTRL_DL_USER_CNT,
	MU_CTRL_UL_USER_CNT,
};

#define UNI_MAX_MCS_SUPPORT_HE 11
#define UNI_MAX_MCS_SUPPORT_EHT 13

enum {
	RUALLOC_BW20 = 122,
	RUALLOC_BW40 = 130,
	RUALLOC_BW80 = 134,
	RUALLOC_BW160 = 137,
	RUALLOC_BW320 = 395,
};

enum {
	MAX_MODBF_VHT = 0,
	MAX_MODBF_HE = 2,
	MAX_MODBF_EHT = 4,
};

enum tm_trx_param_idx {
	TM_TRX_PARAM_RSV,
	/* MAC */
	TM_TRX_PARAM_SET_TRX,
	TM_TRX_PARAM_RX_FILTER,
	TM_TRX_PARAM_RX_FILTER_PKT_LEN,
	TM_TRX_PARAM_SLOT_TIME,
	TM_TRX_PARAM_CLEAN_PERSTA_TXQUEUE,
	TM_TRX_PARAM_AMPDU_WTBL,
	TM_TRX_PARAM_MU_RX_AID,
	TM_TRX_PARAM_PHY_MANUAL_TX,

	/* PHY */
	TM_TRX_PARAM_RX_PATH,
	TM_TRX_PARAM_TX_STREAM,
	TM_TRX_PARAM_TSSI_STATUS,
	TM_TRX_PARAM_DPD_STATUS,
	TM_TRX_PARAM_RATE_POWER_OFFSET_ON_OFF,
	TM_TRX_PARAM_THERMO_COMP_STATUS,
	TM_TRX_PARAM_FREQ_OFFSET,
	TM_TRX_PARAM_FAGC_RSSI_PATH,
	TM_TRX_PARAM_PHY_STATUS_COUNT,
	TM_TRX_PARAM_RXV_INDEX,

	TM_TRX_PARAM_ANTENNA_PORT,
	TM_TRX_PARAM_THERMAL_ONOFF,
	TM_TRX_PARAM_TX_POWER_CONTROL_ALL_RF,
	TM_TRX_PARAM_RATE_POWER_OFFSET,
	TM_TRX_PARAM_SLT_CMD_TEST,
	TM_TRX_PARAM_SKU,
	TM_TRX_PARAM_POWER_PERCENTAGE_ON_OFF,
	TM_TRX_PARAM_BF_BACKOFF_ON_OFF,
	TM_TRX_PARAM_POWER_PERCENTAGE_LEVEL,
	TM_TRX_PARAM_FRTBL_CFG,
	TM_TRX_PARAM_PREAMBLE_PUNC_ON_OFF,

	TM_TRX_PARAM_MAX_NUM,
};

enum trx_action {
	TM_TRX_ACTION_SET,
	TM_TRX_ACTION_GET,
};

struct tm_trx_set {
	u8 type;
	u8 enable;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7996_tm_trx_req {
	u8 param_num;
	u8 _rsv[3];

	__le16 tag;
	__le16 len;

	__le16 param_idx;
	u8 band_idx;
	u8 testmode_en;
	u8 action;
	u8 rsv[3];

	u32 data;
	struct tm_trx_set set_trx;

	u8 buf[220];
} __packed;

#define MT7996_TXBF_SUBCAR_NUM		64
#define MT7996_TXBF_PFMU_DATA_LEN	(MT7996_TXBF_SUBCAR_NUM * sizeof(struct mt7996_pfmu_data))
#define MT7996_TXBF_PFMU_DATA_LEN_5X5	(MT7996_TXBF_SUBCAR_NUM * \
					 sizeof(struct mt7996_pfmu_data_5x5))

enum {
	UNI_EVENT_BF_PFMU_TAG = 0x5,
	UNI_EVENT_BF_PFMU_DATA = 0x7,
	UNI_EVENT_BF_STAREC = 0xB,
	UNI_EVENT_BF_CAL_PHASE = 0xC,
	UNI_EVENT_BF_FBK_INFO = 0x17,
	UNI_EVENT_BF_TXSND_INFO = 0x18,
	UNI_EVENT_BF_PLY_INFO = 0x19,
	UNI_EVENT_BF_METRIC_INFO = 0x1A,
	UNI_EVENT_BF_TXCMD_CFG_INFO = 0x1B,
	UNI_EVENT_BF_SND_CNT_INFO = 0x1D,
	UNI_EVENT_BF_MAX_NUM
};

enum {
	BF_SND_READ_INFO = 0,
	BF_SND_CFG_OPT,
	BF_SND_CFG_INTV,
	BF_SND_STA_STOP,
	BF_SND_CFG_MAX_STA,
	BF_SND_CFG_BFRP,
	BF_SND_CFG_INF,
	BF_SND_CFG_TXOP_SND
};

enum bf_lm_type {
	BF_LM_LEGACY,
	BF_LM_HT,
	BF_LM_VHT,
	BF_LM_HE,
	BF_LM_EHT,
};

struct mt7996_mcu_bf_basic_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;
};

struct pfmu_ru_field {
	__le32 ru_start_id:7;
	__le32 _rsv1:1;
	__le32 ru_end_id:7;
	__le32 _rsv2:1;
} __packed;

struct pfmu_partial_bw_info {
	__le32 partial_bw_info:9;
	__le32 _rsv1:7;
} __packed;

struct mt7996_pfmu_tag1 {
	__le32 pfmu_idx:10;
	__le32 ebf:1;
	__le32 data_bw:3;
	__le32 lm:3;
	__le32 is_mu:1;
	__le32 nr:3;
	__le32 nc:3;
	__le32 codebook:2;
	__le32 ngroup:2;
	__le32 invalid_prof:1;
	__le32 _rsv:3;

	__le32 col_id1:7, row_id1:9;
	__le32 col_id2:7, row_id2:9;
	__le32 col_id3:7, row_id3:9;
	__le32 col_id4:7, row_id4:9;

	union {
		struct pfmu_ru_field field;
		struct pfmu_partial_bw_info bw_info;
	};
	__le32 mob_cal_en:1;
	__le32 _rsv2:3;
	__le32 mob_ru_alloc:9;	/* EHT profile uses full 9 bit */
	__le32 _rsv3:3;

	__le32 snr_sts0:8, snr_sts1:8, snr_sts2:8, snr_sts3:8;
	__le32 snr_sts4:8, snr_sts5:8, snr_sts6:8, snr_sts7:8;

	__le32 _rsv4;
} __packed;

struct mt7996_pfmu_tag2 {
	__le32 smart_ant:24;
	__le32 se_idx:5;
	__le32 _rsv:3;

	__le32 _rsv1:16;
	__le32 ibf_timeout:8;
	__le32 _rsv2:8;

	__le32 ibf_data_bw:3;
	__le32 ibf_nc:3;
	__le32 ibf_nr:3;
	__le32 ibf_ru:9;
	__le32 _rsv3:14;

	__le32 mob_delta_t:8;
	__le32 mob_lq_result:7;
	__le32 _rsv5:1;
	__le32 _rsv6:16;

	__le32 _rsv7;
} __packed;

struct mt7996_pfmu_tag_event {
	struct mt7996_mcu_bf_basic_event event;

	u8 bfer;
	u8 __rsv[3];

	struct mt7996_pfmu_tag1 t1;
	struct mt7996_pfmu_tag2 t2;
};

struct mt7996_pfmu_tag {
	struct mt7996_pfmu_tag1 t1;
	struct mt7996_pfmu_tag2 t2;
};

struct mt7996_mcu_bf_starec_read {

	struct mt7996_mcu_bf_basic_event event;

	__le16 pfmu_id;
	bool is_su_mu;
	u8 txbf_cap;
	u8 sounding_phy;
	u8 ndpa_rate;
	u8 ndp_rate;
	u8 rpt_poll_rate;
	u8 tx_mode;
	u8 nc;
	u8 nr;
	u8 bw;
	u8 total_mem_require;
	u8 mem_require_20m;
	u8 mem_row0;
	u8 mem_col0:6;
	u8 mem_row0_msb:2;
	u8 mem_row1;
	u8 mem_col1:6;
	u8 mem_row1_msb:2;
	u8 mem_row2;
	u8 mem_col2:6;
	u8 mem_row2_msb:2;
	u8 mem_row3;
	u8 mem_col3:6;
	u8 mem_row3_msb:2;

	__le16 smart_ant;
	u8 se_idx;
	u8 auto_sounding_ctrl;

	u8 bf_timeout;
	u8 bf_dbw;
	u8 bf_ncol;
	u8 bf_nrow;

	u8 nr_lt_bw80;
	u8 nc_lt_bw80;
	u8 ru_start_idx;
	u8 ru_end_idx;

	bool trigger_su;
	bool trigger_mu;

	bool ng16_su;
	bool ng16_mu;

	bool codebook42_su;
	bool codebook75_mu;

	u8 he_ltf;
	u8 rsv[3];
};

struct bf_fbk_rpt_info {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx; // Only need for dynamic_pfmu_update 0x4
	u8 action;
	u8 band_idx;
	u8 __rsv[4];

} __packed;

#define TXBF_PFMU_ID_NUM_MAX 48

#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND0 TXBF_PFMU_ID_NUM_MAX
#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND1 TXBF_PFMU_ID_NUM_MAX
#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND2 TXBF_PFMU_ID_NUM_MAX

/* CFG_BF_STA_REC shall be varied based on BAND Num */
#define CFG_BF_STA_REC_NUM (TXBF_PFMU_ID_NUM_MAX_TBTC_BAND0 + TXBF_PFMU_ID_NUM_MAX_TBTC_BAND1 + TXBF_PFMU_ID_NUM_MAX_TBTC_BAND2)

#define BF_SND_CTRL_STA_DWORD_CNT   ((CFG_BF_STA_REC_NUM + 0x1F) >> 5)

#ifndef ALIGN_4
	#define ALIGN_4(_value)             (((_value) + 3) & ~3u)
#endif /* ALIGN_4 */

#define CFG_WIFI_RAM_BAND_NUM 3

struct uni_event_bf_txsnd_sta_info {
	u8 snd_intv;       /* Sounding interval upper bound, unit:15ms */
	u8 snd_intv_cnt;   /* Sounding interval counter */
	u8 snd_tx_cnt;     /* Tx sounding count for debug */
	u8 snd_stop_reason;  /* Bitwise reason to put in Stop Queue */
};

struct mt7996_txbf_phase_out {
	u8 c0_l;
	u8 c1_l;
	u8 c2_l;
	u8 c3_l;
	u8 c0_m;
	u8 c1_m;
	u8 c2_m;
	u8 c3_m;
	u8 c0_mh;
	u8 c1_mh;
	u8 c2_mh;
	u8 c3_mh;
	u8 c0_h;
	u8 c1_h;
	u8 c2_h;
	u8 c3_h;
	u8 c0_uh;
	u8 c1_uh;
	u8 c2_uh;
	u8 c3_uh;
};

struct mt7992_txbf_phase_out {
	u8 c0_l;
	u8 c1_l;
	u8 c2_l;
	u8 c3_l;
	u8 c4_l;
	u8 c0_m;
	u8 c1_m;
	u8 c2_m;
	u8 c3_m;
	u8 c4_m;
	u8 c0_mh;
	u8 c1_mh;
	u8 c2_mh;
	u8 c3_mh;
	u8 c4_mh;
	u8 c0_h;
	u8 c1_h;
	u8 c2_h;
	u8 c3_h;
	u8 c4_h;
	u8 c0_uh;
	u8 c1_uh;
	u8 c2_uh;
	u8 c3_uh;
	u8 c4_uh;
};

struct txbf_rx_phase {
	u8 rx_uh;
	u8 rx_h;
	u8 rx_m;
	u8 rx_l;
	u8 rx_ul;
};

struct txbf_rx_phase_ext {
	u8 rx_uh;
	u8 rx_h;
	u8 rx_mh;
	u8 rx_m;
	u8 rx_l;
	u8 rx_ul;
};

struct mt7996_txbf_phase_info_2g {
	struct txbf_rx_phase r0;
	struct txbf_rx_phase r1;
	struct txbf_rx_phase r2;
	struct txbf_rx_phase r3;
	struct txbf_rx_phase r2_sx2;
	struct txbf_rx_phase r3_sx2;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t2_h_sx2;
	u8 r0_reserved;
	u8 r1_reserved;
	u8 r2_reserved;
	u8 r3_reserved;
	u8 r2_sx2_reserved;
	u8 r3_sx2_reserved;
};

struct mt7996_txbf_phase_info_5g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	struct txbf_rx_phase r2_sx2;	/* no middle-high in r2_sx2 */
	struct txbf_rx_phase r3_sx2;	/* no middle-high in r3_sx2 */
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t2_h_sx2;
	u8 r0_reserved;
	u8 r1_reserved;
	u8 r2_reserved;
	u8 r3_reserved;
	u8 r2_sx2_reserved;
	u8 r3_sx2_reserved;
};

struct mt7992_txbf_phase_info_2g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
};

struct mt7992_txbf_phase_info_5g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	struct txbf_rx_phase_ext r4;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t3_h;
};

struct mt7996_txbf_phase {
	u8 status;
	union {
		union {
			struct mt7996_txbf_phase_info_2g phase_2g;
			struct mt7996_txbf_phase_info_5g phase_5g;
		} v1;
		union {
			struct mt7992_txbf_phase_info_2g phase_2g;
			struct mt7992_txbf_phase_info_5g phase_5g;
		} v2;
		u8 buf[44];
	};
};

#define phase_assign(group, v, field, dump, ...)	({					\
	if (group) {										\
		phase->v.phase_5g.field = cal->v.phase_5g.field;				\
		if (dump)									\
			dev_info(dev->mt76.dev, "%s = %d\n", #field, phase->v.phase_5g.field);	\
	} else {										\
		phase->v.phase_2g.field = cal->v.phase_5g.field;				\
		if (dump)									\
			dev_info(dev->mt76.dev, "%s = %d\n", #field, phase->v.phase_2g.field);	\
	}											\
})

#define phase_assign_rx(group, v, rx, dump, ...)	({					\
	phase_assign(group, v, rx.rx_uh, dump);							\
	phase_assign(group, v, rx.rx_h, dump);							\
	phase_assign(group, v, rx.rx_m, dump);							\
	phase_assign(group, v, rx.rx_l, dump);							\
	phase_assign(group, v, rx.rx_ul, dump);							\
})

#define phase_assign_rx_ext(group, v, rx, dump, ...)	({					\
	phase_assign(group, v, rx.rx_uh, dump);							\
	phase_assign(group, v, rx.rx_h, dump);							\
	phase_assign(group, v, rx.rx_mh, dump);							\
	phase_assign(group, v, rx.rx_m, dump);							\
	phase_assign(group, v, rx.rx_l, dump);							\
	phase_assign(group, v, rx.rx_ul, dump);							\
})

#define phase_assign_rx_v1(group, v, rx, ...)	({						\
	if (group) {										\
		phase_assign(group, v, rx.rx_uh, true);						\
		phase_assign(group, v, rx.rx_h, true);						\
		phase->v.phase_5g.rx.rx_mh = cal->v.phase_5g.rx.rx_mh;				\
		dev_info(dev->mt76.dev, "%s.rx_mh = %d\n", #rx, phase->v.phase_5g.rx.rx_mh);	\
		phase_assign(group, v, rx.rx_m, true);						\
		phase_assign(group, v, rx.rx_l, true);						\
		phase_assign(group, v, rx.rx_ul, true);						\
	} else {										\
		phase_assign_rx(group, v, rx, true, ...);					\
	}											\
})

#define GROUP_L		0
#define GROUP_M		1
#define GROUP_H		2

struct mt7996_mcu_txbf_fbk_info {

	struct mt7996_mcu_bf_basic_event event;

	__le32 u4DeQInterval;     /* By ms */
	__le32 u4PollPFMUIntrStatTimeOut; /* micro-sec */
	__le32 u4RptPktTimeOutListNum;
	__le32 u4RptPktListNum;
	__le32 u4PFMUWRTimeOutCnt;
	__le32 u4PFMUWRFailCnt;
	__le32 u4PFMUWRDoneCnt;
	__le32 u4PFMUWRTimeoutFreeCnt;
	__le32 u4FbRptPktDropCnt;
	__le32 au4RxPerStaFbRptCnt[CFG_BF_STA_REC_NUM];
};

struct mt7996_mcu_tx_snd_info {

	struct mt7996_mcu_bf_basic_event event;

	u8 vht_opt;
	u8 he_opt;
	u8 glo_opt;
	u8 __rsv;
	__le32 snd_rec_su_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_vht_mu_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_he_tb_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_eht_tb_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le16 wlan_idx_for_mc_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 wlan_idx_for_he_tb_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 wlan_idx_for_eht_tb_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 ul_length;
	u8 mcs;
	u8 ldpc;
	struct uni_event_bf_txsnd_sta_info snd_sta_info[CFG_BF_STA_REC_NUM];
};

struct mt7996_ibf_cal_info {
	struct mt7996_mcu_bf_basic_event event;

	u8 category_id;
	u8 group_l_m_n;
	u8 group;
	bool sx2;
	u8 status;
	u8 cal_type;
	u8 nsts;
	u8 version;
	union {
		struct {
			struct mt7996_txbf_phase_out phase_out;
			union {
				struct mt7996_txbf_phase_info_2g phase_2g;
				struct mt7996_txbf_phase_info_5g phase_5g;
			};
		} v1;
		struct {
			struct mt7992_txbf_phase_out phase_out;
			union {
				struct mt7992_txbf_phase_info_2g phase_2g;
				struct mt7992_txbf_phase_info_5g phase_5g;
			};
		} v2;
		u8 buf[64];
	};
} __packed;

enum {
	IBF_PHASE_CAL_UNSPEC,
	IBF_PHASE_CAL_NORMAL,
	IBF_PHASE_CAL_VERIFY,
	IBF_PHASE_CAL_NORMAL_INSTRUMENT,
	IBF_PHASE_CAL_VERIFY_INSTRUMENT,
};

enum ibf_version {
	IBF_VER_1,
	IBF_VER_2 = 3,
};

static inline int get_ibf_version(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case 0x7990:
		return IBF_VER_1;
	case 0x7992:
	default:
		return IBF_VER_2;
	}
}

enum {
	EDCCA_CTRL_SET_EN = 0,
	EDCCA_CTRL_SET_THRES,
	EDCCA_CTRL_GET_EN,
	EDCCA_CTRL_GET_THRES,
	EDCCA_CTRL_NUM,
};

enum {
	EDCCA_DEFAULT = 0,
	EDCCA_FCC = 1,
	EDCCA_ETSI = 2,
	EDCCA_JAPAN = 3,
	EDCCA_ETSI_2023 = 4,
};

#ifdef CONFIG_MTK_VENDOR
struct mt7996_mcu_csi_event {
	struct mt7996_mcu_rxd rxd;

	u8 band_idx;
	u8 _rsv[3];

	__le16 tag;
	__le16 len;
	u8 tlv_buf[0];
};

enum UNI_EVENT_CSI_TAG_T {
	UNI_EVENT_CSI_DATA = 0,
	UNI_EVENT_CSI_MAX_NUM
};

struct csi_tlv {
	struct {
		__le32 tag;
		__le32 len;
	} basic;
	union {
		u8 mac[ETH_ALEN];
		__le32 info;
		s16 data[0];
	};
} __packed;

struct csi_bitmap_info_update {
	u8 action;
	u8 addr[ETH_ALEN];
};

#define CSI_MAX_BUF_NUM	3000

enum CSI_EVENT_TLV_TAG {
	CSI_EVENT_FW_VER,
	CSI_EVENT_CBW,
	CSI_EVENT_RSSI,
	CSI_EVENT_SNR,
	CSI_EVENT_BAND,
	CSI_EVENT_CSI_NUM,
	CSI_EVENT_CSI_I_DATA,
	CSI_EVENT_CSI_Q_DATA,
	CSI_EVENT_DBW,
	CSI_EVENT_CH_IDX,
	CSI_EVENT_TA,
	CSI_EVENT_EXTRA_INFO,
	CSI_EVENT_RX_MODE,
	CSI_EVENT_RSVD1,
	CSI_EVENT_RSVD2,
	CSI_EVENT_RSVD3,
	CSI_EVENT_RSVD4,
	CSI_EVENT_H_IDX,
	CSI_EVENT_TX_RX_IDX,
	CSI_EVENT_TS,
	CSI_EVENT_PKT_SN,
	CSI_EVENT_BW_SEG,
	CSI_EVENT_REMAIN_LAST,
	CSI_EVENT_TR_STREAM,
	CSI_EVENT_TLV_TAG_NUM,
};

enum CSI_CHAIN_TYPE {
	CSI_CHAIN_ERR,
	CSI_CHAIN_COMPLETE,
	CSI_CHAIN_SEGMENT_FIRST,
	CSI_CHAIN_SEGMENT_MIDDLE,
	CSI_CHAIN_SEGMENT_LAST,
	CSI_CHAIN_SEGMENT_ERR,
};

enum CSI_CONTROL_MODE_T {
	CSI_CONTROL_MODE_STOP,
	CSI_CONTROL_MODE_START,
	CSI_CONTROL_MODE_SET,
	CSI_CONTROL_MODE_NUM
};

enum CSI_CONFIG_ITEM_T {
	CSI_CONFIG_RSVD1,
	CSI_CONFIG_WF,
	CSI_CONFIG_RSVD2,
	CSI_CONFIG_FRAME_TYPE,
	CSI_CONFIG_TX_PATH,
	CSI_CONFIG_OUTPUT_FORMAT,
	CSI_CONFIG_INFO,
	CSI_CONFIG_CHAIN_FILTER,
	CSI_CONFIG_STA_FILTER,
	CSI_CONFIG_ACTIVE_MODE,
	CSI_CONFIG_ITEM_NUM
};

/* CSI config Tag */
enum UNI_CMD_CSI_TAG_T {
	UNI_CMD_CSI_STOP = 0,
	UNI_CMD_CSI_START = 1,
	UNI_CMD_CSI_SET_FRAME_TYPE = 2,
	UNI_CMD_CSI_SET_CHAIN_FILTER = 3,
	UNI_CMD_CSI_SET_STA_FILTER = 4,
	UNI_CMD_CSI_SET_ACTIVE_MODE = 5,
};
#endif

#endif
