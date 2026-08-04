#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "try_routing.hpp"

int main(int argc, char** argv)
{
  Timer timer("main");

  if (argc != 3) {
    std::cout << "Usage: " << argv[0] << " <input_file> <output_file>"
              << std::endl;
    return 1;
  }

  std::ifstream input(argv[1], std::ios::in);
  if (!input.is_open()) {
    std::cout << "Error: Cannot open input file " << argv[1] << std::endl;
    return 1;
  }

  std::ofstream output_probe(argv[2], std::ios::out);
  if (!output_probe.is_open()) {
    std::cout << "Error: Cannot open output file" << argv[2] << std::endl;
    return 1;
  }
  output_probe.close();

  std::vector<Bump> die_bumps;
  std::vector<Bump> substrate_bumps;
  std::vector<Net> nets;
  parse(input, die_bumps, substrate_bumps, nets);

  const std::string die_direction("right");
  const std::string substrate_direction("left");
  Escaper escaper;

  std::cout << "Our algorithm starts running..." << std::endl;
  escaper(die_bumps, die_direction, substrate_bumps, substrate_direction, nets);
  (void) escaper.output(std::string(argv[2]), nets);
  return 0;
}
