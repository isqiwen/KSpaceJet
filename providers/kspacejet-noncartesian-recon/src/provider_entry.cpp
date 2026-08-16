// SPDX-License-Identifier: Apache-2.0
//
// The plugin's sole exported ABI symbol. Descriptor construction and lifecycle
// dispatch remain private in provider_api.cpp.

#include "provider_api.hpp"

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* request,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api* out_api,
                                                                   ksj_error_view* out_error) {
  return ksj::noncartesian_recon::api::provider_query(request, out_descriptor, out_api, out_error);
}
