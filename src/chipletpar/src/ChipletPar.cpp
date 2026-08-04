#include "chipletpar/ChipletPar.h"

#include <metis.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ChipletPart3DBloxReader.h"

namespace chipletpar {
namespace {

using WeightedNeighbors = std::vector<std::unordered_map<int, double>>;

idx_t scaleWeight(double weight)
{
  constexpr double kScale = 1000.0;
  const double scaled = std::max(1.0, std::round(weight * kScale));
  return static_cast<idx_t>(
      std::min(scaled, static_cast<double>(std::numeric_limits<idx_t>::max())));
}

std::vector<std::string> collectVertices(
    const chiplet::IRDesign& design,
    std::unordered_map<std::string, int>& vertex_by_name)
{
  std::vector<std::string> names;
  auto add_vertex = [&](const std::string& name) {
    if (name.empty() || vertex_by_name.contains(name)) {
      return;
    }
    vertex_by_name.emplace(name, static_cast<int>(names.size()));
    names.push_back(name);
  };

  for (const chiplet::IRBlock& block : design.blocks) {
    add_vertex(block.name);
  }
  for (const chiplet::IRNet& net : design.nets) {
    for (const std::string& pin : net.pins) {
      add_vertex(pin);
    }
  }
  return names;
}

WeightedNeighbors buildAdjacency(
    const chiplet::IRDesign& design,
    const std::unordered_map<std::string, int>& vertex_by_name)
{
  WeightedNeighbors adjacency(vertex_by_name.size());
  for (const chiplet::IRNet& net : design.nets) {
    std::vector<int> vertices;
    for (const std::string& pin : net.pins) {
      const auto it = vertex_by_name.find(pin);
      if (it != vertex_by_name.end()
          && std::find(vertices.begin(), vertices.end(), it->second)
                 == vertices.end()) {
        vertices.push_back(it->second);
      }
    }

    const double weight = std::max(0.001, static_cast<double>(net.weight));
    for (std::size_t i = 0; i < vertices.size(); ++i) {
      for (std::size_t j = i + 1; j < vertices.size(); ++j) {
        adjacency[vertices[i]][vertices[j]] += weight;
        adjacency[vertices[j]][vertices[i]] += weight;
      }
    }
  }
  return adjacency;
}

std::vector<idx_t> buildVertexWeights(
    const chiplet::IRDesign& design,
    const std::unordered_map<std::string, int>& vertex_by_name)
{
  std::vector<idx_t> weights(vertex_by_name.size(), scaleWeight(1.0));
  for (const chiplet::IRBlock& block : design.blocks) {
    const auto it = vertex_by_name.find(block.name);
    if (it != vertex_by_name.end()) {
      weights[it->second] = scaleWeight(std::max(0.001f, block.area));
    }
  }
  return weights;
}

void buildCsr(const WeightedNeighbors& adjacency,
              std::vector<idx_t>& offsets,
              std::vector<idx_t>& neighbors,
              std::vector<idx_t>& edge_weights)
{
  offsets.resize(adjacency.size() + 1, 0);
  for (std::size_t vertex = 0; vertex < adjacency.size(); ++vertex) {
    std::vector<std::pair<int, double>> ordered(adjacency[vertex].begin(),
                                                adjacency[vertex].end());
    std::sort(ordered.begin(), ordered.end());
    for (const auto& [neighbor, weight] : ordered) {
      neighbors.push_back(static_cast<idx_t>(neighbor));
      edge_weights.push_back(scaleWeight(weight));
    }
    offsets[vertex + 1] = static_cast<idx_t>(neighbors.size());
  }
}

std::string emitYaml(const YAML::Node& node)
{
  YAML::Emitter emitter;
  emitter << node;
  if (!emitter.good()) {
    throw std::runtime_error("Failed to serialize ChipletPart YAML metadata");
  }
  return emitter.c_str();
}

YAML::Node loadDesignExternal(const std::string& design_external_yaml)
{
  const YAML::Node external = YAML::Load(design_external_yaml);
  const YAML::Node vendor = external["chipletpart_vendor_data"];
  if (!external.IsMap() || !vendor || !vendor.IsMap() || !vendor["dataset"]
      || !vendor["dataset"].IsMap()) {
    throw std::runtime_error(
        "ChipletPart metadata has no chipletpart_vendor_data.dataset");
  }
  return external;
}

YAML::Node mergeDatasetWithDesign(const YAML::Node& source_dataset,
                                  const chiplet::IRDesign& design)
{
  YAML::Node dataset = YAML::Clone(source_dataset);

  std::unordered_map<std::string, YAML::Node> source_blocks;
  if (const YAML::Node blocks = source_dataset["blocks"];
      blocks && blocks.IsSequence()) {
    for (const YAML::Node& block : blocks) {
      if (block["name"]) {
        source_blocks.emplace(block["name"].as<std::string>(), block);
      }
    }
  }

  YAML::Node blocks(YAML::NodeType::Sequence);
  for (const chiplet::IRBlock& ir_block : design.blocks) {
    YAML::Node block;
    if (const auto it = source_blocks.find(ir_block.name);
        it != source_blocks.end()) {
      block = YAML::Clone(it->second);
    }
    block["name"] = ir_block.name;
    block["area_mm2"] = ir_block.area;
    block["power_w"] = ir_block.power;
    if (!ir_block.tech.empty()) {
      block["technology"] = ir_block.tech;
    }
    block["is_memory"] = ir_block.is_memory;
    blocks.push_back(block);
  }
  dataset["blocks"] = blocks;

  std::unordered_map<std::string, YAML::Node> source_interconnects;
  if (const YAML::Node interconnects = source_dataset["interconnects"];
      interconnects && interconnects.IsSequence()) {
    for (const YAML::Node& interconnect : interconnects) {
      if (interconnect["name"]) {
        source_interconnects.emplace(interconnect["name"].as<std::string>(),
                                     interconnect);
      }
    }
  }

  YAML::Node interconnects(YAML::NodeType::Sequence);
  for (const chiplet::IRNet& ir_net : design.nets) {
    if (ir_net.pins.size() < 2) {
      continue;
    }
    for (std::size_t sink_index = 1; sink_index < ir_net.pins.size();
         ++sink_index) {
      YAML::Node interconnect;
      if (const auto it = source_interconnects.find(ir_net.name);
          it != source_interconnects.end()) {
        interconnect = YAML::Clone(it->second);
      }
      const std::string name
          = sink_index == 1 ? ir_net.name
                            : ir_net.name + "__" + std::to_string(sink_index);
      interconnect["name"] = name;
      interconnect["source"] = ir_net.pins.front();
      interconnect["sink"] = ir_net.pins[sink_index];
      interconnect["bandwidth_gbps"] = ir_net.weight;
      interconnect["average_bandwidth_utilization"]
          = ir_net.average_bandwidth_utilization;
      if (!ir_net.type.empty()) {
        interconnect["type"] = ir_net.type;
      }
      if (!interconnect["bb_count"]) {
        interconnect["bb_count"] = 1;
      }
      interconnects.push_back(interconnect);
    }
  }
  dataset["interconnects"] = interconnects;
  return dataset;
}

}  // namespace

chiplet::IRDesign read3DBloxDesign(const std::string& dbx_file,
                                   const std::string& dbv_file)
{
  chiplet::ChipletPart3DBloxReader reader;
  return reader.ReadDesign(dbx_file, dbv_file);
}

std::string read3DBloxDesignExternalYaml(const std::string& dbx_file)
{
  const YAML::Node root = YAML::LoadFile(dbx_file);
  const YAML::Node external = root["Design"]["external"];
  if (!external || !external.IsMap() || !external["chipletpart_vendor_data"]
      || !external["chipletpart_vendor_data"].IsMap()) {
    throw std::runtime_error(
        "3DBlox file has no Design.external.chipletpart_vendor_data: "
        + dbx_file);
  }
  return emitYaml(external);
}

void write3DBloxMetadataInput(const std::string& design_external_yaml,
                              const chiplet::IRDesign& design,
                              const std::string& output_dbx)
{
  YAML::Node external = loadDesignExternal(design_external_yaml);
  YAML::Node vendor = external["chipletpart_vendor_data"];
  vendor["dataset"] = mergeDatasetWithDesign(vendor["dataset"], design);
  external["chipletpart_vendor_data"] = vendor;

  YAML::Node root;
  root["Design"]["name"] = design.name.empty() ? "opendb_design" : design.name;
  root["Design"]["external"] = external;
  std::ofstream output(output_dbx);
  if (!output.is_open()) {
    throw std::runtime_error("Cannot write temporary ChipletPart metadata: "
                             + output_dbx);
  }
  output << root;
}

PartitionResult partitionDesign(const chiplet::IRDesign& design,
                                int partition_count,
                                int seed)
{
  std::unordered_map<std::string, int> vertex_by_name;
  PartitionResult result;
  result.vertex_names = collectVertices(design, vertex_by_name);
  result.partition_count = partition_count;

  const int vertex_count = static_cast<int>(result.vertex_names.size());
  if (vertex_count == 0) {
    throw std::invalid_argument("Cannot partition an empty 3DBlox design");
  }
  if (partition_count < 1 || partition_count > vertex_count) {
    throw std::invalid_argument("Partition count must be between 1 and "
                                + std::to_string(vertex_count));
  }
  if (partition_count == 1) {
    result.partition_ids.assign(vertex_count, 0);
    return result;
  }

  const WeightedNeighbors adjacency = buildAdjacency(design, vertex_by_name);
  std::vector<idx_t> offsets;
  std::vector<idx_t> neighbors;
  std::vector<idx_t> edge_weights;
  buildCsr(adjacency, offsets, neighbors, edge_weights);
  std::vector<idx_t> vertex_weights
      = buildVertexWeights(design, vertex_by_name);

  idx_t metis_vertex_count = vertex_count;
  idx_t constraint_count = 1;
  idx_t metis_partition_count = partition_count;
  idx_t edge_cut = 0;
  std::vector<idx_t> partition(vertex_count, 0);
  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_PTYPE] = METIS_PTYPE_KWAY;
  options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
  options[METIS_OPTION_NUMBERING] = 0;
  options[METIS_OPTION_SEED] = seed;

  const int status = METIS_PartGraphKway(
      &metis_vertex_count,
      &constraint_count,
      offsets.data(),
      neighbors.empty() ? nullptr : neighbors.data(),
      vertex_weights.data(),
      nullptr,
      edge_weights.empty() ? nullptr : edge_weights.data(),
      &metis_partition_count,
      nullptr,
      nullptr,
      options,
      &edge_cut,
      partition.data());
  if (status != METIS_OK) {
    throw std::runtime_error("METIS failed with status "
                             + std::to_string(status));
  }

  result.edge_cut = static_cast<int>(edge_cut);
  result.partition_ids.reserve(partition.size());
  for (const idx_t part_id : partition) {
    result.partition_ids.push_back(static_cast<int>(part_id));
  }
  return result;
}

PartitionResult makePartitionResult(const chiplet::IRDesign& design,
                                    std::vector<std::string> vertex_names,
                                    std::vector<int> partition_ids,
                                    float cost)
{
  if (vertex_names.empty() || vertex_names.size() != partition_ids.size()) {
    throw std::invalid_argument(
        "Original ChipletPart result has invalid vertex/partition sizes");
  }

  PartitionResult result;
  result.vertex_names = std::move(vertex_names);
  result.partition_ids = std::move(partition_ids);
  result.cost = cost;
  result.automatic_search = true;

  std::unordered_map<std::string, int> partition_by_name;
  for (std::size_t index = 0; index < result.vertex_names.size(); ++index) {
    const int part_id = result.partition_ids[index];
    if (part_id < 0) {
      throw std::invalid_argument("Partition IDs must be non-negative");
    }
    result.partition_count = std::max(result.partition_count, part_id + 1);
    partition_by_name[result.vertex_names[index]] = part_id;
  }

  std::int64_t edge_cut = 0;
  for (const chiplet::IRNet& net : design.nets) {
    const idx_t weight
        = scaleWeight(std::max(0.001, static_cast<double>(net.weight)));
    for (std::size_t i = 0; i < net.pins.size(); ++i) {
      const auto first = partition_by_name.find(net.pins[i]);
      if (first == partition_by_name.end()) {
        continue;
      }
      for (std::size_t j = i + 1; j < net.pins.size(); ++j) {
        const auto second = partition_by_name.find(net.pins[j]);
        if (second != partition_by_name.end()
            && first->second != second->second) {
          edge_cut += weight;
        }
      }
    }
  }
  result.edge_cut = static_cast<int>(
      std::min<std::int64_t>(edge_cut, std::numeric_limits<int>::max()));
  return result;
}

void annotate3DBlox(const std::string& metadata_source_dbx,
                    const std::string& output_dbx,
                    const PartitionResult& result)
{
  annotate3DBloxFromMetadata(
      read3DBloxDesignExternalYaml(metadata_source_dbx), output_dbx, result);
}

void annotate3DBloxFromMetadata(const std::string& design_external_yaml,
                                const std::string& output_dbx,
                                const PartitionResult& result)
{
  if (result.vertex_names.size() != result.partition_ids.size()) {
    throw std::invalid_argument(
        "Partition result has different name and partition vector sizes");
  }

  YAML::Node output_root = YAML::LoadFile(output_dbx);
  const YAML::Node source_external = loadDesignExternal(design_external_yaml);
  const YAML::Node source_vendor = source_external["chipletpart_vendor_data"];

  YAML::Node vendor = YAML::Clone(source_vendor);
  std::unordered_map<std::string, int> partition_by_name;
  for (std::size_t index = 0; index < result.vertex_names.size(); ++index) {
    partition_by_name[result.vertex_names[index]] = result.partition_ids[index];
  }

  YAML::Node blocks = vendor["dataset"]["blocks"];
  if (blocks && blocks.IsSequence()) {
    for (YAML::Node block : blocks) {
      const YAML::Node name_node = block["name"];
      if (!name_node) {
        continue;
      }
      const auto it = partition_by_name.find(name_node.as<std::string>());
      if (it != partition_by_name.end()) {
        block["partition_id"] = it->second;
      }
    }
  }
  vendor["partition_result"]["partition_count"] = result.partition_count;
  vendor["partition_result"]["edge_cut"] = result.edge_cut;
  if (result.automatic_search) {
    vendor["partition_result"]["cost"] = result.cost;
    vendor["partition_result"]["algorithm"] = "ChipletPart_automatic_search";
  } else {
    vendor["partition_result"]["algorithm"] = "METIS_PartGraphKway";
  }
  YAML::Node output_external = output_root["Design"]["external"];
  for (const auto& entry : source_external) {
    const std::string key = entry.first.as<std::string>();
    if (key != "verilog_file" && key != "chipletpart_vendor_data") {
      output_external[key] = YAML::Clone(entry.second);
    }
  }
  output_external["chipletpart_vendor_data"] = vendor;
  output_root["Design"]["external"] = output_external;

  std::ofstream output(output_dbx);
  if (!output.is_open()) {
    throw std::runtime_error("Cannot write 3DBlox output: " + output_dbx);
  }
  output << output_root;
}

}  // namespace chipletpar
