#ifndef __MTK_DEBUG_I_H
#define __MTK_DEBUG_I_H

#ifdef CONFIG_MTK_DEBUG

// DW0
#define WF_RX_DESCRIPTOR_RX_BYTE_COUNT_DW                                   0
#define WF_RX_DESCRIPTOR_RX_BYTE_COUNT_ADDR                                 0
#define WF_RX_DESCRIPTOR_RX_BYTE_COUNT_MASK                                 0x0000ffff // 15- 0
#define WF_RX_DESCRIPTOR_RX_BYTE_COUNT_SHIFT                                0
#define WF_RX_DESCRIPTOR_PACKET_TYPE_DW                                     0
#define WF_RX_DESCRIPTOR_PACKET_TYPE_ADDR                                   0
#define WF_RX_DESCRIPTOR_PACKET_TYPE_MASK                                   0xf8000000 // 31-27
#define WF_RX_DESCRIPTOR_PACKET_TYPE_SHIFT                                  27
// DW1
#define WF_RX_DESCRIPTOR_MLD_ID_DW                                          1
#define WF_RX_DESCRIPTOR_MLD_ID_ADDR                                        4
#define WF_RX_DESCRIPTOR_MLD_ID_MASK                                        0x00000fff // 11- 0
#define WF_RX_DESCRIPTOR_MLD_ID_SHIFT                                       0
#define WF_RX_DESCRIPTOR_GROUP_VLD_DW                                       1
#define WF_RX_DESCRIPTOR_GROUP_VLD_ADDR                                     4
#define WF_RX_DESCRIPTOR_GROUP_VLD_MASK                                     0x001f0000 // 20-16
#define WF_RX_DESCRIPTOR_GROUP_VLD_SHIFT                                    16
#define WF_RX_DESCRIPTOR_KID_DW                                             1
#define WF_RX_DESCRIPTOR_KID_ADDR                                           4
#define WF_RX_DESCRIPTOR_KID_MASK                                           0x00600000 // 22-21
#define WF_RX_DESCRIPTOR_KID_SHIFT                                          21
#define WF_RX_DESCRIPTOR_CM_DW                                              1
#define WF_RX_DESCRIPTOR_CM_ADDR                                            4
#define WF_RX_DESCRIPTOR_CM_MASK                                            0x00800000 // 23-23
#define WF_RX_DESCRIPTOR_CM_SHIFT                                           23
#define WF_RX_DESCRIPTOR_CLM_DW                                             1
#define WF_RX_DESCRIPTOR_CLM_ADDR                                           4
#define WF_RX_DESCRIPTOR_CLM_MASK                                           0x01000000 // 24-24
#define WF_RX_DESCRIPTOR_CLM_SHIFT                                          24
#define WF_RX_DESCRIPTOR_I_DW                                               1
#define WF_RX_DESCRIPTOR_I_ADDR                                             4
#define WF_RX_DESCRIPTOR_I_MASK                                             0x02000000 // 25-25
#define WF_RX_DESCRIPTOR_I_SHIFT                                            25
#define WF_RX_DESCRIPTOR_T_DW                                               1
#define WF_RX_DESCRIPTOR_T_ADDR                                             4
#define WF_RX_DESCRIPTOR_T_MASK                                             0x04000000 // 26-26
#define WF_RX_DESCRIPTOR_T_SHIFT                                            26
#define WF_RX_DESCRIPTOR_BN_DW                                              1
#define WF_RX_DESCRIPTOR_BN_ADDR                                            4
#define WF_RX_DESCRIPTOR_BN_MASK                                            0x18000000 // 28-27
#define WF_RX_DESCRIPTOR_BN_SHIFT                                           27
#define WF_RX_DESCRIPTOR_BIPN_FAIL_DW                                       1
#define WF_RX_DESCRIPTOR_BIPN_FAIL_ADDR                                     4
#define WF_RX_DESCRIPTOR_BIPN_FAIL_MASK                                     0x20000000 // 29-29
#define WF_RX_DESCRIPTOR_BIPN_FAIL_SHIFT                                    29
// DW2
#define WF_RX_DESCRIPTOR_BSSID_DW                                           2
#define WF_RX_DESCRIPTOR_BSSID_ADDR                                         8
#define WF_RX_DESCRIPTOR_BSSID_MASK                                         0x0000003f //  5- 0
#define WF_RX_DESCRIPTOR_BSSID_SHIFT                                        0
#define WF_RX_DESCRIPTOR_H_DW                                               2
#define WF_RX_DESCRIPTOR_H_ADDR                                             8
#define WF_RX_DESCRIPTOR_H_MASK                                             0x00000080 //  7- 7
#define WF_RX_DESCRIPTOR_H_SHIFT                                            7
#define WF_RX_DESCRIPTOR_HEADER_LENGTH_DW                                   2
#define WF_RX_DESCRIPTOR_HEADER_LENGTH_ADDR                                 8
#define WF_RX_DESCRIPTOR_HEADER_LENGTH_MASK                                 0x00001f00 // 12- 8
#define WF_RX_DESCRIPTOR_HEADER_LENGTH_SHIFT                                8
#define WF_RX_DESCRIPTOR_HO_DW                                              2
#define WF_RX_DESCRIPTOR_HO_ADDR                                            8
#define WF_RX_DESCRIPTOR_HO_MASK                                            0x0000e000 // 15-13
#define WF_RX_DESCRIPTOR_HO_SHIFT                                           13
#define WF_RX_DESCRIPTOR_SEC_MODE_DW                                        2
#define WF_RX_DESCRIPTOR_SEC_MODE_ADDR                                      8
#define WF_RX_DESCRIPTOR_SEC_MODE_MASK                                      0x001f0000 // 20-16
#define WF_RX_DESCRIPTOR_SEC_MODE_SHIFT                                     16
#define WF_RX_DESCRIPTOR_MUBAR_DW                                           2
#define WF_RX_DESCRIPTOR_MUBAR_ADDR                                         8
#define WF_RX_DESCRIPTOR_MUBAR_MASK                                         0x00200000 // 21-21
#define WF_RX_DESCRIPTOR_MUBAR_SHIFT                                        21
#define WF_RX_DESCRIPTOR_SWBIT_DW                                           2
#define WF_RX_DESCRIPTOR_SWBIT_ADDR                                         8
#define WF_RX_DESCRIPTOR_SWBIT_MASK                                         0x00400000 // 22-22
#define WF_RX_DESCRIPTOR_SWBIT_SHIFT                                        22
#define WF_RX_DESCRIPTOR_DAF_DW                                             2
#define WF_RX_DESCRIPTOR_DAF_ADDR                                           8
#define WF_RX_DESCRIPTOR_DAF_MASK                                           0x00800000 // 23-23
#define WF_RX_DESCRIPTOR_DAF_SHIFT                                          23
#define WF_RX_DESCRIPTOR_EL_DW                                              2
#define WF_RX_DESCRIPTOR_EL_ADDR                                            8
#define WF_RX_DESCRIPTOR_EL_MASK                                            0x01000000 // 24-24
#define WF_RX_DESCRIPTOR_EL_SHIFT                                           24
#define WF_RX_DESCRIPTOR_HTF_DW                                             2
#define WF_RX_DESCRIPTOR_HTF_ADDR                                           8
#define WF_RX_DESCRIPTOR_HTF_MASK                                           0x02000000 // 25-25
#define WF_RX_DESCRIPTOR_HTF_SHIFT                                          25
#define WF_RX_DESCRIPTOR_INTF_DW                                            2
#define WF_RX_DESCRIPTOR_INTF_ADDR                                          8
#define WF_RX_DESCRIPTOR_INTF_MASK                                          0x04000000 // 26-26
#define WF_RX_DESCRIPTOR_INTF_SHIFT                                         26
#define WF_RX_DESCRIPTOR_FRAG_DW                                            2
#define WF_RX_DESCRIPTOR_FRAG_ADDR                                          8
#define WF_RX_DESCRIPTOR_FRAG_MASK                                          0x08000000 // 27-27
#define WF_RX_DESCRIPTOR_FRAG_SHIFT                                         27
#define WF_RX_DESCRIPTOR_NUL_DW                                             2
#define WF_RX_DESCRIPTOR_NUL_ADDR                                           8
#define WF_RX_DESCRIPTOR_NUL_MASK                                           0x10000000 // 28-28
#define WF_RX_DESCRIPTOR_NUL_SHIFT                                          28
#define WF_RX_DESCRIPTOR_NDATA_DW                                           2
#define WF_RX_DESCRIPTOR_NDATA_ADDR                                         8
#define WF_RX_DESCRIPTOR_NDATA_MASK                                         0x20000000 // 29-29
#define WF_RX_DESCRIPTOR_NDATA_SHIFT                                        29
#define WF_RX_DESCRIPTOR_NAMP_DW                                            2
#define WF_RX_DESCRIPTOR_NAMP_ADDR                                          8
#define WF_RX_DESCRIPTOR_NAMP_MASK                                          0x40000000 // 30-30
#define WF_RX_DESCRIPTOR_NAMP_SHIFT                                         30
#define WF_RX_DESCRIPTOR_BF_RPT_DW                                          2
#define WF_RX_DESCRIPTOR_BF_RPT_ADDR                                        8
#define WF_RX_DESCRIPTOR_BF_RPT_MASK                                        0x80000000 // 31-31
#define WF_RX_DESCRIPTOR_BF_RPT_SHIFT                                       31
// DW3
#define WF_RX_DESCRIPTOR_RXV_SN_DW                                          3
#define WF_RX_DESCRIPTOR_RXV_SN_ADDR                                        12
#define WF_RX_DESCRIPTOR_RXV_SN_MASK                                        0x000000ff //  7- 0
#define WF_RX_DESCRIPTOR_RXV_SN_SHIFT                                       0
#define WF_RX_DESCRIPTOR_CH_FREQUENCY_DW                                    3
#define WF_RX_DESCRIPTOR_CH_FREQUENCY_ADDR                                  12
#define WF_RX_DESCRIPTOR_CH_FREQUENCY_MASK                                  0x0000ff00 // 15- 8
#define WF_RX_DESCRIPTOR_CH_FREQUENCY_SHIFT                                 8
#define WF_RX_DESCRIPTOR_A1_TYPE_DW                                         3
#define WF_RX_DESCRIPTOR_A1_TYPE_ADDR                                       12
#define WF_RX_DESCRIPTOR_A1_TYPE_MASK                                       0x00030000 // 17-16
#define WF_RX_DESCRIPTOR_A1_TYPE_SHIFT                                      16
#define WF_RX_DESCRIPTOR_HTC_DW                                             3
#define WF_RX_DESCRIPTOR_HTC_ADDR                                           12
#define WF_RX_DESCRIPTOR_HTC_MASK                                           0x00040000 // 18-18
#define WF_RX_DESCRIPTOR_HTC_SHIFT                                          18
#define WF_RX_DESCRIPTOR_TCL_DW                                             3
#define WF_RX_DESCRIPTOR_TCL_ADDR                                           12
#define WF_RX_DESCRIPTOR_TCL_MASK                                           0x00080000 // 19-19
#define WF_RX_DESCRIPTOR_TCL_SHIFT                                          19
#define WF_RX_DESCRIPTOR_BBM_DW                                             3
#define WF_RX_DESCRIPTOR_BBM_ADDR                                           12
#define WF_RX_DESCRIPTOR_BBM_MASK                                           0x00100000 // 20-20
#define WF_RX_DESCRIPTOR_BBM_SHIFT                                          20
#define WF_RX_DESCRIPTOR_BU_DW                                              3
#define WF_RX_DESCRIPTOR_BU_ADDR                                            12
#define WF_RX_DESCRIPTOR_BU_MASK                                            0x00200000 // 21-21
#define WF_RX_DESCRIPTOR_BU_SHIFT                                           21
#define WF_RX_DESCRIPTOR_CO_ANT_DW                                          3
#define WF_RX_DESCRIPTOR_CO_ANT_ADDR                                        12
#define WF_RX_DESCRIPTOR_CO_ANT_MASK                                        0x00400000 // 22-22
#define WF_RX_DESCRIPTOR_CO_ANT_SHIFT                                       22
#define WF_RX_DESCRIPTOR_BF_CQI_DW                                          3
#define WF_RX_DESCRIPTOR_BF_CQI_ADDR                                        12
#define WF_RX_DESCRIPTOR_BF_CQI_MASK                                        0x00800000 // 23-23
#define WF_RX_DESCRIPTOR_BF_CQI_SHIFT                                       23
#define WF_RX_DESCRIPTOR_FC_DW                                              3
#define WF_RX_DESCRIPTOR_FC_ADDR                                            12
#define WF_RX_DESCRIPTOR_FC_MASK                                            0x01000000 // 24-24
#define WF_RX_DESCRIPTOR_FC_SHIFT                                           24
#define WF_RX_DESCRIPTOR_VLAN_DW                                            3
#define WF_RX_DESCRIPTOR_VLAN_ADDR                                          12
#define WF_RX_DESCRIPTOR_VLAN_MASK                                          0x80000000 // 31-31
#define WF_RX_DESCRIPTOR_VLAN_SHIFT                                         31
// DW4
#define WF_RX_DESCRIPTOR_PF_DW                                              4
#define WF_RX_DESCRIPTOR_PF_ADDR                                            16
#define WF_RX_DESCRIPTOR_PF_MASK                                            0x00000003 //  1- 0
#define WF_RX_DESCRIPTOR_PF_SHIFT                                           0
#define WF_RX_DESCRIPTOR_MAC_DW                                             4
#define WF_RX_DESCRIPTOR_MAC_ADDR                                           16
#define WF_RX_DESCRIPTOR_MAC_MASK                                           0x00000004 //  2- 2
#define WF_RX_DESCRIPTOR_MAC_SHIFT                                          2
#define WF_RX_DESCRIPTOR_TID_DW                                             4
#define WF_RX_DESCRIPTOR_TID_ADDR                                           16
#define WF_RX_DESCRIPTOR_TID_MASK                                           0x00000078 //  6- 3
#define WF_RX_DESCRIPTOR_TID_SHIFT                                          3
#define WF_RX_DESCRIPTOR_ETHER_TYPE_OFFSET_DW                               4
#define WF_RX_DESCRIPTOR_ETHER_TYPE_OFFSET_ADDR                             16
#define WF_RX_DESCRIPTOR_ETHER_TYPE_OFFSET_MASK                             0x00003f80 // 13- 7
#define WF_RX_DESCRIPTOR_ETHER_TYPE_OFFSET_SHIFT                            7
#define WF_RX_DESCRIPTOR_IP_DW                                              4
#define WF_RX_DESCRIPTOR_IP_ADDR                                            16
#define WF_RX_DESCRIPTOR_IP_MASK                                            0x00004000 // 14-14
#define WF_RX_DESCRIPTOR_IP_SHIFT                                           14
#define WF_RX_DESCRIPTOR_UT_DW                                              4
#define WF_RX_DESCRIPTOR_UT_ADDR                                            16
#define WF_RX_DESCRIPTOR_UT_MASK                                            0x00008000 // 15-15
#define WF_RX_DESCRIPTOR_UT_SHIFT                                           15
#define WF_RX_DESCRIPTOR_PSE_FID_DW                                         4
#define WF_RX_DESCRIPTOR_PSE_FID_ADDR                                       16
#define WF_RX_DESCRIPTOR_PSE_FID_MASK                                       0x0fff0000 // 27-16
#define WF_RX_DESCRIPTOR_PSE_FID_SHIFT                                      16
// DW5
// DW6
#define WF_RX_DESCRIPTOR_CLS_BITMAP_31_0__DW                                6
#define WF_RX_DESCRIPTOR_CLS_BITMAP_31_0__ADDR                              24
#define WF_RX_DESCRIPTOR_CLS_BITMAP_31_0__MASK                              0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_CLS_BITMAP_31_0__SHIFT                             0
// DW7
#define WF_RX_DESCRIPTOR_CLS_BITMAP_33_32__DW                               7
#define WF_RX_DESCRIPTOR_CLS_BITMAP_33_32__ADDR                             28
#define WF_RX_DESCRIPTOR_CLS_BITMAP_33_32__MASK                             0x00000003 //  1- 0
#define WF_RX_DESCRIPTOR_CLS_BITMAP_33_32__SHIFT                            0
#define WF_RX_DESCRIPTOR_DP_DW                                              7
#define WF_RX_DESCRIPTOR_DP_ADDR                                            28
#define WF_RX_DESCRIPTOR_DP_MASK                                            0x00080000 // 19-19
#define WF_RX_DESCRIPTOR_DP_SHIFT                                           19
#define WF_RX_DESCRIPTOR_CLS_DW                                             7
#define WF_RX_DESCRIPTOR_CLS_ADDR                                           28
#define WF_RX_DESCRIPTOR_CLS_MASK                                           0x00100000 // 20-20
#define WF_RX_DESCRIPTOR_CLS_SHIFT                                          20
#define WF_RX_DESCRIPTOR_OFLD_DW                                            7
#define WF_RX_DESCRIPTOR_OFLD_ADDR                                          28
#define WF_RX_DESCRIPTOR_OFLD_MASK                                          0x00600000 // 22-21
#define WF_RX_DESCRIPTOR_OFLD_SHIFT                                         21
#define WF_RX_DESCRIPTOR_MGC_DW                                             7
#define WF_RX_DESCRIPTOR_MGC_ADDR                                           28
#define WF_RX_DESCRIPTOR_MGC_MASK                                           0x00800000 // 23-23
#define WF_RX_DESCRIPTOR_MGC_SHIFT                                          23
#define WF_RX_DESCRIPTOR_WOL_DW                                             7
#define WF_RX_DESCRIPTOR_WOL_ADDR                                           28
#define WF_RX_DESCRIPTOR_WOL_MASK                                           0x1f000000 // 28-24
#define WF_RX_DESCRIPTOR_WOL_SHIFT                                          24
#define WF_RX_DESCRIPTOR_PF_MODE_DW                                         7
#define WF_RX_DESCRIPTOR_PF_MODE_ADDR                                       28
#define WF_RX_DESCRIPTOR_PF_MODE_MASK                                       0x20000000 // 29-29
#define WF_RX_DESCRIPTOR_PF_MODE_SHIFT                                      29
#define WF_RX_DESCRIPTOR_PF_STS_DW                                          7
#define WF_RX_DESCRIPTOR_PF_STS_ADDR                                        28
#define WF_RX_DESCRIPTOR_PF_STS_MASK                                        0xc0000000 // 31-30
#define WF_RX_DESCRIPTOR_PF_STS_SHIFT                                       30
// DW8
#define WF_RX_DESCRIPTOR_FRAME_CONTROL_FIELD_DW                             8
#define WF_RX_DESCRIPTOR_FRAME_CONTROL_FIELD_ADDR                           32
#define WF_RX_DESCRIPTOR_FRAME_CONTROL_FIELD_MASK                           0x0000ffff // 15- 0
#define WF_RX_DESCRIPTOR_FRAME_CONTROL_FIELD_SHIFT                          0
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_15_0__DW                          8
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_15_0__ADDR                        32
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_15_0__MASK                        0xffff0000 // 31-16
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_15_0__SHIFT                       16
// DW9
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_47_16__DW                         9
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_47_16__ADDR                       36
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_47_16__MASK                       0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_PEER_MLD_ADDRESS_47_16__SHIFT                      0
// DW10
#define WF_RX_DESCRIPTOR_FRAGMENT_NUMBER_DW                                 10
#define WF_RX_DESCRIPTOR_FRAGMENT_NUMBER_ADDR                               40
#define WF_RX_DESCRIPTOR_FRAGMENT_NUMBER_MASK                               0x0000000f //  3- 0
#define WF_RX_DESCRIPTOR_FRAGMENT_NUMBER_SHIFT                              0
#define WF_RX_DESCRIPTOR_SEQUENCE_NUMBER_DW                                 10
#define WF_RX_DESCRIPTOR_SEQUENCE_NUMBER_ADDR                               40
#define WF_RX_DESCRIPTOR_SEQUENCE_NUMBER_MASK                               0x0000fff0 // 15- 4
#define WF_RX_DESCRIPTOR_SEQUENCE_NUMBER_SHIFT                              4
#define WF_RX_DESCRIPTOR_QOS_CONTROL_FIELD_DW                               10
#define WF_RX_DESCRIPTOR_QOS_CONTROL_FIELD_ADDR                             40
#define WF_RX_DESCRIPTOR_QOS_CONTROL_FIELD_MASK                             0xffff0000 // 31-16
#define WF_RX_DESCRIPTOR_QOS_CONTROL_FIELD_SHIFT                            16
// DW11
#define WF_RX_DESCRIPTOR_HT_CONTROL_FIELD_DW                                11
#define WF_RX_DESCRIPTOR_HT_CONTROL_FIELD_ADDR                              44
#define WF_RX_DESCRIPTOR_HT_CONTROL_FIELD_MASK                              0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_HT_CONTROL_FIELD_SHIFT                             0
// DW12
#define WF_RX_DESCRIPTOR_PN_31_0__DW                                        12
#define WF_RX_DESCRIPTOR_PN_31_0__ADDR                                      48
#define WF_RX_DESCRIPTOR_PN_31_0__MASK                                      0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_PN_31_0__SHIFT                                     0
// DW13
#define WF_RX_DESCRIPTOR_PN_63_32__DW                                       13
#define WF_RX_DESCRIPTOR_PN_63_32__ADDR                                     52
#define WF_RX_DESCRIPTOR_PN_63_32__MASK                                     0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_PN_63_32__SHIFT                                    0
// DW14
#define WF_RX_DESCRIPTOR_PN_95_64__DW                                       14
#define WF_RX_DESCRIPTOR_PN_95_64__ADDR                                     56
#define WF_RX_DESCRIPTOR_PN_95_64__MASK                                     0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_PN_95_64__SHIFT                                    0
// DW15
#define WF_RX_DESCRIPTOR_PN_127_96__DW                                      15
#define WF_RX_DESCRIPTOR_PN_127_96__ADDR                                    60
#define WF_RX_DESCRIPTOR_PN_127_96__MASK                                    0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_PN_127_96__SHIFT                                   0
// DW16
#define WF_RX_DESCRIPTOR_TIMESTAMP_DW                                       16
#define WF_RX_DESCRIPTOR_TIMESTAMP_ADDR                                     64
#define WF_RX_DESCRIPTOR_TIMESTAMP_MASK                                     0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_TIMESTAMP_SHIFT                                    0
// DW17
#define WF_RX_DESCRIPTOR_CRC_DW                                             17
#define WF_RX_DESCRIPTOR_CRC_ADDR                                           68
#define WF_RX_DESCRIPTOR_CRC_MASK                                           0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_CRC_SHIFT                                          0
// DW18
// DW19
// DW20
#define WF_RX_DESCRIPTOR_P_RXV_DW                                           20
#define WF_RX_DESCRIPTOR_P_RXV_ADDR                                         80
#define WF_RX_DESCRIPTOR_P_RXV_MASK                                         0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_P_RXV_SHIFT                                        0
// DW21
// DO NOT process repeat field(p_rxv)
// DW22
#define WF_RX_DESCRIPTOR_DBW_DW                                             22
#define WF_RX_DESCRIPTOR_DBW_ADDR                                           88
#define WF_RX_DESCRIPTOR_DBW_MASK                                           0x00000007 //  2- 0
#define WF_RX_DESCRIPTOR_DBW_SHIFT                                          0
#define WF_RX_DESCRIPTOR_GI_DW                                              22
#define WF_RX_DESCRIPTOR_GI_ADDR                                            88
#define WF_RX_DESCRIPTOR_GI_MASK                                            0x00000018 //  4- 3
#define WF_RX_DESCRIPTOR_GI_SHIFT                                           3
#define WF_RX_DESCRIPTOR_DCM_DW                                             22
#define WF_RX_DESCRIPTOR_DCM_ADDR                                           88
#define WF_RX_DESCRIPTOR_DCM_MASK                                           0x00000020 //  5- 5
#define WF_RX_DESCRIPTOR_DCM_SHIFT                                          5
#define WF_RX_DESCRIPTOR_NUM_RX_DW                                          22
#define WF_RX_DESCRIPTOR_NUM_RX_ADDR                                        88
#define WF_RX_DESCRIPTOR_NUM_RX_MASK                                        0x000001c0 //  8- 6
#define WF_RX_DESCRIPTOR_NUM_RX_SHIFT                                       6
#define WF_RX_DESCRIPTOR_STBC_DW                                            22
#define WF_RX_DESCRIPTOR_STBC_ADDR                                          88
#define WF_RX_DESCRIPTOR_STBC_MASK                                          0x00000600 // 10- 9
#define WF_RX_DESCRIPTOR_STBC_SHIFT                                         9
#define WF_RX_DESCRIPTOR_TX_MODE_DW                                         22
#define WF_RX_DESCRIPTOR_TX_MODE_ADDR                                       88
#define WF_RX_DESCRIPTOR_TX_MODE_MASK                                       0x00007800 // 14-11
#define WF_RX_DESCRIPTOR_TX_MODE_SHIFT                                      11
// DW23
#define WF_RX_DESCRIPTOR_RCPI_DW                                            23
#define WF_RX_DESCRIPTOR_RCPI_ADDR                                          92
#define WF_RX_DESCRIPTOR_RCPI_MASK                                          0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_RCPI_SHIFT                                         0
// DW24
#define WF_RX_DESCRIPTOR_C_RXV_DW                                           24
#define WF_RX_DESCRIPTOR_C_RXV_ADDR                                         96
#define WF_RX_DESCRIPTOR_C_RXV_MASK                                         0xffffffff // 31- 0
#define WF_RX_DESCRIPTOR_C_RXV_SHIFT                                        0
// DW25
// DO NOT process repeat field(c_rxv)
// DW26
// DO NOT process repeat field(c_rxv)
// DW27
// DO NOT process repeat field(c_rxv)
// DW28
// DO NOT process repeat field(c_rxv)
// DW29
// DO NOT process repeat field(c_rxv)
// DW30
// DO NOT process repeat field(c_rxv)
// DW31
// DO NOT process repeat field(c_rxv)
// DW32
// DO NOT process repeat field(c_rxv)
// DW33
// DO NOT process repeat field(c_rxv)
// DW34
// DO NOT process repeat field(c_rxv)
// DW35
// DO NOT process repeat field(c_rxv)
// DW36
// DO NOT process repeat field(c_rxv)
// DW37
// DO NOT process repeat field(c_rxv)
// DW38
// DO NOT process repeat field(c_rxv)
// DW39
// DO NOT process repeat field(c_rxv)
// DW40
// DO NOT process repeat field(c_rxv)
// DW41
// DO NOT process repeat field(c_rxv)
// DW42
// DO NOT process repeat field(c_rxv)
// DW43
// DO NOT process repeat field(c_rxv)
// DW44
// DO NOT process repeat field(c_rxv)
// DW45
// DO NOT process repeat field(c_rxv)
// DW46
// DW47

/* TXD */
// DW0
#define WF_TX_DESCRIPTOR_TX_BYTE_COUNT_DW                                   0
#define WF_TX_DESCRIPTOR_TX_BYTE_COUNT_ADDR                                 0
#define WF_TX_DESCRIPTOR_TX_BYTE_COUNT_MASK                                 0x0000ffff // 15- 0
#define WF_TX_DESCRIPTOR_TX_BYTE_COUNT_SHIFT                                0
#define WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET_DW                               0
#define WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET_ADDR                             0
#define WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET_MASK                             0x007f0000 // 22-16
#define WF_TX_DESCRIPTOR_ETHER_TYPE_OFFSET_SHIFT                            16
#define WF_TX_DESCRIPTOR_PKT_FT_DW                                          0
#define WF_TX_DESCRIPTOR_PKT_FT_ADDR                                        0
#define WF_TX_DESCRIPTOR_PKT_FT_MASK                                        0x01800000 // 24-23
#define WF_TX_DESCRIPTOR_PKT_FT_SHIFT                                       23
#define WF_TX_DESCRIPTOR_Q_IDX_DW                                           0
#define WF_TX_DESCRIPTOR_Q_IDX_ADDR                                         0
#define WF_TX_DESCRIPTOR_Q_IDX_MASK                                         0xfe000000 // 31-25
#define WF_TX_DESCRIPTOR_Q_IDX_SHIFT                                        25
// DW1
#define WF_TX_DESCRIPTOR_MLD_ID_DW                                          1
#define WF_TX_DESCRIPTOR_MLD_ID_ADDR                                        4
#define WF_TX_DESCRIPTOR_MLD_ID_MASK                                        0x00000fff // 11- 0
#define WF_TX_DESCRIPTOR_MLD_ID_SHIFT                                       0
#define WF_TX_DESCRIPTOR_TGID_DW                                            1
#define WF_TX_DESCRIPTOR_TGID_ADDR                                          4
#define WF_TX_DESCRIPTOR_TGID_MASK                                          0x00003000 // 13-12
#define WF_TX_DESCRIPTOR_TGID_SHIFT                                         12
#define WF_TX_DESCRIPTOR_HF_DW                                              1
#define WF_TX_DESCRIPTOR_HF_ADDR                                            4
#define WF_TX_DESCRIPTOR_HF_MASK                                            0x0000c000 // 15-14
#define WF_TX_DESCRIPTOR_HF_SHIFT                                           14
#define WF_TX_DESCRIPTOR_HEADER_LENGTH_DW                                   1
#define WF_TX_DESCRIPTOR_HEADER_LENGTH_ADDR                                 4
#define WF_TX_DESCRIPTOR_HEADER_LENGTH_MASK                                 0x001f0000 // 20-16
#define WF_TX_DESCRIPTOR_HEADER_LENGTH_SHIFT                                16
#define WF_TX_DESCRIPTOR_MRD_DW                                             1
#define WF_TX_DESCRIPTOR_MRD_ADDR                                           4
#define WF_TX_DESCRIPTOR_MRD_MASK                                           0x00010000 // 16-16
#define WF_TX_DESCRIPTOR_MRD_SHIFT                                          16
#define WF_TX_DESCRIPTOR_EOSP_DW                                            1
#define WF_TX_DESCRIPTOR_EOSP_ADDR                                          4
#define WF_TX_DESCRIPTOR_EOSP_MASK                                          0x00020000 // 17-17
#define WF_TX_DESCRIPTOR_EOSP_SHIFT                                         17
#define WF_TX_DESCRIPTOR_EOSP_DW                                            1
#define WF_TX_DESCRIPTOR_EOSP_ADDR                                          4
#define WF_TX_DESCRIPTOR_EOSP_MASK                                          0x00020000 // 17-17
#define WF_TX_DESCRIPTOR_EOSP_SHIFT                                         17
#define WF_TX_DESCRIPTOR_AMS_DW                                             1
#define WF_TX_DESCRIPTOR_AMS_ADDR                                           4
#define WF_TX_DESCRIPTOR_AMS_MASK                                           0x00040000 // 18-18
#define WF_TX_DESCRIPTOR_AMS_SHIFT                                          18
#define WF_TX_DESCRIPTOR_RMVL_DW                                            1
#define WF_TX_DESCRIPTOR_RMVL_ADDR                                          4
#define WF_TX_DESCRIPTOR_RMVL_MASK                                          0x00040000 // 18-18
#define WF_TX_DESCRIPTOR_RMVL_SHIFT                                         18
#define WF_TX_DESCRIPTOR_VLAN_DW                                            1
#define WF_TX_DESCRIPTOR_VLAN_ADDR                                          4
#define WF_TX_DESCRIPTOR_VLAN_MASK                                          0x00080000 // 19-19
#define WF_TX_DESCRIPTOR_VLAN_SHIFT                                         19
#define WF_TX_DESCRIPTOR_ETYP_DW                                            1
#define WF_TX_DESCRIPTOR_ETYP_ADDR                                          4
#define WF_TX_DESCRIPTOR_ETYP_MASK                                          0x00100000 // 20-20
#define WF_TX_DESCRIPTOR_ETYP_SHIFT                                         20
#define WF_TX_DESCRIPTOR_TID_MGMT_TYPE_DW                                   1
#define WF_TX_DESCRIPTOR_TID_MGMT_TYPE_ADDR                                 4
#define WF_TX_DESCRIPTOR_TID_MGMT_TYPE_MASK                                 0x01e00000 // 24-21
#define WF_TX_DESCRIPTOR_TID_MGMT_TYPE_SHIFT                                21
#define WF_TX_DESCRIPTOR_OM_DW                                              1
#define WF_TX_DESCRIPTOR_OM_ADDR                                            4
#define WF_TX_DESCRIPTOR_OM_MASK                                            0x7e000000 // 30-25
#define WF_TX_DESCRIPTOR_OM_SHIFT                                           25
#define WF_TX_DESCRIPTOR_FR_DW                                              1
#define WF_TX_DESCRIPTOR_FR_ADDR                                            4
#define WF_TX_DESCRIPTOR_FR_MASK                                            0x80000000 // 31-31
#define WF_TX_DESCRIPTOR_FR_SHIFT                                           31
// DW2
#define WF_TX_DESCRIPTOR_SUBTYPE_DW                                         2
#define WF_TX_DESCRIPTOR_SUBTYPE_ADDR                                       8
#define WF_TX_DESCRIPTOR_SUBTYPE_MASK                                       0x0000000f //  3- 0
#define WF_TX_DESCRIPTOR_SUBTYPE_SHIFT                                      0
#define WF_TX_DESCRIPTOR_FTYPE_DW                                           2
#define WF_TX_DESCRIPTOR_FTYPE_ADDR                                         8
#define WF_TX_DESCRIPTOR_FTYPE_MASK                                         0x00000030 //  5- 4
#define WF_TX_DESCRIPTOR_FTYPE_SHIFT                                        4
#define WF_TX_DESCRIPTOR_BF_TYPE_DW                                         2
#define WF_TX_DESCRIPTOR_BF_TYPE_ADDR                                       8
#define WF_TX_DESCRIPTOR_BF_TYPE_MASK                                       0x000000c0 //  7- 6
#define WF_TX_DESCRIPTOR_BF_TYPE_SHIFT                                      6
#define WF_TX_DESCRIPTOR_OM_MAP_DW                                          2
#define WF_TX_DESCRIPTOR_OM_MAP_ADDR                                        8
#define WF_TX_DESCRIPTOR_OM_MAP_MASK                                        0x00000100 //  8- 8
#define WF_TX_DESCRIPTOR_OM_MAP_SHIFT                                       8
#define WF_TX_DESCRIPTOR_RTS_DW                                             2
#define WF_TX_DESCRIPTOR_RTS_ADDR                                           8
#define WF_TX_DESCRIPTOR_RTS_MASK                                           0x00000200 //  9- 9
#define WF_TX_DESCRIPTOR_RTS_SHIFT                                          9
#define WF_TX_DESCRIPTOR_HEADER_PADDING_DW                                  2
#define WF_TX_DESCRIPTOR_HEADER_PADDING_ADDR                                8
#define WF_TX_DESCRIPTOR_HEADER_PADDING_MASK                                0x00000c00 // 11-10
#define WF_TX_DESCRIPTOR_HEADER_PADDING_SHIFT                               10
#define WF_TX_DESCRIPTOR_DU_DW                                              2
#define WF_TX_DESCRIPTOR_DU_ADDR                                            8
#define WF_TX_DESCRIPTOR_DU_MASK                                            0x00001000 // 12-12
#define WF_TX_DESCRIPTOR_DU_SHIFT                                           12
#define WF_TX_DESCRIPTOR_HE_DW                                              2
#define WF_TX_DESCRIPTOR_HE_ADDR                                            8
#define WF_TX_DESCRIPTOR_HE_MASK                                            0x00002000 // 13-13
#define WF_TX_DESCRIPTOR_HE_SHIFT                                           13
#define WF_TX_DESCRIPTOR_FRAG_DW                                            2
#define WF_TX_DESCRIPTOR_FRAG_ADDR                                          8
#define WF_TX_DESCRIPTOR_FRAG_MASK                                          0x0000c000 // 15-14
#define WF_TX_DESCRIPTOR_FRAG_SHIFT                                         14
#define WF_TX_DESCRIPTOR_REMAINING_TX_TIME_DW                               2
#define WF_TX_DESCRIPTOR_REMAINING_TX_TIME_ADDR                             8
#define WF_TX_DESCRIPTOR_REMAINING_TX_TIME_MASK                             0x03ff0000 // 25-16
#define WF_TX_DESCRIPTOR_REMAINING_TX_TIME_SHIFT                            16
#define WF_TX_DESCRIPTOR_POWER_OFFSET_DW                                    2
#define WF_TX_DESCRIPTOR_POWER_OFFSET_ADDR                                  8
#define WF_TX_DESCRIPTOR_POWER_OFFSET_MASK                                  0xfc000000 // 31-26
#define WF_TX_DESCRIPTOR_POWER_OFFSET_SHIFT                                 26
// DW3
#define WF_TX_DESCRIPTOR_NA_DW                                              3
#define WF_TX_DESCRIPTOR_NA_ADDR                                            12
#define WF_TX_DESCRIPTOR_NA_MASK                                            0x00000001 //  0- 0
#define WF_TX_DESCRIPTOR_NA_SHIFT                                           0
#define WF_TX_DESCRIPTOR_PF_DW                                              3
#define WF_TX_DESCRIPTOR_PF_ADDR                                            12
#define WF_TX_DESCRIPTOR_PF_MASK                                            0x00000002 //  1- 1
#define WF_TX_DESCRIPTOR_PF_SHIFT                                           1
#define WF_TX_DESCRIPTOR_EMRD_DW                                            3
#define WF_TX_DESCRIPTOR_EMRD_ADDR                                          12
#define WF_TX_DESCRIPTOR_EMRD_MASK                                          0x00000004 //  2- 2
#define WF_TX_DESCRIPTOR_EMRD_SHIFT                                         2
#define WF_TX_DESCRIPTOR_EEOSP_DW                                           3
#define WF_TX_DESCRIPTOR_EEOSP_ADDR                                         12
#define WF_TX_DESCRIPTOR_EEOSP_MASK                                         0x00000008 //  3- 3
#define WF_TX_DESCRIPTOR_EEOSP_SHIFT                                        3
#define WF_TX_DESCRIPTOR_BM_DW                                              3
#define WF_TX_DESCRIPTOR_BM_ADDR                                            12
#define WF_TX_DESCRIPTOR_BM_MASK                                            0x00000010 //  4- 4
#define WF_TX_DESCRIPTOR_BM_SHIFT                                           4
#define WF_TX_DESCRIPTOR_HW_AMSDU_CAP_DW                                    3
#define WF_TX_DESCRIPTOR_HW_AMSDU_CAP_ADDR                                  12
#define WF_TX_DESCRIPTOR_HW_AMSDU_CAP_MASK                                  0x00000020 //  5- 5
#define WF_TX_DESCRIPTOR_HW_AMSDU_CAP_SHIFT                                 5
#define WF_TX_DESCRIPTOR_TX_COUNT_DW                                        3
#define WF_TX_DESCRIPTOR_TX_COUNT_ADDR                                      12
#define WF_TX_DESCRIPTOR_TX_COUNT_MASK                                      0x000007c0 // 10- 6
#define WF_TX_DESCRIPTOR_TX_COUNT_SHIFT                                     6
#define WF_TX_DESCRIPTOR_REMAINING_TX_COUNT_DW                              3
#define WF_TX_DESCRIPTOR_REMAINING_TX_COUNT_ADDR                            12
#define WF_TX_DESCRIPTOR_REMAINING_TX_COUNT_MASK                            0x0000f800 // 15-11
#define WF_TX_DESCRIPTOR_REMAINING_TX_COUNT_SHIFT                           11
#define WF_TX_DESCRIPTOR_SN_DW                                              3
#define WF_TX_DESCRIPTOR_SN_ADDR                                            12
#define WF_TX_DESCRIPTOR_SN_MASK                                            0x0fff0000 // 27-16
#define WF_TX_DESCRIPTOR_SN_SHIFT                                           16
#define WF_TX_DESCRIPTOR_BA_DIS_DW                                          3
#define WF_TX_DESCRIPTOR_BA_DIS_ADDR                                        12
#define WF_TX_DESCRIPTOR_BA_DIS_MASK                                        0x10000000 // 28-28
#define WF_TX_DESCRIPTOR_BA_DIS_SHIFT                                       28
#define WF_TX_DESCRIPTOR_PM_DW                                              3
#define WF_TX_DESCRIPTOR_PM_ADDR                                            12
#define WF_TX_DESCRIPTOR_PM_MASK                                            0x20000000 // 29-29
#define WF_TX_DESCRIPTOR_PM_SHIFT                                           29
#define WF_TX_DESCRIPTOR_PN_VLD_DW                                          3
#define WF_TX_DESCRIPTOR_PN_VLD_ADDR                                        12
#define WF_TX_DESCRIPTOR_PN_VLD_MASK                                        0x40000000 // 30-30
#define WF_TX_DESCRIPTOR_PN_VLD_SHIFT                                       30
#define WF_TX_DESCRIPTOR_SN_VLD_DW                                          3
#define WF_TX_DESCRIPTOR_SN_VLD_ADDR                                        12
#define WF_TX_DESCRIPTOR_SN_VLD_MASK                                        0x80000000 // 31-31
#define WF_TX_DESCRIPTOR_SN_VLD_SHIFT                                       31
// DW4
#define WF_TX_DESCRIPTOR_PN_31_0__DW                                        4
#define WF_TX_DESCRIPTOR_PN_31_0__ADDR                                      16
#define WF_TX_DESCRIPTOR_PN_31_0__MASK                                      0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_PN_31_0__SHIFT                                     0
// DW5
#define WF_TX_DESCRIPTOR_PID_DW                                             5
#define WF_TX_DESCRIPTOR_PID_ADDR                                           20
#define WF_TX_DESCRIPTOR_PID_MASK                                           0x000000ff //  7- 0
#define WF_TX_DESCRIPTOR_PID_SHIFT                                          0
#define WF_TX_DESCRIPTOR_TXSFM_DW                                           5
#define WF_TX_DESCRIPTOR_TXSFM_ADDR                                         20
#define WF_TX_DESCRIPTOR_TXSFM_MASK                                         0x00000100 //  8- 8
#define WF_TX_DESCRIPTOR_TXSFM_SHIFT                                        8
#define WF_TX_DESCRIPTOR_TXS2M_DW                                           5
#define WF_TX_DESCRIPTOR_TXS2M_ADDR                                         20
#define WF_TX_DESCRIPTOR_TXS2M_MASK                                         0x00000200 //  9- 9
#define WF_TX_DESCRIPTOR_TXS2M_SHIFT                                        9
#define WF_TX_DESCRIPTOR_TXS2H_DW                                           5
#define WF_TX_DESCRIPTOR_TXS2H_ADDR                                         20
#define WF_TX_DESCRIPTOR_TXS2H_MASK                                         0x00000400 // 10-10
#define WF_TX_DESCRIPTOR_TXS2H_SHIFT                                        10
#define WF_TX_DESCRIPTOR_FBCZ_DW                                            5
#define WF_TX_DESCRIPTOR_FBCZ_ADDR                                          20
#define WF_TX_DESCRIPTOR_FBCZ_MASK                                          0x00001000 // 12-12
#define WF_TX_DESCRIPTOR_FBCZ_SHIFT                                         12
#define WF_TX_DESCRIPTOR_BYPASS_RBB_DW                                      5
#define WF_TX_DESCRIPTOR_BYPASS_RBB_ADDR                                    20
#define WF_TX_DESCRIPTOR_BYPASS_RBB_MASK                                    0x00002000 // 13-13
#define WF_TX_DESCRIPTOR_BYPASS_RBB_SHIFT                                   13
#define WF_TX_DESCRIPTOR_BYPASS_TBB_DW                                      5
#define WF_TX_DESCRIPTOR_BYPASS_TBB_ADDR                                    20
#define WF_TX_DESCRIPTOR_BYPASS_TBB_MASK                                    0x00004000 // 14-14
#define WF_TX_DESCRIPTOR_BYPASS_TBB_SHIFT                                   14
#define WF_TX_DESCRIPTOR_FL_DW                                              5
#define WF_TX_DESCRIPTOR_FL_ADDR                                            20
#define WF_TX_DESCRIPTOR_FL_MASK                                            0x00008000 // 15-15
#define WF_TX_DESCRIPTOR_FL_SHIFT                                           15
#define WF_TX_DESCRIPTOR_PN_47_32__DW                                       5
#define WF_TX_DESCRIPTOR_PN_47_32__ADDR                                     20
#define WF_TX_DESCRIPTOR_PN_47_32__MASK                                     0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_PN_47_32__SHIFT                                    16
// DW6
#define WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB_DW                                  6
#define WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB_ADDR                                24
#define WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB_MASK                                0x00000002 //  1- 1
#define WF_TX_DESCRIPTOR_AMSDU_CAP_UTXB_SHIFT                               1
#define WF_TX_DESCRIPTOR_DAS_DW                                             6
#define WF_TX_DESCRIPTOR_DAS_ADDR                                           24
#define WF_TX_DESCRIPTOR_DAS_MASK                                           0x00000004 //  2- 2
#define WF_TX_DESCRIPTOR_DAS_SHIFT                                          2
#define WF_TX_DESCRIPTOR_DIS_MAT_DW                                         6
#define WF_TX_DESCRIPTOR_DIS_MAT_ADDR                                       24
#define WF_TX_DESCRIPTOR_DIS_MAT_MASK                                       0x00000008 //  3- 3
#define WF_TX_DESCRIPTOR_DIS_MAT_SHIFT                                      3
#define WF_TX_DESCRIPTOR_MSDU_COUNT_DW                                      6
#define WF_TX_DESCRIPTOR_MSDU_COUNT_ADDR                                    24
#define WF_TX_DESCRIPTOR_MSDU_COUNT_MASK                                    0x000003f0 //  9- 4
#define WF_TX_DESCRIPTOR_MSDU_COUNT_SHIFT                                   4
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX_DW                            6
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX_ADDR                          24
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX_MASK                          0x00007c00 // 14-10
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_IDX_SHIFT                         10
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_EN_DW                             6
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_EN_ADDR                           24
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_EN_MASK                           0x00008000 // 15-15
#define WF_TX_DESCRIPTOR_TIMESTAMP_OFFSET_EN_SHIFT                          15
#define WF_TX_DESCRIPTOR_FIXED_RATE_IDX_DW                                  6
#define WF_TX_DESCRIPTOR_FIXED_RATE_IDX_ADDR                                24
#define WF_TX_DESCRIPTOR_FIXED_RATE_IDX_MASK                                0x003f0000 // 21-16
#define WF_TX_DESCRIPTOR_FIXED_RATE_IDX_SHIFT                               16
#define WF_TX_DESCRIPTOR_BW_DW                                              6
#define WF_TX_DESCRIPTOR_BW_ADDR                                            24
#define WF_TX_DESCRIPTOR_BW_MASK                                            0x03c00000 // 25-22
#define WF_TX_DESCRIPTOR_BW_SHIFT                                           22
#define WF_TX_DESCRIPTOR_VTA_DW                                             6
#define WF_TX_DESCRIPTOR_VTA_ADDR                                           24
#define WF_TX_DESCRIPTOR_VTA_MASK                                           0x10000000 // 28-28
#define WF_TX_DESCRIPTOR_VTA_SHIFT                                          28
#define WF_TX_DESCRIPTOR_SRC_DW                                             6
#define WF_TX_DESCRIPTOR_SRC_ADDR                                           24
#define WF_TX_DESCRIPTOR_SRC_MASK                                           0xc0000000 // 31-30
#define WF_TX_DESCRIPTOR_SRC_SHIFT                                          30
// DW7
#define WF_TX_DESCRIPTOR_SW_TX_TIME_DW                                      7
#define WF_TX_DESCRIPTOR_SW_TX_TIME_ADDR                                    28
#define WF_TX_DESCRIPTOR_SW_TX_TIME_MASK                                    0x000003ff //  9- 0
#define WF_TX_DESCRIPTOR_SW_TX_TIME_SHIFT                                   0
#define WF_TX_DESCRIPTOR_UT_DW                                              7
#define WF_TX_DESCRIPTOR_UT_ADDR                                            28
#define WF_TX_DESCRIPTOR_UT_MASK                                            0x00008000 // 15-15
#define WF_TX_DESCRIPTOR_UT_SHIFT                                           15
#define WF_TX_DESCRIPTOR_CTXD_CNT_DW                                        7
#define WF_TX_DESCRIPTOR_CTXD_CNT_ADDR                                      28
#define WF_TX_DESCRIPTOR_CTXD_CNT_MASK                                      0x03c00000 // 25-22
#define WF_TX_DESCRIPTOR_CTXD_CNT_SHIFT                                     22
#define WF_TX_DESCRIPTOR_CTXD_DW                                            7
#define WF_TX_DESCRIPTOR_CTXD_ADDR                                          28
#define WF_TX_DESCRIPTOR_CTXD_MASK                                          0x04000000 // 26-26
#define WF_TX_DESCRIPTOR_CTXD_SHIFT                                         26
#define WF_TX_DESCRIPTOR_HM_DW                                              7
#define WF_TX_DESCRIPTOR_HM_ADDR                                            28
#define WF_TX_DESCRIPTOR_HM_MASK                                            0x08000000 // 27-27
#define WF_TX_DESCRIPTOR_HM_SHIFT                                           27
#define WF_TX_DESCRIPTOR_DP_DW                                              7
#define WF_TX_DESCRIPTOR_DP_ADDR                                            28
#define WF_TX_DESCRIPTOR_DP_MASK                                            0x10000000 // 28-28
#define WF_TX_DESCRIPTOR_DP_SHIFT                                           28
#define WF_TX_DESCRIPTOR_IP_DW                                              7
#define WF_TX_DESCRIPTOR_IP_ADDR                                            28
#define WF_TX_DESCRIPTOR_IP_MASK                                            0x20000000 // 29-29
#define WF_TX_DESCRIPTOR_IP_SHIFT                                           29
#define WF_TX_DESCRIPTOR_TXD_LEN_DW                                         7
#define WF_TX_DESCRIPTOR_TXD_LEN_ADDR                                       28
#define WF_TX_DESCRIPTOR_TXD_LEN_MASK                                       0xc0000000 // 31-30
#define WF_TX_DESCRIPTOR_TXD_LEN_SHIFT                                      30
// DW8
#define WF_TX_DESCRIPTOR_MSDU0_DW                                           8
#define WF_TX_DESCRIPTOR_MSDU0_ADDR                                         32
#define WF_TX_DESCRIPTOR_MSDU0_MASK                                         0x0000ffff // 15- 0
#define WF_TX_DESCRIPTOR_MSDU0_SHIFT                                        0
#define WF_TX_DESCRIPTOR_MSDU1_DW                                           8
#define WF_TX_DESCRIPTOR_MSDU1_ADDR                                         32
#define WF_TX_DESCRIPTOR_MSDU1_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_MSDU1_SHIFT                                        16
// DW9
#define WF_TX_DESCRIPTOR_MSDU2_DW                                           9
#define WF_TX_DESCRIPTOR_MSDU2_ADDR                                         36
#define WF_TX_DESCRIPTOR_MSDU2_MASK                                         0x0000ffff // 15- 0
#define WF_TX_DESCRIPTOR_MSDU2_SHIFT                                        0
#define WF_TX_DESCRIPTOR_MSDU3_DW                                           9
#define WF_TX_DESCRIPTOR_MSDU3_ADDR                                         36
#define WF_TX_DESCRIPTOR_MSDU3_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_MSDU3_SHIFT                                        16
// DW10
#define WF_TX_DESCRIPTOR_TXP0_DW                                            10
#define WF_TX_DESCRIPTOR_TXP0_ADDR                                          40
#define WF_TX_DESCRIPTOR_TXP0_MASK                                          0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP0_SHIFT                                         0
// DW11
// DO NOT process repeat field(txp[0])
#define WF_TX_DESCRIPTOR_TXP1_DW                                            11
#define WF_TX_DESCRIPTOR_TXP1_ADDR                                          44
#define WF_TX_DESCRIPTOR_TXP1_MASK                                          0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP1_SHIFT                                         16
// DW12
// DO NOT process repeat field(txp[1])
// DW13
#define WF_TX_DESCRIPTOR_TXP2_DW                                            13
#define WF_TX_DESCRIPTOR_TXP2_ADDR                                          52
#define WF_TX_DESCRIPTOR_TXP2_MASK                                          0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP2_SHIFT                                         0
// DW14
// DO NOT process repeat field(txp[2])
#define WF_TX_DESCRIPTOR_TXP3_DW                                            14
#define WF_TX_DESCRIPTOR_TXP3_ADDR                                          56
#define WF_TX_DESCRIPTOR_TXP3_MASK                                          0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP3_SHIFT                                         16
// DW15
// DO NOT process repeat field(txp[3])
// DW16
#define WF_TX_DESCRIPTOR_MSDU4_DW                                           16
#define WF_TX_DESCRIPTOR_MSDU4_ADDR                                         64
#define WF_TX_DESCRIPTOR_MSDU4_MASK                                         0x0000ffff // 15- 0
#define WF_TX_DESCRIPTOR_MSDU4_SHIFT                                        0
#define WF_TX_DESCRIPTOR_MSDU5_DW                                           16
#define WF_TX_DESCRIPTOR_MSDU5_ADDR                                         64
#define WF_TX_DESCRIPTOR_MSDU5_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_MSDU5_SHIFT                                        16
// DW17
#define WF_TX_DESCRIPTOR_MSDU6_DW                                           17
#define WF_TX_DESCRIPTOR_MSDU6_ADDR                                         68
#define WF_TX_DESCRIPTOR_MSDU6_MASK                                         0x0000ffff // 15- 0
#define WF_TX_DESCRIPTOR_MSDU6_SHIFT                                        0
#define WF_TX_DESCRIPTOR_MSDU7_DW                                           17
#define WF_TX_DESCRIPTOR_MSDU7_ADDR                                         68
#define WF_TX_DESCRIPTOR_MSDU7_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_MSDU7_SHIFT                                        16
// DW18
#define WF_TX_DESCRIPTOR_TXP4_DW                                            18
#define WF_TX_DESCRIPTOR_TXP4_ADDR                                          72
#define WF_TX_DESCRIPTOR_TXP4_MASK                                          0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP4_SHIFT                                         0
// DW19
// DO NOT process repeat field(txp[4])
#define WF_TX_DESCRIPTOR_TXP5_DW                                            19
#define WF_TX_DESCRIPTOR_TXP5_ADDR                                          76
#define WF_TX_DESCRIPTOR_TXP5_MASK                                          0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP5_SHIFT                                         16
// DW20
// DO NOT process repeat field(txp[5])
// DW21
#define WF_TX_DESCRIPTOR_TXP6_DW                                            21
#define WF_TX_DESCRIPTOR_TXP6_ADDR                                          84
#define WF_TX_DESCRIPTOR_TXP6_MASK                                          0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP6_SHIFT                                         0
// DW22
// DO NOT process repeat field(txp[6])
#define WF_TX_DESCRIPTOR_TXP7_DW                                            22
#define WF_TX_DESCRIPTOR_TXP7_ADDR                                          88
#define WF_TX_DESCRIPTOR_TXP7_MASK                                          0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP7_SHIFT                                         16
// DW23
// DO NOT process repeat field(txp[7])
// DW24
#define WF_TX_DESCRIPTOR_TXP8_DW                                            24
#define WF_TX_DESCRIPTOR_TXP8_ADDR                                          96
#define WF_TX_DESCRIPTOR_TXP8_MASK                                          0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP8_SHIFT                                         0
// DW25
// DO NOT process repeat field(txp[8])
#define WF_TX_DESCRIPTOR_TXP9_DW                                            25
#define WF_TX_DESCRIPTOR_TXP9_ADDR                                          100
#define WF_TX_DESCRIPTOR_TXP9_MASK                                          0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP9_SHIFT                                         16
// DW26
// DO NOT process repeat field(txp[9])
// DW27
#define WF_TX_DESCRIPTOR_TXP10_DW                                           27
#define WF_TX_DESCRIPTOR_TXP10_ADDR                                         108
#define WF_TX_DESCRIPTOR_TXP10_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP10_SHIFT                                        0
// DW28
// DO NOT process repeat field(txp[10])
#define WF_TX_DESCRIPTOR_TXP11_DW                                           28
#define WF_TX_DESCRIPTOR_TXP11_ADDR                                         112
#define WF_TX_DESCRIPTOR_TXP11_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP11_SHIFT                                        16
// DW29
// DO NOT process repeat field(txp[11])
// DW30
#define WF_TX_DESCRIPTOR_TXP12_DW                                           30
#define WF_TX_DESCRIPTOR_TXP12_ADDR                                         120
#define WF_TX_DESCRIPTOR_TXP12_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP12_SHIFT                                        0
// DW31
// DO NOT process repeat field(txp[12])
#define WF_TX_DESCRIPTOR_TXP13_DW                                           31
#define WF_TX_DESCRIPTOR_TXP13_ADDR                                         124
#define WF_TX_DESCRIPTOR_TXP13_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP13_SHIFT                                        16
// DW32
// DO NOT process repeat field(txp[13])
// DW33
#define WF_TX_DESCRIPTOR_TXP14_DW                                           33
#define WF_TX_DESCRIPTOR_TXP14_ADDR                                         132
#define WF_TX_DESCRIPTOR_TXP14_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP14_SHIFT                                        0
// DW34
// DO NOT process repeat field(txp[14])
#define WF_TX_DESCRIPTOR_TXP15_DW                                           34
#define WF_TX_DESCRIPTOR_TXP15_ADDR                                         136
#define WF_TX_DESCRIPTOR_TXP15_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP15_SHIFT                                        16
// DW35
// DO NOT process repeat field(txp[15])
// DW36
#define WF_TX_DESCRIPTOR_TXP16_DW                                           36
#define WF_TX_DESCRIPTOR_TXP16_ADDR                                         144
#define WF_TX_DESCRIPTOR_TXP16_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP16_SHIFT                                        0
// DW37
// DO NOT process repeat field(txp[16])
#define WF_TX_DESCRIPTOR_TXP17_DW                                           37
#define WF_TX_DESCRIPTOR_TXP17_ADDR                                         148
#define WF_TX_DESCRIPTOR_TXP17_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP17_SHIFT                                        16
// DW38
// DO NOT process repeat field(txp[17])
// DW39
#define WF_TX_DESCRIPTOR_TXP18_DW                                           39
#define WF_TX_DESCRIPTOR_TXP18_ADDR                                         156
#define WF_TX_DESCRIPTOR_TXP18_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP18_SHIFT                                        0
// DW40
// DO NOT process repeat field(txp[18])
#define WF_TX_DESCRIPTOR_TXP19_DW                                           40
#define WF_TX_DESCRIPTOR_TXP19_ADDR                                         160
#define WF_TX_DESCRIPTOR_TXP19_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP19_SHIFT                                        16
// DW41
// DO NOT process repeat field(txp[19])
// DW42
#define WF_TX_DESCRIPTOR_TXP20_DW                                           42
#define WF_TX_DESCRIPTOR_TXP20_ADDR                                         168
#define WF_TX_DESCRIPTOR_TXP20_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP20_SHIFT                                        0
// DW43
// DO NOT process repeat field(txp[20])
#define WF_TX_DESCRIPTOR_TXP21_DW                                           43
#define WF_TX_DESCRIPTOR_TXP21_ADDR                                         172
#define WF_TX_DESCRIPTOR_TXP21_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP21_SHIFT                                        16
// DW44
// DO NOT process repeat field(txp[21])
// DW45
#define WF_TX_DESCRIPTOR_TXP22_DW                                           45
#define WF_TX_DESCRIPTOR_TXP22_ADDR                                         180
#define WF_TX_DESCRIPTOR_TXP22_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP22_SHIFT                                        0
// DW46
// DO NOT process repeat field(txp[22])
#define WF_TX_DESCRIPTOR_TXP23_DW                                           46
#define WF_TX_DESCRIPTOR_TXP23_ADDR                                         184
#define WF_TX_DESCRIPTOR_TXP23_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP23_SHIFT                                        16
// DW47
// DO NOT process repeat field(txp[23])
// DW48
#define WF_TX_DESCRIPTOR_TXP24_DW                                           48
#define WF_TX_DESCRIPTOR_TXP24_ADDR                                         192
#define WF_TX_DESCRIPTOR_TXP24_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP24_SHIFT                                        0
// DW49
// DO NOT process repeat field(txp[24])
#define WF_TX_DESCRIPTOR_TXP25_DW                                           49
#define WF_TX_DESCRIPTOR_TXP25_ADDR                                         196
#define WF_TX_DESCRIPTOR_TXP25_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP25_SHIFT                                        16
// DW50
// DO NOT process repeat field(txp[25])
// DW51
#define WF_TX_DESCRIPTOR_TXP26_DW                                           51
#define WF_TX_DESCRIPTOR_TXP26_ADDR                                         204
#define WF_TX_DESCRIPTOR_TXP26_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP26_SHIFT                                        0
// DW52
// DO NOT process repeat field(txp[26])
#define WF_TX_DESCRIPTOR_TXP27_DW                                           52
#define WF_TX_DESCRIPTOR_TXP27_ADDR                                         208
#define WF_TX_DESCRIPTOR_TXP27_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP27_SHIFT                                        16
// DW53
// DO NOT process repeat field(txp[27])
// DW54
#define WF_TX_DESCRIPTOR_TXP28_DW                                           54
#define WF_TX_DESCRIPTOR_TXP28_ADDR                                         216
#define WF_TX_DESCRIPTOR_TXP28_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP28_SHIFT                                        0
// DW55
// DO NOT process repeat field(txp[28])
#define WF_TX_DESCRIPTOR_TXP29_DW                                           55
#define WF_TX_DESCRIPTOR_TXP29_ADDR                                         220
#define WF_TX_DESCRIPTOR_TXP29_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP29_SHIFT                                        16
// DW56
// DO NOT process repeat field(txp[29])
// DW57
#define WF_TX_DESCRIPTOR_TXP30_DW                                           57
#define WF_TX_DESCRIPTOR_TXP30_ADDR                                         228
#define WF_TX_DESCRIPTOR_TXP30_MASK                                         0xffffffff // 31- 0
#define WF_TX_DESCRIPTOR_TXP30_SHIFT                                        0
// DW58
// DO NOT process repeat field(txp[30])
#define WF_TX_DESCRIPTOR_TXP31_DW                                           58
#define WF_TX_DESCRIPTOR_TXP31_ADDR                                         232
#define WF_TX_DESCRIPTOR_TXP31_MASK                                         0xffff0000 // 31-16
#define WF_TX_DESCRIPTOR_TXP31_SHIFT                                        16
// DW59
// DO NOT process repeat field(txp[31])

/* TXP PAO */
#define HIF_TXP_V2_SIZE (24 * 4)
/* DW0 */
#define HIF_TXD_VERSION_SHIFT 19
#define HIF_TXD_VERSION_MASK 0x00780000

/* DW8 */
#define HIF_TXP_PRIORITY_SHIFT 0
#define HIF_TXP_PRIORITY_MASK 0x00000001
#define HIF_TXP_FIXED_RATE_SHIFT 1
#define HIF_TXP_FIXED_RATE_MASK 0x00000002
#define HIF_TXP_TCP_SHIFT 2
#define HIF_TXP_TCP_MASK 0x00000004
#define HIF_TXP_NON_CIPHER_SHIFT 3
#define HIF_TXP_NON_CIPHER_MASK 0x00000008
#define HIF_TXP_VLAN_SHIFT 4
#define HIF_TXP_VLAN_MASK 0x00000010
#define HIF_TXP_BC_MC_FLAG_SHIFT 5
#define HIF_TXP_BC_MC_FLAG_MASK 0x00000060
#define HIF_TXP_FR_HOST_SHIFT 7
#define HIF_TXP_FR_HOST_MASK 0x00000080
#define HIF_TXP_ETYPE_SHIFT 8
#define HIF_TXP_ETYPE_MASK 0x00000100
#define HIF_TXP_TXP_AMSDU_SHIFT 9
#define HIF_TXP_TXP_AMSDU_MASK 0x00000200
#define HIF_TXP_TXP_MC_CLONE_SHIFT 10
#define HIF_TXP_TXP_MC_CLONE_MASK 0x00000400
#define HIF_TXP_TOKEN_ID_SHIFT 16
#define HIF_TXP_TOKEN_ID_MASK 0xffff0000

/* DW9 */
#define HIF_TXP_BSS_IDX_SHIFT 0
#define HIF_TXP_BSS_IDX_MASK 0x000000ff
#define HIF_TXP_USER_PRIORITY_SHIFT 8
#define HIF_TXP_USER_PRIORITY_MASK 0x0000ff00
#define HIF_TXP_BUF_NUM_SHIFT 16
#define HIF_TXP_BUF_NUM_MASK 0x001f0000
#define HIF_TXP_MSDU_CNT_SHIFT 21
#define HIF_TXP_MSDU_CNT_MASK 0x03e00000
#define HIF_TXP_SRC_SHIFT 26
#define HIF_TXP_SRC_MASK 0x0c000000

/* DW10 */
#define HIF_TXP_ETH_TYPE_SHIFT 0
#define HIF_TXP_ETH_TYPE_MASK 0x0000ffff
#define HIF_TXP_WLAN_IDX_SHIFT 16
#define HIF_TXP_WLAN_IDX_MASK 0x0fff0000

/* DW11 */
#define HIF_TXP_PPE_INFO_SHIFT 0
#define HIF_TXP_PPE_INFO_MASK 0xffffffff

/* DW12 - DW31 */
#define HIF_TXP_BUF_PTR0_L_SHIFT 0
#define HIF_TXP_BUF_PTR0_L_MASK 0xffffffff
#define HIF_TXP_BUF_LEN0_SHIFT 0
#define HIF_TXP_BUF_LEN0_MASK 0x00000fff
#define HIF_TXP_BUF_PTR0_H_SHIFT 12
#define HIF_TXP_BUF_PTR0_H_MASK 0x0000f000
#define HIF_TXP_BUF_LEN1_SHIFT 16
#define HIF_TXP_BUF_LEN1_MASK 0x0fff0000
#define HIF_TXP_BUF_PTR1_H_SHIFT 28
#define HIF_TXP_BUF_PTR1_H_MASK 0xf0000000
#define HIF_TXP_BUF_PTR1_L_SHIFT 0
#define HIF_TXP_BUF_PTR1_L_MASK 0xffffffff

/* DW31 */
#define HIF_TXP_ML_SHIFT 16
#define HIF_TXP_ML_MASK 0xffff0000

/* UWTBL */
#define MT_WF_UWTBL_BASE		0x820c4000
#define MT_WF_UWTBL(ofs)		(MT_WF_UWTBL_BASE + (ofs))

#define MT_WF_UWTBL_ITCR		MT_WF_UWTBL(0x130)
#define MT_WF_UWTBL_ITCR0		MT_WF_UWTBL(0x138)
#define MT_WF_UWTBL_ITCR1		MT_WF_UWTBL(0x13c)

#define MT_WF_UWTBL_ITCR_SET		BIT(31)
#define MT_WF_UWTBL_ITCR_INDEX		GENMASK(5, 0)

/* RMAC */
#define MT_WF_RMAC_SRAM_DATA0(_band)	MT_WF_RMAC(_band, 0x210)
#define MT_WF_RMAC_SRAM_DATA1(_band)	MT_WF_RMAC(_band, 0x214)
#define MT_WF_RMAC_SRAM_BITMAP0(_band)	MT_WF_RMAC(_band, 0x220)
#define MT_WF_RMAC_SRAM_BITMAP1(_band)	MT_WF_RMAC(_band, 0x224)
#define MT_WF_RMAC_MEM_CTRL(_band)	MT_WF_RMAC(_band, 0x228)

#define MT_WF_RMAC_MEM_CRTL_TRIG	BIT(31)
#define MT_WF_RMAC_MEM_CRTL_TDX		GENMASK(7, 0)

/* AGG */
#define MT_AGG_REMAP_CTRL(_band)	MT_WF_AGG(_band, 0x094)
#define MT_AGG_REMAP_CTRL_OM_REMAP	GENMASK(5, 0)

/* TMAC */
#define MT_WF_TMAC_WMM0_OFFSET		0x0c4
#define MT_WF_TMAC_WMM1_OFFSET		0x364
#define MT_WF_TMAC_WMM2_OFFSET		0x36c
#define MT_WF_TMAC_WMM3_OFFSET		0x374
#define MT_WF_TMAC_WMM_TXOP_MASK	GENMASK(31, 16)
#define MT_WF_TMAC_WMM_TXOP_SHIFT	16
#endif

#endif
