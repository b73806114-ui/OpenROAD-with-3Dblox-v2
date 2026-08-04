module DieChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9); endmodule
module SubChiplet (input port_NET_0, input port_NET_1, input port_NET_2, input port_NET_3, input port_NET_4, input port_NET_5, input port_NET_6, input port_NET_7, input port_NET_8, input port_NET_9); endmodule
module EscapeRouting_10 ();
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
  DieChiplet die_inst (.port_NET_0(NET_0), .port_NET_1(NET_1), .port_NET_2(NET_2), .port_NET_3(NET_3), .port_NET_4(NET_4), .port_NET_5(NET_5), .port_NET_6(NET_6), .port_NET_7(NET_7), .port_NET_8(NET_8), .port_NET_9(NET_9));
  SubChiplet substrate_inst (.port_NET_0(NET_0), .port_NET_1(NET_1), .port_NET_2(NET_2), .port_NET_3(NET_3), .port_NET_4(NET_4), .port_NET_5(NET_5), .port_NET_6(NET_6), .port_NET_7(NET_7), .port_NET_8(NET_8), .port_NET_9(NET_9));
endmodule
