# (c) Copyright 2024, Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
############################################################

# don't care about the reset performance
set_false_path -quiet -from [get_pins "*/clock_reset/usr_?_psr/U0/ACTIVE_LOW_BSR_OUT_DFF[0].FDRE_BSR_N/C"]
set_false_path -quiet -from [get_pins "*/clock_reset/usr_?_psr/U0/ACTIVE_LOW_PR_OUT_DFF[0].FDRE_PER_N/C"]
# Slash pblock


# Service Layer pblock
# resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NMU128_X0Y12:NOC_NMU128_X0Y12}
# resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NSU128_X0Y7:NOC_NSU128_X0Y7}

# SLR0 pblock

# Indicate that SLR pblocks must strictly be obeyed.

# BASE NSUs/NMUs






#set_property LOC DCMAC_X0Y2 [get_cells cKSgftop_i~fservice_layer~fqsfp_2_n_3~fDCMAC_subsys~fdcmac_1_core~finst~fi_service_layer_inst_0_dcmac_1_core_0_top~tobsabqrqac5cbvet~]
create_pblock pblock_service_layer
add_cells_to_pblock [get_pblocks pblock_service_layer] [get_cells -quiet [list top_i/service_layer]]
resize_pblock [get_pblocks pblock_service_layer] -add {SLICE_X0Y296:SLICE_X379Y572}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFG_FABRIC_X0Y167:BUFG_FABRIC_X4Y96}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFG_GT_X0Y167:BUFG_GT_X1Y96}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFG_GT_SYNC_X0Y286:BUFG_GT_SYNC_X1Y164}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFG_PS_X1Y36:BUFG_PS_X1Y47}
resize_pblock [get_pblocks pblock_service_layer] -add {DCMAC_X0Y2:DCMAC_X1Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {DPLL_X0Y12:DPLL_X3Y8}
resize_pblock [get_pblocks pblock_service_layer] -add {DSP58_CPLX_X0Y148:DSP58_CPLX_X11Y286}
resize_pblock [get_pblocks pblock_service_layer] -add {DSP_X0Y148:DSP_X23Y286}
resize_pblock [get_pblocks pblock_service_layer] -add {GTM_QUAD_X1Y7:GTM_QUAD_X1Y8 GTM_QUAD_X0Y9:GTM_QUAD_X0Y10}
resize_pblock [get_pblocks pblock_service_layer] -add {GTM_REFCLK_X1Y14:GTM_REFCLK_X1Y17 GTM_REFCLK_X0Y18:GTM_REFCLK_X0Y21}
resize_pblock [get_pblocks pblock_service_layer] -add {HSC_X0Y1:HSC_X0Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {ILKNF_X0Y0:ILKNF_X0Y0}
resize_pblock [get_pblocks pblock_service_layer] -add {IRI_QUAD_X0Y2319:IRI_QUAD_X254Y1212}
resize_pblock [get_pblocks pblock_service_layer] -add {MRMAC_X0Y3:MRMAC_X1Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NMU512_X0Y6:NOC_NMU512_X3Y11}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NPS_VNOC_X0Y12:NOC_NPS_VNOC_X3Y23}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NSU512_X0Y6:NOC_NSU512_X3Y11}
resize_pblock [get_pblocks pblock_service_layer] -add {PCIE50_X0Y1:PCIE50_X0Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {PS9_X0Y1:PS9_X0Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {RAMB18_X0Y150:RAMB18_X16Y289}
resize_pblock [get_pblocks pblock_service_layer] -add {RAMB36_X0Y75:RAMB36_X16Y144}
resize_pblock [get_pblocks pblock_service_layer] -add {URAM288_X0Y75:URAM288_X8Y144}
resize_pblock [get_pblocks pblock_service_layer] -add {URAM_CAS_DLY_X0Y3:URAM_CAS_DLY_X8Y5}
resize_pblock [get_pblocks pblock_service_layer] -add {CLOCKREGION_X10Y0:CLOCKREGION_X12Y0}
set_property SNAPPING_MODE ON [get_pblocks pblock_service_layer]
set_property IS_SOFT FALSE [get_pblocks pblock_service_layer]
set_property NOC_HIGH_ID_MAX 63 [get_pblocks pblock_service_layer]
set_property NOC_HIGH_ID_MIN 48 [get_pblocks pblock_service_layer]


create_pblock pblock_SLR0
resize_pblock [get_pblocks pblock_SLR0] -add {CLOCKREGION_X0Y0:CLOCKREGION_X7Y0}
resize_pblock [get_pblocks pblock_SLR0] -add {CLOCKREGION_X0Y1:CLOCKREGION_X9Y4}
add_cells_to_pblock pblock_SLR0 [get_cells "top_i/static_region/aved/clock_reset"]
add_cells_to_pblock pblock_SLR0 [get_cells -hierarchical -filter {PARENT =~ "*/base_logic" && NAME !~ "*/base_logic/pcie_slr*_sc" && NAME !~ "*/base_logic/axi_gpio_0"}]
add_cells_to_pblock pblock_SLR0 [get_cells "top_i/static_region/aved/base_logic/pcie_slr0_mgmt_sc"]

# Indicate that SLR pblocks must strictly be obeyed.
set_property IS_SOFT FALSE [get_pblocks pblock_SLR0]

# BASE NSUs/NMUs
set_property LOC NOC_NSU512_X0Y0  [get_cells -filter {REF_NAME == NOC_NSU512} -of [get_pins -leaf -filter {DIRECTION == OUT} -of [get_nets -of [get_pins "*/base_logic/pcie_slr0_mgmt_sc/S00_AXI_wvalid"]]]]


create_pblock pblock_slash
add_cells_to_pblock [get_pblocks pblock_slash] [get_cells -quiet [list top_i/slash]]
resize_pblock [get_pblocks pblock_slash] -add {SLICE_X28Y592:SLICE_X351Y895}
resize_pblock [get_pblocks pblock_slash] -add {BUFG_FABRIC_X0Y239:BUFG_FABRIC_X4Y168}
resize_pblock [get_pblocks pblock_slash] -add {BUFG_PS_X2Y48:BUFG_PS_X2Y59}
resize_pblock [get_pblocks pblock_slash] -add {DSP58_CPLX_X0Y296:DSP58_CPLX_X11Y447}
resize_pblock [get_pblocks pblock_slash] -add {DSP_X0Y296:DSP_X23Y447}
resize_pblock [get_pblocks pblock_slash] -add {IRI_QUAD_X18Y2892:IRI_QUAD_X236Y3611 IRI_QUAD_X29Y2508:IRI_QUAD_X236Y2891 IRI_QUAD_X4Y2396:IRI_QUAD_X84Y2507}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NMU512_X0Y12:NOC_NMU512_X3Y18}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NPS_VNOC_X0Y24:NOC_NPS_VNOC_X3Y37}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NSU512_X0Y13:NOC_NSU512_X3Y18}
resize_pblock [get_pblocks pblock_slash] -add {RAMB18_X1Y298:RAMB18_X16Y449}
resize_pblock [get_pblocks pblock_slash] -add {RAMB36_X1Y149:RAMB36_X16Y224}
resize_pblock [get_pblocks pblock_slash] -add {URAM288_X1Y149:URAM288_X7Y224}
resize_pblock [get_pblocks pblock_slash] -add {URAM_CAS_DLY_X1Y6:URAM_CAS_DLY_X7Y8}
resize_pblock [get_pblocks pblock_slash] -add {CLOCKREGION_X8Y0:CLOCKREGION_X9Y0}
set_property SNAPPING_MODE ON [get_pblocks pblock_slash]
set_property IS_SOFT FALSE [get_pblocks pblock_slash]
set_property NOC_HIGH_ID_MAX 47 [get_pblocks pblock_slash]
set_property NOC_HIGH_ID_MIN 36 [get_pblocks pblock_slash]

set_clock_groups -asynchronous -group [get_clocks -of [get_pins top_i/static_region/aved/cips/inst/pspmc_0/inst/buffer_pl_clk_3.PL_CLK_3_BUFG/O]] -group [get_clocks -of [get_pins top_i/slash/clk_wizard_0/inst/clock_primitive_inst/BUFG_clkout1_inst/O]]
set_clock_groups -asynchronous -group [get_clocks -of [get_pins top_i/static_region/aved/cips/inst/pspmc_0/inst/buffer_pl_clk_3.PL_CLK_3_BUFG/O]] -group [get_clocks -of [get_pins top_i/service_layer/clk_wizard_0/inst/clock_primitive_inst/BUFG_clkout1_inst/O]]
