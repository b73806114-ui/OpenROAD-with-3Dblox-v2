#!/usr/bin/env python3
"""Generate larger ESC test cases following case0 structure exactly."""
import os, math

def gen_case(name, nets, die_cols, pitch):
    os.makedirs(name, exist_ok=True)
    rows = math.ceil(nets / die_cols)

    die_w = die_cols * pitch + pitch
    die_h = rows * pitch + pitch
    sub_w = die_w + pitch * 8  # extra space for routing
    sub_h = die_h + pitch * 2

    # Die bumps: grid layout, centered
    dx0 = (die_w - (die_cols - 1) * pitch) / 2
    dy0 = (die_h - (rows - 1) * pitch) / 2

    # Sub bumps: same layout but offset to right side of substrate
    sx0 = sub_w - die_cols * pitch - pitch
    sy0 = (sub_h - (rows - 1) * pitch) / 2

    f_die = open(f"{name}/die_bumps.bmap", "w")
    f_die.write(f"# {nets} die micro-bumps\n")
    f_sub = open(f"{name}/substrate_bumps.bmap", "w")
    f_sub.write(f"# {nets} substrate target bumps\n")

    for i in range(nets):
        r, c = divmod(i, die_cols)
        net = f"NET_{i}"
        dx = round(dx0 + c * pitch, 4)
        dy = round(dy0 + r * pitch, 4)
        sx = round(sx0 + c * pitch, 4)
        sy = round(sy0 + r * pitch, 4)
        f_die.write(f"bump_{net}  BUMP  {dx:.4f}  {dy:.4f}  port_{net}  {net}\n")
        f_sub.write(f"bump_{net}  BUMP  {sx:.4f}  {sy:.4f}  port_{net}  {net}\n")

    f_die.close()
    f_sub.close()

    with open(f"{name}/die.3dbv", "w") as f:
        f.write(f"""Header:
  version: "1.0"
  unit: "micron"
  precision: 2000
ChipletDef:
  DieChiplet:
    type: die
    design_area: [{die_w}, {die_h}]
    thickness: 90.0
    regions:
      bond_region:
        side: back
        bmap: die_bumps.bmap
        coords:
          - [-{pitch}, -{pitch}]
          - [{die_w + pitch}, -{pitch}]
          - [{die_w + pitch}, {die_h + pitch}]
          - [-{pitch}, {die_h + pitch}]
    external:
      APR_tech_file:
        - ../../../../test/Nangate45/Nangate45_tech.lef
      LEF_file:
        - ../../../../test/Nangate45/fake_bumps.lef
""")

    with open(f"{name}/substrate.3dbv", "w") as f:
        f.write(f"""Header:
  version: "1.0"
  unit: "micron"
  precision: 2000
ChipletDef:
  SubChiplet:
    type: substrate
    design_area: [{sub_w}, {sub_h}]
    thickness: 90.0
    regions:
      bond_region:
        side: front
        bmap: substrate_bumps.bmap
        coords:
          - [-{pitch}, -{pitch}]
          - [{sub_w + pitch}, -{pitch}]
          - [{sub_w + pitch}, {sub_h + pitch}]
          - [-{pitch}, {sub_h + pitch}]
    external:
      APR_tech_file:
        - ../../../../test/Nangate45/Nangate45_tech.lef
      LEF_file:
        - ../../../../test/Nangate45/fake_bumps.lef
""")

    sub_off_x = -(sub_w // 2)
    sub_off_y = -(sub_h - die_h) // 2

    with open(f"{name}/top.3dbx", "w") as f:
        f.write(f"""Header:
  version: "1.0"
  unit: "micron"
  precision: 2000
  include:
    - die.3dbv
    - substrate.3dbv
Design:
  name: "EscapeRouting_{nets}"
  external:
    verilog_file: [top.v]
ChipletInst:
  die_inst:
    reference: DieChiplet
  substrate_inst:
    reference: SubChiplet
Stack:
  die_inst:
    loc: [0.0, 0.0]
    z: 116.0
    orient: R0
  substrate_inst:
    loc: [{sub_off_x}, {sub_off_y}]
    z: 0.0
    orient: R0
Connection:
  die_to_substrate:
    top: die_inst.regions.bond_region
    bot: substrate_inst.regions.bond_region
    thickness: 26.0
""")

    with open(f"{name}/top.v", "w") as f:
        pins = ", ".join(f"input port_NET_{i}" for i in range(nets))
        ports = ", ".join(f".port_NET_{i}(NET_{i})" for i in range(nets))
        wires = "\n  ".join(f"wire NET_{i};" for i in range(nets))
        f.write(f"module DieChiplet ({pins}); endmodule\n")
        f.write(f"module SubChiplet ({pins}); endmodule\n")
        f.write(f"module EscapeRouting_{nets} ();\n  {wires}\n")
        f.write(f"  DieChiplet die_inst ({ports});\n")
        f.write(f"  SubChiplet substrate_inst ({ports});\n")
        f.write(f"endmodule\n")

    with open(f"{name}/load.tcl", "w") as f:
        f.write(f"""read_3dbx {name}/top.3dbx
esc -net_width 13 -min_spacing 13 -die_radius 45 -substrate_radius 45 -layer metal1
puts [esc_report]
""")

    print(f"  {name}/ ({nets} nets, {rows}x{die_cols}, die={die_w}x{die_h}, sub={sub_w}x{sub_h})")


if __name__ == "__main__":
    gen_case("case10", nets=10, die_cols=5, pitch=160)
    gen_case("case20", nets=20, die_cols=10, pitch=160)
