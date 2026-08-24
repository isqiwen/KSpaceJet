// SPDX-License-Identifier: Apache-2.0
//
// Private opaque-handle state for the non-Cartesian reconstruction Provider.

#pragma once

#include "kspacejet/provider/provider.h"

#include <cstdint>
#include <string_view>

struct ksj_provider_operator;

namespace ksj::noncartesian_recon::state {

struct NonCartesianReconOperatorImplementation;

} // namespace ksj::noncartesian_recon::state

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  const ksj::noncartesian_recon::state::NonCartesianReconOperatorImplementation* implementation{nullptr};
  std::uint32_t channels{0U};
  std::uint32_t image_rows{0U};
  std::uint32_t image_cols{0U};
  std::uint32_t sample_count{0U};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};

namespace ksj::noncartesian_recon::state {

// Every entry is a private implementation record. The Provider API owns the
// descriptor array and lifecycle dispatch; an Operator owns only its
// configuration validation and frame-local computation.
struct NonCartesianReconOperatorImplementation {
  std::string_view id;
  const char* unsupported_config_error{nullptr};
  std::uint64_t unsupported_config_error_size{0U};
  std::uint64_t max_output_bytes_per_firing{0U};
  std::uint64_t max_scratch_bytes_per_firing{0U};
  bool (*configure)(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*is_valid)(const ksj_provider_operator& operator_handle) noexcept {nullptr};
  ksj_status (*process)(ksj_provider_operator& operator_handle, ksj_firing_lease* lease,
                        const ksj_firing_lease_callbacks* callbacks, ksj_process_result* out_result,
                        ksj_error_view* out_error) noexcept {nullptr};
};

} // namespace ksj::noncartesian_recon::state
