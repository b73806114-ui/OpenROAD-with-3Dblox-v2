#include <tcl.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "esc/MakeEsc.h"
#include "odb/db.h"
#include "odb/dbTransform.h"
#include "ord/OpenRoad.hh"
#include "try_routing.hpp"

namespace esc {
namespace {

// ── property names for persisting results ──────────────────────────
constexpr const char* kRouteProperty = "esc_route";
constexpr const char* kLengthProperty = "esc_route_length";
constexpr const char* kLayerProperty = "esc_route_layer";
constexpr const char* kWidthProperty = "esc_route_width";
constexpr const char* kCriticalLengthProperty = "esc_critical_length";
constexpr const char* kTotalLengthProperty = "esc_total_wirelength";
// Legacy releases materialized ESC routes as dbWire objects. Retain these
// names only so esc_clear can remove pre-existing 2D routes after upgrade.
constexpr const char* kManagedWireProperty = "esc_managed_wire";
constexpr const char* kCreatedNetProperty = "esc_created_net";

// ── helpers ────────────────────────────────────────────────────────

void setDoubleProperty(odb::dbObject* object, const char* name, double value)
{
  if (odb::dbDoubleProperty* prop = odb::dbDoubleProperty::find(object, name)) {
    prop->setValue(value);
  } else {
    odb::dbDoubleProperty::create(object, name, value);
  }
}

void setStringProperty(odb::dbObject* object,
                       const char* name,
                       const std::string& value)
{
  if (odb::dbStringProperty* prop = odb::dbStringProperty::find(object, name)) {
    prop->setValue(value.c_str());
  } else {
    odb::dbStringProperty::create(object, name, value.c_str());
  }
}

void destroyProperty(odb::dbObject* object, const char* name)
{
  if (odb::dbProperty* prop = odb::dbProperty::find(object, name)) {
    odb::dbProperty::destroy(prop);
  }
}

odb::dbChip* requireTopChip()
{
  odb::dbChip* top_chip = ord::OpenRoad::openRoad()->getDb()->getChip();
  if (top_chip == nullptr) {
    throw std::runtime_error(
        "OpenDB has no top chip; read a 3DBlox or chiplet design first");
  }
  return top_chip;
}

// ── extract bumps from OpenDB ──────────────────────────────────────

struct BumpRecord
{
  std::string name;  // net name this bump belongs to
  double x;
  double y;
  std::string chip_name;  // which chip this bump sits on
  std::string region_name;
  odb::dbChip::ChipType chip_type;
  odb::dbChipRegion::Side side;
};

void extractBumpsAndNets(
    odb::dbChip* top_chip,
    std::vector<BumpRecord>& die_records,
    std::vector<BumpRecord>& substrate_records,
    std::vector<std::pair<std::string, std::string>>& net_pairs)
{
  const double dbu = top_chip->getDb()->getDbuPerMicron();
  if (dbu <= 0.0) {
    throw std::runtime_error("Database DBU-per-micron is zero");
  }

  // Collect bumps per net name for classification.
  struct NetBumps
  {
    std::string net_name;
    std::vector<BumpRecord> bumps;
  };
  std::unordered_map<std::string, NetBumps> net_bumps_map;

  // ── Approach A: dbChipNet (chiplet-level nets) ──────────────────
  for (odb::dbChipNet* chip_net : top_chip->getChipNets()) {
    const std::string net_name = chip_net->getName();
    NetBumps& entry = net_bumps_map[net_name];
    entry.net_name = net_name;

    for (uint32_t i = 0; i < chip_net->getNumBumpInsts(); ++i) {
      std::vector<odb::dbChipInst*> path;
      odb::dbChipBumpInst* bump_inst = chip_net->getBumpInst(i, path);
      odb::dbChipBump* chip_bump = bump_inst->getChipBump();
      odb::dbChip* bump_chip = chip_bump->getChip();
      odb::dbInst* phys_inst = chip_bump->getInst();
      if (phys_inst == nullptr || bump_chip == nullptr) {
        continue;
      }

      // dbInst::getLocation() is the placement origin, which is commonly a
      // lower-left macro origin rather than the electrical bump site.  ESC
      // paths and the web viewer must share the physical bump center.
      odb::Point phys_loc = phys_inst->getLocation();
      if (phys_inst->getBBox() != nullptr) {
        const odb::Rect bbox = phys_inst->getBBox()->getBox();
        phys_loc = odb::Point(bbox.xCenter(), bbox.yCenter());
      }
      odb::dbTransform world_xfm;
      for (odb::dbChipInst* inst : path) {
        if (inst == nullptr) {
          continue;
        }
        odb::dbTransform child_xfm = inst->getTransform();
        child_xfm.concat(world_xfm);
        world_xfm = child_xfm;
      }
      world_xfm.apply(phys_loc);
      BumpRecord rec;
      rec.name = net_name;
      rec.x = phys_loc.x() / dbu;
      rec.y = phys_loc.y() / dbu;
      rec.chip_name = bump_chip->getName();
      rec.region_name = chip_bump->getChipRegion()->getName();
      rec.chip_type = bump_chip->getChipType();
      rec.side = chip_bump->getChipRegion()->getSide();
      entry.bumps.push_back(std::move(rec));
    }
  }

  // ── Approach B: dbChipBump::getNet() through chip instances ─────
  if (net_bumps_map.empty()) {
    auto collect_bumps = [&](auto&& self,
                             odb::dbChip* chip,
                             const odb::dbTransform& world_xfm) -> void {
      if (chip == nullptr) {
        return;
      }
      for (odb::dbChipRegion* region : chip->getChipRegions()) {
        for (odb::dbChipBump* bump : region->getChipBumps()) {
          odb::dbNet* net = bump->getNet();
          odb::dbInst* phys_inst = bump->getInst();
          if (phys_inst == nullptr) {
            continue;
          }

          std::string net_name = net ? net->getName() : "__unconnected__";
          if (net_name.empty()) {
            net_name = "__unconnected__";
          }

          NetBumps& entry = net_bumps_map[net_name];
          entry.net_name = net_name;

          // Keep the fallback path consistent with dbChipNet extraction:
          // route from the actual bump center, never from macro origin.
          odb::Point phys_loc = phys_inst->getLocation();
          if (phys_inst->getBBox() != nullptr) {
            const odb::Rect bbox = phys_inst->getBBox()->getBox();
            phys_loc = odb::Point(bbox.xCenter(), bbox.yCenter());
          }
          world_xfm.apply(phys_loc);
          BumpRecord rec;
          rec.name = net_name;
          rec.x = phys_loc.x() / dbu;
          rec.y = phys_loc.y() / dbu;
          rec.chip_name = chip->getName();
          rec.region_name = region->getName();
          rec.chip_type = chip->getChipType();
          rec.side = region->getSide();
          entry.bumps.push_back(std::move(rec));
        }
      }
      for (odb::dbChipInst* child : chip->getChipInsts()) {
        odb::dbTransform child_xfm = child->getTransform();
        child_xfm.concat(world_xfm);
        self(self, child->getMasterChip(), child_xfm);
      }
    };
    collect_bumps(collect_bumps, top_chip, odb::dbTransform{});
  }

  // ── Classification: die-side vs substrate-side ──────────────────
  for (auto& kv : net_bumps_map) {
    auto& entry = kv.second;
    if (entry.bumps.size() < 2) {
      continue;
    }

    std::vector<BumpRecord> die;
    std::vector<BumpRecord> substrate_top_rdl;
    std::vector<BumpRecord> substrate_back;
    std::vector<BumpRecord> substrate_other;
    for (auto& b : entry.bumps) {
      // ESC performs only top-RDL planar routing. A substrate FRONT-side
      // rdl-pad region is its preferred target; a substrate BACK-side package
      // bump remains the compatibility fallback for older single-endpoint
      // collateral. A top bond pad is never an escape target.
      if (b.chip_type == odb::dbChip::ChipType::SUBSTRATE
          || b.chip_type == odb::dbChip::ChipType::RDL) {
        const bool is_top_rdl_pad
            = b.side == odb::dbChipRegion::Side::FRONT
              && (b.region_name.find("rdl") != std::string::npos
                  || b.region_name.find("RDL") != std::string::npos);
        if (is_top_rdl_pad) {
          substrate_top_rdl.push_back(std::move(b));
        } else if (b.side == odb::dbChipRegion::Side::BACK) {
          substrate_back.push_back(std::move(b));
        } else {
          substrate_other.push_back(std::move(b));
        }
      } else if (b.side == odb::dbChipRegion::Side::BACK) {
        die.push_back(std::move(b));
      } else if (b.side == odb::dbChipRegion::Side::FRONT) {
        substrate_other.push_back(std::move(b));
      } else {
        die.push_back(std::move(b));
      }
    }

    std::vector<BumpRecord> sub;
    if (!substrate_top_rdl.empty()) {
      sub = std::move(substrate_top_rdl);
    } else if (!substrate_back.empty()) {
      sub = std::move(substrate_back);
    } else {
      sub = std::move(substrate_other);
    }

    // Fallback: split by distinct chip name
    if (sub.empty() && die.size() >= 2) {
      std::string first_chip = die.front().chip_name;
      for (auto it = die.begin(); it != die.end(); ++it) {
        if (it->chip_name != first_chip) {
          sub.push_back(std::move(*it));
          break;
        }
      }
      die.erase(std::remove_if(
                    die.begin(),
                    die.end(),
                    [](const BumpRecord& r) { return r.chip_name.empty(); }),
                die.end());
    }

    if (die.empty() || sub.empty()) {
      continue;
    }

    die_records.insert(die_records.end(),
                       std::make_move_iterator(die.begin()),
                       std::make_move_iterator(die.end()));
    substrate_records.insert(substrate_records.end(),
                             std::make_move_iterator(sub.begin()),
                             std::make_move_iterator(sub.end()));
  }

  // Build net_pairs by matching die↔sub records per net name.
  std::unordered_map<std::string, std::vector<std::size_t>> die_idx, sub_idx;
  for (std::size_t i = 0; i < die_records.size(); ++i) {
    die_idx[die_records[i].name].push_back(i);
  }
  for (std::size_t i = 0; i < substrate_records.size(); ++i) {
    sub_idx[substrate_records[i].name].push_back(i);
  }

  for (auto& kv : die_idx) {
    auto it = sub_idx.find(kv.first);
    if (it == sub_idx.end()) {
      continue;
    }
    for (std::size_t di : kv.second) {
      for (std::size_t si : it->second) {
        net_pairs.emplace_back(die_records[di].name,
                               substrate_records[si].name);
      }
    }
  }
  std::sort(net_pairs.begin(), net_pairs.end());
  net_pairs.erase(std::unique(net_pairs.begin(), net_pairs.end()),
                  net_pairs.end());
}

// ── serialize / deserialize route polylines ────────────────────────

std::string serializeRoute(const std::vector<Point>& route)
{
  std::ostringstream oss;
  for (std::size_t i = 0; i < route.size(); ++i) {
    if (i != 0) {
      oss << ' ';
    }
    oss << route[i].first << ' ' << route[i].second;
  }
  return oss.str();
}

std::string routePropertyName(const std::string& net_name)
{
  return std::string(kRouteProperty) + "." + net_name;
}

std::string lengthPropertyName(const std::string& net_name)
{
  return std::string(kLengthProperty) + "." + net_name;
}

std::string layerPropertyName(const std::string& net_name)
{
  return std::string(kLayerProperty) + "." + net_name;
}

std::string widthPropertyName(const std::string& net_name)
{
  return std::string(kWidthProperty) + "." + net_name;
}

odb::dbChipNet* findChipNet(odb::dbChip* top_chip, const std::string& net_name)
{
  for (odb::dbChipNet* chip_net : top_chip->getChipNets()) {
    if (chip_net->getName() == net_name) {
      return chip_net;
    }
  }
  return nullptr;
}

std::vector<std::string> getRoutedNetNames(odb::dbChip* top_chip)
{
  const std::string prefix = std::string(kRouteProperty) + ".";
  std::vector<std::string> net_names;
  for (odb::dbProperty* property : odb::dbProperty::getProperties(top_chip)) {
    const std::string name = property->getName();
    if (property->getType() == odb::dbProperty::STRING_PROP
        && name.starts_with(prefix)) {
      net_names.push_back(name.substr(prefix.size()));
    }
  }
  std::sort(net_names.begin(), net_names.end());
  return net_names;
}

odb::dbBlock* findPackageRouteBlock(odb::dbChip* chip)
{
  if (chip == nullptr) {
    return nullptr;
  }
  for (odb::dbChipInst* inst : chip->getChipInsts()) {
    odb::dbChip* child = inst->getMasterChip();
    if (child == nullptr) {
      continue;
    }
    if ((child->getChipType() == odb::dbChip::ChipType::SUBSTRATE
         || child->getChipType() == odb::dbChip::ChipType::RDL)
        && child->getBlock() != nullptr) {
      return child->getBlock();
    }
    if (odb::dbBlock* nested = findPackageRouteBlock(child);
        nested != nullptr) {
      return nested;
    }
  }
  return nullptr;
}

// ── Tcl command implementations ────────────────────────────────────

struct RouteOptions
{
  double net_width = 0.0;
  double min_spacing = 0.0;
  double die_radius = 0.0;
  double substrate_radius = 0.0;
  std::string layer = "metal1";
};

RouteOptions parseRouteOptions(Tcl_Interp* interp,
                               int objc,
                               Tcl_Obj* const objv[])
{
  RouteOptions opts;
  for (int i = 1; i < objc; i += 2) {
    if (i + 1 >= objc) {
      throw std::invalid_argument(std::string("Missing value for ")
                                  + Tcl_GetString(objv[i]));
    }
    const std::string flag = Tcl_GetString(objv[i]);
    if (flag == "-layer") {
      opts.layer = Tcl_GetString(objv[i + 1]);
      continue;
    }
    double val = 0.0;
    if (Tcl_GetDoubleFromObj(interp, objv[i + 1], &val) != TCL_OK) {
      throw std::invalid_argument(flag + " must be a number");
    }
    if (flag == "-net_width") {
      opts.net_width = val;
    } else if (flag == "-min_spacing") {
      opts.min_spacing = val;
    } else if (flag == "-die_radius") {
      opts.die_radius = val;
    } else if (flag == "-substrate_radius") {
      opts.substrate_radius = val;
    } else {
      throw std::invalid_argument("Unknown option: " + flag);
    }
  }

  if (opts.net_width <= 0.0 || opts.min_spacing <= 0.0 || opts.die_radius <= 0.0
      || opts.substrate_radius <= 0.0) {
    throw std::invalid_argument(
        "Usage: esc "
        "-net_width <val> -min_spacing <val> "
        "-die_radius <val> -substrate_radius <val>\n"
        "All four parameters are required and must be positive.");
  }
  return opts;
}

int routeCommand(ClientData,
                 Tcl_Interp* interp,
                 int objc,
                 Tcl_Obj* const objv[])
{
  try {
    const RouteOptions opts = parseRouteOptions(interp, objc, objv);
    odb::dbChip* top_chip = requireTopChip();

    // 1. Extract bumps and nets from OpenDB.
    std::vector<BumpRecord> die_records, substrate_records;
    std::vector<std::pair<std::string, std::string>> net_pairs;
    extractBumpsAndNets(top_chip, die_records, substrate_records, net_pairs);

    if (net_pairs.empty()) {
      throw std::runtime_error(
          "No routable net pairs found. The design needs bump-to-bump "
          "connections via dbChipNet or dbChipBump::getNet().");
    }

    // 2. Build NamedPoint vectors.
    std::vector<NamedPoint> die_pins, substrate_pins;
    for (const auto& r : die_records) {
      die_pins.push_back({r.name, r.x, r.y});
    }
    for (const auto& r : substrate_records) {
      substrate_pins.push_back({r.name, r.x, r.y});
    }

    // 3. Construct algorithm input.
    std::vector<Bump> die_bumps, substrate_bumps;
    std::vector<Net> nets;
    makeInput(die_pins,
              substrate_pins,
              net_pairs,
              opts.net_width,
              opts.min_spacing,
              opts.die_radius,
              opts.substrate_radius,
              die_bumps,
              substrate_bumps,
              nets);

    // 4. Execute the escape router.
    Escaper escaper;
    escaper(die_bumps, "right", substrate_bumps, "left", nets);

    // 5. Collect routes from the final solution.
    std::vector<std::vector<Point>> routes;
    const auto& optimal_order = escaper.optimal_order;
    Solution replay;
    replay.contour.resize(escaper.resource_table.size());
    (void) escaper.route(replay);

    for (std::uint32_t net_idx : optimal_order) {
      replay.order.push_back(static_cast<std::uint16_t>(net_idx));
      routes.push_back(escaper.route(replay));
    }

    // ─ Apply relaxation passes (same as output() does) ─────────────
    {
      const double pitch = NET_WIDTH + MIN_SPACING;
      const std::size_t cols = escaper.resource_table.size();

      // Build occupied intervals and first columns for each route
      // by replaying the routes from scratch (mimics output() behavior).
      std::vector<std::vector<std::uint32_t>> occupied(routes.size());
      std::vector<std::uint32_t> first_col(routes.size(), 0);
      {
        Solution fresh;
        fresh.contour.resize(cols);
        (void) escaper.route(fresh);  // init — empty order
        std::vector<ContourItem> prev = fresh.contour;

        for (std::size_t ri = 0; ri < routes.size(); ++ri) {
          fresh.order.push_back(static_cast<std::uint16_t>(optimal_order[ri]));
          (void) escaper.route(fresh);  // route one net at a time
          auto& occ = occupied[ri];
          occ.resize(cols, UINT32_MAX);
          for (std::size_t col = 0; col < cols; ++col) {
            if (fresh.contour[col].interval_index != prev[col].interval_index
                || fresh.contour[col].point_index != prev[col].point_index) {
              occ[col] = fresh.contour[col].interval_index;
            }
          }
          prev = fresh.contour;
          while (first_col[ri] < cols && occ[first_col[ri]] == UINT32_MAX) {
            ++first_col[ri];
          }
        }
      }

      // Separate routes sharing a point in same column.
      for (std::size_t col = 0; col < cols; ++col) {
        std::size_t ri = 0;
        double ceiling = 1e100;
        while (ri < routes.size()) {
          while (ri < routes.size() && occupied[ri][col] == UINT32_MAX) {
            ++ri;
          }
          if (ri >= routes.size()) {
            break;
          }

          const std::size_t run_beg = ri;
          auto& fp = routes[run_beg][col - first_col[run_beg]];
          std::size_t run_end = run_beg + 1;
          while (run_end < routes.size() && occupied[run_end][col] != UINT32_MAX
                 && fp.first == routes[run_end][col - first_col[run_end]].first
                 && fp.second
                        == routes[run_end][col - first_col[run_end]].second) {
            ++run_end;
          }
          const std::size_t run_sz = run_end - run_beg;

          if (run_sz == 1) {
            if (ceiling < fp.second) {
              fp.second = ceiling;
            }
            ceiling = fp.second - pitch;
            ri = run_end;
            continue;
          }

          double top_y = fp.second;
          const auto& interval
              = escaper.resource_table[col][occupied[run_beg][col]];
          if (interval.points.size() == 1) {
            top_y += static_cast<double>(run_sz >> 1) * pitch;
          } else if (top_y == interval.points.back().second) {
            top_y += static_cast<double>(run_sz) * pitch;
          } else {
            top_y += static_cast<double>(run_sz >> 1) * pitch;
          }
          if (ceiling <= top_y) {
            top_y = ceiling;
          }
          ceiling = top_y;
          for (std::size_t j = run_beg; j < run_end; ++j) {
            auto& pt = routes[j][col - first_col[j]];
            pt.first = fp.first;
            pt.second = top_y;
            ceiling = top_y - pitch;
            top_y = ceiling;
          }
          ri = run_end;
        }
      }

      auto interpolate_y = [](const std::vector<Point>& path, std::size_t i) {
        const auto& pr = path[i - 1];
        const auto& nx = path[i + 1];
        const auto& cu = path[i];
        return ((nx.second - pr.second) * (cu.first - pr.first))
                   / (nx.first - pr.first)
               + pr.second;
      };

      const double bump_half = BUMP_SIZE * 0.5 + NET_WIDTH * 0.5 + MIN_SPACING;

      // 10 top→bottom passes
      for (int pass = 0; pass < 10; ++pass) {
        std::vector<double> limits(cols);
        for (std::size_t c = 0; c < cols; ++c) {
          limits[c] = escaper.resource_table[c].front().points.front().second;
        }

        auto adj = routes;
        for (std::size_t ri = 0; ri < routes.size(); ++ri) {
          const auto& src = routes[ri];
          const auto fc = first_col[ri];
          auto& adjusted = adj[ri];
          for (std::size_t pi = 0; pi < src.size(); ++pi) {
            if (pi == 0 || pi + 1 == src.size()) {
              adjusted[pi] = src[pi];
              continue;
            }
            double line_y = interpolate_y(src, pi);
            double cur_y = src[pi].second;
            double lim = limits[fc + pi];
            if (line_y <= cur_y || lim <= line_y) {
              adjusted[pi].second
                  = (line_y <= lim || lim <= cur_y) ? cur_y : lim;
            } else {
              adjusted[pi].second = line_y;
            }
          }
          for (std::size_t pi = 0; pi < adjusted.size(); ++pi) {
            bool ep = (pi == 0 || pi + 1 == adjusted.size());
            limits[fc + pi] = adjusted[pi].second - (ep ? bump_half : pitch);
          }
        }
        routes = adj;
      }

      // 10 bottom→top passes
      for (int pass = 0; pass < 10; ++pass) {
        std::vector<double> limits(cols);
        for (std::size_t c = 0; c < cols; ++c) {
          limits[c] = escaper.resource_table[c].back().points.back().second;
        }

        auto adj = routes;
        for (std::size_t ri = routes.size(); ri != 0;) {
          --ri;
          const auto& src = routes[ri];
          const auto fc = first_col[ri];
          auto& adjusted = adj[ri];
          for (std::size_t pi = 0; pi < src.size(); ++pi) {
            if (pi == 0 || pi + 1 == src.size()) {
              adjusted[pi] = src[pi];
              continue;
            }
            double cur_y = src[pi].second;
            double lim = limits[fc + pi];
            if (lim <= cur_y) {
              double line_y = interpolate_y(src, pi);
              adjusted[pi].second
                  = (cur_y <= line_y || line_y <= lim)
                        ? ((line_y < lim && lim < cur_y) ? lim : cur_y)
                        : line_y;
            } else {
              adjusted[pi].second = lim;
            }
          }
          for (std::size_t pi = 0; pi < adjusted.size(); ++pi) {
            bool ep = (pi == 0 || pi + 1 == adjusted.size());
            limits[fc + pi] = adjusted[pi].second + (ep ? bump_half : pitch);
          }
        }
        routes = adj;
      }
    }

    // 6. Persist package-RDL routes for the 3D view only.  ESC operates on
    // package geometry, not block-level 2D implementation routing; emitting
    // a dbWire here would make the package RDL appear in the 2D layout.
    double total_length = 0.0;
    double critical_length = 0.0;

    // Validate and retain the top-RDL layer name as route metadata.
    odb::dbTechLayer* routing_layer
        = top_chip->getDb()->getTech()->findLayer(opts.layer.c_str());
    if (routing_layer == nullptr) {
      throw std::runtime_error("Routing layer not found: " + opts.layer);
    }

    if (!getRoutedNetNames(top_chip).empty()) {
      throw std::runtime_error(
          "ESC routes already exist; run esc_clear before rerouting");
    }

    for (std::size_t ri = 0; ri < routes.size(); ++ri) {
      double route_length = 0.0;
      for (std::size_t pi = 1; pi < routes[ri].size(); ++pi) {
        const double dx = routes[ri][pi].first - routes[ri][pi - 1].first;
        const double dy = routes[ri][pi].second - routes[ri][pi - 1].second;
        route_length += std::sqrt(dx * dx + dy * dy);
      }
      total_length += route_length;
      if (route_length > critical_length) {
        critical_length = route_length;
      }

      const std::string& net_name = nets[optimal_order[ri]].name;

      // Store the world-space polyline for esc_report and the 3D viewer.
      setStringProperty(top_chip,
                        routePropertyName(net_name).c_str(),
                        serializeRoute(routes[ri]));
      setDoubleProperty(
          top_chip, lengthPropertyName(net_name).c_str(), route_length);
      setStringProperty(
          top_chip, layerPropertyName(net_name).c_str(), opts.layer);
      setDoubleProperty(
          top_chip, widthPropertyName(net_name).c_str(), opts.net_width);

      // A dbChipNet is the package-level logical owner of this route when
      // the input collateral provides one.  Preserve the same metadata there
      // for package consumers while retaining the top-chip keyed form for
      // 3DBlox designs that only expose dbChipBump::getNet().
      if (odb::dbChipNet* chip_net = findChipNet(top_chip, net_name)) {
        setStringProperty(chip_net, kRouteProperty, serializeRoute(routes[ri]));
        setDoubleProperty(chip_net, kLengthProperty, route_length);
        setStringProperty(chip_net, kLayerProperty, opts.layer);
        setDoubleProperty(chip_net, kWidthProperty, opts.net_width);
      }
    }

    setDoubleProperty(top_chip, kCriticalLengthProperty, critical_length);
    setDoubleProperty(top_chip, kTotalLengthProperty, total_length);

    // 7. Return a Tcl dict with summary.
    Tcl_Obj* result = Tcl_NewDictObj();
    Tcl_DictObjPut(interp,
                   result,
                   Tcl_NewStringObj("nets_routed", -1),
                   Tcl_NewIntObj(static_cast<int>(routes.size())));
    Tcl_DictObjPut(interp,
                   result,
                   Tcl_NewStringObj("critical_length", -1),
                   Tcl_NewDoubleObj(critical_length));
    Tcl_DictObjPut(interp,
                   result,
                   Tcl_NewStringObj("total_wirelength", -1),
                   Tcl_NewDoubleObj(total_length));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;

  } catch (const std::exception& e) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
    return TCL_ERROR;
  }
}

int reportCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const[])
{
  try {
    if (objc != 1) {
      throw std::invalid_argument("Usage: esc_report");
    }

    odb::dbChip* top_chip = requireTopChip();
    Tcl_Obj* result = Tcl_NewDictObj();

    auto* crit_prop
        = odb::dbDoubleProperty::find(top_chip, kCriticalLengthProperty);
    auto* total_prop
        = odb::dbDoubleProperty::find(top_chip, kTotalLengthProperty);

    if (crit_prop == nullptr && total_prop == nullptr) {
      Tcl_DictObjPut(interp,
                     result,
                     Tcl_NewStringObj("status", -1),
                     Tcl_NewStringObj("no routes stored", -1));
      Tcl_SetObjResult(interp, result);
      return TCL_OK;
    }

    Tcl_DictObjPut(interp,
                   result,
                   Tcl_NewStringObj("critical_length", -1),
                   Tcl_NewDoubleObj(crit_prop ? crit_prop->getValue() : 0.0));
    Tcl_DictObjPut(interp,
                   result,
                   Tcl_NewStringObj("total_wirelength", -1),
                   Tcl_NewDoubleObj(total_prop ? total_prop->getValue() : 0.0));

    // Routes are stored as top-chip properties because 3DBlox input may use
    // dbChipBump nets without creating top-level dbChipNet objects.
    Tcl_Obj* net_routes = Tcl_NewDictObj();
    for (const std::string& net_name : getRoutedNetNames(top_chip)) {
      auto* route_prop = odb::dbStringProperty::find(
          top_chip, routePropertyName(net_name).c_str());
      auto* len_prop = odb::dbDoubleProperty::find(
          top_chip, lengthPropertyName(net_name).c_str());
      if (route_prop != nullptr) {
        Tcl_Obj* net_info = Tcl_NewDictObj();
        Tcl_DictObjPut(interp,
                       net_info,
                       Tcl_NewStringObj("route", -1),
                       Tcl_NewStringObj(route_prop->getValue().c_str(), -1));
        if (len_prop != nullptr) {
          Tcl_DictObjPut(interp,
                         net_info,
                         Tcl_NewStringObj("length", -1),
                         Tcl_NewDoubleObj(len_prop->getValue()));
        }
        Tcl_DictObjPut(interp,
                       net_routes,
                       Tcl_NewStringObj(net_name.c_str(), -1),
                       net_info);
      }
    }
    Tcl_DictObjPut(interp, result, Tcl_NewStringObj("nets", -1), net_routes);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;

  } catch (const std::exception& e) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
    return TCL_ERROR;
  }
}

int clearCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const[])
{
  try {
    if (objc != 1) {
      throw std::invalid_argument("Usage: esc_clear");
    }

    odb::dbChip* top_chip = requireTopChip();
    int cleared = 0;
    odb::dbBlock* legacy_route_block = findPackageRouteBlock(top_chip);
    for (const std::string& net_name : getRoutedNetNames(top_chip)) {
      destroyProperty(top_chip, routePropertyName(net_name).c_str());
      destroyProperty(top_chip, lengthPropertyName(net_name).c_str());
      destroyProperty(top_chip, layerPropertyName(net_name).c_str());
      destroyProperty(top_chip, widthPropertyName(net_name).c_str());
      if (odb::dbChipNet* chip_net = findChipNet(top_chip, net_name)) {
        destroyProperty(chip_net, kRouteProperty);
        destroyProperty(chip_net, kLengthProperty);
        destroyProperty(chip_net, kLayerProperty);
        destroyProperty(chip_net, kWidthProperty);
      }
      if (legacy_route_block != nullptr) {
        if (odb::dbNet* net = legacy_route_block->findNet(net_name.c_str())) {
          if (odb::dbStringProperty::find(net, kManagedWireProperty)
              != nullptr) {
            if (odb::dbWire* wire = net->getWire()) {
              odb::dbWire::destroy(wire);
            }
            destroyProperty(net, kManagedWireProperty);
          }
          if (odb::dbStringProperty::find(net, kCreatedNetProperty)
              != nullptr) {
            odb::dbNet::destroy(net);
          }
        }
      }
      ++cleared;
    }
    // Clear top-level summary properties.
    destroyProperty(top_chip, kCriticalLengthProperty);
    destroyProperty(top_chip, kTotalLengthProperty);

    Tcl_SetObjResult(interp, Tcl_NewIntObj(cleared));
    return TCL_OK;

  } catch (const std::exception& e) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
    return TCL_ERROR;
  }
}

}  // namespace

void initEsc(Tcl_Interp* interp)
{
  Tcl_Eval(interp, "namespace eval esc {}");
  Tcl_CreateObjCommand(interp, "esc::route", routeCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(interp, "esc", routeCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(interp, "esc::report", reportCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(interp, "esc_report", reportCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(interp, "esc::clear", clearCommand, nullptr, nullptr);
  Tcl_CreateObjCommand(interp, "esc_clear", clearCommand, nullptr, nullptr);
}

}  // namespace esc
