module DieChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9, input port_NET_10, input port_NET_11, input port_NET_12, input port_NET_13, input port_NET_14, input port_NET_15, input port_NET_16, input port_NET_17, input port_NET_18, input port_NET_19); endmodule
module SubChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9, input port_NET_10, input port_NET_11, input port_NET_12, input port_NET_13, input port_NET_14, input port_NET_15, input port_NET_16, input port_NET_17, input port_NET_18, input port_NET_19); endmodule
module EscapeRouting_20 ();
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
  wire NET_15;
  wire NET_16;
  wire NET_17;
  wire NET_18;
  wire NET_19;
  DieChiplet die_inst (.port_NET_0(NET_0), .port_NET_1(NET_1), .port_NET_2(NET_2), .port_NET_3(NET_3), .port_NET_4(NET_4), .port_NET_5(NET_5), .port_NET_6(NET_6), .port_NET_7(NET_7), .port_NET_8(NET_8), .port_NET_9(NET_9), .port_NET_10(NET_10), .port_NET_11(NET_11), .port_NET_12(NET_12), .port_NET_13(NET_13), .port_NET_14(NET_14), .port_NET_15(NET_15), .port_NET_16(NET_16), .port_NET_17(NET_17), .port_NET_18(NET_18), .port_NET_19(NET_19));
  SubChiplet substrate_inst (.port_NET_0(NET_0), .port_NET_1(NET_1), .port_NET_2(NET_2), .port_NET_3(NET_3), .port_NET_4(NET_4), .port_NET_5(NET_5), .port_NET_6(NET_6), .port_NET_7(NET_7), .port_NET_8(NET_8), .port_NET_9(NET_9), .port_NET_10(NET_10), .port_NET_11(NET_11), .port_NET_12(NET_12), .port_NET_13(NET_13), .port_NET_14(NET_14), .port_NET_15(NET_15), .port_NET_16(NET_16), .port_NET_17(NET_17), .port_NET_18(NET_18), .port_NET_19(NET_19));
endmodule
