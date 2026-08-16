// SPDX-License-Identifier: Apache-2.0

#include "provider_api.hpp"

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* const request,
                                                                   ksj_provider_descriptor* const out_descriptor,
                                                                   ksj_provider_api* const out_api,
                                                                   ksj_error_view* const out_error) {
  return ksj::calibration::api::provider_query(request, out_descriptor, out_api, out_error);
}
