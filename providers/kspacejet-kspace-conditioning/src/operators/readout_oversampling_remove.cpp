// SPDX-License-Identifier: Apache-2.0
//
// Copy one explicit readout crop from every [channel, ky] k-space row. The
// configured offset is literal; this Operator never assumes a centred crop.

#include "operators/readout_oversampling_remove.hpp"

#include "support/kspace_frame.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ksj::kspace_conditioning::operators {

using ::ksj::kspace_conditioning::state::ConditioningOperatorImplementation;
using ::ksj::kspace_conditioning::support::checked_add;
using ::ksj::kspace_conditioning::support::Complex;
using ::ksj::kspace_conditioning::support::kMaximumChannelCount;
using ::ksj::kspace_conditioning::support::kMaximumSpatialDimension;
using ::ksj::kspace_conditioning::support::kMinimumSpatialDimension;
using ::ksj::kspace_conditioning::support::kspace_frame_byte_count;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::parse_canonical_positive_u32;

namespace {

constexpr char kOperatorId[] = "readout_oversampling_remove";
constexpr char kUnsupportedConfigError[] =
  "K-space-conditioning Provider readout_oversampling_remove requires canonical "
  "{\"channel_count\":N,\"input_cols\":N,\"output_cols\":N,\"readout_offset\":N,\"rows\":N}, "
  "where 1 <= channel_count <= 64, 2 <= rows,input_cols,output_cols <= 512, and "
  "readout_offset + output_cols <= input_cols";

[[nodiscard]] bool parse_canonical_nonnegative_u32(const char* const first, const char* const last,
                                                   std::uint32_t& output) noexcept {
  if (first == nullptr || last == nullptr || first >= last) {
    return false;
  }
  if (*first == '0') {
    if (first + 1U != last) {
      return false;
    }
    output = 0U;
    return true;
  }
  return parse_canonical_positive_u32(first, last, output);
}

[[nodiscard]] bool parse_config(const ksj_byte_view& config, std::uint32_t& channel_count, std::uint32_t& input_cols,
                                std::uint32_t& output_cols, std::uint32_t& readout_offset,
                                std::uint32_t& rows) noexcept {
  constexpr std::string_view kPrefix{"{\"channel_count\":"};
  constexpr std::string_view kInputCols{",\"input_cols\":"};
  constexpr std::string_view kOutputCols{",\"output_cols\":"};
  constexpr std::string_view kOffset{",\"readout_offset\":"};
  constexpr std::string_view kRows{",\"rows\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto input_cols_offset = encoded.find(kInputCols, kPrefix.size());
  const auto output_cols_offset = input_cols_offset == std::string_view::npos
                                    ? std::string_view::npos
                                    : encoded.find(kOutputCols, input_cols_offset + kInputCols.size());
  const auto offset_offset = output_cols_offset == std::string_view::npos
                               ? std::string_view::npos
                               : encoded.find(kOffset, output_cols_offset + kOutputCols.size());
  const auto rows_offset = offset_offset == std::string_view::npos
                             ? std::string_view::npos
                             : encoded.find(kRows, offset_offset + kOffset.size());
  if (input_cols_offset == std::string_view::npos || output_cols_offset == std::string_view::npos ||
      offset_offset == std::string_view::npos || rows_offset == std::string_view::npos) {
    return false;
  }
  const char* const first = encoded.data();
  const char* const channel_first = first + kPrefix.size();
  const char* const channel_last = first + input_cols_offset;
  const char* const input_cols_first = channel_last + kInputCols.size();
  const char* const input_cols_last = first + output_cols_offset;
  const char* const output_cols_first = input_cols_last + kOutputCols.size();
  const char* const output_cols_last = first + offset_offset;
  const char* const offset_first = output_cols_last + kOffset.size();
  const char* const offset_last = first + rows_offset;
  const char* const rows_first = offset_last + kRows.size();
  const char* const rows_last = first + encoded.size() - kSuffix.size();
  std::uint64_t crop_end = 0U;
  return parse_canonical_positive_u32(channel_first, channel_last, channel_count) &&
         parse_canonical_positive_u32(input_cols_first, input_cols_last, input_cols) &&
         parse_canonical_positive_u32(output_cols_first, output_cols_last, output_cols) &&
         parse_canonical_nonnegative_u32(offset_first, offset_last, readout_offset) &&
         parse_canonical_positive_u32(rows_first, rows_last, rows) && channel_count <= kMaximumChannelCount &&
         input_cols >= kMinimumSpatialDimension && input_cols <= kMaximumSpatialDimension &&
         output_cols >= kMinimumSpatialDimension && output_cols <= kMaximumSpatialDimension &&
         rows >= kMinimumSpatialDimension && rows <= kMaximumSpatialDimension &&
         checked_add(readout_offset, output_cols, crop_end) && crop_end <= input_cols;
}

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  std::uint32_t channel_count = 0U;
  std::uint32_t input_cols = 0U;
  std::uint32_t output_cols = 0U;
  std::uint32_t readout_offset = 0U;
  std::uint32_t rows = 0U;
  if (!parse_config(config, channel_count, input_cols, output_cols, readout_offset, rows)) {
    return false;
  }
  operator_handle.channel_count = channel_count;
  operator_handle.input_cols = input_cols;
  operator_handle.output_cols = output_cols;
  operator_handle.readout_offset = readout_offset;
  operator_handle.rows = rows;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  std::uint64_t crop_end = 0U;
  return operator_handle.channel_count != 0U && operator_handle.channel_count <= kMaximumChannelCount &&
         operator_handle.rows >= kMinimumSpatialDimension && operator_handle.rows <= kMaximumSpatialDimension &&
         operator_handle.input_cols >= kMinimumSpatialDimension &&
         operator_handle.input_cols <= kMaximumSpatialDimension &&
         operator_handle.output_cols >= kMinimumSpatialDimension &&
         operator_handle.output_cols <= kMaximumSpatialDimension &&
         checked_add(operator_handle.readout_offset, operator_handle.output_cols, crop_end) &&
         crop_end <= operator_handle.input_cols;
}

[[nodiscard]] bool matches_dynamic_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_kspace_frame(&type) != 0;
}

[[nodiscard]] bool matches_output_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_kspace_frame(&type) != 0;
}

[[nodiscard]] ksj_type_descriptor_view output_type() {
  return ksj_type_registry_kspace_frame();
}

[[nodiscard]] bool expected_dynamic_input_byte_count(const ksj_provider_operator& operator_handle,
                                                     std::uint64_t& byte_count) noexcept {
  return is_valid(operator_handle) && kspace_frame_byte_count(operator_handle.channel_count, operator_handle.rows,
                                                              operator_handle.input_cols, byte_count);
}

[[nodiscard]] bool expected_output_byte_count(const ksj_provider_operator& operator_handle,
                                              std::uint64_t& byte_count) noexcept {
  return is_valid(operator_handle) && kspace_frame_byte_count(operator_handle.channel_count, operator_handle.rows,
                                                              operator_handle.output_cols, byte_count);
}

[[nodiscard]] bool transform(const ksj_provider_operator& operator_handle, const void* const dynamic_input, const void*,
                             void* const output) noexcept {
  try {
    if (dynamic_input == nullptr || output == nullptr || !is_valid(operator_handle)) {
      return false;
    }
    const auto row_count = static_cast<std::size_t>(operator_handle.channel_count) * operator_handle.rows;
    const auto input_cols = static_cast<std::size_t>(operator_handle.input_cols);
    const auto output_cols = static_cast<std::size_t>(operator_handle.output_cols);
    const auto readout_offset = static_cast<std::size_t>(operator_handle.readout_offset);
    const auto input =
      ksj::array::MatrixView<const Complex>{static_cast<const Complex*>(dynamic_input), row_count, input_cols};
    const auto cropped = ksj::array::MatrixView<Complex>{static_cast<Complex*>(output), row_count, output_cols};
    for (std::size_t row = 0U; row < row_count; ++row) {
      std::copy_n(input.row(row).data() + readout_offset, output_cols, cropped.row(row).data());
    }
    return true;
  } catch (...) {
    KSJ_LOG_ERROR("readout_oversampling_remove trapped an unexpected exception while transforming k-space");
    return false;
  }
}

} // namespace

const ConditioningOperatorImplementation& readout_oversampling_remove_operator() noexcept {
  static const ConditioningOperatorImplementation implementation{
    .id = kOperatorId,
    .unsupported_config_error = kUnsupportedConfigError,
    .unsupported_config_error_size = sizeof(kUnsupportedConfigError) - 1U,
    .input_batch_count = 1U,
    .configure = &configure,
    .is_valid = &is_valid,
    .matches_dynamic_input_type = &matches_dynamic_input_type,
    .matches_static_input_type = nullptr,
    .matches_output_type = &matches_output_type,
    .output_type = &output_type,
    .expected_dynamic_input_byte_count = &expected_dynamic_input_byte_count,
    .expected_static_input_byte_count = nullptr,
    .expected_output_byte_count = &expected_output_byte_count,
    .transform = &transform,
  };
  return implementation;
}

} // namespace ksj::kspace_conditioning::operators
