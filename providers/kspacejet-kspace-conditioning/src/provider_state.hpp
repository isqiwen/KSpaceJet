// SPDX-License-Identifier: Apache-2.0
//
// Provider-owned opaque handle definitions and dispatch state for
// kspacejet-kspace-conditioning. The only exported ABI symbol is
// ksj_provider_query.

#pragma once

#include "kspacejet/provider/provider.h"

#include <cstdint>
#include <string_view>

namespace ksj::kspace_conditioning::state {

struct ConditioningOperatorImplementation;

} // namespace ksj::kspace_conditioning::state

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  const ksj::kspace_conditioning::state::ConditioningOperatorImplementation* implementation{nullptr};
  std::uint32_t channel_count{0U};
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t physical_channel_count{0U};
  std::uint32_t virtual_channel_count{0U};
  std::uint32_t input_cols{0U};
  std::uint32_t output_cols{0U};
  std::uint32_t readout_offset{0U};
  std::uint32_t sample_count{0U};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};

namespace ksj::kspace_conditioning::state {

struct ConditioningOperatorImplementation {
  std::string_view id;
  const char* unsupported_config_error{nullptr};
  std::uint64_t unsupported_config_error_size{0U};
  std::uint32_t input_batch_count{0U};
  bool (*configure)(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*is_valid)(const ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*matches_dynamic_input_type)(const ksj_type_descriptor_view& type) noexcept {nullptr};
  bool (*matches_static_input_type)(const ksj_type_descriptor_view& type) noexcept {nullptr};
  bool (*matches_output_type)(const ksj_type_descriptor_view& type) noexcept {nullptr};
  ksj_type_descriptor_view (*output_type)(){nullptr};
  bool (*expected_dynamic_input_byte_count)(const ksj_provider_operator& operator_handle,
                                            std::uint64_t& byte_count) noexcept {nullptr};
  bool (*expected_static_input_byte_count)(const ksj_provider_operator& operator_handle,
                                           std::uint64_t& byte_count) noexcept {nullptr};
  bool (*expected_output_byte_count)(const ksj_provider_operator& operator_handle,
                                     std::uint64_t& byte_count) noexcept {nullptr};
  bool (*transform)(const ksj_provider_operator& operator_handle, const void* dynamic_input, const void* static_input,
                    void* output) noexcept {nullptr};
};

} // namespace ksj::kspace_conditioning::state
