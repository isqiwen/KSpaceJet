#pragma once

/// Image geometry conversions, coordinates, and transformations between pixel and physical domains.

#include "kspacejet/linalg/fixed_matrix.hpp"

#include <cstddef>

namespace ksj::image {

struct OrientedBox3d {
  ksj::linalg::Vector3d column_axis;
  ksj::linalg::Vector3d row_axis;
  ksj::linalg::Vector3d center;

  double width{};
  double height{};
  double depth{};
};

struct SliceGeometry3d {
  OrientedBox3d box;
  std::size_t rows{};
  std::size_t columns{};
};

struct VolumeGeometry3d {
  OrientedBox3d box;
  std::size_t rows{};
  std::size_t columns{};
  std::size_t slices{};
};

} // namespace ksj::image
