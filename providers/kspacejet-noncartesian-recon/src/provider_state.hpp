// SPDX-License-Identifier: Apache-2.0
//
// Private opaque-handle state for the non-Cartesian reconstruction Provider.

#pragma once

#include "kspacejet/provider/provider.h"

#include <cstdint>

struct ksj_provider_operator;

struct ksj_execution_context {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_key_state {
  ksj_provider_operator* owner{nullptr};
};

struct ksj_provider_operator {
  std::uint32_t channels{0U};
  std::uint32_t image_rows{0U};
  std::uint32_t image_cols{0U};
  std::uint32_t sample_count{0U};
  ksj_execution_context context{};
  ksj_key_state key_state{};
  bool context_active{false};
  bool key_state_active{false};
};
