// SPDX-License-Identifier: Apache-2.0
//
// Provider descriptor construction and lifecycle dispatch. The direct-adjoint
// algorithm remains in operators/noncartesian_adjoint_reconstruct.cpp.

#include "provider_api.hpp"
#include "provider_state.hpp"
#include "operators/noncartesian_adjoint_reconstruct.hpp"

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"

namespace ksj::noncartesian_recon::api {

using namespace ksj::provider::detail;

namespace {

constexpr char kProviderId[] = "org.kspacejet.noncartesian-recon";
constexpr char kProviderBundleDigestHex[] = "6ca4098a56512026f49ee9c023d92e20b063369dcfccb610e12f5a1132db94a0";
constexpr char kErrorBadAbi[] = "Non-Cartesian reconstruction Provider received incompatible ABI storage";
constexpr char kErrorInternal[] = "Non-Cartesian reconstruction Provider trapped an unexpected internal exception";

[[nodiscard]] ksj_provider_descriptor make_provider_descriptor() noexcept {
  ksj_provider_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS | KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS |
                                      KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO | KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO);
  descriptor.provider_id = make_utf8_view(kProviderId);
  descriptor.bundle_digest = make_bundle_digest_from_hex(kProviderBundleDigestHex);
  descriptor.operator_count = 1U;
  descriptor.operators = &operators::noncartesian_adjoint_reconstruct_descriptor();
  return descriptor;
}

} // namespace

const ksj_provider_descriptor& provider_descriptor() noexcept {
  static const ksj_provider_descriptor descriptor = make_provider_descriptor();
  return descriptor;
}

ksj_status provider_query(const ksj_provider_query_request* request, ksj_provider_descriptor* out_descriptor,
                          ksj_provider_api* out_api, ksj_error_view* out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || !has_full_compatible_header(out_descriptor) ||
        !has_full_compatible_header(out_api)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    if (!has_full_compatible_header(&request->host_build_id) ||
        (request->host_build_id.size != 0U && request->host_build_id.data == nullptr)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    *out_descriptor = provider_descriptor();
    *out_api = {};
    out_api->abi = make_header(sizeof(*out_api));
    out_api->operator_create = &operators::noncartesian_adjoint_reconstruct_create;
    out_api->execution_context_create = &operators::noncartesian_adjoint_reconstruct_execution_context_create;
    out_api->key_state_init = &operators::noncartesian_adjoint_reconstruct_key_state_init;
    out_api->operator_on_start = &operators::noncartesian_adjoint_reconstruct_on_start;
    out_api->operator_process_batch = &operators::noncartesian_adjoint_reconstruct_process_batch;
    out_api->operator_on_scan_end = &operators::noncartesian_adjoint_reconstruct_on_scan_end;
    out_api->operator_on_cancel = &operators::noncartesian_adjoint_reconstruct_on_cancel;
    out_api->key_state_reset = &operators::noncartesian_adjoint_reconstruct_key_state_reset;
    out_api->execution_context_destroy = &operators::noncartesian_adjoint_reconstruct_execution_context_destroy;
    out_api->operator_destroy = &operators::noncartesian_adjoint_reconstruct_destroy;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR(
      "Non-Cartesian reconstruction Provider trapped an unexpected exception while answering provider query");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

} // namespace ksj::noncartesian_recon::api
