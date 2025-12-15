proc create_root_design { parentCell } {

  variable script_folder
  variable design_name

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports
  set HBM_AXI_00 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_00 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_00

  set HBM_AXI_01 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_01 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_01

  set HBM_AXI_10 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_10 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_10

  set HBM_AXI_11 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_11 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_11

  set HBM_AXI_12 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_12 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_12

  set HBM_AXI_13 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_13 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_13

  set HBM_AXI_14 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_14 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_14

  set HBM_AXI_15 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_15 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_15

  set HBM_AXI_16 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_16 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_16

  set HBM_AXI_17 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_17 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_17

  set HBM_AXI_18 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_18 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_18

  set HBM_AXI_19 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_19 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_19

  set HBM_AXI_02 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_02 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_02

  set HBM_AXI_20 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_20 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_20

  set HBM_AXI_21 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_21 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_21

  set HBM_AXI_22 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_22 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_22

  set HBM_AXI_23 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_23 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_23

  set HBM_AXI_24 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_24 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_24

  set HBM_AXI_25 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_25 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_25

  set HBM_AXI_26 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_26 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_26

  set HBM_AXI_27 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_27 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_27

  set HBM_AXI_28 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_28 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_28

  set HBM_AXI_29 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_29 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_29

  set HBM_AXI_03 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_03 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_03

  set HBM_AXI_30 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_30 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_30

  set HBM_AXI_31 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_31 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_31

  set HBM_AXI_32 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_32 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_32

  set HBM_AXI_33 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_33 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_33

  set HBM_AXI_34 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_34 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_34

  set HBM_AXI_35 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_35 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_35

  set HBM_AXI_36 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_36 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_36

  set HBM_AXI_37 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_37 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_37

  set HBM_AXI_38 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_38 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_38

  set HBM_AXI_39 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_39 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_39

  set HBM_AXI_04 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_04 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_04

  set HBM_AXI_40 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_40 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_40

  set HBM_AXI_41 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_41 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_41

  set HBM_AXI_42 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_42 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_42

  set HBM_AXI_43 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_43 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_43

  set HBM_AXI_44 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_44 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_44

  set HBM_AXI_45 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_45 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_45

  set HBM_AXI_46 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_46 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_46

  set HBM_AXI_47 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_47 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_47

  set HBM_AXI_48 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_48 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_48

  set HBM_AXI_49 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_49 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_49

  set HBM_AXI_05 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_05 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_05

  set HBM_AXI_50 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_50 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_50

  set HBM_AXI_51 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_51 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_51

  set HBM_AXI_52 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_52 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_52

  set HBM_AXI_53 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_53 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_53

  set HBM_AXI_54 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_54 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_54

  set HBM_AXI_55 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_55 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_55

  set HBM_AXI_56 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_56 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_56

  set HBM_AXI_57 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_57 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_57

  set HBM_AXI_58 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_58 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_58

  set HBM_AXI_59 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_59 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_59

  set HBM_AXI_06 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_06 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_06

  set HBM_AXI_60 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_60 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_60

  set HBM_AXI_61 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_61 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_61

  set HBM_AXI_62 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_62 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_62

  set HBM_AXI_63 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_63 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_63

  set HBM_AXI_07 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_07 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_07

  set HBM_AXI_08 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_08 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_08

  set HBM_AXI_09 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 HBM_AXI_09 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {256} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.HAS_RRESP {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $HBM_AXI_09

  set S_AXILITE_INI [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inimm_rtl:1.0 S_AXILITE_INI ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $S_AXILITE_INI
  {% raw %}
  set_property APERTURES {{0x202_0000_0000 128M}} [get_bd_intf_ports S_AXILITE_INI]
  {% endraw %}
  set M00_INI [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M00_INI ]

  set M01_INI [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M01_INI ]

  set M02_INI [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M02_INI ]

  set M03_INI [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 M03_INI ]

  set HBM_VNOC_INI_00 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_00 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_00

  set HBM_VNOC_INI_01 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_01 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_01

  set HBM_VNOC_INI_02 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_02 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_02

  set HBM_VNOC_INI_03 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_03 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_03

  set HBM_VNOC_INI_04 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_04 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_04

  set HBM_VNOC_INI_05 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_05 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_05

  set HBM_VNOC_INI_06 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_06 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_06

  set HBM_VNOC_INI_07 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inimm_rtl:1.0 HBM_VNOC_INI_07 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $HBM_VNOC_INI_07

  set M00_INIS_0 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_0 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_0

  set M00_INIS_1 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_1 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_1

  set M00_INIS_2 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_2 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_2

  set M00_INIS_3 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_3 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_3

  set M00_INIS_4 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_4 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_4

  set M00_INIS_5 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_5 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_5

  set M00_INIS_6 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_6 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_6

  set M00_INIS_7 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:inis_rtl:1.0 M00_INIS_7 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   ] $M00_INIS_7

  set DCMAC_INIS_0 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_0 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_0

  set DCMAC_INIS_1 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_1 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_1

  set DCMAC_INIS_2 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_2 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_2

  set DCMAC_INIS_3 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_3 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_3

  set DCMAC_INIS_4 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_4 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_4

  set DCMAC_INIS_5 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_5 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_5

  set DCMAC_INIS_6 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_6 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_6

  set DCMAC_INIS_7 [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:inis_rtl:1.0 DCMAC_INIS_7 ]
  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   ] $DCMAC_INIS_7

  set SL_VIRT_0 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 SL_VIRT_0 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {512} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   ] $SL_VIRT_0

  set SL_VIRT_1 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 SL_VIRT_1 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {512} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   ] $SL_VIRT_1

  set SL_VIRT_2 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 SL_VIRT_2 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {512} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   ] $SL_VIRT_2

  set SL_VIRT_3 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 SL_VIRT_3 ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {512} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   ] $SL_VIRT_3

  set QDMA_SLAVE_BRIDGE [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:aximm_rtl:1.0 QDMA_SLAVE_BRIDGE ]
  set_property -dict [ list \
   CONFIG.ADDR_WIDTH {64} \
   CONFIG.DATA_WIDTH {512} \
   CONFIG.FREQ_HZ {400000000} \
   CONFIG.HAS_BURST {0} \
   CONFIG.NUM_READ_OUTSTANDING {16} \
   CONFIG.NUM_WRITE_OUTSTANDING {16} \
   CONFIG.PROTOCOL {AXI4} \
   CONFIG.READ_WRITE_MODE {WRITE_ONLY} \
   ] $QDMA_SLAVE_BRIDGE

  # Create ports
  set aclk1 [ create_bd_port -dir I -type clk -freq_hz 400000000 aclk1 ]
  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {HBM_AXI_062:HBM_AXI_060:HBM_AXI_059:HBM_AXI_028:HBM_AXI_019:HBM_AXI_051:HBM_AXI_013:HBM_AXI_021:HBM_AXI_031:HBM_AXI_022:HBM_AXI_017:HBM_AXI_044:HBM_AXI_025:HBM_AXI_015:HBM_AXI_027:HBM_AXI_024:HBM_AXI_05:HBM_AXI_061:HBM_AXI_023:HBM_AXI_02:HBM_AXI_03:HBM_AXI_018:HBM_AXI_06:HBM_AXI_048:HBM_AXI_052:HBM_AXI_056:HBM_AXI_016:HBM_AXI_026:HBM_AXI_033:HBM_AXI_09:HBM_AXI_020:HBM_AXI_07:HBM_AXI_011:HBM_AXI_055:HBM_AXI_014:HBM_AXI_038:HBM_AXI_010:HBM_AXI_08:HBM_AXI_012:HBM_AXI_045:HBM_AXI_029:HBM_AXI_063:HBM_AXI_030:HBM_AXI_032:HBM_AXI_034:HBM_AXI_039:HBM_AXI_035:HBM_AXI_00:HBM_AXI_047:HBM_AXI_037:HBM_AXI_058:HBM_AXI_040:HBM_AXI_043:HBM_AXI_036:HBM_AXI_046:HBM_AXI_042:HBM_AXI_049:HBM_AXI_01:HBM_AXI_050:HBM_AXI_04:HBM_AXI_053:HBM_AXI_041:HBM_AXI_054:HBM_AXI_057:HBM_AXI_10:HBM_AXI_11:HBM_AXI_12:HBM_AXI_13:HBM_AXI_14:HBM_AXI_15:HBM_AXI_16:HBM_AXI_17:HBM_AXI_18:HBM_AXI_19:HBM_AXI_20:HBM_AXI_21:HBM_AXI_22:HBM_AXI_23:HBM_AXI_24:HBM_AXI_25:HBM_AXI_26:HBM_AXI_27:HBM_AXI_28:HBM_AXI_29:HBM_AXI_30:HBM_AXI_31:HBM_AXI_32:HBM_AXI_33:HBM_AXI_34:HBM_AXI_35:HBM_AXI_36:HBM_AXI_37:HBM_AXI_38:HBM_AXI_39:HBM_AXI_40:HBM_AXI_41:HBM_AXI_42:HBM_AXI_43:HBM_AXI_44:HBM_AXI_45:HBM_AXI_46:HBM_AXI_47:HBM_AXI_48:HBM_AXI_49:HBM_AXI_50:HBM_AXI_51:HBM_AXI_52:HBM_AXI_53:HBM_AXI_54:HBM_AXI_55:HBM_AXI_56:HBM_AXI_57:HBM_AXI_58:HBM_AXI_59:HBM_AXI_60:HBM_AXI_61:HBM_AXI_62:HBM_AXI_63:SL_VIRT_0:SL_VIRT_1:SL_VIRT_2:SL_VIRT_3:QDMA_SLAVE_BRIDGE} \
   CONFIG.CLK_DOMAIN {top_clk_wizard_0_0_clk_out1} \
 ] $aclk1
  set ap_rst_n [ create_bd_port -dir I -type rst ap_rst_n ]
  set arstn [ create_bd_port -dir I -type rst arstn ]
  set s_axi_aclk [ create_bd_port -dir I -type clk s_axi_aclk ]
  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {s_axilite} \
   CONFIG.CLK_DOMAIN {bd_4885_pspmc_0_0_pl0_ref_clk} \
 ] $s_axi_aclk

# Create instance: ddr_noc_0, and set properties
  set ddr_noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 ddr_noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $ddr_noc_0


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /ddr_noc_0/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /ddr_noc_0/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /ddr_noc_0/aclk0]

  # Create instance: ddr_noc_1, and set properties
  set ddr_noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 ddr_noc_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $ddr_noc_1


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /ddr_noc_1/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /ddr_noc_1/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /ddr_noc_1/aclk0]

  # Create instance: ddr_noc_2, and set properties
  set ddr_noc_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 ddr_noc_2 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $ddr_noc_2


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /ddr_noc_2/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /ddr_noc_2/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /ddr_noc_2/aclk0]

  # Create instance: ddr_noc_3, and set properties
  set ddr_noc_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 ddr_noc_3 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $ddr_noc_3


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /ddr_noc_3/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /ddr_noc_3/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /ddr_noc_3/aclk0]

 # Create instance: hbm_vnoc_00, and set properties
  set hbm_vnoc_00 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_00 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_00


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_00/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_00/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_00/aclk0]

  # Create instance: hbm_vnoc_01, and set properties
  set hbm_vnoc_01 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_01 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_01


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_01/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_01/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_01/aclk0]

  # Create instance: hbm_vnoc_02, and set properties
  set hbm_vnoc_02 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_02 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_02


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_02/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_02/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_02/aclk0]

  # Create instance: hbm_vnoc_03, and set properties
  set hbm_vnoc_03 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_03 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_03


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_03/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_03/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_03/aclk0]

  # Create instance: hbm_vnoc_04, and set properties
  set hbm_vnoc_04 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_04 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_04


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_04/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_04/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_04/aclk0]

  # Create instance: hbm_vnoc_05, and set properties
  set hbm_vnoc_05 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_05 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_05


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_05/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_05/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_05/aclk0]

  # Create instance: hbm_vnoc_06, and set properties
  set hbm_vnoc_06 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_06 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_06


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_06/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_06/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_06/aclk0]

  # Create instance: hbm_vnoc_07, and set properties
  set hbm_vnoc_07 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 hbm_vnoc_07 ]
  set_property -dict [list \
    CONFIG.NSI_NAMES {} \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
    CONFIG.NUM_NSI {0} \
  ] $hbm_vnoc_07


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /hbm_vnoc_07/M00_INI]

  set_property -dict [ list \
   CONFIG.CONNECTIONS {M00_INI {read_bw {500} write_bw {500}}} \
   CONFIG.NOC_PARAMS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /hbm_vnoc_07/S00_AXI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXI} \
 ] [get_bd_pins /hbm_vnoc_07/aclk0]

  # Create instance: dcmac_axis_noc_0, and set properties
  set dcmac_axis_noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_0


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_0/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS { write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_0/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_0/aclk0]

  # Create instance: dcmac_axis_noc_1, and set properties
  set dcmac_axis_noc_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_1 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_1


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_1/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_1/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_1/aclk0]

  # Create instance: dcmac_axis_noc_2, and set properties
  set dcmac_axis_noc_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_2 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_2


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_2/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_2/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_2/aclk0]

  # Create instance: dcmac_axis_noc_3, and set properties
  set dcmac_axis_noc_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_3 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_3


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_3/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_3/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_3/aclk0]

  # Create instance: dcmac_axis_noc_4, and set properties
  set dcmac_axis_noc_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_4 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_4


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_4/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_4/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_4/aclk0]

  # Create instance: dcmac_axis_noc_5, and set properties
  set dcmac_axis_noc_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_5 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_5


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_5/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_5/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_5/aclk0]

  # Create instance: dcmac_axis_noc_6, and set properties
  set dcmac_axis_noc_6 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_6 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_6


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_6/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_6/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_6/aclk0]

  # Create instance: dcmac_axis_noc_7, and set properties
  set dcmac_axis_noc_7 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_7 ]
  set_property -dict [list \
    CONFIG.NUM_MI {0} \
    CONFIG.NUM_NMI {1} \
  ] $dcmac_axis_noc_7


  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
 ] [get_bd_intf_pins /dcmac_axis_noc_7/M00_INIS]

  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CONNECTIONS {M00_INIS {write_bw {500}}} \
   CONFIG.DEST_IDS {} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_7/S00_AXIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {S00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_7/aclk0]

  # Create instance: dcmac_axis_noc_s_0, and set properties
  set dcmac_axis_noc_s_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_0 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_0


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_0/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_0/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_0/aclk0]

  # Create instance: dcmac_axis_noc_s_1, and set properties
  set dcmac_axis_noc_s_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_1 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_1


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_1/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_1/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_1/aclk0]

  # Create instance: dcmac_axis_noc_s_2, and set properties
  set dcmac_axis_noc_s_2 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_2 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_2


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_2/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_2/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_2/aclk0]

  # Create instance: dcmac_axis_noc_s_3, and set properties
  set dcmac_axis_noc_s_3 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_3 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_3


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_3/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_3/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_3/aclk0]

  # Create instance: dcmac_axis_noc_s_4, and set properties
  set dcmac_axis_noc_s_4 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_4 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_4


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_4/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_4/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_4/aclk0]

  # Create instance: dcmac_axis_noc_s_5, and set properties
  set dcmac_axis_noc_s_5 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_5 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_5


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_5/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_5/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_5/aclk0]

  # Create instance: dcmac_axis_noc_s_6, and set properties
  set dcmac_axis_noc_s_6 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_6 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_6


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_6/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_6/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_6/aclk0]

  # Create instance: dcmac_axis_noc_s_7, and set properties
  set dcmac_axis_noc_s_7 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_noc:1.0 dcmac_axis_noc_s_7 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $dcmac_axis_noc_s_7


  set_property -dict [ list \
   CONFIG.TDATA_NUM_BYTES {64} \
   CONFIG.TDEST_WIDTH {0} \
   CONFIG.TID_WIDTH {0} \
   CONFIG.CATEGORY {pl} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_7/M00_AXIS]

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {load} \
   CONFIG.CONNECTIONS {M00_AXIS { write_bw {500} write_avg_burst {4}}} \
   CONFIG.DEST_IDS {} \
 ] [get_bd_intf_pins /dcmac_axis_noc_s_7/S00_INIS]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXIS} \
 ] [get_bd_pins /dcmac_axis_noc_s_7/aclk0]

   set axi_noc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_noc:1.1 axi_noc_0 ]
  set_property -dict [list \
    CONFIG.NUM_NSI {1} \
    CONFIG.NUM_SI {0} \
  ] $axi_noc_0

{% raw %}
set_property -dict [ list \
  CONFIG.APERTURES {{0x202_0000_0000 128M}} \
  CONFIG.CATEGORY {pl} \
] [get_bd_intf_pins /axi_noc_0/M00_AXI]
{% endraw %}

  set_property -dict [ list \
   CONFIG.INI_STRATEGY {driver} \
   CONFIG.CONNECTIONS {M00_AXI {read_bw {5} write_bw {5} read_avg_burst {4} write_avg_burst {4}}} \
 ] [get_bd_intf_pins /axi_noc_0/S00_INI]

  set_property -dict [ list \
   CONFIG.ASSOCIATED_BUSIF {M00_AXI} \
 ] [get_bd_pins /axi_noc_0/aclk0]

  connect_bd_intf_net -intf_net S_AXILITE_INI_1 [get_bd_intf_ports S_AXILITE_INI] [get_bd_intf_pins axi_noc_0/S00_INI]  
  connect_bd_intf_net -intf_net S00_INIS_0_1 [get_bd_intf_ports DCMAC_INIS_0] [get_bd_intf_pins dcmac_axis_noc_s_0/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_1_1 [get_bd_intf_ports DCMAC_INIS_1] [get_bd_intf_pins dcmac_axis_noc_s_1/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_2_1 [get_bd_intf_ports DCMAC_INIS_2] [get_bd_intf_pins dcmac_axis_noc_s_2/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_3_1 [get_bd_intf_ports DCMAC_INIS_3] [get_bd_intf_pins dcmac_axis_noc_s_3/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_4_1 [get_bd_intf_ports DCMAC_INIS_4] [get_bd_intf_pins dcmac_axis_noc_s_4/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_5_1 [get_bd_intf_ports DCMAC_INIS_5] [get_bd_intf_pins dcmac_axis_noc_s_5/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_6_1 [get_bd_intf_ports DCMAC_INIS_6] [get_bd_intf_pins dcmac_axis_noc_s_6/S00_INIS]
  connect_bd_intf_net -intf_net S00_INIS_7_1 [get_bd_intf_ports DCMAC_INIS_7] [get_bd_intf_pins dcmac_axis_noc_s_7/S00_INIS]

  connect_bd_intf_net -intf_net ddr_noc_0_M00_INI [get_bd_intf_ports M00_INI] [get_bd_intf_pins ddr_noc_0/M00_INI]
  connect_bd_intf_net -intf_net ddr_noc_1_M00_INI [get_bd_intf_ports M01_INI] [get_bd_intf_pins ddr_noc_1/M00_INI]
  connect_bd_intf_net -intf_net ddr_noc_2_M00_INI [get_bd_intf_ports M02_INI] [get_bd_intf_pins ddr_noc_2/M00_INI]
  connect_bd_intf_net -intf_net ddr_noc_3_M00_INI [get_bd_intf_ports M03_INI] [get_bd_intf_pins ddr_noc_3/M00_INI]

  connect_bd_intf_net -intf_net hbm_vnoc_00_M00_INI [get_bd_intf_ports HBM_VNOC_INI_00] [get_bd_intf_pins hbm_vnoc_00/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_01_M00_INI [get_bd_intf_ports HBM_VNOC_INI_01] [get_bd_intf_pins hbm_vnoc_01/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_02_M00_INI [get_bd_intf_ports HBM_VNOC_INI_02] [get_bd_intf_pins hbm_vnoc_02/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_03_M00_INI [get_bd_intf_ports HBM_VNOC_INI_03] [get_bd_intf_pins hbm_vnoc_03/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_04_M00_INI [get_bd_intf_ports HBM_VNOC_INI_04] [get_bd_intf_pins hbm_vnoc_04/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_05_M00_INI [get_bd_intf_ports HBM_VNOC_INI_05] [get_bd_intf_pins hbm_vnoc_05/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_06_M00_INI [get_bd_intf_ports HBM_VNOC_INI_06] [get_bd_intf_pins hbm_vnoc_06/M00_INI]
  connect_bd_intf_net -intf_net hbm_vnoc_07_M00_INI [get_bd_intf_ports HBM_VNOC_INI_07] [get_bd_intf_pins hbm_vnoc_07/M00_INI]

  connect_bd_intf_net -intf_net dcmac_axis_noc_0_M00_INIS [get_bd_intf_ports M00_INIS_0] [get_bd_intf_pins dcmac_axis_noc_0/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_1_M00_INIS [get_bd_intf_ports M00_INIS_1] [get_bd_intf_pins dcmac_axis_noc_1/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_2_M00_INIS [get_bd_intf_ports M00_INIS_2] [get_bd_intf_pins dcmac_axis_noc_2/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_3_M00_INIS [get_bd_intf_ports M00_INIS_3] [get_bd_intf_pins dcmac_axis_noc_3/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_4_M00_INIS [get_bd_intf_ports M00_INIS_4] [get_bd_intf_pins dcmac_axis_noc_4/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_5_M00_INIS [get_bd_intf_ports M00_INIS_5] [get_bd_intf_pins dcmac_axis_noc_5/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_6_M00_INIS [get_bd_intf_ports M00_INIS_6] [get_bd_intf_pins dcmac_axis_noc_6/M00_INIS]
  connect_bd_intf_net -intf_net dcmac_axis_noc_7_M00_INIS [get_bd_intf_ports M00_INIS_7] [get_bd_intf_pins dcmac_axis_noc_7/M00_INIS]

    connect_bd_net -net clk_wizard_0_clk_out1  [get_bd_ports aclk1] \
  [get_bd_pins ddr_noc_0/aclk0] \
  [get_bd_pins ddr_noc_3/aclk0] \
  [get_bd_pins ddr_noc_2/aclk0] \
  [get_bd_pins ddr_noc_1/aclk0] \
  [get_bd_pins hbm_vnoc_00/aclk0] \
  [get_bd_pins hbm_vnoc_01/aclk0] \
  [get_bd_pins hbm_vnoc_02/aclk0] \
  [get_bd_pins hbm_vnoc_03/aclk0] \
  [get_bd_pins hbm_vnoc_04/aclk0] \
  [get_bd_pins hbm_vnoc_05/aclk0] \
  [get_bd_pins hbm_vnoc_06/aclk0] \
  [get_bd_pins hbm_vnoc_07/aclk0] \
  [get_bd_pins dcmac_axis_noc_0/aclk0] \
  [get_bd_pins dcmac_axis_noc_1/aclk0] \
  [get_bd_pins dcmac_axis_noc_2/aclk0] \
  [get_bd_pins dcmac_axis_noc_3/aclk0] \
  [get_bd_pins dcmac_axis_noc_4/aclk0] \
  [get_bd_pins dcmac_axis_noc_5/aclk0] \
  [get_bd_pins dcmac_axis_noc_6/aclk0] \
  [get_bd_pins dcmac_axis_noc_7/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_0/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_1/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_2/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_3/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_4/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_5/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_6/aclk0] \
  [get_bd_pins dcmac_axis_noc_s_7/aclk0] \
  [get_bd_pins axi_noc_0/aclk0]

# === Instantiate kernel IPs ===
{% for name, inst in instances.items() %}
set {{ name }} [ create_bd_cell -type ip -vlnv {{ inst.kernel.vlnv }} {{ name }} ]
{% endfor %}

# === Connect kernel clocks to aclk1 ===
{% for c in clocks %}
connect_bd_net [get_bd_pins {{ c.src_pin }}] [get_bd_ports aclk1]
{% endfor %}

# === Connect kernel resets to ap_rst_n ===
{% for r in resets %}
connect_bd_net [get_bd_pins {{ r.src_pin }}] [get_bd_ports ap_rst_n]
{% endfor %}

# === SmartConnects for AXI-Lite control ===
{% for sc in smartconnects %}
# Create {{ sc.name }}
set {{ sc.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ sc.name }} ]
set_property -dict [list \
  CONFIG.NUM_CLKS {2} \
  CONFIG.NUM_SI {1} \
  CONFIG.NUM_MI {{ '{' ~ sc.num_mi ~ '}' }} \
] ${{ sc.name }}

# Clocks/Reset
connect_bd_net [get_bd_pins {{ sc.name }}/aclk]    [get_bd_ports s_axi_aclk]
connect_bd_net [get_bd_pins {{ sc.name }}/aclk1]   [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ sc.name }}/aresetn] [get_bd_ports arstn]

# SI (slave) connection
{% if sc.si_from.type == 'bd_port' %}
connect_bd_intf_net [get_bd_intf_pins {{ sc.si_from.name }}] [get_bd_intf_pins {{ sc.name }}/S00_AXI]
{% else %}
connect_bd_intf_net [get_bd_intf_pins {{ sc.si_from.prev }}/M{{ "%02d"|format(sc.chain_slot) }}_AXI] [get_bd_intf_pins {{ sc.name }}/S00_AXI]
{% endif %}

# MI (master) connections to kernel AXI-Lite pins
{% for m in sc.mi %}
connect_bd_intf_net [get_bd_intf_pins {{ sc.name }}/M{{ "%02d"|format(m.slot) }}_AXI] [get_bd_intf_pins {{ m.dst_pin }}]
{% endfor %}

{% endfor %}

# === HBM AXI-MM connections ===

# Direct connections (single writer per HBMx)
{% for c in hbm_direct|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ c.src_pin }}] [get_bd_intf_ports {{ c.dst_port }}]
{% endfor %}

# SmartConnect reduction nodes (NUM_CLKS=1, aclk→aclk1, aresetn→ap_rst_n)
{% for n in hbm_smart_nodes|default([]) %}
# {{ n.name }}  (NUM_SI={{ n.num_si }}, NUM_MI=1)
set {{ n.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ n.name }} ]
set_property -dict [list \
  CONFIG.NUM_CLKS {1} \
  CONFIG.NUM_MI {1} \
  CONFIG.NUM_SI {{ '{' ~ n.num_si ~ '}' }} \
] ${{ n.name }}

# Clocks/Reset
connect_bd_net [get_bd_pins {{ n.name }}/aclk]    [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ n.name }}/aresetn] [get_bd_ports ap_rst_n]

# Slave interfaces (sources into this node)
{% for si in n.si %}
connect_bd_intf_net [get_bd_intf_pins {{ si.src }}] [get_bd_intf_pins {{ n.name }}/S{{ "%02d"|format(si.slot) }}_AXI]
{% endfor %}

{% endfor %}

# Root outputs to HBM ports
{% for r in hbm_smart_roots|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ r.sc_name }}/M00_AXI] [get_bd_intf_ports {{ r.dst_port }}]
{% endfor %}

# === DDR AXI-MM connections (via Versal NoC) ===

# Direct connects: single writer to a DDRx NoC slave
{% for c in ddr_direct|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ c.src_pin }}] [get_bd_intf_pins {{ c.dst_pin }}]
{% endfor %}

# SmartConnect reduction nodes (NUM_CLKS=1, aclk→aclk1, aresetn→ap_rst_n)
{% for n in ddr_smart_nodes|default([]) %}
# {{ n.name }} (NUM_SI={{ n.num_si }}, NUM_MI=1)
set {{ n.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ n.name }} ]
set_property -dict [list \
  CONFIG.NUM_CLKS {1} \
  CONFIG.NUM_MI {1} \
  CONFIG.NUM_SI {{ '{' ~ n.num_si ~ '}' }} \
] ${{ n.name }}

# Clocks/Reset
connect_bd_net [get_bd_pins {{ n.name }}/aclk]    [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ n.name }}/aresetn] [get_bd_ports ap_rst_n]

# SIs into this SmartConnect
{% for si in n.si %}
connect_bd_intf_net [get_bd_intf_pins {{ si.src }}] [get_bd_intf_pins {{ n.name }}/S{{ "%02d"|format(si.slot) }}_AXI]
{% endfor %}

{% endfor %}

# Root outputs to DDR NoC slaves
{% for r in ddr_smart_roots|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ r.sc_name }}/M00_AXI] [get_bd_intf_pins {{ r.dst_pin }}]
{% endfor %}

# === MEM AXI-MM connections (via VNOC) ===

# Direct connects: single writer to a MEMx VNOC slave
{% for c in mem_direct|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ c.src_pin }}] [get_bd_intf_pins {{ c.dst_pin }}]
{% endfor %}

# SmartConnect reduction nodes for MEMx (NUM_CLKS=1, aclk→aclk1, aresetn→ap_rst_n)
{% for n in mem_smart_nodes|default([]) %}
# {{ n.name }} (NUM_SI={{ n.num_si }}, NUM_MI=1)
set {{ n.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ n.name }} ]
set_property -dict [list \
  CONFIG.NUM_CLKS {1} \
  CONFIG.NUM_MI {1} \
  CONFIG.NUM_SI {{ '{' ~ n.num_si ~ '}' }} \
] ${{ n.name }}

# Clocks/Reset
connect_bd_net [get_bd_pins {{ n.name }}/aclk]    [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ n.name }}/aresetn] [get_bd_ports ap_rst_n]

# SIs into this SmartConnect
{% for si in n.si %}
connect_bd_intf_net [get_bd_intf_pins {{ si.src }}] [get_bd_intf_pins {{ n.name }}/S{{ "%02d"|format(si.slot) }}_AXI]
{% endfor %}

{% endfor %}

# Root outputs to VNOC slaves
{% for r in mem_smart_roots|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ r.sc_name }}/M00_AXI] [get_bd_intf_pins {{ r.dst_pin }}]
{% endfor %}

# === VIRT AXI-MM connections (direct to BD interface ports, like HBM) ===

# Direct connects
{% for c in virt_direct|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ c.src_pin }}] [get_bd_intf_ports {{ c.dst_pin }}]
{% endfor %}

# SmartConnect reduction nodes (NUM_CLKS=1, aclk→aclk1, aresetn→ap_rst_n)
{% for n in virt_smart_nodes|default([]) %}
# {{ n.name }} (NUM_SI={{ n.num_si }}, NUM_MI=1)
set {{ n.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ n.name }} ]
set_property -dict [list \
  CONFIG.NUM_CLKS {1} \
  CONFIG.NUM_MI {1} \
  CONFIG.NUM_SI {{ '{' ~ n.num_si ~ '}' }} \
] ${{ n.name }}

# Clocks/Reset
connect_bd_net [get_bd_pins {{ n.name }}/aclk]    [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ n.name }}/aresetn] [get_bd_ports ap_rst_n]

# SIs
{% for si in n.si %}
connect_bd_intf_net [get_bd_intf_pins {{ si.src }}] [get_bd_intf_pins {{ n.name }}/S{{ "%02d"|format(si.slot) }}_AXI]
{% endfor %}

# Root output to the VIRT interface **port**
connect_bd_intf_net [get_bd_intf_pins {{ n.name }}/M00_AXI] [get_bd_intf_ports {{ (virt_smart_roots | selectattr('sc_name','equalto', n.name) | map(attribute='dst_pin') | list | first) }}]

{% endfor %}

# === AXIS stream connections from config ===
{% for e in axis_streams|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ e.src_pin }}] [get_bd_intf_pins {{ e.dst_pin }}]
{% endfor %}

# === AXIS network: instance -> fabric TX ===
{% for e in axis_to_fabric|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ e.src_pin }}] [get_bd_intf_pins {{ e.dst_pin }}]
{% endfor %}

# === AXIS network: fabric RX -> instance ===
{% for e in axis_from_fabric|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ e.src_pin }}] [get_bd_intf_pins {{ e.dst_pin }}]
{% endfor %}

# === Regular AXIS streams (instance <-> instance) ===
{% for e in axis_streams|default([]) %}
connect_bd_intf_net [get_bd_intf_pins {{ e.src_pin }}] [get_bd_intf_pins {{ e.dst_pin }}]
{% endfor %}


# === AXI Register Slice terminators for UNUSED memory endpoints ===
{% for t in axi_terminators|default([]) %}
# {{ t.name }} -> {{ t.dst_kind }} {{ t.dst }}
set {{ t.name }} [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 {{ t.name }} ]

# Clock / Reset
connect_bd_net [get_bd_pins {{ t.name }}/aclk]    [get_bd_ports aclk1]
connect_bd_net [get_bd_pins {{ t.name }}/aresetn] [get_bd_ports ap_rst_n]

# Leave S_AXI unconnected on purpose

# Connect M_AXI to the free destination
{% if t.dst_kind == 'port' %}
connect_bd_intf_net [get_bd_intf_pins {{ t.name }}/M_AXI] [get_bd_intf_ports {{ t.dst }}]
{% else %}
connect_bd_intf_net [get_bd_intf_pins {{ t.name }}/M_AXI] [get_bd_intf_pins {{ t.dst }}]
{% endif %}

{% endfor %}

# === HOST aggregation: SmartConnect tree -> QDMA_SLAVE_BRIDGE ===
{% if host_sc_defs and host_sc_defs|length > 0 %}
  # Create SmartConnect nodes
  {% for sc in host_sc_defs %}
    create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 {{ sc.name }}
    set_property -dict [list \
      CONFIG.NUM_CLKS {{ "{2}" if host_use_two_clks else "{1}" }} \
      CONFIG.NUM_SI   {{ "{" ~ sc.num_si ~ "}" }} \
      CONFIG.NUM_MI   {1} \
    ] [get_bd_cells {{ sc.name }}]
    {% if host_use_two_clks %}
      connect_bd_net [get_bd_ports {{ host_clk }}]     [get_bd_pins {{ sc.name }}/aclk]
      connect_bd_net [get_bd_ports {{ host_clk_aux }}] [get_bd_pins {{ sc.name }}/aclk1]
    {% else %}
      connect_bd_net [get_bd_ports {{ host_clk }}]     [get_bd_pins {{ sc.name }}/aclk]
    {% endif %}
    connect_bd_net [get_bd_ports {{ host_rstn }}]      [get_bd_pins {{ sc.name }}/aresetn]

    # Wire SIs for this stage
    {% if sc.si_srcs %}
      {% for src in sc.si_srcs %}
        {% set idx = "%02d"|format(loop.index0) %}
        connect_bd_intf_net \
          [get_bd_intf_pins {{ src }}] \
          [get_bd_intf_pins {{ sc.name }}/S{{ idx }}_AXI]
      {% endfor %}
    {% elif sc.si_up %}
      {% for up in sc.si_up %}
        {% set idx = "%02d"|format(loop.index0) %}
        connect_bd_intf_net \
          [get_bd_intf_pins {{ up }}] \
          [get_bd_intf_pins {{ sc.name }}/S{{ idx }}_AXI]
      {% endfor %}
    {% endif %}
  {% endfor %}

  # Final M00_AXI -> QDMA_SLAVE_BRIDGE
  connect_bd_intf_net \
    [get_bd_intf_pins {{ host_final }}/M00_AXI] \
    [get_bd_intf_ports {{ host_target_rtl }}]

{% elif host_need_term %}
  # No HOST-mapped sources → park QDMA_SLAVE_BRIDGE with an AXI register slice
  create_bd_cell -type ip -vlnv xilinx.com:ip:axi_register_slice:2.1 {{ host_term_name }}
  # Clocks & reset
  connect_bd_net [get_bd_ports {{ host_clk }}]    [get_bd_pins {{ host_term_name }}/aclk]
  connect_bd_net [get_bd_ports {{ host_rstn }}]   [get_bd_pins {{ host_term_name }}/aresetn]
  # Leave S_AXI unconnected, drive M_AXI to QDMA_SLAVE_BRIDGE
  connect_bd_intf_net \
    [get_bd_intf_pins {{ host_term_name }}/M_AXI] \
    [get_bd_intf_ports {{ host_target_rtl }}]
{% else %}
  # HOST section: nothing to do
{% endif %}




}