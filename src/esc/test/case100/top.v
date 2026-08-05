module DieChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9, input port_NET_10, input port_NET_11, input port_NET_12, input port_NET_13, input port_NET_14); endmodule
module SubChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9, input port_NET_10, input port_NET_11, input port_NET_12, input port_NET_13, input port_NET_14); endmodule
module EscapeRouting_15 ();
  wire NET_0;
  wire NET_1;
  wire NET_2;
  wire NET_3;
  wire NET_4;
  wire NET_5;
  wire NET_6;
  wire NET_7;
  wire NET_8;
  wire NET_9;
  wire NET_10;
  wire NET_11;
  wire NET_12;
  wire NET_13;
  wire NET_14;
  DieChiplet die_inst (.port_NET_0(NET_0), .port_NET_1(NET_1), .port_NET_2(NET_2), .port_NET_3(NET_3), .port_NET_4(NET_4), .port_NET_5(NET_5), .port_NET_6(NET_6), .port_NET_7(NET_7), .port_NET_8(NET_8), .port_NET_9(NET_9), .port_NET_10(NET_10), .port_NET_11(NET_11), .port_NET_12(NET_12), .port_NET_13(NET_13), .port_NET_14(NET_14));
  SubChiplet substrate_inst (.port_BOND_NET_0(NET_0), .port_BOND_NET_1(NET_1), .port_BOND_NET_2(NET_2), .port_BOND_NET_3(NET_3), .port_BOND_NET_4(NET_4), .port_BOND_NET_5(NET_5), .port_BOND_NET_6(NET_6), .port_BOND_NET_7(NET_7), .port_BOND_NET_8(NET_8), .port_BOND_NET_9(NET_9), .port_BOND_NET_10(NET_10), .port_BOND_NET_11(NET_11), .port_BOND_NET_12(NET_12), .port_BOND_NET_13(NET_13), .port_BOND_NET_14(NET_14), .port_RDL_NET_0(NET_0), .port_RDL_NET_1(NET_1), .port_RDL_NET_2(NET_2), .port_RDL_NET_3(NET_3), .port_RDL_NET_4(NET_4), .port_RDL_NET_5(NET_5), .port_RDL_NET_6(NET_6), .port_RDL_NET_7(NET_7), .port_RDL_NET_8(NET_8), .port_RDL_NET_9(NET_9), .port_RDL_NET_10(NET_10), .port_RDL_NET_11(NET_11), .port_RDL_NET_12(NET_12), .port_RDL_NET_13(NET_13), .port_RDL_NET_14(NET_14), .port_PKG_NET_0(NET_0), .port_PKG_NET_1(NET_1), .port_PKG_NET_2(NET_2), .port_PKG_NET_3(NET_3), .port_PKG_NET_4(NET_4), .port_PKG_NET_5(NET_5), .port_PKG_NET_6(NET_6), .port_PKG_NET_7(NET_7), .port_PKG_NET_8(NET_8), .port_PKG_NET_9(NET_9), .port_PKG_NET_10(NET_10), .port_PKG_NET_11(NET_11), .port_PKG_NET_12(NET_12), .port_PKG_NET_13(NET_13), .port_PKG_NET_14(NET_14));
endmodule
