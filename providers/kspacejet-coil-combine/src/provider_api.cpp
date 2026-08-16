// SPDX-License-Identifier: Apache-2.0

#include "provider_api.hpp"
#include "provider_state.hpp"
#include "operators/coil_combine_rss.hpp"

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"

namespace ksj::coil_combine::api {

using namespace ksj::provider::detail;

namespace {

constexpr char kProviderId[] = "org.kspacejet.coil-combine";
constexpr char kProviderBundleDigestHex[] = "ba4f619047391419c8ffc42220ace7d888f047c9e2fd64aaeb78d9093191aed5";
constexpr char kErrorBadAbi[] = "Coil-combine Provider received incompatible ABI storage";
constexpr char kErrorInternal[] = "Coil-combine Provider trapped an unexpected internal exception";

[[nodiscard]] ksj_provider_descriptor make_provider_descriptor() noexcept {
  ksj_provider_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS | KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS |
                                      KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO | KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO);
  descriptor.provider_id = make_utf8_view(kProviderId);
  descriptor.bundle_digest = make_bundle_digest_from_hex(kProviderBundleDigestHex);
  descriptor.operator_count = 1U;
  descriptor.operators = &operators::coil_combine_rss_descriptor();
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
    out_api->operator_create = &operators::coil_combine_rss_create;
    out_api->execution_context_create = &operators::coil_combine_rss_execution_context_create;
    out_api->key_state_init = &operators::coil_combine_rss_key_state_init;
    out_api->operator_on_start = &operators::coil_combine_rss_on_start;
    out_api->operator_process_batch = &operators::coil_combine_rss_process_batch;
    out_api->operator_on_scan_end = &operators::coil_combine_rss_on_scan_end;
    out_api->operator_on_cancel = &operators::coil_combine_rss_on_cancel;
    out_api->key_state_reset = &operators::coil_combine_rss_key_state_reset;
    out_api->execution_context_destroy = &operators::coil_combine_rss_execution_context_destroy;
    out_api->operator_destroy = &operators::coil_combine_rss_destroy;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("Coil-combine Provider trapped an unexpected exception while answering provider query");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

} // namespace ksj::coil_combine::api
