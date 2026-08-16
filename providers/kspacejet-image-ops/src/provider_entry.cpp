// SPDX-License-Identifier: Apache-2.0
//
// This is the sole exported Provider ABI symbol. All descriptor construction,
// lifecycle dispatch, and Operator implementations remain private to the
// plugin module.

#include "provider_api.hpp"

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* request,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api* out_api,
                                                                   ksj_error_view* out_error) {
  return ksj::image_ops::api::provider_query(request, out_descriptor, out_api, out_error);
}
