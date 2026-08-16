// SPDX-License-Identifier: Apache-2.0
//
// Private lifecycle declarations for the coil_combine_rss Operator.

#pragma once

#include "kspacejet/provider/provider.h"

namespace ksj::coil_combine::operators {

[[nodiscard]] const ksj_operator_descriptor& coil_combine_rss_descriptor() noexcept;

ksj_status KSJ_PROVIDER_CALL coil_combine_rss_create(const ksj_operator_create_request* request,
                                                     ksj_provider_operator** out_operator,
                                                     ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_execution_context_create(
  ksj_provider_operator* operator_handle, const ksj_execution_context_descriptor* descriptor,
  ksj_execution_context** out_context, ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_key_state_init(ksj_provider_operator* operator_handle,
                                                             ksj_execution_context* context,
                                                             const ksj_key_state_descriptor* descriptor,
                                                             ksj_key_state** out_key_state,
                                                             ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_on_start(ksj_provider_operator* operator_handle,
                                                       ksj_execution_context* context, ksj_key_state* key_state,
                                                       const ksj_scan_start_descriptor* descriptor,
                                                       ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_process_batch(ksj_provider_operator* operator_handle,
                                                            ksj_execution_context* context, ksj_key_state* key_state,
                                                            ksj_firing_lease* lease,
                                                            const ksj_firing_lease_callbacks* callbacks,
                                                            ksj_process_result* out_result,
                                                            ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_on_scan_end(
  ksj_provider_operator* operator_handle, ksj_execution_context* context, ksj_key_state* key_state,
  const ksj_scan_end_descriptor* descriptor, ksj_firing_lease* terminal_lease,
  const ksj_firing_lease_callbacks* callbacks, ksj_process_result* out_result, ksj_error_view* out_error) noexcept;
ksj_status KSJ_PROVIDER_CALL coil_combine_rss_on_cancel(ksj_provider_operator* operator_handle,
                                                        ksj_execution_context* context, ksj_key_state* key_state,
                                                        const ksj_cancel_context* descriptor,
                                                        ksj_error_view* out_error) noexcept;
void KSJ_PROVIDER_CALL coil_combine_rss_key_state_reset(ksj_provider_operator* operator_handle,
                                                        ksj_execution_context* context,
                                                        ksj_key_state* key_state) noexcept;
void KSJ_PROVIDER_CALL coil_combine_rss_execution_context_destroy(ksj_provider_operator* operator_handle,
                                                                  ksj_execution_context* context) noexcept;
void KSJ_PROVIDER_CALL coil_combine_rss_destroy(ksj_provider_operator* operator_handle) noexcept;

} // namespace ksj::coil_combine::operators
