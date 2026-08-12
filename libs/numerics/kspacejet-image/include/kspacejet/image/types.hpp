#pragma once

/// Image-domain enums, pixel/geometry value types, and shared algorithm configuration types.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image {

enum class BorderMode {
  constant,
  replicate,
  reflect,
  reflect101,
};

enum class Connectivity {
  four = 4,
  eight = 8,
};

enum class ResizeMethod {
  nearest,
  linear,
  cubic,
  area,
  lanczos4,
};

enum class InterpolationAxis {
  row,
  column,
};

enum class InterpolationStatus {
  success,
  empty_input,
  backend_error,
};

enum class HoleFillMode {
  exterior,
  corner_pair,
};

enum class FilterAnchor {
  center,
  top_left,
};

enum class StructuringElementShape {
  rectangle,
  ellipse,
};

using ConnectedComponentLabel = std::int32_t;
using RegionGrowMaskValue = std::uint8_t;

struct ConnectedComponentStats {
  ConnectedComponentLabel label{};
  std::size_t area{};
  std::size_t min_row{};
  std::size_t min_col{};
  std::size_t max_row{};
  std::size_t max_col{};
  double centroid_row{};
  double centroid_col{};
};

struct ConnectedComponentsResult {
  ksj::array::PooledImage<ConnectedComponentLabel> labels;
  std::vector<ConnectedComponentStats> stats;
};

struct InterpolationResult {
  InterpolationStatus status{InterpolationStatus::success};
  int backend_status{};
};

struct MagnitudeDenoiseParameters {
  float nlm_h{};
  int nlm_template_window_size{};
  int nlm_search_window_size{};
  float diffusion_kappa{};
  int diffusion_iterations{};
  float diffusion_lambda{};
  int diffusion_option{};
  float sharpen_sigma{};
  float sharpen_amount{};
};

struct BgrColor {
  std::uint8_t blue{};
  std::uint8_t green{};
  std::uint8_t red{};
};

struct ImagePoint {
  int x{};
  int y{};
};

struct TextSize {
  int width{};
  int height{};
  int baseline{};
};

struct EccRegistrationOptions {
  int iterations{20};
  double epsilon{1.0E-10};
};

} // namespace ksj::image
