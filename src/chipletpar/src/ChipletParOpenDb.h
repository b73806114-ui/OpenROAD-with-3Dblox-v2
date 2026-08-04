#pragma once

#include <string>

#include "chipletpar/ChipletPar.h"

namespace odb {
class dbChip;
}

namespace chipletpar {

inline constexpr const char* kDesignExternalProperty
    = "chipletpart.3dbx.design_external_yaml";
inline constexpr const char* kMetadataSourceProperty
    = "chipletpar_metadata_source";
inline constexpr const char* kPartitionIdProperty = "chipletpar_partition_id";
inline constexpr const char* kPartitionCountProperty
    = "chipletpar_partition_count";
inline constexpr const char* kPartitionEdgeCutProperty
    = "chipletpar_partition_edge_cut";
inline constexpr const char* kPartitionCostProperty
    = "chipletpar_partition_cost";
inline constexpr const char* kPartitionModeProperty
    = "chipletpar_partition_mode";

struct OpenDbWriteStats
{
  int instances_written = 0;
  int missing_instances = 0;
};

chiplet::IRDesign readOpenDbDesign(odb::dbChip* top_chip,
                                   const std::string& design_external_yaml
                                   = std::string());

void storeDesignExternalMetadata(odb::dbChip* top_chip,
                                 const std::string& design_external_yaml,
                                 const std::string& source = std::string());

std::string getDesignExternalMetadata(odb::dbChip* top_chip);

OpenDbWriteStats writePartitionResult(odb::dbChip* top_chip,
                                      const PartitionResult& result);

PartitionResult readPartitionResult(odb::dbChip* top_chip);

int clearPartitionResult(odb::dbChip* top_chip);

}  // namespace chipletpar
