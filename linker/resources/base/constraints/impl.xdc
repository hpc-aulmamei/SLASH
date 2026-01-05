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
create_pblock pblock_slash
add_cells_to_pblock [get_pblocks pblock_slash] [get_cells -quiet [list top_i/slash]]
resize_pblock [get_pblocks pblock_slash] -add {SLICE_X28Y860:SLICE_X349Y637}
resize_pblock [get_pblocks pblock_slash] -add {BUFG_FABRIC_X0Y239:BUFG_FABRIC_X4Y168}
resize_pblock [get_pblocks pblock_slash] -add {BUFG_PS_X2Y48:BUFG_PS_X2Y59}
resize_pblock [get_pblocks pblock_slash] -add {DSP58_CPLX_X0Y318:DSP58_CPLX_X11Y430}
resize_pblock [get_pblocks pblock_slash] -add {DSP_X0Y318:DSP_X23Y430}
resize_pblock [get_pblocks pblock_slash] -add {IRI_QUAD_X18Y3471:IRI_QUAD_X229Y2576}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NMU512_X0Y13:NOC_NMU512_X3Y17}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NPS_VNOC_X0Y26:NOC_NPS_VNOC_X3Y35}
resize_pblock [get_pblocks pblock_slash] -add {NOC_NSU512_X0Y14:NOC_NSU512_X3Y17}
resize_pblock [get_pblocks pblock_slash] -add {RAMB18_X1Y320:RAMB18_X15Y433}
resize_pblock [get_pblocks pblock_slash] -add {RAMB36_X1Y160:RAMB36_X15Y216}
resize_pblock [get_pblocks pblock_slash] -add {URAM288_X1Y160:URAM288_X7Y216}
resize_pblock [get_pblocks pblock_slash] -add {URAM_CAS_DLY_X1Y7:URAM_CAS_DLY_X7Y8}
set_property SNAPPING_MODE ON [get_pblocks pblock_slash]
set_property IS_SOFT FALSE [get_pblocks pblock_slash]
set_property NOC_HIGH_ID_MAX 47 [get_pblocks pblock_slash]
set_property NOC_HIGH_ID_MIN 36 [get_pblocks pblock_slash]

# Service Layer pblock
create_pblock pblock_service_layer
add_cells_to_pblock [get_pblocks pblock_service_layer] [get_cells -quiet [list top_i/service_layer]]
resize_pblock [get_pblocks pblock_service_layer] -add {SLICE_X0Y296:SLICE_X379Y572}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFGCE_X11Y0:BUFGCE_X12Y23}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFGCE_DIV_X11Y0:BUFGCE_DIV_X11Y2}
resize_pblock [get_pblocks pblock_service_layer] -add {BUFGCTRL_X11Y0:BUFGCTRL_X11Y6}
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
resize_pblock [get_pblocks pblock_service_layer] -add {MMCM_X11Y0:MMCM_X12Y0}
resize_pblock [get_pblocks pblock_service_layer] -add {MRMAC_X0Y3:MRMAC_X1Y1}
# resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NMU128_X0Y12:NOC_NMU128_X0Y12}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NMU512_X0Y6:NOC_NMU512_X3Y11}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NPS_VNOC_X0Y12:NOC_NPS_VNOC_X3Y23}
# resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NSU128_X0Y7:NOC_NSU128_X0Y7}
resize_pblock [get_pblocks pblock_service_layer] -add {NOC_NSU512_X0Y6:NOC_NSU512_X3Y11}
resize_pblock [get_pblocks pblock_service_layer] -add {PCIE50_X0Y1:PCIE50_X0Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {PS9_X0Y1:PS9_X0Y1}
resize_pblock [get_pblocks pblock_service_layer] -add {RAMB18_X0Y150:RAMB18_X16Y289}
resize_pblock [get_pblocks pblock_service_layer] -add {RAMB36_X0Y75:RAMB36_X16Y144}
resize_pblock [get_pblocks pblock_service_layer] -add {URAM288_X0Y75:URAM288_X8Y144}
resize_pblock [get_pblocks pblock_service_layer] -add {URAM_CAS_DLY_X0Y3:URAM_CAS_DLY_X8Y5}
set_property SNAPPING_MODE ON [get_pblocks pblock_service_layer]
set_property IS_SOFT FALSE [get_pblocks pblock_service_layer]
set_property NOC_HIGH_ID_MAX 63 [get_pblocks pblock_service_layer]
set_property NOC_HIGH_ID_MIN 48 [get_pblocks pblock_service_layer]

# SLR0 pblock
create_pblock pblock_SLR0
resize_pblock [get_pblocks pblock_SLR0] -add {CLOCKREGION_X0Y0:CLOCKREGION_X12Y0}
resize_pblock [get_pblocks pblock_SLR0] -add {CLOCKREGION_X0Y1:CLOCKREGION_X9Y4}
add_cells_to_pblock pblock_SLR0 [get_cells "*/clock_reset"]
add_cells_to_pblock pblock_SLR0 [get_cells -hierarchical -filter {PARENT =~ "*/base_logic" && NAME !~ "*/base_logic/pcie_slr*_sc" && NAME !~ "*/base_logic/axi_gpio_0"}]
add_cells_to_pblock pblock_SLR0 [get_cells "*/base_logic/pcie_slr0_mgmt_sc"]

# Indicate that SLR pblocks must strictly be obeyed.
set_property IS_SOFT FALSE [get_pblocks pblock_SLR0]

# BASE NSUs/NMUs
set_property LOC NOC_NSU512_X0Y0  [get_cells -filter {REF_NAME == NOC_NSU512} -of [get_pins -leaf -filter {DIRECTION == OUT} -of [get_nets -of [get_pins "*/base_logic/pcie_slr0_mgmt_sc/S00_AXI_wvalid"]]]]

