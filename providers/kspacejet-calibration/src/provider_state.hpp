// SPDX-License-Identifier: Apache-2.0
//
// Provider-owned opaque handle definitions and dispatch state for
// kspacejet-calibration. The only exported ABI symbol is ksj_provider_query.

#pragma once

#include "kspacejet/provider/provider.h"

#include <cstdint>
#include <string_view>

namespace ksj::calibration::state {

struct CalibrationOperatorImplementation;

} // namespace ksj::calibration::state

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  const ksj::calibration::state::CalibrationOperatorImplementation* implementation{nullptr};
  std::uint32_t channel_count{0U};
  std::uint32_t readout_sample_count{0U};
  std::uint32_t physical_channel_count{0U};
  std::uint32_t virtual_channel_count{0U};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};

namespace ksj::calibration::state {

struct CalibrationOperatorImplementation {
  std::string_view id;
  std::uint64_t maximum_scratch_bytes_per_firing{0U};
  const char* unsupported_config_error{nullptr};
  std::uint64_t unsupported_config_error_size{0U};
  bool (*configure)(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*is_valid)(const ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*matches_input_type)(const ksj_type_descriptor_view& type) noexcept {nullptr};
  bool (*matches_output_type)(const ksj_type_descriptor_view& type) noexcept {nullptr};
  ksj_type_descriptor_view (*output_type)(){nullptr};
  bool (*has_compatible_input_byte_count)(const ksj_provider_operator& operator_handle,
                                          std::uint64_t byte_count) noexcept {nullptr};
  bool (*expected_output_byte_count)(const ksj_provider_operator& operator_handle,
                                     std::uint64_t& byte_count) noexcept {nullptr};
  bool (*required_scratch_bytes)(const ksj_provider_operator& operator_handle,
                                 std::uint64_t& byte_count) noexcept {nullptr};
  bool (*estimate)(const ksj_provider_operator& operator_handle, const void* input, std::uint64_t input_byte_count,
                   void* output, std::uint64_t output_byte_count, void* scratch,
                   std::uint64_t scratch_byte_count) noexcept {nullptr};
};

} // namespace ksj::calibration::state
