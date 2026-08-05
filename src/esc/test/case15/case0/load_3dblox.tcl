# Load 3Dblox case0 into OpenROAD
puts "=== Loading 3Dblox Case0: EscapeRouting ==="

# Read the main assembly relative to this script.
set case_dir [file dirname [file normalize [info script]]]
read_3dbx [file join $case_dir top.3dbx]

# Check results
set db [ord::get_db]
puts "Chip Insts: [llength [$db getUnfoldedChipInsts]]"
puts "Chip Conns: [llength [$db getUnfoldedChipConns]]"
puts "Chip Regions: [llength [$db getUnfoldedChipRegionInsts]]"
puts "Chip Bumps: [llength [$db getUnfoldedChipBumpInsts]]"
puts "Chip Nets: [llength [$db getUnfoldedChipNets]]"

puts "=== Load complete. Open 3D viewer from the Windows menu ==="
