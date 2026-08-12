#pragma once

/// Owning image-volume containers and factories with KSpaceJet row-major storage semantics.

#include "kspacejet/array/pooled_cube.hpp"
#include "kspacejet/image/geometry.hpp"
#include "kspacejet/image/volume_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ksj::image {

template <typename T> class Volume {
public:
  using storage_type = ksj::array::PooledCube<T>;
  using view_type = VolumeView<T>;
  using const_view_type = VolumeView<const T>;

  void resize(const std::size_t row_count, const std::size_t column_count, const std::size_t slice_count,
              const T* initial_data = nullptr) {
    rows_ = row_count;
    columns_ = column_count;
    slices_ = slice_count;
    voxels_ = storage_type(slices_, rows_, columns_);

    if (initial_data != nullptr) {
      std::copy_n(initial_data, size(), data());
    }
  }

  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
  [[nodiscard]] std::size_t slices() const noexcept { return slices_; }
  [[nodiscard]] std::size_t slice_size() const noexcept { return rows_ * columns_; }
  [[nodiscard]] std::size_t size() const noexcept { return voxels_.size(); }

  [[nodiscard]] T* data() noexcept { return voxels_.data(); }
  [[nodiscard]] const T* data() const noexcept { return voxels_.data(); }

  [[nodiscard]] view_type view() noexcept { return view_type::contiguous(data(), rows_, columns_, slices_); }

  [[nodiscard]] const_view_type view() const noexcept {
    return const_view_type::contiguous(data(), rows_, columns_, slices_);
  }

  [[nodiscard]] T* slice_data(const std::size_t slice) noexcept { return voxels_.data() + slice_size() * slice; }

  [[nodiscard]] const T* slice_data(const std::size_t slice) const noexcept {
    return voxels_.data() + slice_size() * slice;
  }

  void set_slice(const std::size_t slice, const T* pixels) { std::copy_n(pixels, slice_size(), slice_data(slice)); }

private:
  std::size_t rows_{};
  std::size_t columns_{};
  std::size_t slices_{};
  storage_type voxels_;
};

template <typename T> class VolumeReslicer {
public:
  static void render_slice(const VolumeGeometry3d& volume_geometry, const T* voxels,
                           const SliceGeometry3d& slice_geometry, T* pixels) {
    const auto& slice_box = slice_geometry.box;
    const auto& column_axis = slice_box.column_axis;
    const auto& row_axis = slice_box.row_axis;

    const double slice_row_spacing = slice_box.height / static_cast<double>(slice_geometry.rows);
    const double slice_column_spacing = slice_box.width / static_cast<double>(slice_geometry.columns);

    const auto first_pixel_position = slice_box.center +
                                      column_axis * (0.0 - slice_box.width / 2.0 + slice_column_spacing / 2.0) +
                                      row_axis * (0.0 - slice_box.height / 2.0 + slice_row_spacing / 2.0);

    for (std::size_t row = 0; row < slice_geometry.rows; ++row) {
      for (std::size_t column = 0; column < slice_geometry.columns; ++column) {
        const auto position = first_pixel_position +
                              column_axis * (static_cast<double>(column) * slice_column_spacing) +
                              row_axis * (static_cast<double>(row) * slice_row_spacing);

        pixels[slice_geometry.columns * row + column] = trilinear(volume_geometry, voxels, position);
      }
    }
  }

private:
  static T trilinear(const VolumeGeometry3d& volume_geometry, const T* voxels, const ksj::linalg::Vector3d& position) {
    const auto& box = volume_geometry.box;
    const auto offset = position - box.center;

    const double row_spacing = box.height / static_cast<double>(volume_geometry.rows);
    const double column_spacing = box.width / static_cast<double>(volume_geometry.columns);
    const double slice_spacing = box.depth / static_cast<double>(volume_geometry.slices);

    const auto normal = ksj::linalg::Vector3d::cross(box.column_axis, box.row_axis);
    const double distance[3] = {
      ksj::linalg::Vector3d::dot(offset, box.column_axis),
      ksj::linalg::Vector3d::dot(offset, box.row_axis),
      ksj::linalg::Vector3d::dot(offset, normal),
    };

    if ((box.width / 2.0) < std::abs(distance[0]) || (box.height / 2.0) < std::abs(distance[1]) ||
        (box.depth / 2.0) < std::abs(distance[2])) {
      return T{};
    }

    const double x_index = (distance[0] + box.width / 2.0) / column_spacing;
    const std::size_t x0_index = floor_index(x_index, volume_geometry.columns);
    const std::size_t x1_index = std::min(x0_index + 1U, last_index(volume_geometry.columns));

    const double x = column_spacing * x_index;
    const double x0 = column_spacing * static_cast<double>(x0_index);
    const double x1 = column_spacing * static_cast<double>(x1_index);
    const double xd = x0_index == x1_index ? 0.5 : (x - x0) / (x1 - x0);

    auto y_index = (distance[1] + box.height / 2.0) / row_spacing - 1.0;
    y_index = std::max(0.0, y_index);
    const std::size_t y0_index = floor_index(y_index, volume_geometry.rows);
    const std::size_t y1_index = std::min(y0_index + 1U, last_index(volume_geometry.rows));

    const double y = row_spacing * y_index;
    const double y0 = row_spacing * static_cast<double>(y0_index);
    const double y1 = row_spacing * static_cast<double>(y1_index);
    const double yd = y0_index == y1_index ? 0.5 : (y - y0) / (y1 - y0);

    const auto z_index = (distance[2] + box.depth / 2.0) / slice_spacing;
    const std::size_t z0_index = floor_index(z_index, volume_geometry.slices);
    const std::size_t z1_index = std::min(z0_index + 1U, last_index(volume_geometry.slices));

    const double z = slice_spacing * z_index;
    const double z0 = slice_spacing * static_cast<double>(z0_index);
    const double z1 = slice_spacing * static_cast<double>(z1_index);
    const double zd = z0_index == z1_index ? 0.5 : (z - z0) / (z1 - z0);

    const auto voxel = [&](const std::size_t row, const std::size_t column, const std::size_t slice) -> const T& {
      return voxels[slice * (volume_geometry.columns * volume_geometry.rows) + volume_geometry.columns * row + column];
    };

    const T c000 = voxel(y1_index, x0_index, z1_index);
    const T c001 = voxel(y0_index, x0_index, z1_index);
    const T c101 = voxel(y0_index, x1_index, z1_index);
    const T c100 = voxel(y1_index, x1_index, z1_index);
    const T c010 = voxel(y1_index, x0_index, z0_index);
    const T c011 = voxel(y0_index, x0_index, z0_index);
    const T c111 = voxel(y0_index, x1_index, z0_index);
    const T c110 = voxel(y1_index, x1_index, z0_index);

    const T c00 = lerp(c000, c100, xd);
    const T c01 = lerp(c001, c101, xd);
    const T c10 = lerp(c010, c110, xd);
    const T c11 = lerp(c011, c111, xd);

    const T c0 = lerp(c10, c00, zd);
    const T c1 = lerp(c11, c01, zd);

    return lerp(c1, c0, yd);
  }

  [[nodiscard]] static constexpr std::size_t last_index(const std::size_t count) noexcept { return count - 1U; }

  [[nodiscard]] static std::size_t floor_index(const double value, const std::size_t count) noexcept {
    return static_cast<std::size_t>(std::clamp(std::floor(value), 0.0, static_cast<double>(last_index(count))));
  }

  [[nodiscard]] static T lerp(const T lhs, const T rhs, const double fraction) noexcept {
    return static_cast<T>((static_cast<double>(lhs) * (1.0 - fraction)) + (static_cast<double>(rhs) * fraction));
  }
};

} // namespace ksj::image
