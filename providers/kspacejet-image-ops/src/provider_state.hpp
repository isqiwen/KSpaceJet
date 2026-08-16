// SPDX-License-Identifier: Apache-2.0
//
// Provider-private state shared by the image-ops lifecycle dispatcher and
// its Operators. Nothing in this header is part of the Provider ABI.

#pragma once

#include "kspacejet/provider/provider.h"

#include <cstdint>
#include <string_view>

namespace ksj::image_ops::state {

struct ImageOperatorImplementation {
  std::string_view id;
  const char* unsupported_config_error{nullptr};
  std::uint64_t unsupported_config_error_size{0U};
  bool (*configure)(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {nullptr};
  bool (*is_valid)(const ksj_provider_operator& operator_handle) noexcept {nullptr};
  float (*transform)(const ksj_provider_operator& operator_handle, float value) noexcept {nullptr};
};

} // namespace ksj::image_ops::state

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  const ksj::image_ops::state::ImageOperatorImplementation* implementation{nullptr};
  float scalar{0.0F};
  float clamp_minimum{0.0F};
  float clamp_maximum{0.0F};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};
