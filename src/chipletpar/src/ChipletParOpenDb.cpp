#include "ChipletParOpenDb.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "odb/db.h"

namespace chipletpar {
namespace {

float asFloat(const YAML::Node& node, float fallback)
{
  return node && !node.IsNull() ? node.as<float>() : fallback;
}

bool asBool(const YAML::Node& node, bool fallback)
{
  return node && !node.IsNull() ? node.as<bool>() : fallback;
}

std::string asString(const YAML::Node& node,
                     const std::string& fallback = std::string())
{
  return node && !node.IsNull() ? node.as<std::string>() : fallback;
}

void setIntProperty(odb::dbObject* object, const char* name, int value)
{
  if (odb::dbIntProperty* property = odb::dbIntProperty::find(object, name)) {
    property->setValue(value);
  } else {
    odb::dbIntProperty::create(object, name, value);
  }
}

void setDoubleProperty(odb::dbObject* object, const char* name, double value)
{
  if (odb::dbDoubleProperty* property
      = odb::dbDoubleProperty::find(object, name)) {
    property->setValue(value);
  } else {
    odb::dbDoubleProperty::create(object, name, value);
  }
}

void setStringProperty(odb::dbObject* object,
                       const char* name,
                       const std::string& value)
{
  if (odb::dbStringProperty* property
      = odb::dbStringProperty::find(object, name)) {
    property->setValue(value.c_str());
  } else {
    odb::dbStringProperty::create(object, name, value.c_str());
  }
}

void destroyProperty(odb::dbObject* object, const char* name)
{
  if (odb::dbProperty* property = odb::dbProperty::find(object, name)) {
    odb::dbProperty::destroy(property);
  }
}

YAML::Node loadDataset(const std::string& design_external_yaml)
{
  if (design_external_yaml.empty()) {
    return YAML::Node();
  }
  const YAML::Node external = YAML::Load(design_external_yaml);
  const YAML::Node dataset = external["chipletpart_vendor_data"]["dataset"];
  if (!dataset || !dataset.IsMap()) {
    throw std::runtime_error(
        "Stored ChipletPart metadata has no vendor dataset");
  }
  return dataset;
}

std::unordered_map<std::string, YAML::Node> nodesByName(
    const YAML::Node& sequence)
{
  std::unordered_map<std::string, YAML::Node> result;
  if (!sequence || !sequence.IsSequence()) {
    return result;
  }
  for (const YAML::Node& node : sequence) {
    const std::string name = asString(node["name"]);
    if (!name.empty()) {
      result.emplace(name, node);
    }
  }
  return result;
}

std::unordered_map<std::string, float> reachesByType(const YAML::Node& dataset)
{
  std::unordered_map<std::string, float> result;
  const YAML::Node library = dataset["io_library"];
  if (!library || !library.IsSequence()) {
    return result;
  }
  for (const YAML::Node& io : library) {
    const std::string type = asString(io["type"]);
    if (!type.empty()) {
      result[type] = asFloat(io["reach"], -1.0f);
    }
  }
  return result;
}

chiplet::IRNet makeMetadataNet(
    const YAML::Node& node,
    const std::unordered_map<std::string, float>& reach_by_type)
{
  chiplet::IRNet net;
  net.name = asString(node["name"]);
  net.type = asString(node["type"]);
  net.weight = asFloat(node["bandwidth_gbps"], 1.0f);
  net.average_bandwidth_utilization
      = asFloat(node["average_bandwidth_utilization"], 0.5f);
  if (const auto it = reach_by_type.find(net.type); it != reach_by_type.end()) {
    net.reach = it->second;
  }
  const std::string source = asString(node["source"]);
  const std::string sink = asString(node["sink"]);
  if (!source.empty()) {
    net.pins.push_back(source);
  }
  if (!sink.empty() && sink != source) {
    net.pins.push_back(sink);
  }
  return net;
}

}  // namespace

chiplet::IRDesign readOpenDbDesign(odb::dbChip* top_chip,
                                   const std::string& design_external_yaml)
{
  if (top_chip == nullptr) {
    throw std::invalid_argument("OpenDB has no top dbChip");
  }

  const YAML::Node dataset = loadDataset(design_external_yaml);
  const auto metadata_blocks = nodesByName(dataset["blocks"]);
  const auto metadata_nets = nodesByName(dataset["interconnects"]);
  const auto reach_by_type = reachesByType(dataset);

  chiplet::IRDesign design;
  design.name = top_chip->getName();
  const double dbu_per_micron = top_chip->getDb()->getDbuPerMicron();
  std::unordered_set<std::string> instance_names;
  std::unordered_map<std::string, chiplet::IRBlock> blocks_by_name;
  std::vector<std::string> database_block_order;
  for (odb::dbChipInst* instance : top_chip->getChipInsts()) {
    chiplet::IRBlock block;
    block.name = instance->getName();
    instance_names.insert(block.name);

    odb::dbChip* master = instance->getMasterChip();
    if (master != nullptr && dbu_per_micron > 0.0) {
      const double width_microns = master->getWidth() / dbu_per_micron;
      const double height_microns = master->getHeight() / dbu_per_micron;
      block.area = static_cast<float>(width_microns * height_microns / 1.0e6);
      if (master->getTech() != nullptr) {
        block.tech = master->getTech()->getName();
      }
    }
    const odb::Point3D location = instance->getLoc();
    if (dbu_per_micron > 0.0) {
      block.location_x = location.x() / dbu_per_micron;
      block.location_y = location.y() / dbu_per_micron;
    }

    if (const auto it = metadata_blocks.find(block.name);
        it != metadata_blocks.end()) {
      const YAML::Node& metadata = it->second;
      block.area = asFloat(metadata["area_mm2"], block.area);
      block.power = asFloat(metadata["power_w"], block.power);
      block.tech = asString(metadata["technology"], block.tech);
      block.is_memory = asBool(metadata["is_memory"], block.is_memory);
    }
    if (block.area <= 0.0f) {
      block.area = 1.0f;
    }
    database_block_order.push_back(block.name);
    blocks_by_name.emplace(block.name, std::move(block));
  }

  if (blocks_by_name.empty()) {
    throw std::runtime_error(
        "OpenDB top chip contains no dbChipInst objects; ordinary dbInst/dbNet "
        "designs are not supported by this command yet");
  }

  std::unordered_set<std::string> ordered_names;
  const YAML::Node metadata_interconnects = dataset["interconnects"];
  if (metadata_interconnects && metadata_interconnects.IsSequence()) {
    for (const YAML::Node& interconnect : metadata_interconnects) {
      for (const char* endpoint_key : {"source", "sink"}) {
        const std::string endpoint = asString(interconnect[endpoint_key]);
        if (blocks_by_name.contains(endpoint)
            && ordered_names.insert(endpoint).second) {
          design.blocks.push_back(blocks_by_name.at(endpoint));
        }
      }
    }
  }
  for (const std::string& name : database_block_order) {
    if (ordered_names.insert(name).second) {
      design.blocks.push_back(blocks_by_name.at(name));
    }
  }

  for (odb::dbChipNet* chip_net : top_chip->getChipNets()) {
    chiplet::IRNet net;
    net.name = chip_net->getName();
    if (const auto it = metadata_nets.find(net.name);
        it != metadata_nets.end()) {
      net = makeMetadataNet(it->second, reach_by_type);
    }

    std::unordered_set<std::string> seen_endpoints;
    net.pins.clear();
    for (uint32_t index = 0; index < chip_net->getNumBumpInsts(); ++index) {
      std::vector<odb::dbChipInst*> path;
      chip_net->getBumpInst(index, path);
      if (path.empty() || path.front() == nullptr) {
        continue;
      }
      const std::string endpoint = path.front()->getName();
      if (instance_names.contains(endpoint)
          && seen_endpoints.insert(endpoint).second) {
        net.pins.push_back(endpoint);
      }
    }
    if (net.pins.size() >= 2) {
      design.nets.push_back(std::move(net));
    }
  }

  if (design.nets.empty()) {
    const YAML::Node interconnects = dataset["interconnects"];
    if (interconnects && interconnects.IsSequence()) {
      for (const YAML::Node& node : interconnects) {
        chiplet::IRNet net = makeMetadataNet(node, reach_by_type);
        if (net.pins.size() >= 2 && instance_names.contains(net.pins[0])
            && instance_names.contains(net.pins[1])) {
          design.nets.push_back(std::move(net));
        }
      }
    }
  }

  if (design.nets.empty()) {
    throw std::runtime_error(
        "OpenDB contains no usable dbChipNet connectivity and the stored "
        "ChipletPart metadata contains no matching interconnects");
  }
  return design;
}

void storeDesignExternalMetadata(odb::dbChip* top_chip,
                                 const std::string& design_external_yaml,
                                 const std::string& source)
{
  if (top_chip == nullptr) {
    throw std::invalid_argument("Cannot store metadata without a top dbChip");
  }
  loadDataset(design_external_yaml);
  setStringProperty(top_chip, kDesignExternalProperty, design_external_yaml);
  if (!source.empty()) {
    setStringProperty(top_chip, kMetadataSourceProperty, source);
  }
}

std::string getDesignExternalMetadata(odb::dbChip* top_chip)
{
  if (top_chip == nullptr) {
    return {};
  }
  if (odb::dbStringProperty* property
      = odb::dbStringProperty::find(top_chip, kDesignExternalProperty)) {
    return property->getValue();
  }
  return {};
}

OpenDbWriteStats writePartitionResult(odb::dbChip* top_chip,
                                      const PartitionResult& result)
{
  if (top_chip == nullptr) {
    throw std::invalid_argument("Cannot write a result without a top dbChip");
  }
  if (result.vertex_names.size() != result.partition_ids.size()) {
    throw std::invalid_argument(
        "Partition result has different vertex and partition vector sizes");
  }

  clearPartitionResult(top_chip);
  OpenDbWriteStats stats;
  for (std::size_t index = 0; index < result.vertex_names.size(); ++index) {
    odb::dbChipInst* instance
        = top_chip->findChipInst(result.vertex_names[index]);
    if (instance == nullptr) {
      ++stats.missing_instances;
      continue;
    }
    setIntProperty(instance, kPartitionIdProperty, result.partition_ids[index]);
    ++stats.instances_written;
  }
  setIntProperty(top_chip, kPartitionCountProperty, result.partition_count);
  setIntProperty(top_chip, kPartitionEdgeCutProperty, result.edge_cut);
  setDoubleProperty(top_chip, kPartitionCostProperty, result.cost);
  setStringProperty(
      top_chip,
      kPartitionModeProperty,
      result.automatic_search ? "original_automatic_search" : "fixed_metis");
  return stats;
}

PartitionResult readPartitionResult(odb::dbChip* top_chip)
{
  if (top_chip == nullptr) {
    throw std::invalid_argument("OpenDB has no top dbChip");
  }
  odb::dbIntProperty* partition_count
      = odb::dbIntProperty::find(top_chip, kPartitionCountProperty);
  if (partition_count == nullptr) {
    throw std::runtime_error("OpenDB contains no ChipletPart result");
  }

  PartitionResult result;
  result.partition_count = partition_count->getValue();
  if (odb::dbIntProperty* edge_cut
      = odb::dbIntProperty::find(top_chip, kPartitionEdgeCutProperty)) {
    result.edge_cut = edge_cut->getValue();
  }
  if (odb::dbDoubleProperty* cost
      = odb::dbDoubleProperty::find(top_chip, kPartitionCostProperty)) {
    result.cost = cost->getValue();
  }
  if (odb::dbStringProperty* mode
      = odb::dbStringProperty::find(top_chip, kPartitionModeProperty)) {
    result.automatic_search = mode->getValue() == "original_automatic_search";
  }
  for (odb::dbChipInst* instance : top_chip->getChipInsts()) {
    if (odb::dbIntProperty* partition
        = odb::dbIntProperty::find(instance, kPartitionIdProperty)) {
      result.vertex_names.push_back(instance->getName());
      result.partition_ids.push_back(partition->getValue());
    }
  }
  return result;
}

int clearPartitionResult(odb::dbChip* top_chip)
{
  if (top_chip == nullptr) {
    throw std::invalid_argument("OpenDB has no top dbChip");
  }
  std::vector<odb::dbProperty*> instance_properties;
  for (odb::dbChipInst* instance : top_chip->getChipInsts()) {
    if (odb::dbProperty* property
        = odb::dbProperty::find(instance, kPartitionIdProperty)) {
      instance_properties.push_back(property);
    }
  }
  for (odb::dbProperty* property : instance_properties) {
    odb::dbProperty::destroy(property);
  }
  destroyProperty(top_chip, kPartitionCountProperty);
  destroyProperty(top_chip, kPartitionEdgeCutProperty);
  destroyProperty(top_chip, kPartitionCostProperty);
  destroyProperty(top_chip, kPartitionModeProperty);
  return static_cast<int>(instance_properties.size());
}

}  // namespace chipletpar
