# ESC — Escape Router for Chiplet Packaging

**ESC** (Escape) is a high-density escape routing solver for advanced IC packaging. It plans non-conflicting paths from die-side microbumps to substrate-side bumps under spacing, width, and manufacturability constraints.

The solver uses column-based DFS with dynamic-programming wirelength evaluation. A resource table of sampled candidate waypoints is constructed from the bump geometry; Pareto pruning discards dominated partial solutions. Final routes are relaxed through alternating top-down / bottom-up passes to equalize spacing and resolve shared-column collisions.

## OpenROAD Integration

The module is registered as `esc` in the OpenROAD Tcl interpreter. It reads bump coordinates directly from the in-memory OpenDB (`dbChipBump` → `dbInst`), classifies them into die-side and substrate-side, and runs the escape router. Its result is retained as package-RDL metadata for the 3D viewer; it deliberately does not create a block-level `dbWire`, so package RDL is absent from the 2D layout.

### Quick Start

```tcl
# Load a 3DBlox chiplet design
read_3dbx src/esc/test/case0/top.3dbx

# Run escape routing
esc -net_width 13 -min_spacing 13 -die_radius 45 -substrate_radius 45 -layer metal1

# Inspect results
puts [esc_report]

# Save the routed database
write_db routed.odb

# Clear routing results (keep bump data)
esc_clear
```

### Tcl Commands

| Command | Description |
|---|---|
| `esc -net_width W -min_spacing S -die_radius D -substrate_radius R ?-layer L?` | Run escape routing |
| `esc_report` | Return a Tcl dict with critical_length, total_wirelength, and per-net routes |
| `esc_clear` | Remove stored routing results from the database |

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `-net_width` | double | required | Trace width (same units as bump coordinates) |
| `-min_spacing` | double | required | Minimum trace-to-trace clearance |
| `-die_radius` | double | required | Die-side microbump radius |
| `-substrate_radius` | double | required | Substrate-side bump radius |
| `-layer` | string | `metal1` | Routing layer name |

### How Bump Classification Works

ESC automatically classifies bumps into two groups:

| Bump side / chip type | Classified as |
|---|---|
| `dbChip::ChipType::SUBSTRATE` / `RDL` | substrate-side (regardless of face) |
| Die `dbChipRegion::Side::BACK` | die-side (the die underside) |
| Die `dbChipRegion::Side::FRONT` | substrate-side fallback |

Bumps are paired across the two sides by their physical net name (`dbChipBump::getNet()`). Each die-side bump that shares a net name with a substrate-side bump forms one routing connection.

### Data Flow

```
Input (3DBlox)                    Algorithm                   Output (OpenDB)
─────────────────                 ─────────                   ────────────────
top.3dbx ─┐
die.3dbv ─┤─ read_3dbx ─→ dbChipBump::getNet() ─→ Escaper ─→ top dbChip route properties
sub.3dbv ─┤  top.v ─→ dbChipNet (die / bond-bump / RDL-pad / package)│ dbStringProperty (route polylines)
           └────────── dbInst::getLocation() ───────────┘       dbDoubleProperty (lengths)
```

### Output Format

Each routed net is written to OpenDB as:

- **`dbChipNet`** at the package level, created from `top.v`; each test net
  contains the die bottom bump, aligned substrate-top bond bump, top-RDL
  landing pad, and substrate-bottom package bump
- **`dbStringProperty`** `esc_route.<NET>` — serialised route polyline
- **`dbDoubleProperty`** `esc_route_length.<NET>` — wire length
- **`dbStringProperty`** `esc_route_layer.<NET>` — selected routing layer
- **`dbDoubleProperty`** `esc_route_width.<NET>` — requested wire width
- **`dbDoubleProperty`** `esc_critical_length` — longest route
- **`dbDoubleProperty`** `esc_total_wirelength` — sum of all routes

ESC deliberately does not create a substrate `dbWire` or `dbSWire`. Its RDL is
a package-level, 3D-only result carried by these properties, so it is not drawn
by the 2D Layout. A later physical package-RDL implementation may materialize
the same route as `dbSWire/dbSBox` on an explicit package technology layer.

The route properties are stored on the top `dbChip`. When a same-named
`dbChipNet` exists, ESC also attaches unqualified `esc_route`,
`esc_route_length`, `esc_route_layer`, and `esc_route_width` properties to the
logical package net. The web 3D viewer exports the detailed top-chip routes as
a detailed polyline of the matching `Chip Nets` entry on the substrate/RDL
plane. All properties are preserved by `write_db`.

### Test Case

```
src/esc/test/case0/
├── top.3dbx          # 3DBlox assembly (EscapeRouting, 2 chiplets)
├── die.3dbv          # DieChiplet (5 bumps, BACK side, 680×680 µm)
├── substrate.3dbv    # SubChiplet (top bond bumps/RDL pads + bottom bumps, 4000×3000 µm)
├── die_bumps.bmap    # Die bump map
├── top_bond_bumps.bmap # Substrate-top bond bumps aligned to die bumps
├── top_rdl_pads.bmap # Substrate-top RDL landing pads (ESC targets)
├── substrate_bumps.bmap
├── top.v             # Package-level NET_* dbChipNet connectivity
└── load_3dblox.tcl   # Example script
```

Expected result (golden):

| Net | Length (µm) | Waypoints |
|---|---|---|
| NET_B | 2088.82 | 4 pts |
| NET_C | 2128.72 | 4 pts |
| NET_A | 2314.28 | 3 pts |
| NET_D | 1680.00 | 2 pts (straight) |
| NET_E | 1680.00 | 2 pts (straight) |

Routing order: B → C → A → D → E  
Critical length: 2314.28 µm  
Total wirelength: 9891.83 µm

The substrate is centred below the die: it occupies `x=-1660..2340 µm`,
`y=-1160..1840 µm`, and `z=0..90 µm`, while the die footprint is
`x=0..680 µm`, `y=0..680 µm`, and `z=116..206 µm`. The assembly has a valid
`dbChipConn` named `die_to_substrate` between the die's `BACK` bond region and
the substrate's `FRONT` bond region. Its 26-µm thickness matches the physical
gap. A second virtual `dbChipConn`, `substrate_to_package`, anchors the
substrate's bottom RDL face to the package/PCB; this supplies OpenDB's ground
group and prevents floating-chip warnings. The die's `BACK` bumps are at
`z=116 µm`; the substrate's `BACK` RDL/package bumps are at `z=0 µm`. Package
bumps use substrate-local coordinates and populate all four substrate perimeter
edges. The top bond sites are physical bumps: each directly contacts the
matching die-bottom bump, with no chip-net line between them in the web viewer.
The RDL landing sites remain planar pads. Both use the 3DBlox bump endpoint
representation to participate in a `dbChipNet`. ESC routes only on the
substrate's top RDL, toward the east-side top-RDL landing pads, which matches
its current single-direction model.

For every `NET_*`, `top.v` creates one `dbChipNet` with four physical
terminals: the die's bottom bump, the XY-aligned substrate-top bond bump, a
top-RDL landing pad, and the substrate-bottom package bump. ESC writes only
the top-RDL route from the bond bump to the RDL landing pad. The landing pad and
bottom package bump share XY, so the web 3D viewer renders their direct
through-substrate connection without creating a second horizontal RDL layer.

### Dependencies

- C++20
- Boost (headers only)
- pthread
- OpenDB (chiplet API)

### Algorithm Reference

Net ordering is explored via DFS with dynamic-programming wirelength evaluation in each column. A resource table of sampled candidate waypoints is constructed from the bump geometry; Pareto pruning discards dominated partial solutions. Final routes are relaxed through 10 top-down and 10 bottom-up passes to equalize spacing and resolve shared-column collisions.
