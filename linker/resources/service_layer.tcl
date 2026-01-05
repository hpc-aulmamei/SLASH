# Clone /service_layer into {{ service_layer_bd_name }}
set cur_design [current_bd_design]
create_bd_design -boundary_from_container [get_bd_cells /service_layer] {{ service_layer_bd_name }}
current_bd_design $cur_design
set_property -dict [list \
  CONFIG.LIST_SYNTH_BD {service_layer.bd:{{ service_layer_bd_name }}.bd} \
  CONFIG.LIST_SIM_BD   {service_layer.bd:{{ service_layer_bd_name }}.bd} \
] [get_bd_cells /service_layer]
current_bd_design {{ service_layer_bd_name }}

update_compile_order -fileset sources_1

    set axi_noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 axi_noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $axi_noc_0

{% raw %}
  set_property -dict [ list \
   CONFIG.APERTURES {{0x203_0000_0000 128M}} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /axi_noc_0/M00_AXI]
  
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXI {read_bw {5} write_bw {5} read_avg_burst {4} write_avg_burst {4}}} \
 ] [get_bd_intf_pins /axi_noc_0/S00_INI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXI:S_QDMA_SLAVE_BRIDGE:M_QDMA_SLAVE_BRIDGE:S_VIRT_3:S_VIRT_2:S_VIRT_1:S_VIRT_0} \
 ] [get_bd_pins /axi_noc_0/aclk0]
  set_property APERTURES {{0x203_0000_0000 128M}} [get_bd_intf_ports S_AXILITE_INI]
  {% endraw %}

 # Create instance: dummy_noc_0, and set properties
  set dummy_noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_0


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_0/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_0/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_0/aclk0]

  # Create instance: dummy_noc_1, and set properties
  set dummy_noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_1 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_1


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_1/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_1/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_1/aclk0]

  # Create instance: dummy_noc_2, and set properties
  set dummy_noc_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_2 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_2


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_2/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_2/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_2/aclk0]

  # Create instance: dummy_noc_3, and set properties
  set dummy_noc_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_3 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_3


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_3/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_3/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_3/aclk0]

  # Create instance: dummy_noc_4, and set properties
  set dummy_noc_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_4 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_4


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_4/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_4/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_4/aclk0]

  # Create instance: dummy_noc_5, and set properties
  set dummy_noc_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_5 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_5


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_5/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_5/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_5/aclk0]

  # Create instance: dummy_noc_6, and set properties
  set dummy_noc_6 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_6 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_6


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_6/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_6/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_6/aclk0]

  # Create instance: dummy_noc_7, and set properties
  set dummy_noc_7 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_7 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dummy_noc_7


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_7/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dummy_noc_7/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dummy_noc_7/aclk0]

  # Create instance: dummy_noc_m_0, and set properties
  set dummy_noc_m_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_0


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_0/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_0/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_0/aclk0]

  # Create instance: dummy_noc_m_1, and set properties
  set dummy_noc_m_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_1


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_1/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_1/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_1/aclk0]

  # Create instance: dummy_noc_m_2, and set properties
  set dummy_noc_m_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_2 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_2


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_2/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_2/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_2/aclk0]

  # Create instance: dummy_noc_m_3, and set properties
  set dummy_noc_m_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_3 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_3


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_3/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_3/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_3/aclk0]

  # Create instance: dummy_noc_m_4, and set properties
  set dummy_noc_m_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_4 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_4


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_4/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_4/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_4/aclk0]

  # Create instance: dummy_noc_m_5, and set properties
  set dummy_noc_m_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_5 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_5


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_5/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_5/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_5/aclk0]

  # Create instance: dummy_noc_m_6, and set properties
  set dummy_noc_m_6 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_6 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_6


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_6/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_6/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_6/aclk0]

  # Create instance: dummy_noc_m_7, and set properties
  set dummy_noc_m_7 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dummy_noc_m_7 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dummy_noc_m_7


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dummy_noc_m_7/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dummy_noc_m_7/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dummy_noc_m_7/aclk0]

# Create instance: sl2noc_0, and set properties
  set sl2noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_0


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_0/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_0/aclk0]

  # Create instance: sl2noc_1, and set properties
  set sl2noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_1


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_1/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_1/aclk0]

  # Create instance: sl2noc_2, and set properties
  set sl2noc_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_2 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_2


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_2/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_2/aclk0]

  # Create instance: sl2noc_3, and set properties
  set sl2noc_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_3 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_3


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_3/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_3/aclk0]

  # Create instance: sl2noc_4, and set properties
  set sl2noc_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_4 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_4


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_4/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_4/aclk0]

  # Create instance: sl2noc_5, and set properties
  set sl2noc_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_5 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_5


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_5/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_5/aclk0]

  # Create instance: sl2noc_6, and set properties
  set sl2noc_6 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_6 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_6


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_6/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_6/aclk0]

  # Create instance: sl2noc_7, and set properties
  set sl2noc_7 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_7 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_7


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_7/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_7/aclk0]

# Create instance: sl2noc_virt_0, and set properties
  set sl2noc_virt_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_virt_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_virt_0


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_virt_0/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_virt_0/aclk0]

  # Create instance: sl2noc_virt_1, and set properties
  set sl2noc_virt_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_virt_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_virt_1


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_virt_1/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_virt_1/aclk0]

  # Create instance: sl2noc_virt_2, and set properties
  set sl2noc_virt_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_virt_2 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_virt_2


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_virt_2/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_virt_2/aclk0]

  # Create instance: sl2noc_virt_3, and set properties
  set sl2noc_virt_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 sl2noc_virt_3 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $sl2noc_virt_3


  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /sl2noc_virt_3/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /sl2noc_virt_3/aclk0]

  set axi_register_slice_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 axi_register_slice_4 ]


  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins dummy_noc_m_0/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS0]
  connect_bd_intf_net -intf_net Conn2 [get_bd_intf_pins dummy_noc_m_1/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS1]
  connect_bd_intf_net -intf_net Conn3 [get_bd_intf_pins dummy_noc_m_2/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS2]
  connect_bd_intf_net -intf_net Conn4 [get_bd_intf_pins dummy_noc_m_3/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS3]
  connect_bd_intf_net -intf_net Conn5 [get_bd_intf_pins dummy_noc_m_4/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS4]
  connect_bd_intf_net -intf_net Conn6 [get_bd_intf_pins dummy_noc_m_5/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS5]
  connect_bd_intf_net -intf_net Conn7 [get_bd_intf_pins dummy_noc_m_6/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS6]
  connect_bd_intf_net -intf_net Conn8 [get_bd_intf_pins dummy_noc_m_7/M00_INIS] [get_bd_intf_pins M_DCMAC_INIS7]
  connect_bd_intf_net -intf_net Conn9 [get_bd_intf_pins dummy_noc_0/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS0]
  connect_bd_intf_net -intf_net Conn10 [get_bd_intf_pins dummy_noc_1/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS1]
  connect_bd_intf_net -intf_net Conn11 [get_bd_intf_pins dummy_noc_2/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS2]
  connect_bd_intf_net -intf_net Conn12 [get_bd_intf_pins dummy_noc_3/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS3]
  connect_bd_intf_net -intf_net Conn13 [get_bd_intf_pins dummy_noc_4/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS4]
  connect_bd_intf_net -intf_net Conn14 [get_bd_intf_pins dummy_noc_5/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS5]
  connect_bd_intf_net -intf_net Conn15 [get_bd_intf_pins dummy_noc_6/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS6]
  connect_bd_intf_net -intf_net Conn16 [get_bd_intf_pins dummy_noc_7/S00_INIS] [get_bd_intf_pins S_DCMAC_INIS7]

  connect_bd_intf_net -intf_net sl2noc_virt_0_M00_INI [get_bd_intf_pins M_VIRT_0] [get_bd_intf_pins sl2noc_virt_0/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_virt_1_M00_INI [get_bd_intf_pins M_VIRT_1] [get_bd_intf_pins sl2noc_virt_1/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_virt_2_M00_INI [get_bd_intf_pins M_VIRT_2] [get_bd_intf_pins sl2noc_virt_2/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_virt_3_M00_INI [get_bd_intf_pins M_VIRT_3] [get_bd_intf_pins sl2noc_virt_3/M00_INI]

  connect_bd_intf_net -intf_net sl2noc_0_M00_INI [get_bd_intf_pins SL2NOC_0] [get_bd_intf_pins sl2noc_0/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_1_M00_INI [get_bd_intf_pins SL2NOC_1] [get_bd_intf_pins sl2noc_1/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_2_M00_INI [get_bd_intf_pins SL2NOC_2] [get_bd_intf_pins sl2noc_2/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_3_M00_INI [get_bd_intf_pins SL2NOC_3] [get_bd_intf_pins sl2noc_3/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_4_M00_INI [get_bd_intf_pins SL2NOC_4] [get_bd_intf_pins sl2noc_4/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_5_M00_INI [get_bd_intf_pins SL2NOC_5] [get_bd_intf_pins sl2noc_5/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_6_M00_INI [get_bd_intf_pins SL2NOC_6] [get_bd_intf_pins sl2noc_6/M00_INI]
  connect_bd_intf_net -intf_net sl2noc_7_M00_INI [get_bd_intf_pins SL2NOC_7] [get_bd_intf_pins sl2noc_7/M00_INI]
  connect_bd_intf_net -intf_net S_AXILITE_INI_1 [get_bd_intf_ports S_AXILITE_INI] [get_bd_intf_pins axi_noc_0/S00_INI]
  connect_bd_intf_net -intf_net S_AXI_0_1 [get_bd_intf_ports S_QDMA_SLAVE_BRIDGE] [get_bd_intf_pins axi_register_slice_4/S_AXI]
  connect_bd_intf_net -intf_net axi_register_slice_4_M_AXI [get_bd_intf_ports M_QDMA_SLAVE_BRIDGE] [get_bd_intf_pins axi_register_slice_4/M_AXI]
  connect_bd_net -net aresetn0_1  [get_bd_pins ap_rst_n] \
  [get_bd_pins axi_register_slice_4/aresetn]

  connect_bd_net -net aclk0_1  [get_bd_pins aclk0] \
  [get_bd_pins dummy_noc_0/aclk0] \
  [get_bd_pins dummy_noc_1/aclk0] \
  [get_bd_pins dummy_noc_2/aclk0] \
  [get_bd_pins dummy_noc_3/aclk0] \
  [get_bd_pins dummy_noc_4/aclk0] \
  [get_bd_pins dummy_noc_5/aclk0] \
  [get_bd_pins dummy_noc_6/aclk0] \
  [get_bd_pins dummy_noc_7/aclk0] \
  [get_bd_pins dummy_noc_m_0/aclk0] \
  [get_bd_pins dummy_noc_m_1/aclk0] \
  [get_bd_pins dummy_noc_m_2/aclk0] \
  [get_bd_pins dummy_noc_m_3/aclk0] \
  [get_bd_pins dummy_noc_m_4/aclk0] \
  [get_bd_pins dummy_noc_m_5/aclk0] \
  [get_bd_pins dummy_noc_m_6/aclk0] \
  [get_bd_pins dummy_noc_m_7/aclk0] \
  [get_bd_pins sl2noc_0/aclk0] \
  [get_bd_pins sl2noc_1/aclk0] \
  [get_bd_pins sl2noc_2/aclk0] \
  [get_bd_pins sl2noc_3/aclk0] \
  [get_bd_pins sl2noc_4/aclk0] \
  [get_bd_pins sl2noc_5/aclk0] \
  [get_bd_pins sl2noc_6/aclk0] \
  [get_bd_pins sl2noc_7/aclk0] \
  [get_bd_pins sl2noc_virt_0/aclk0] \
  [get_bd_pins sl2noc_virt_1/aclk0] \
  [get_bd_pins sl2noc_virt_2/aclk0] \
  [get_bd_pins sl2noc_virt_3/aclk0] \
  [get_bd_pins axi_noc_0/aclk0] \
  [get_bd_pins axi_register_slice_4/aclk]

  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_0/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_0/S00_AXI]

  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_1/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_1/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_2/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_2/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_3/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_3/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_4/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_4/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_5/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_5/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_6/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_6/S00_AXI]
  set_property -dict [list CONFIG.INI_STRATEGY {driver}] [get_bd_intf_pins /sl2noc_7/M00_INI]
  set_property -dict [list CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}}] [get_bd_intf_pins /sl2noc_7/S00_AXI]



proc add_dcmac_inst {} {

  set DCMAC0_ENABLED 1
  set DCMAC1_ENABLED 1

  ## Each DCMAC can support 2 QSFP56 interfaces
  ## select how many QSFP56 you want for each DCMAC, provided they are enabled

  ## Setup number of QSFP56 interfaces for DCMAC0
  set DUAL_QSFP_DCMAC0 0

  ## Setup number of QSFP56 interfaces for DCMAC1
  set DUAL_QSFP_DCMAC1 0

    # Create network hierarchy
    if { ${DCMAC0_ENABLED} == "1" } {
        create_qsfp_hierarchy 0 ${DUAL_QSFP_DCMAC0}
    }
    if { ${DCMAC1_ENABLED} == "1" } {
        create_qsfp_hierarchy 1 ${DUAL_QSFP_DCMAC1}
    }
}
# ===== Service Layer (generated) =====
# create_service_layer ""

# Absolute paths (normalized)
set ::slash_proj_root  [file normalize "{{ proj_root }}"]
set ::slash_dcmac_tcl  [file normalize "{{ dcmac_tcl }}"]
set ::slash_dcmac_hdl  [file normalize "{{ dcmac_hdl_dir }}"]

# Source the DCMAC Tcl helpers
source $::slash_dcmac_tcl

# Optional HDL imports (comment out lines below to skip)
{% for vf in dcmac_hdl_files %}
#import_files -fileset sources_1 -norecurse [file normalize "{{ vf }}"]
{% endfor %}

# --- Drive DCMAC creation based on config ---
{% if needs_dcmac %}
  set DCMAC0_ENABLED {{ dc_enable_0 }}
  set DCMAC1_ENABLED {{ dc_enable_1 }}
  set DUAL_QSFP_DCMAC0 {{ dual_qsfp_0 }}
  set DUAL_QSFP_DCMAC1 {{ dual_qsfp_1 }}

  # Calls proc add_dcmac_inst which expects the above variables
  add_dcmac_inst
{% else %}
  set DCMAC0_ENABLED 0
  set DCMAC1_ENABLED 0
  set DUAL_QSFP_DCMAC0 0
  set DUAL_QSFP_DCMAC1 0
  # Ethernet disabled; no DCMAC hierarchy created.
{% endif %}

# === AXI-Lite SmartConnect for service_layer control ===
# === AXI-Lite SmartConnect for service_layer control ===
{% if sl_have_xbar %}
  current_bd_design service_layer_user

  # Create SmartConnect inside the service_layer BD with a local name
  create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_0
  set_property -dict [list \
    CONFIG.NUM_CLKS {{ "{" ~ sl_num_clks ~ "}" }} \
    CONFIG.NUM_MI   {{ "{" ~ sl_num_mi  ~ "}" }} \
    CONFIG.NUM_SI   {{ "{" ~ sl_num_si  ~ "}" }} \
  ] [get_bd_cells smartconnect_0]
  # Rename for convenience
  set_property name {{ sl_smartconnect_name }} [get_bd_cells smartconnect_0]

  # Clocks & reset (design pins to SC pins)
  connect_bd_net [get_bd_pins {{ sl_clk0 }}] [get_bd_pins {{ sl_smartconnect_name }}/aclk]
  connect_bd_net [get_bd_pins {{ sl_clk1 }}] [get_bd_pins {{ sl_smartconnect_name }}/aclk1]
  connect_bd_net [get_bd_pins {{ sl_rstn }}] [get_bd_pins {{ sl_smartconnect_name }}/aresetn]

  # SI: service_layer design's S_AXILITE -> SmartConnect S00_AXI
  connect_bd_intf_net \
    [get_bd_intf_pins {{ sl_si_src_if }}] \
    [get_bd_intf_pins {{ sl_smartconnect_name }}/S00_AXI]

  # MI: fan-out to DCMAC hierarchies' s_axi
  {% for tgt in sl_mi_targets %}
    {% set idx = "%02d"|format(loop.index0) %}
    connect_bd_intf_net \
      [get_bd_intf_pins {{ sl_smartconnect_name }}/M{{ idx }}_AXI] \
      [get_bd_intf_pins {{ tgt }}]
  {% endfor %}

  # Tie QSFP block clocks/resets: ap_clk -> aclk0, ap_rst_n -> service ap_rst_n
  {% for q in sl_qsfp_blocks %}
    connect_bd_net [get_bd_pins {{ sl_clk0 }}] [get_bd_pins {{ q }}/ap_clk]
    connect_bd_net [get_bd_pins {{ sl_rstn }}] [get_bd_pins {{ q }}/ap_rst_n]
  {% endfor %}

{% else %}
  # No AXI-Lite users: create a dummy SmartConnect with clocks+reset,
  # connect S_AXILITE -> S00_AXI, but leave the single M00_AXI unconnected.
  # current_bd_design service_layer_user

  # create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 smartconnect_0
  # set_property -dict [list \
  #   CONFIG.NUM_CLKS {2} \
  #   CONFIG.NUM_MI   {1} \
  #   CONFIG.NUM_SI   {1} \
  # ] [get_bd_cells smartconnect_0]
  # set_property name {{ sl_smartconnect_name }} [get_bd_cells smartconnect_0]

  # # Clocks & reset
  # connect_bd_net [get_bd_pins aclk0]     [get_bd_pins {{ sl_smartconnect_name }}/aclk]
  # connect_bd_net [get_bd_pins aclk1]     [get_bd_pins {{ sl_smartconnect_name }}/aclk1]
  # connect_bd_net [get_bd_pins ap_rst_n]  [get_bd_pins {{ sl_smartconnect_name }}/aresetn]

  # # SI: service_layer design's S_AXILITE -> SmartConnect S00_AXI
  # connect_bd_intf_net \
  #   [get_bd_intf_pins axi_noc_0/M00_AXI] \
  #   [get_bd_intf_pins {{ sl_smartconnect_name }}/S00_AXI]

  # M00_AXI intentionally left unconnected

{% endif %}


# === QSFP <-> NoC AXIS links (inside service_layer) ===
{% if sl_axis_noc_links %}
  # Enter service_layer hierarchy
  set __oldCurInst [current_bd_instance .]
  current_bd_instance [get_bd_cells /service_layer]

  {% for L in sl_axis_noc_links %}
  # Link: {{ L.src_pin }} -> {{ L.dst_pin }}
  set __src [get_bd_intf_pins {{ L.src_pin }}]
  set __dst [get_bd_intf_pins {{ L.dst_pin }}]
  if { $__src eq "" } {
    error "AXIS source pin '{{ L.src_pin }}' not found in service_layer."
  }
  if { $__dst eq "" } {
    error "AXIS dest pin '{{ L.dst_pin }}' not found in service_layer."
  }
  connect_bd_intf_net $__src $__dst
  {% endfor %}

  # Restore previous instance
  current_bd_instance $__oldCurInst
{% else %}
  # No QSFP <-> NoC links required
{% endif %}

# @TODO: change this when virtualization core is available.
# === Temporary VIRT wiring: S_VIRT_x -> sl2noc_virt_x/S00_AXI ===
current_bd_design service_layer_user
for {set i 0} {$i < 4} {incr i} {
  set sv     [get_bd_intf_pins S_VIRT_$i]
  set noc_in [get_bd_intf_pins sl2noc_virt_$i/S00_AXI]

  if { $sv eq "" } {
    puts "Info: service_layer: S_VIRT_$i not present; skipping."
    continue
  }
  if { $noc_in eq "" } {
    puts "Info: service_layer: sl2noc_virt_$i/S00_AXI not present; skipping."
    continue
  }
  connect_bd_intf_net $sv $noc_in
}

assign_bd_address
validate_bd_design
save_bd_design
current_bd_design [get_bd_designs top]
validate_bd_design
save_bd_design

# ===== End Service Layer =====
