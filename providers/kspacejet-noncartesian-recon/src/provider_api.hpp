// SPDX-License-Identifier: Apache-2.0
//
// Private Provider-level API for the non-Cartesian reconstruction dynamic
// library.

#pragma once

#include "kspacejet/provider/provider.h"

namespace ksj::noncartesian_recon::api {

[[nodiscard]] ksj_status provider_query(const ksj_provider_query_request* request,
                                        ksj_provider_descriptor* out_descriptor, ksj_provider_api* out_api,
                                        ksj_error_view* out_error) noexcept;

} // namespace ksj::noncartesian_recon::api
