// Package-level connectivity. Each NET_* owns a die micro-bump, an aligned
// substrate-top bond bump, a top-RDL landing pad, and a bottom package bump.
module DieChiplet (
    input port_NET_A,
    input port_NET_B,
    input port_NET_C,
    input port_NET_D,
    input port_NET_E
);
endmodule

module SubChiplet (
    input port_BOND_NET_A,
    input port_BOND_NET_B,
    input port_BOND_NET_C,
    input port_BOND_NET_D,
    input port_BOND_NET_E,
    input port_RDL_PAD_NET_A,
    input port_RDL_PAD_NET_B,
    input port_RDL_PAD_NET_C,
    input port_RDL_PAD_NET_D,
    input port_RDL_PAD_NET_E,
    input port_PACKAGE_NET_A,
    input port_PACKAGE_NET_B,
    input port_PACKAGE_NET_C,
    input port_PACKAGE_NET_D,
    input port_PACKAGE_NET_E
);
endmodule

module EscapeRouting ();
  wire NET_A;
  wire NET_B;
  wire NET_C;
  wire NET_D;
  wire NET_E;

  DieChiplet die_inst (
      .port_NET_A(NET_A), .port_NET_B(NET_B), .port_NET_C(NET_C),
      .port_NET_D(NET_D), .port_NET_E(NET_E));
  SubChiplet substrate_inst (
      .port_BOND_NET_A(NET_A), .port_BOND_NET_B(NET_B),
      .port_BOND_NET_C(NET_C), .port_BOND_NET_D(NET_D),
      .port_BOND_NET_E(NET_E), .port_PACKAGE_NET_A(NET_A),
      .port_RDL_PAD_NET_A(NET_A), .port_RDL_PAD_NET_B(NET_B),
      .port_RDL_PAD_NET_C(NET_C), .port_RDL_PAD_NET_D(NET_D),
      .port_RDL_PAD_NET_E(NET_E),
      .port_PACKAGE_NET_B(NET_B), .port_PACKAGE_NET_C(NET_C),
      .port_PACKAGE_NET_D(NET_D), .port_PACKAGE_NET_E(NET_E));
endmodule
