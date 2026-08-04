#include <tcl.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChipletParOpenDb.h"
#include "ChipletPart.h"
#include "ChipletPart3DBloxReader.h"
#include "chipletpar/ChipletPar.h"
#include "chipletpar/MakeChipletPar.h"
#include "db_sta/dbSta.hh"
#include "odb/3dblox.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"

namespace chipletpar {
namespace {

struct CommandOptions
{
  std::string input;
  std::string output;
  std::string config;
  int partition_count = 0;
  bool partition_count_specified = false;
  int max_partitions = 8;
  bool max_partitions_specified = false;
  int seed = 42;
  double reach = 0.5;
  double separation = 0.25;
  std::string technology = "7nm";
};

class TemporaryDirectory
{
 public:
  TemporaryDirectory()
  {
    const auto nonce
        = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path()
            / ("chipletpar-original-" + std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class Prepared3DBloxInput
{
 public:
  explicit Prepared3DBloxInput(const std::string& input_dbx)
      : input_path_(input_dbx)
  {
    prepareIfTechnologyIsMissing();
  }

  ~Prepared3DBloxInput()
  {
    if (!temporary_directory_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(temporary_directory_, error);
    }
  }

  const std::string& path() const { return prepared_path_; }

 private:
  static bool needsSyntheticTechnology(const YAML::Node& dbv_root)
  {
    const YAML::Node definitions = dbv_root["ChipletDef"];
    if (!definitions || !definitions.IsMap()) {
      return false;
    }
    for (const auto& definition : definitions) {
      const YAML::Node chiplet = definition.second;
      if (chiplet["type"] && chiplet["type"].as<std::string>() == "die"
          && !chiplet["external"]["APR_tech_file"]) {
        return true;
      }
    }
    return false;
  }

  static void writeSyntheticTechnology(
      const std::filesystem::path& technology_path)
  {
    std::ofstream technology(technology_path);
    if (!technology.is_open()) {
      throw std::runtime_error("Cannot create temporary technology LEF");
    }
    technology << "VERSION 5.8 ;\n"
               << "BUSBITCHARS \"[]\" ;\n"
               << "DIVIDERCHAR \"/\" ;\n"
               << "UNITS\n"
               << "  DATABASE MICRONS 1000 ;\n"
               << "END UNITS\n"
               << "LAYER chipletpar_m1\n"
               << "  TYPE ROUTING ;\n"
               << "  DIRECTION HORIZONTAL ;\n"
               << "  PITCH 1.0 ;\n"
               << "  WIDTH 0.5 ;\n"
               << "END chipletpar_m1\n"
               << "END LIBRARY\n";
  }

  void prepareIfTechnologyIsMissing()
  {
    prepared_path_ = input_path_;
    YAML::Node dbx_root = YAML::LoadFile(input_path_);
    YAML::Node includes = dbx_root["Header"]["include"];
    if (!includes || !includes.IsSequence()) {
      return;
    }

    const std::filesystem::path input_directory
        = std::filesystem::absolute(input_path_).parent_path();
    std::vector<std::pair<std::size_t, YAML::Node>> dbv_roots;
    for (std::size_t index = 0; index < includes.size(); ++index) {
      const std::filesystem::path include_path
          = input_directory / includes[index].as<std::string>();
      if (include_path.extension() != ".3dbv") {
        continue;
      }
      YAML::Node dbv_root = YAML::LoadFile(include_path.string());
      if (needsSyntheticTechnology(dbv_root)) {
        dbv_roots.emplace_back(index, dbv_root);
      }
    }
    if (dbv_roots.empty()) {
      return;
    }

    const auto nonce
        = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary_directory_ = std::filesystem::temp_directory_path()
                           / ("chipletpar-3dblox-" + std::to_string(nonce));
    std::filesystem::create_directories(temporary_directory_);
    const std::filesystem::path technology_path
        = temporary_directory_ / "chipletpar_synthetic_tech.lef";
    writeSyntheticTechnology(technology_path);

    for (std::size_t output_index = 0; output_index < dbv_roots.size();
         ++output_index) {
      const std::size_t include_index = dbv_roots[output_index].first;
      YAML::Node& dbv_root = dbv_roots[output_index].second;
      YAML::Node definitions = dbv_root["ChipletDef"];
      for (auto definition : definitions) {
        YAML::Node chiplet = definition.second;
        if (!chiplet["type"] || chiplet["type"].as<std::string>() != "die"
            || chiplet["external"]["APR_tech_file"]) {
          continue;
        }
        YAML::Node technology_files;
        technology_files.push_back(technology_path.string());
        chiplet["external"]["APR_tech_file"] = technology_files;
      }

      const std::filesystem::path prepared_dbv
          = temporary_directory_
            / ("included_" + std::to_string(output_index) + ".3dbv");
      std::ofstream dbv_output(prepared_dbv);
      if (!dbv_output.is_open()) {
        throw std::runtime_error("Cannot create temporary 3DBV file");
      }
      dbv_output << dbv_root;
      includes[include_index] = prepared_dbv.string();
    }

    const std::filesystem::path prepared_dbx
        = temporary_directory_ / "input.3dbx";
    std::ofstream dbx_output(prepared_dbx);
    if (!dbx_output.is_open()) {
      throw std::runtime_error("Cannot create temporary 3DBX file");
    }
    dbx_output << dbx_root;
    prepared_path_ = prepared_dbx.string();
  }

  std::string input_path_;
  std::string prepared_path_;
  std::filesystem::path temporary_directory_;
};

std::filesystem::path resolveInput3DBloxPath(const std::string& input)
{
  const std::filesystem::path input_path
      = std::filesystem::absolute(input).lexically_normal();
  if (!std::filesystem::exists(input_path)) {
    throw std::invalid_argument("Input path does not exist: "
                                + input_path.string());
  }

  if (std::filesystem::is_directory(input_path)) {
    std::vector<std::filesystem::path> dbx_files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(input_path)) {
      if (entry.is_regular_file() && entry.path().extension() == ".3dbx") {
        dbx_files.push_back(entry.path());
      }
    }
    std::sort(dbx_files.begin(), dbx_files.end());
    if (dbx_files.empty()) {
      throw std::invalid_argument("Input directory contains no .3dbx file: "
                                  + input_path.string());
    }
    if (dbx_files.size() != 1) {
      throw std::invalid_argument(
          "Input directory must contain exactly one .3dbx file: "
          + input_path.string());
    }
    return dbx_files.front();
  }

  if (!std::filesystem::is_regular_file(input_path)
      || input_path.extension() != ".3dbx") {
    throw std::invalid_argument("Input must be a directory or a .3dbx file: "
                                + input_path.string());
  }
  return input_path;
}

std::filesystem::path resolveOutput3DBloxPath(
    const std::string& output,
    const std::filesystem::path& default_filename)
{
  const std::filesystem::path output_path
      = std::filesystem::absolute(output).lexically_normal();
  if (std::filesystem::exists(output_path)) {
    if (std::filesystem::is_directory(output_path)) {
      return output_path / default_filename.filename();
    }
    if (!std::filesystem::is_regular_file(output_path)
        || output_path.extension() != ".3dbx") {
      throw std::invalid_argument("Output must be a directory or a .3dbx file: "
                                  + output_path.string());
    }
    return output_path;
  }

  if (output_path.extension() == ".3dbx") {
    const std::filesystem::path parent = output_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    return output_path;
  }

  std::filesystem::create_directories(output_path);
  return output_path / default_filename.filename();
}

CommandOptions parsePartitionOptions(Tcl_Interp* interp,
                                     int object_count,
                                     Tcl_Obj* const objects[],
                                     bool require_file_io)
{
  CommandOptions options;
  for (int index = 1; index < object_count; index += 2) {
    if (index + 1 >= object_count) {
      throw std::invalid_argument(std::string("Missing value for ")
                                  + Tcl_GetString(objects[index]));
    }
    const std::string option = Tcl_GetString(objects[index]);
    if (option == "-input") {
      options.input = Tcl_GetString(objects[index + 1]);
    } else if (option == "-output") {
      options.output = Tcl_GetString(objects[index + 1]);
    } else if (option == "-config") {
      options.config = Tcl_GetString(objects[index + 1]);
    } else if (option == "-num_parts") {
      options.partition_count_specified = true;
      if (Tcl_GetIntFromObj(
              interp, objects[index + 1], &options.partition_count)
          != TCL_OK) {
        throw std::invalid_argument("-num_parts must be an integer");
      }
    } else if (option == "-max_parts") {
      options.max_partitions_specified = true;
      if (Tcl_GetIntFromObj(interp, objects[index + 1], &options.max_partitions)
          != TCL_OK) {
        throw std::invalid_argument("-max_parts must be an integer");
      }
    } else if (option == "-seed") {
      if (Tcl_GetIntFromObj(interp, objects[index + 1], &options.seed)
          != TCL_OK) {
        throw std::invalid_argument("-seed must be an integer");
      }
    } else if (option == "-reach") {
      if (Tcl_GetDoubleFromObj(interp, objects[index + 1], &options.reach)
          != TCL_OK) {
        throw std::invalid_argument("-reach must be a number");
      }
    } else if (option == "-separation") {
      if (Tcl_GetDoubleFromObj(interp, objects[index + 1], &options.separation)
          != TCL_OK) {
        throw std::invalid_argument("-separation must be a number");
      }
    } else if (option == "-technology") {
      options.technology = Tcl_GetString(objects[index + 1]);
    } else {
      throw std::invalid_argument("Unknown option: " + option);
    }
  }

  if (require_file_io && (options.input.empty() || options.output.empty())) {
    throw std::invalid_argument(
        "Usage: chipletpar_partition_3dblox -input input_dir "
        "-output output_dir ?-num_parts count? ?-max_parts count? "
        "?-seed value? ?-reach value? ?-separation value? "
        "?-technology node?");
  }
  if ((options.partition_count_specified && options.partition_count < 1)
      || options.max_partitions < 1) {
    throw std::invalid_argument(
        "-num_parts must be positive when specified and -max_parts must be "
        "positive");
  }
  if (options.partition_count_specified && options.max_partitions_specified) {
    throw std::invalid_argument(
        "-num_parts and -max_parts cannot be used together");
  }
  if (options.reach < 0.0 || options.separation < 0.0
      || options.technology.empty()) {
    throw std::invalid_argument(
        "-reach and -separation must be non-negative and -technology cannot "
        "be empty");
  }
  if (require_file_io) {
    const std::filesystem::path input_dbx
        = resolveInput3DBloxPath(options.input);
    const std::filesystem::path output_dbx
        = resolveOutput3DBloxPath(options.output, input_dbx.filename());
    if (input_dbx == output_dbx) {
      throw std::invalid_argument("Input and output 3DBlox paths must differ");
    }
    options.input = input_dbx.string();
    options.output = output_dbx.string();
  } else if (!options.input.empty() || !options.output.empty()) {
    throw std::invalid_argument(
        "chipletpar_partition operates on the current OpenDB; use -config "
        "for metadata and separate read_db/read_3dbx/write_db commands for "
        "file I/O");
  }
  if (!options.config.empty()) {
    options.config = resolveInput3DBloxPath(options.config).string();
  }
  return options;
}

PartitionResult runOriginalPartitionSearch(const CommandOptions& options,
                                           const chiplet::IRDesign& design,
                                           const std::string& metadata_yaml)
{
  if (design.blocks.empty()) {
    throw std::invalid_argument("Cannot partition an empty 3DBlox design");
  }

  TemporaryDirectory temporary_directory;
  const std::filesystem::path metadata_dbx
      = temporary_directory.path() / "opendb_metadata.3dbx";
  write3DBloxMetadataInput(metadata_yaml, design, metadata_dbx.string());
  chiplet::ChipletPart3DBloxReader reader;
  const chiplet::ChipletPartLegacyInputFiles files
      = reader.MaterializeLegacyInputs(metadata_dbx.string(),
                                       temporary_directory.path());

  chiplet::ChipletPart partitioner(options.seed);
  partitioner.SetWriteLegacyArtifacts(false);
  partitioner.SetMaxPartitions(
      std::min(options.max_partitions, static_cast<int>(design.blocks.size())));
  partitioner.SetIRInput(design);
  partitioner.Partition(files.io_file,
                        files.layer_file,
                        files.wafer_process_file,
                        files.assembly_process_file,
                        files.test_file,
                        files.netlist_file,
                        files.blocks_file,
                        static_cast<float>(options.reach),
                        static_cast<float>(options.separation),
                        options.technology);

  return makePartitionResult(design,
                             partitioner.GetVertexNames(),
                             partitioner.GetSolution(),
                             partitioner.GetBestCost());
}

PartitionResult runPartition(const CommandOptions& options,
                             const chiplet::IRDesign& design,
                             const std::string& metadata_yaml)
{
  if (options.partition_count_specified) {
    return partitionDesign(design, options.partition_count, options.seed);
  }
  if (metadata_yaml.empty()) {
    throw std::runtime_error(
        "Original automatic search requires ChipletPart cost metadata. Pass "
        "-config source.3dbx once, or read an .odb that already contains the "
        "persisted metadata. Fixed -num_parts mode can run without it.");
  }
  return runOriginalPartitionSearch(options, design, metadata_yaml);
}

void writeOpenDb3DBlox(odb::ThreeDBlox& three_dblox,
                       odb::dbChip* top_chip,
                       const std::filesystem::path& output_path,
                       const std::string& metadata_yaml,
                       const PartitionResult& result)
{
  const std::filesystem::path output_dir
      = output_path.has_parent_path() ? output_path.parent_path() : ".";
  if (!std::filesystem::exists(output_dir)) {
    throw std::runtime_error("Output directory does not exist: "
                             + output_dir.string());
  }

  const std::string design_name = top_chip->getName();
  const std::filesystem::path dbv_path = output_dir / (design_name + ".3dbv");
  const std::filesystem::path verilog_path = output_dir / (design_name + ".v");
  three_dblox.writeDbv(dbv_path.string(), top_chip);
  three_dblox.writeDbx(output_path.string(), top_chip);
  three_dblox.writeVerilog(verilog_path.string(), top_chip);
  annotate3DBloxFromMetadata(metadata_yaml, output_path.string(), result);
}

Tcl_Obj* makePartitionResponse(Tcl_Interp* interp,
                               const PartitionResult& result,
                               const OpenDbWriteStats& stats,
                               const std::string& output = std::string())
{
  Tcl_Obj* response = Tcl_NewDictObj();
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("vertices", -1),
                 Tcl_NewIntObj(result.partition_ids.size()));
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("partitions", -1),
                 Tcl_NewIntObj(result.partition_count));
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("edge_cut", -1),
                 Tcl_NewIntObj(result.edge_cut));
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("cost", -1),
                 Tcl_NewDoubleObj(result.cost));
  Tcl_DictObjPut(
      interp,
      response,
      Tcl_NewStringObj("mode", -1),
      Tcl_NewStringObj(
          result.automatic_search ? "original_automatic_search" : "fixed_metis",
          -1));
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("annotated_odb_instances", -1),
                 Tcl_NewIntObj(stats.instances_written));
  Tcl_DictObjPut(interp,
                 response,
                 Tcl_NewStringObj("missing_odb_instances", -1),
                 Tcl_NewIntObj(stats.missing_instances));
  if (!output.empty()) {
    Tcl_DictObjPut(interp,
                   response,
                   Tcl_NewStringObj("output", -1),
                   Tcl_NewStringObj(output.c_str(), -1));
  }
  return response;
}

odb::dbChip* requireTopChip()
{
  odb::dbChip* top_chip = ord::OpenRoad::openRoad()->getDb()->getChip();
  if (top_chip == nullptr) {
    throw std::runtime_error(
        "OpenDB has no design; run read_3dbx or read_db first");
  }
  return top_chip;
}

int partitionCurrentDbCommand(ClientData,
                              Tcl_Interp* interp,
                              int object_count,
                              Tcl_Obj* const objects[])
{
  try {
    const CommandOptions options
        = parsePartitionOptions(interp, object_count, objects, false);
    odb::dbChip* top_chip = requireTopChip();
    if (!options.config.empty()) {
      storeDesignExternalMetadata(top_chip,
                                  read3DBloxDesignExternalYaml(options.config),
                                  options.config);
    }
    const std::string metadata_yaml = getDesignExternalMetadata(top_chip);
    const chiplet::IRDesign design = readOpenDbDesign(top_chip, metadata_yaml);
    const PartitionResult result = runPartition(options, design, metadata_yaml);
    const OpenDbWriteStats stats = writePartitionResult(top_chip, result);
    Tcl_SetObjResult(interp, makePartitionResponse(interp, result, stats));
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

int partition3DBloxCommand(ClientData,
                           Tcl_Interp* interp,
                           int object_count,
                           Tcl_Obj* const objects[])
{
  try {
    const CommandOptions options
        = parsePartitionOptions(interp, object_count, objects, true);
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    odb::dbDatabase* database = openroad->getDb();
    if (database->getChip() != nullptr) {
      throw std::runtime_error(
          "OpenDB already contains a design; run this command in a fresh "
          "OpenROAD process");
    }

    Prepared3DBloxInput prepared_input(options.input);
    odb::ThreeDBlox three_dblox(
        openroad->getLogger(), database, openroad->getSta());
    three_dblox.readDbx(prepared_input.path());

    odb::dbChip* top_chip = database->getChip();
    const std::string metadata_yaml
        = read3DBloxDesignExternalYaml(options.input);
    storeDesignExternalMetadata(top_chip, metadata_yaml, options.input);
    const chiplet::IRDesign design = readOpenDbDesign(top_chip, metadata_yaml);
    const PartitionResult result = runPartition(options, design, metadata_yaml);
    const OpenDbWriteStats stats = writePartitionResult(top_chip, result);
    writeOpenDb3DBlox(
        three_dblox, top_chip, options.output, metadata_yaml, result);

    Tcl_SetObjResult(
        interp, makePartitionResponse(interp, result, stats, options.output));
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

int write3DBloxCommand(ClientData,
                       Tcl_Interp* interp,
                       int object_count,
                       Tcl_Obj* const objects[])
{
  try {
    if (object_count != 3
        || std::string(Tcl_GetString(objects[1])) != "-output") {
      throw std::invalid_argument(
          "Usage: chipletpar_write_3dblox -output output_dir_or_file");
    }
    odb::dbChip* top_chip = requireTopChip();
    const std::string metadata_yaml = getDesignExternalMetadata(top_chip);
    if (metadata_yaml.empty()) {
      throw std::runtime_error(
          "OpenDB contains no persisted ChipletPart metadata");
    }
    const PartitionResult result = readPartitionResult(top_chip);
    const std::filesystem::path output_path = resolveOutput3DBloxPath(
        Tcl_GetString(objects[2]), std::string(top_chip->getName()) + ".3dbx");
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    odb::ThreeDBlox three_dblox(
        openroad->getLogger(), openroad->getDb(), openroad->getSta());
    writeOpenDb3DBlox(
        three_dblox, top_chip, output_path, metadata_yaml, result);
    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(output_path.string().c_str(), -1));
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

int reportCommand(ClientData,
                  Tcl_Interp* interp,
                  int object_count,
                  Tcl_Obj* const[])
{
  try {
    if (object_count != 1) {
      throw std::invalid_argument("Usage: chipletpar_report");
    }
    odb::dbChip* top_chip = requireTopChip();
    const PartitionResult result = readPartitionResult(top_chip);
    Tcl_Obj* response = makePartitionResponse(
        interp,
        result,
        OpenDbWriteStats{static_cast<int>(result.partition_ids.size()), 0});
    Tcl_Obj* assignments = Tcl_NewDictObj();
    for (std::size_t index = 0; index < result.vertex_names.size(); ++index) {
      Tcl_DictObjPut(interp,
                     assignments,
                     Tcl_NewStringObj(result.vertex_names[index].c_str(), -1),
                     Tcl_NewIntObj(result.partition_ids[index]));
    }
    Tcl_DictObjPut(
        interp, response, Tcl_NewStringObj("assignments", -1), assignments);
    Tcl_DictObjPut(
        interp,
        response,
        Tcl_NewStringObj("metadata_persisted", -1),
        Tcl_NewBooleanObj(!getDesignExternalMetadata(top_chip).empty()));
    Tcl_SetObjResult(interp, response);
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

int clearCommand(ClientData,
                 Tcl_Interp* interp,
                 int object_count,
                 Tcl_Obj* const[])
{
  try {
    if (object_count != 1) {
      throw std::invalid_argument("Usage: chipletpar_clear");
    }
    Tcl_SetObjResult(interp,
                     Tcl_NewIntObj(clearPartitionResult(requireTopChip())));
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

}  // namespace

void initChipletPar(Tcl_Interp* interp)
{
  Tcl_Eval(interp, "namespace eval chipletpar {}");
  Tcl_CreateObjCommand(interp,
                       "chipletpar::partition",
                       partitionCurrentDbCommand,
                       nullptr,
                       nullptr);
  Tcl_CreateObjCommand(interp,
                       "chipletpar_partition",
                       partitionCurrentDbCommand,
                       nullptr,
                       nullptr);
  Tcl_CreateObjCommand(interp,
                       "chipletpar::partition_3dblox",
                       partition3DBloxCommand,
                       nullptr,
                       nullptr);
  Tcl_CreateObjCommand(interp,
                       "chipletpar_partition_3dblox",
                       partition3DBloxCommand,
                       nullptr,
                       nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar::write_3dblox", write3DBloxCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar_write_3dblox", write3DBloxCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar::report", reportCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar_report", reportCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar::clear", clearCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(
      interp, "chipletpar_clear", clearCommand, nullptr, nullptr);
}

}  // namespace chipletpar
