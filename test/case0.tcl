set case_dir [file join [file dirname [file normalize [info script]]] case0]
read_3dbx [file join $case_dir top.3dbx]

# Each package net owns four physical endpoints: the die micro-bump, aligned
# substrate bond bump, top-RDL landing pad, and bottom package bump.
set chip_nets [[ord::get_db] getChipNets]
if {[llength $chip_nets] != 5} {
  error "expected five package dbChipNets"
}
foreach chip_net $chip_nets {
  if {[$chip_net getNumBumpInsts] != 4} {
    error "expected four physical endpoints per package dbChipNet"
  }
}

set routed [esc \
  -net_width 13 \
  -min_spacing 13 \
  -die_radius 45 \
  -substrate_radius 45 \
  -layer metal1]
if {[dict get $routed nets_routed] != 5} {
  error "expected five routed nets"
}

# ESC package-RDL is a 3D-only route. It must not materialize a dbWire that
# would be rendered in the substrate's 2D block layout.
set top_chip [[ord::get_db] getChip]
set substrate_inst [$top_chip findChipInst substrate_inst]
set substrate_block [[$substrate_inst getMasterChip] getBlock]
foreach net_name {NET_A NET_B NET_C NET_D NET_E} {
  set substrate_net [$substrate_block findNet $net_name]
  if {[$substrate_net getWire] ne "NULL"} {
    error "ESC must not create a 2D dbWire for $net_name"
  }
}

set report [esc_report]
if {[dict size [dict get $report nets]] != 5} {
  error "expected five route entries in esc_report"
}

if {[esc_clear] != 5} {
  error "expected five cleared routes"
}
if {[dict get [esc_report] status] ne "no routes stored"} {
  error "expected esc_clear to remove all stored routes"
}

set rerouted [esc \
  -net_width 13 \
  -min_spacing 13 \
  -die_radius 45 \
  -substrate_radius 45 \
  -layer metal1]
if {[dict get $rerouted nets_routed] != 5} {
  error "expected five rerouted nets"
}
if {[esc_clear] != 5} {
  error "expected five cleared rerouted nets"
}

puts "pass"
