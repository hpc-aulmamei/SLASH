# Ethernet-only service_layer post-opt placement/pin constraints.
# Use -quiet so this hook is safe even when the expected ETH hierarchy/ports are absent.

set_property -quiet LOC GTM_QUAD_X0Y10 [get_cells -quiet {top_i/service_layer/qsfp_2_n_3/DCMAC_subsys/dcmac_gt1_wrapper/gt0_quad/inst/quad_inst}]
set_property -quiet LOC GTM_QUAD_X1Y7 [get_cells -quiet {top_i/service_layer/qsfp_0_n_1/DCMAC_subsys/dcmac_gt0_wrapper/gt0_quad/inst/quad_inst}]
set_property -quiet -dict { PACKAGE_PIN AR51 } [get_ports -quiet {qsfp0_322mhz_clk_p}]
set_property -quiet -dict { PACKAGE_PIN AL17 } [get_ports -quiet {qsfp2_322mhz_clk_p}]
