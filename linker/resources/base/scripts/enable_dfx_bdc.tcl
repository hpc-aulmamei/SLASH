
current_bd_design [get_bd_designs top]

set_property -dict [list CONFIG.ENABLE_DFX {true}] [get_bd_cells slash]
set_property -dict [list CONFIG.ENABLE_DFX {true}] [get_bd_cells service_layer]

set_property -dict [list CONFIG.LOCK_PROPAGATE {true}] [get_bd_cells slash]
set_property -dict [list CONFIG.LOCK_PROPAGATE {true}] [get_bd_cells service_layer]

validate_bd_design
save_bd_design
