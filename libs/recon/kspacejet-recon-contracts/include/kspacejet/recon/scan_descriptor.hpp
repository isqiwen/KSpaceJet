#pragma once

#include "kspacejet/recon/bounded_value.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

struct MatrixDimensions {
  Quantity x = 0;
  Quantity y = 0;
  Quantity z = 0;

  friend constexpr bool operator==(const MatrixDimensions&, const MatrixDimensions&) noexcept = default;
};

struct FieldOfViewMm {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  friend constexpr bool operator==(const FieldOfViewMm&, const FieldOfViewMm&) noexcept = default;
};

class IndexLimit final {
public:
  [[nodiscard]] static Result<IndexLimit> create(Quantity minimum, Quantity maximum, Quantity center,
                                                 std::string_view field_name);

  [[nodiscard]] constexpr Quantity minimum() const noexcept { return minimum_.value(); }
  [[nodiscard]] constexpr Quantity maximum() const noexcept { return maximum_.value(); }
  [[nodiscard]] constexpr Quantity center() const noexcept { return center_.value(); }
  [[nodiscard]] constexpr Quantity cardinality() const noexcept { return maximum() - minimum() + 1; }

  friend constexpr bool operator==(const IndexLimit&, const IndexLimit&) noexcept = default;

private:
  IndexLimit(CanonicalQuantity minimum, CanonicalQuantity maximum, CanonicalQuantity center) noexcept
      : minimum_(minimum), maximum_(maximum), center_(center) {}

  CanonicalQuantity minimum_;
  CanonicalQuantity maximum_;
  CanonicalQuantity center_;
};

enum class EncodingLimitDimension {
  kspace_encode_step_0,
  kspace_encode_step_1,
  kspace_encode_step_2,
  average,
  slice,
  contrast,
  phase,
  repetition,
  set,
  segment,
  user_0,
  user_1,
  user_2,
  user_3,
  user_4,
  user_5,
  user_6,
  user_7,
};

enum class TrajectoryType {
  cartesian,
  epi,
  radial,
  golden_angle,
  spiral,
  other,
};

class EncodingLimits final {
public:
  [[nodiscard]] const std::optional<IndexLimit>& at(EncodingLimitDimension dimension) const noexcept;

  // Only use after each present IndexLimit and its source field have been
  // validated.  The parser needs this vendor-free construction step to keep
  // ISMRMRD XML classes out of the public ABI.
  [[nodiscard]] static EncodingLimits from_validated(std::array<std::optional<IndexLimit>, 18> limits) noexcept;

private:
  std::array<std::optional<IndexLimit>, 18> limits_{};
};

class EncodingDescriptor final {
public:
  [[nodiscard]] const MatrixDimensions& encoded_matrix() const noexcept { return encoded_matrix_; }
  [[nodiscard]] const FieldOfViewMm& encoded_field_of_view_mm() const noexcept { return encoded_field_of_view_mm_; }
  [[nodiscard]] const MatrixDimensions& recon_matrix() const noexcept { return recon_matrix_; }
  [[nodiscard]] const FieldOfViewMm& recon_field_of_view_mm() const noexcept { return recon_field_of_view_mm_; }
  [[nodiscard]] TrajectoryType trajectory() const noexcept { return trajectory_; }
  [[nodiscard]] const EncodingLimits& limits() const noexcept { return limits_; }

  [[nodiscard]] static EncodingDescriptor
  from_validated(MatrixDimensions encoded_matrix, FieldOfViewMm encoded_field_of_view_mm, MatrixDimensions recon_matrix,
                 FieldOfViewMm recon_field_of_view_mm, TrajectoryType trajectory, EncodingLimits limits) noexcept;

private:
  MatrixDimensions encoded_matrix_;
  FieldOfViewMm encoded_field_of_view_mm_;
  MatrixDimensions recon_matrix_;
  FieldOfViewMm recon_field_of_view_mm_;
  TrajectoryType trajectory_ = TrajectoryType::other;
  EncodingLimits limits_;
};

struct ScanDescriptorParseOptions {
  // Bounded XML parsing prevents a pre-admission metadata message from
  // allocating an unbounded amount of process memory.
  Quantity max_xml_bytes = 16ULL * 1024ULL * 1024ULL;
};

// A vendor-free, immutable description of the metadata knowable before raw
// acquisition delivery.  Values that first appear in individual acquisition
// headers deliberately do not belong here.
class ScanDescriptor final {
public:
  [[nodiscard]] static Result<ScanDescriptor> parse_ismrmrd_xml(std::string_view xml,
                                                                const ScanDescriptorParseOptions& options = {});

  [[nodiscard]] const std::vector<EncodingDescriptor>& encodings() const noexcept { return encodings_; }
  [[nodiscard]] std::optional<Quantity> declared_receiver_channels() const noexcept {
    return declared_receiver_channels_;
  }
  [[nodiscard]] Quantity source_xml_bytes() const noexcept { return source_xml_bytes_.value(); }

private:
  ScanDescriptor(std::vector<EncodingDescriptor> encodings, std::optional<Quantity> declared_receiver_channels,
                 CanonicalQuantity source_xml_bytes) noexcept
      : encodings_(std::move(encodings)), declared_receiver_channels_(declared_receiver_channels),
        source_xml_bytes_(source_xml_bytes) {}

  std::vector<EncodingDescriptor> encodings_;
  std::optional<Quantity> declared_receiver_channels_;
  CanonicalQuantity source_xml_bytes_;
};

[[nodiscard]] Result<ScanDescriptor> parse_ismrmrd_xml(std::string_view xml,
                                                       const ScanDescriptorParseOptions& options = {});

} // namespace ksj::recon
