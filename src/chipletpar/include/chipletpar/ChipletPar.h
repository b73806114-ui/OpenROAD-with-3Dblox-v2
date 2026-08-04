#pragma once

#include <string>
#include <vector>

#include "ChipletPartIR.h"

namespace chipletpar {

struct PartitionResult
{
  std::vector<std::string> vertex_names;
  std::vector<int> partition_ids;
  int partition_count = 0;
  int edge_cut = 0;
  float cost = 0.0f;
  bool automatic_search = false;
};

chiplet::IRDesign read3DBloxDesign(const std::string& dbx_file,
                                   const std::string& dbv_file = std::string());

std::string read3DBloxDesignExternalYaml(const std::string& dbx_file);

void write3DBloxMetadataInput(const std::string& design_external_yaml,
                              const chiplet::IRDesign& design,
                              const std::string& output_dbx);

PartitionResult partitionDesign(const chiplet::IRDesign& design,
                                int partition_count,
                                int seed = 42);

PartitionResult makePartitionResult(const chiplet::IRDesign& design,
                                    std::vector<std::string> vertex_names,
                                    std::vector<int> partition_ids,
                                    float cost);

void annotate3DBlox(const std::string& metadata_source_dbx,
                    const std::string& output_dbx,
                    const PartitionResult& result);

void annotate3DBloxFromMetadata(const std::string& design_external_yaml,
                                const std::string& output_dbx,
                                const PartitionResult& result);

}  // namespace chipletpar
