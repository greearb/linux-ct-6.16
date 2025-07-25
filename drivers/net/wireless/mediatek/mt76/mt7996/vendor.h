#ifndef __MT7996_VENDOR_H
#define __MT7996_VENDOR_H

#define MTK_NL80211_VENDOR_ID	0x0ce7

#ifdef CONFIG_MTK_VENDOR

enum mtk_nl80211_vendor_subcmds {
	MTK_NL80211_VENDOR_SUBCMD_AMNT_CTRL = 0xae,
	MTK_NL80211_VENDOR_SUBCMD_CSI_CTRL = 0xc2,
	MTK_NL80211_VENDOR_SUBCMD_RFEATURE_CTRL = 0xc3,
	MTK_NL80211_VENDOR_SUBCMD_WIRELESS_CTRL = 0xc4,
	MTK_NL80211_VENDOR_SUBCMD_MU_CTRL = 0xc5,
	MTK_NL80211_VENDOR_SUBCMD_EDCCA_CTRL = 0xc7,
	MTK_NL80211_VENDOR_SUBCMD_3WIRE_CTRL = 0xc8,
	MTK_NL80211_VENDOR_SUBCMD_IBF_CTRL = 0xc9,
	MTK_NL80211_VENDOR_SUBCMD_BSS_COLOR_CTRL = 0xca,
	MTK_NL80211_VENDOR_SUBCMD_BACKGROUND_RADAR_CTRL = 0xcb,
	MTK_NL80211_VENDOR_SUBCMD_PP_CTRL = 0xcc,
	MTK_NL80211_VENDOR_SUBCMD_BEACON_CTRL = 0xcd,
	MTK_NL80211_VENDOR_SUBCMD_SCS_CTRL = 0xd0,
	MTK_NL80211_VENDOR_SUBCMD_EML_CTRL = 0xd3,
	MTK_NL80211_VENDOR_SUBCMD_EPCS_CTRL = 0xd4,
	MTK_NL80211_VENDOR_SUBCMD_DFS_TX_CTRL = 0xd5,
};

enum mtk_nl80211_vendor_events {
	MTK_NL80211_VENDOR_EVENT_PP_BMP_UPDATE = 0x5,
};

enum mtk_vendor_attr_edcca_ctrl {
	MTK_VENDOR_ATTR_EDCCA_THRESHOLD_INVALID = 0,

	MTK_VENDOR_ATTR_EDCCA_CTRL_MODE,
	MTK_VENDOR_ATTR_EDCCA_CTRL_PRI20_VAL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_SEC20_VAL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_SEC40_VAL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_SEC80_VAL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_COMPENSATE,
	MTK_VENDOR_ATTR_EDCCA_CTRL_SEC160_VAL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_RADIO_IDX,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_EDCCA_CTRL,
	MTK_VENDOR_ATTR_EDCCA_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_EDCCA_CTRL - 1
};

enum mtk_vendor_attr_edcca_dump {
	MTK_VENDOR_ATTR_EDCCA_DUMP_UNSPEC = 0,

	MTK_VENDOR_ATTR_EDCCA_DUMP_MODE,
	MTK_VENDOR_ATTR_EDCCA_DUMP_PRI20_VAL,
	MTK_VENDOR_ATTR_EDCCA_DUMP_SEC40_VAL,
	MTK_VENDOR_ATTR_EDCCA_DUMP_SEC80_VAL,
	MTK_VENDOR_ATTR_EDCCA_DUMP_SEC160_VAL,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_EDCCA_DUMP,
	MTK_VENDOR_ATTR_EDCCA_DUMP_MAX =
		NUM_MTK_VENDOR_ATTRS_EDCCA_DUMP - 1
};

enum mtk_vendor_attr_3wire_ctrl {
	MTK_VENDOR_ATTR_3WIRE_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_3WIRE_CTRL_MODE,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_3WIRE_CTRL,
	MTK_VENDOR_ATTR_3WIRE_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_3WIRE_CTRL - 1
};

enum mtk_vendor_attr_csi_ctrl {
	MTK_VENDOR_ATTR_CSI_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_CSI_CTRL_CFG,
	MTK_VENDOR_ATTR_CSI_CTRL_CFG_MODE,
	MTK_VENDOR_ATTR_CSI_CTRL_CFG_TYPE,
	MTK_VENDOR_ATTR_CSI_CTRL_CFG_VAL1,
	MTK_VENDOR_ATTR_CSI_CTRL_CFG_VAL2,
	MTK_VENDOR_ATTR_CSI_CTRL_MAC_ADDR,

	MTK_VENDOR_ATTR_CSI_CTRL_DUMP_NUM,

	MTK_VENDOR_ATTR_CSI_CTRL_DATA,

	MTK_VENDOR_ATTR_CSI_CTRL_RADIO_IDX,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_CSI_CTRL,
	MTK_VENDOR_ATTR_CSI_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_CSI_CTRL - 1
};

enum mtk_vendor_attr_csi_data {
	MTK_VENDOR_ATTR_CSI_DATA_UNSPEC,
	MTK_VENDOR_ATTR_CSI_DATA_PAD,

	MTK_VENDOR_ATTR_CSI_DATA_VER,
	MTK_VENDOR_ATTR_CSI_DATA_TS,
	MTK_VENDOR_ATTR_CSI_DATA_RSSI,
	MTK_VENDOR_ATTR_CSI_DATA_SNR,
	MTK_VENDOR_ATTR_CSI_DATA_BW,
	MTK_VENDOR_ATTR_CSI_DATA_CH_IDX,
	MTK_VENDOR_ATTR_CSI_DATA_TA,
	MTK_VENDOR_ATTR_CSI_DATA_NUM,
	MTK_VENDOR_ATTR_CSI_DATA_I,
	MTK_VENDOR_ATTR_CSI_DATA_Q,
	MTK_VENDOR_ATTR_CSI_DATA_INFO,
	MTK_VENDOR_ATTR_CSI_DATA_RSVD1,
	MTK_VENDOR_ATTR_CSI_DATA_RSVD2,
	MTK_VENDOR_ATTR_CSI_DATA_RSVD3,
	MTK_VENDOR_ATTR_CSI_DATA_RSVD4,
	MTK_VENDOR_ATTR_CSI_DATA_TX_ANT,
	MTK_VENDOR_ATTR_CSI_DATA_RX_ANT,
	MTK_VENDOR_ATTR_CSI_DATA_MODE,
	MTK_VENDOR_ATTR_CSI_DATA_CHAIN_INFO,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_CSI_DATA,
	MTK_VENDOR_ATTR_CSI_DATA_MAX =
		NUM_MTK_VENDOR_ATTRS_CSI_DATA - 1
};

enum mtk_vendor_attr_mnt_ctrl {
	MTK_VENDOR_ATTR_AMNT_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_AMNT_CTRL_SET,
	MTK_VENDOR_ATTR_AMNT_CTRL_DUMP,
	MTK_VENDOR_ATTR_AMNT_CTRL_LINK_ID,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_AMNT_CTRL,
	MTK_VENDOR_ATTR_AMNT_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_AMNT_CTRL - 1
};

enum mtk_vendor_attr_mnt_set {
	MTK_VENDOR_ATTR_AMNT_SET_UNSPEC,

	MTK_VENDOR_ATTR_AMNT_SET_INDEX,
	MTK_VENDOR_ATTR_AMNT_SET_MACADDR,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_AMNT_SET,
	MTK_VENDOR_ATTR_AMNT_SET_MAX =
		NUM_MTK_VENDOR_ATTRS_AMNT_SET - 1
};

enum mtk_vendor_attr_mnt_dump {
	MTK_VENDOR_ATTR_AMNT_DUMP_UNSPEC,

	MTK_VENDOR_ATTR_AMNT_DUMP_INDEX,
	MTK_VENDOR_ATTR_AMNT_DUMP_LEN,
	MTK_VENDOR_ATTR_AMNT_DUMP_RESULT,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_AMNT_DUMP,
	MTK_VENDOR_ATTR_AMNT_DUMP_MAX =
		NUM_MTK_VENDOR_ATTRS_AMNT_DUMP - 1
};

enum mtk_vendor_attr_wireless_ctrl {
	MTK_VENDOR_ATTR_WIRELESS_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_WIRELESS_CTRL_FIXED_MCS,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_FIXED_OFDMA,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_PPDU_TX_TYPE,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_NUSERS_OFDMA,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_BA_BUFFER_SIZE,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_MIMO,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_AMSDU,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_AMPDU,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_CERT = 9,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_RTS_SIGTA,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_MU_EDCA, /* reserve */
	MTK_VENDOR_ATTR_WIRELESS_CTRL_LINK_ID,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_WIRELESS_CTRL,
	MTK_VENDOR_ATTR_WIRELESS_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_WIRELESS_CTRL - 1
};

enum mtk_vendor_attr_wireless_dump {
	MTK_VENDOR_ATTR_WIRELESS_DUMP_UNSPEC,

	MTK_VENDOR_ATTR_WIRELESS_DUMP_AMSDU,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_WIRELESS_DUMP,
	MTK_VENDOR_ATTR_WIRELESS_DUMP_MAX =
		NUM_MTK_VENDOR_ATTRS_WIRELESS_DUMP - 1
};

enum mtk_vendor_attr_rfeature_ctrl {
	MTK_VENDOR_ATTR_RFEATURE_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_RFEATURE_CTRL_HE_GI,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_HE_LTF,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE_CFG,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE_EN,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_ACK_PLCY,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TXBF,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_VARIANT_TYPE,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_CODING_TYPE,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_LINK_ID,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_RFEATURE_CTRL,
	MTK_VENDOR_ATTR_RFEATURE_CTRL_MAX =
	NUM_MTK_VENDOR_ATTRS_RFEATURE_CTRL - 1
};

enum mtk_vendor_attr_mu_ctrl {
	MTK_VENDOR_ATTR_MU_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_MU_CTRL_ONOFF,
	MTK_VENDOR_ATTR_MU_CTRL_DUMP,
	MTK_VENDOR_ATTR_MU_CTRL_STRUCT,
	MTK_VENDOR_ATTR_MU_CTRL_RADIO_IDX,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_MU_CTRL,
	MTK_VENDOR_ATTR_MU_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_MU_CTRL - 1
};

enum mtk_vendor_attr_ibf_ctrl {
	MTK_VENDOR_ATTR_IBF_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_IBF_CTRL_ENABLE,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_IBF_CTRL,
	MTK_VENDOR_ATTR_IBF_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_IBF_CTRL - 1
};

enum mtk_vendor_attr_ibf_dump {
	MTK_VENDOR_ATTR_IBF_DUMP_UNSPEC,

	MTK_VENDOR_ATTR_IBF_DUMP_ENABLE,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_IBF_DUMP,
	MTK_VENDOR_ATTR_IBF_DUMP_MAX =
		NUM_MTK_VENDOR_ATTRS_IBF_DUMP - 1
};

enum bw_sig {
	BW_SIGNALING_DISABLE,
	BW_SIGNALING_STATIC,
	BW_SIGNALING_DYNAMIC
};

enum mtk_vendor_attr_bss_color_ctrl {
	MTK_VENDOR_ATTR_BSS_COLOR_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_AVAL_BSS_COLOR_BMP,
	MTK_VENDOR_ATTR_AVAL_BSS_COLOR_LINK_ID,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_BSS_COLOR_CTRL,
	MTK_VENDOR_ATTR_BSS_COLOR_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_BSS_COLOR_CTRL - 1
};

enum mtk_vendor_attr_background_radar_ctrl {
	MTK_VENDOR_ATTR_BACKGROUND_RADAR_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_BACKGROUND_RADAR_CTRL_MODE,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_BACKGROUND_RADAR_CTRL,
	MTK_VENDOR_ATTR_BACKGROUND_RADAR_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_BACKGROUND_RADAR_CTRL - 1
};

enum mtk_vendor_attr_pp_ctrl {
	MTK_VENDOR_ATTR_PP_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_PP_MODE,
	MTK_VENDOR_ATTR_PP_LINK_ID,
	MTK_VENDOR_ATTR_PP_BITMAP,
	MTK_VENDOR_ATTR_PP_CURR_FREQ,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_PP_CTRL,
	MTK_VENDOR_ATTR_PP_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_PP_CTRL - 1
};

enum mtk_vendor_attr_beacon_ctrl {
	MTK_VENDOR_ATTR_BEACON_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_BEACON_CTRL_MODE,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_BEACON_CTRL,
	MTK_VENDOR_ATTR_BEACON_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_BEACON_CTRL - 1
};

enum mtk_vendor_attr_eml_ctrl {

	MTK_VENDOR_ATTR_EML_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_EML_LINK_ID,
	MTK_VENDOR_ATTR_EML_STA_ADDR,
	MTK_VENDOR_ATTR_EML_CTRL_STRUCT,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_EML_CTRL,
	MTK_VENDOR_ATTR_EML_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_EML_CTRL - 1
};

enum mtk_vendor_attr_dfs_tx_ctrl {
	MTK_VENDOR_ATTR_DFS_TX_CTRL_UNSPEC,

	MTK_VENDOR_ATTR_DFS_TX_CTRL_MODE,
	MTK_VENDOR_ATTR_DFS_TX_CTRL_RADIO_IDX,

	/* keep last */
	NUM_MTK_VENDOR_ATTRS_DFS_TX_CTRL,
	MTK_VENDOR_ATTR_DFS_TX_CTRL_MAX =
		NUM_MTK_VENDOR_ATTRS_DFS_TX_CTRL - 1
};

#endif

#endif
