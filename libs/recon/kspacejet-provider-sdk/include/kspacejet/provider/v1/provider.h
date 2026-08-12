/*
 * KSpaceJet Provider ABI v1.
 *
 * This is a C ABI.  It intentionally exposes neither C++ containers nor
 * KSpaceJet C++ value/status types.  A provider is an in-process dynamic
 * library today; this header does not define a transport protocol or a
 * process-isolation mechanism.
 *
 * ABI rules (normative):
 *   - Every descriptor starts with ksj_provider_abi_header.  Callers set
 *     abi.struct_size to the storage they provide and only read fields that
 *     fit in the returned struct_size.
 *   - The ABI major must match.  A lower compatible minor and struct_size are
 *     the forward-compatibility mechanism.
 *   - UTF-8 views and error messages are borrowed.  A returned descriptor's
 *     static strings/arrays remain valid while its provider module is loaded;
 *     a callback error view remains valid only until that callback returns.
 *   - Provider-owned operator/context/key-state handles are destroyed only
 *     through the provider API table.  Firing leases, output grants,
 *     retention handles, async tokens and host allocations are host-owned.
 *   - C++ exceptions must never cross an ABI entry point.  C++ providers must
 *     implement the callbacks as noexcept boundaries.
 *   - A raw payload pointer obtained from an input, scratch, key-state or
 *     output mapping is borrowed.  It is valid only for the lifetime stated
 *     by the granting callback; retaining a raw callback pointer is a
 *     contract violation.
 */

#ifndef KSPACEJET_PROVIDER_V1_PROVIDER_H_
#define KSPACEJET_PROVIDER_V1_PROVIDER_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define KSJ_PROVIDER_CALL __cdecl
#if defined(KSJ_PROVIDER_BUILDING_PLUGIN)
#define KSJ_PROVIDER_SYMBOL_EXPORT __declspec(dllexport)
#else
#define KSJ_PROVIDER_SYMBOL_EXPORT
#endif
#elif defined(__clang__) || defined(__GNUC__)
#define KSJ_PROVIDER_CALL
#if defined(KSJ_PROVIDER_BUILDING_PLUGIN)
#define KSJ_PROVIDER_SYMBOL_EXPORT __attribute__((visibility("default")))
#else
#define KSJ_PROVIDER_SYMBOL_EXPORT
#endif
#else
#define KSJ_PROVIDER_CALL
#define KSJ_PROVIDER_SYMBOL_EXPORT
#endif

#if defined(__cplusplus)
#define KSJ_PROVIDER_EXTERN_C extern "C"
#else
#define KSJ_PROVIDER_EXTERN_C extern
#endif

#define KSJ_PROVIDER_ENTRY KSJ_PROVIDER_EXTERN_C KSJ_PROVIDER_SYMBOL_EXPORT

#define KSJ_PROVIDER_ABI_MAJOR UINT16_C(1)
#define KSJ_PROVIDER_ABI_MINOR UINT16_C(0)
#define KSJ_PROVIDER_DIGEST256_SIZE UINT32_C(32)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All ABI structs embed this as their first member.  capability_bits are
 * interpreted by the containing descriptor/callback table; they are not a
 * global, untyped feature set.
 */
typedef struct ksj_provider_abi_header {
  uint32_t struct_size;
  uint16_t abi_major;
  uint16_t abi_minor;
  uint64_t capability_bits;
  uint64_t reserved[2];
} ksj_provider_abi_header;

static inline ksj_provider_abi_header ksj_provider_abi_header_make(const uint32_t struct_size,
                                                                   const uint64_t capability_bits) {
  ksj_provider_abi_header header;
  header.struct_size = struct_size;
  header.abi_major = KSJ_PROVIDER_ABI_MAJOR;
  header.abi_minor = KSJ_PROVIDER_ABI_MINOR;
  header.capability_bits = capability_bits;
  header.reserved[0] = UINT64_C(0);
  header.reserved[1] = UINT64_C(0);
  return header;
}

/*
 * ABI enums are fixed-width integer aliases rather than C enum fields.  This
 * prevents compiler enum-size switches from changing a descriptor layout.
 */

/* A call result is intentionally a small value, not a framework C++ error object. */
typedef uint32_t ksj_status;
#define KSJ_STATUS_OK UINT32_C(0)
#define KSJ_STATUS_BAD_ABI UINT32_C(1)
#define KSJ_STATUS_INVALID_ARGUMENT UINT32_C(2)
#define KSJ_STATUS_UNSUPPORTED UINT32_C(3)
#define KSJ_STATUS_RESOURCE_EXHAUSTED UINT32_C(4)
#define KSJ_STATUS_CANCELLED UINT32_C(5)
#define KSJ_STATUS_FAILED_PRECONDITION UINT32_C(6)
#define KSJ_STATUS_INTERNAL_ERROR UINT32_C(7)
#define KSJ_STATUS_CONTRACT_VIOLATION UINT32_C(8)

typedef uint32_t ksj_provider_process_outcome;
#define KSJ_PROVIDER_PROCESS_DONE UINT32_C(0)
#define KSJ_PROVIDER_PROCESS_YIELD UINT32_C(1)
#define KSJ_PROVIDER_PROCESS_ASYNC_PENDING UINT32_C(2)
#define KSJ_PROVIDER_PROCESS_STRUCTURED_FAILURE UINT32_C(3)
#define KSJ_PROVIDER_PROCESS_CONTRACT_VIOLATION UINT32_C(4)

typedef uint32_t ksj_provider_thread_safety;
#define KSJ_PROVIDER_SERIAL_INSTANCE UINT32_C(0)
#define KSJ_PROVIDER_SERIAL_PER_KEY_REENTRANT_ACROSS_KEYS UINT32_C(1)
#define KSJ_PROVIDER_FULLY_REENTRANT UINT32_C(2)

typedef uint32_t ksj_provider_cancellation_state;
#define KSJ_PROVIDER_NOT_CANCELLED UINT32_C(0)
#define KSJ_PROVIDER_CANCELLATION_REQUESTED UINT32_C(1)
#define KSJ_PROVIDER_CANCELLATION_FORCED UINT32_C(2)

typedef uint32_t ksj_provider_scan_end_kind;
#define KSJ_PROVIDER_SCAN_END_NORMAL UINT32_C(0)
#define KSJ_PROVIDER_SCAN_END_CANCELLED UINT32_C(1)
#define KSJ_PROVIDER_SCAN_END_FAILED UINT32_C(2)

typedef uint32_t ksj_provider_memory_domain;
#define KSJ_PROVIDER_MEMORY_HOST_PAGEABLE UINT32_C(1)
#define KSJ_PROVIDER_MEMORY_HOST_PINNED UINT32_C(2)
#define KSJ_PROVIDER_MEMORY_DEVICE UINT32_C(4)
#define KSJ_PROVIDER_MEMORY_SHARED UINT32_C(8)

/*
 * Exact TypeDescriptor v1 enum encodings.
 *
 * These fields intentionally remain fixed-width integer fields in
 * ksj_type_descriptor_view.  Publish their values here rather than making
 * Providers duplicate private C++ enum ordinals.  A host must still compare
 * the complete descriptor (including descriptor_digest); matching one enum
 * value alone never establishes payload compatibility.
 */
typedef uint32_t ksj_payload_kind;
#define KSJ_PAYLOAD_KIND_BUFFER_HANDLE UINT32_C(0)
#define KSJ_PAYLOAD_KIND_MESSAGE_HANDLE UINT32_C(1)
#define KSJ_PAYLOAD_KIND_CONTROL_TOKEN UINT32_C(2)
#define KSJ_PAYLOAD_KIND_OPAQUE_HANDLE UINT32_C(3)

typedef uint32_t ksj_element_type;
#define KSJ_ELEMENT_TYPE_NONE UINT32_C(0)
#define KSJ_ELEMENT_TYPE_UINT8 UINT32_C(1)
#define KSJ_ELEMENT_TYPE_INT16 UINT32_C(2)
#define KSJ_ELEMENT_TYPE_UINT16 UINT32_C(3)
#define KSJ_ELEMENT_TYPE_INT32 UINT32_C(4)
#define KSJ_ELEMENT_TYPE_UINT32 UINT32_C(5)
#define KSJ_ELEMENT_TYPE_FLOAT32 UINT32_C(6)
#define KSJ_ELEMENT_TYPE_FLOAT64 UINT32_C(7)
#define KSJ_ELEMENT_TYPE_COMPLEX_INT16 UINT32_C(8)
#define KSJ_ELEMENT_TYPE_COMPLEX_FLOAT32 UINT32_C(9)
#define KSJ_ELEMENT_TYPE_COMPLEX_FLOAT64 UINT32_C(10)

typedef uint32_t ksj_payload_mutability;
#define KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH UINT32_C(0)
#define KSJ_PAYLOAD_MUTABILITY_MUTABLE_EXCLUSIVE UINT32_C(1)

/* Exactly one layout and exactly one stride encoding bit must be present. */
typedef uint64_t ksj_type_layout_flags;
#define KSJ_TYPE_LAYOUT_CANONICAL_CONTIGUOUS (UINT64_C(1) << 0)
#define KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS (UINT64_C(1) << 1)
#define KSJ_TYPE_LAYOUT_ROW_MAJOR_CONTIGUOUS (UINT64_C(1) << 2)
#define KSJ_TYPE_LAYOUT_COLUMN_MAJOR_CONTIGUOUS (UINT64_C(1) << 3)
#define KSJ_TYPE_LAYOUT_OPAQUE (UINT64_C(1) << 4)
#define KSJ_TYPE_STRIDES_CANONICAL (UINT64_C(1) << 16)
#define KSJ_TYPE_STRIDES_EXPLICIT_BYTE (UINT64_C(1) << 17)

/* Provider descriptor capability_bits. */
#define KSJ_PROVIDER_CAP_SYNC_PROCESS (UINT64_C(1) << 0)
#define KSJ_PROVIDER_CAP_ASYNC_PROCESS (UINT64_C(1) << 1)
#define KSJ_PROVIDER_CAP_INPUT_RETENTION (UINT64_C(1) << 2)
#define KSJ_PROVIDER_CAP_TERMINAL_OUTPUT (UINT64_C(1) << 3)
#define KSJ_PROVIDER_CAP_HOST_MEMORY_BROKER (UINT64_C(1) << 4)
#define KSJ_PROVIDER_CAP_HOST_BACKEND_EXECUTOR (UINT64_C(1) << 5)
#define KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS (UINT64_C(1) << 6)
#define KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO (UINT64_C(1) << 7)
#define KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO (UINT64_C(1) << 8)

/* Operator descriptor capability_bits. */
#define KSJ_OPERATOR_CAP_MAY_YIELD (UINT64_C(1) << 0)
#define KSJ_OPERATOR_CAP_MAY_ASYNC (UINT64_C(1) << 1)
#define KSJ_OPERATOR_CAP_MAY_RETAIN_INPUT (UINT64_C(1) << 2)
#define KSJ_OPERATOR_CAP_MAY_EMIT_TERMINAL_OUTPUT (UINT64_C(1) << 3)
#define KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION (UINT64_C(1) << 4)
#define KSJ_OPERATOR_CAP_CANCEL_NO_THROW (UINT64_C(1) << 5)

/* Firing lease callback capability_bits. */
#define KSJ_LEASE_CAP_INPUT_BATCHES (UINT64_C(1) << 0)
#define KSJ_LEASE_CAP_OUTPUT_GRANTS (UINT64_C(1) << 1)
#define KSJ_LEASE_CAP_SCRATCH (UINT64_C(1) << 2)
#define KSJ_LEASE_CAP_KEY_STATE (UINT64_C(1) << 3)
#define KSJ_LEASE_CAP_RETENTION (UINT64_C(1) << 4)
#define KSJ_LEASE_CAP_ASYNC (UINT64_C(1) << 5)
#define KSJ_LEASE_CAP_CANCELLATION (UINT64_C(1) << 6)

/* Async registration flags. */
#define KSJ_ASYNC_REGISTRATION_RETAINS_OUTPUT_GRANTS (UINT64_C(1) << 0)
#define KSJ_ASYNC_REGISTRATION_RETAINS_SCRATCH (UINT64_C(1) << 1)

/* Opaque handle ownership is specified in the header comment above. */
typedef struct ksj_provider_operator ksj_provider_operator;
typedef struct ksj_execution_context ksj_execution_context;
typedef struct ksj_key_state ksj_key_state;
typedef struct ksj_firing_lease ksj_firing_lease;
typedef struct ksj_output_grant ksj_output_grant;
typedef struct ksj_retention_handle ksj_retention_handle;
typedef struct ksj_async_token ksj_async_token;
typedef struct ksj_host_memory_allocation ksj_host_memory_allocation;
typedef struct ksj_backend_executor ksj_backend_executor;

typedef struct ksj_utf8_view {
  ksj_provider_abi_header abi;
  const char* data;
  uint64_t size;
} ksj_utf8_view;

typedef struct ksj_byte_view {
  ksj_provider_abi_header abi;
  const void* data;
  uint64_t size;
} ksj_byte_view;

typedef struct ksj_digest256 {
  ksj_provider_abi_header abi;
  uint8_t bytes[KSJ_PROVIDER_DIGEST256_SIZE];
} ksj_digest256;

/*
 * The host owns out_error storage.  message is provider-owned, borrowed UTF-8
 * and is valid only until the enclosing ABI callback returns.  Providers must
 * not require the host to call free/delete for an error.
 */
typedef struct ksj_error_view {
  ksj_provider_abi_header abi;
  ksj_status status;
  uint32_t category;
  uint64_t provider_error_code;
  ksj_utf8_view message;
} ksj_error_view;

/*
 * Exact type/layout descriptor used at the plugin boundary.  The schema and
 * dimension-name storage are borrowed under the same lifetime as the parent
 * descriptor.  A host must reject a payload if its descriptor digest, domain,
 * alignment, mutability or layout requirements do not match the frozen plan.
 */
typedef struct ksj_type_descriptor_view {
  ksj_provider_abi_header abi;
  ksj_utf8_view type_id;
  uint32_t revision;
  uint32_t payload_kind;
  ksj_digest256 payload_schema_digest;
  ksj_digest256 descriptor_digest;
  uint32_t element_type;
  uint32_t rank;
  const ksj_utf8_view* dimension_names;
  uint64_t layout_flags;
  uint64_t stride_bytes[8];
  uint32_t allowed_memory_domains;
  uint32_t minimum_alignment;
  uint32_t mutability;
  uint32_t reserved0;
  ksj_digest256 metadata_schema_digest;
} ksj_type_descriptor_view;

typedef struct ksj_provider_version {
  ksj_provider_abi_header abi;
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
  uint32_t prerelease;
} ksj_provider_version;

typedef struct ksj_operator_descriptor {
  ksj_provider_abi_header abi;
  ksj_utf8_view operator_id;
  uint32_t interface_revision;
  uint32_t max_in_flight;
  ksj_digest256 interface_digest;
  ksj_digest256 contract_digest;
  ksj_provider_thread_safety thread_safety;
  uint32_t max_private_threads;
  uint32_t max_input_items_per_firing;
  uint32_t max_output_items_per_firing;
  uint64_t max_output_bytes_per_firing;
  uint64_t max_scratch_bytes_per_firing;
  uint64_t max_retained_input_bytes;
  uint64_t max_async_tail_bytes;
} ksj_operator_descriptor;

typedef struct ksj_provider_descriptor {
  ksj_provider_abi_header abi;
  ksj_utf8_view provider_id;
  ksj_provider_version version;
  uint32_t provider_abi_major;
  uint32_t provider_abi_minor;
  ksj_digest256 bundle_digest;
  uint32_t operator_count;
  uint32_t reserved0;
  const ksj_operator_descriptor* operators;
} ksj_provider_descriptor;

typedef struct ksj_provider_query_request {
  ksj_provider_abi_header abi;
  uint16_t minimum_abi_minor;
  uint16_t maximum_abi_minor;
  uint32_t reserved0;
  uint64_t host_capability_bits;
  ksj_utf8_view host_build_id;
} ksj_provider_query_request;

typedef struct ksj_host_allocation_request {
  ksj_provider_abi_header abi;
  uint64_t byte_count;
  uint32_t alignment;
  uint32_t memory_domain;
  uint64_t resource_occurrence_id;
  uint64_t allocation_flags;
} ksj_host_allocation_request;

typedef struct ksj_mutable_payload_view {
  ksj_provider_abi_header abi;
  void* data;
  uint64_t capacity_bytes;
  uint64_t committed_bytes;
  uint32_t memory_domain;
  uint32_t alignment;
  ksj_type_descriptor_view type;
} ksj_mutable_payload_view;

typedef struct ksj_host_services_v1 ksj_host_services_v1;

typedef ksj_status(KSJ_PROVIDER_CALL* ksj_host_allocate_fn)(void* host_context,
                                                            const ksj_host_allocation_request* request,
                                                            ksj_host_memory_allocation** out_allocation,
                                                            ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_host_map_allocation_fn)(void* host_context,
                                                                  ksj_host_memory_allocation* allocation,
                                                                  ksj_mutable_payload_view* out_view,
                                                                  ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_host_release_allocation_fn)(void* host_context,
                                                                      ksj_host_memory_allocation* allocation,
                                                                      ksj_error_view* out_error);

/*
 * Host services are supplied at operator creation.  They are the sanctioned
 * route for accounted long-lived allocation.  A strict profile may reject a
 * provider that needs unsanctioned allocation, threads, device streams or I/O.
 */
struct ksj_host_services_v1 {
  ksj_provider_abi_header abi;
  void* host_context;
  ksj_host_allocate_fn allocate;
  ksj_host_map_allocation_fn map_allocation;
  ksj_host_release_allocation_fn release_allocation;
  ksj_backend_executor* backend_executor;
};

typedef struct ksj_operator_create_request {
  ksj_provider_abi_header abi;
  ksj_utf8_view operator_id;
  ksj_digest256 required_contract_digest;
  ksj_byte_view canonical_config;
  const ksj_host_services_v1* host_services;
} ksj_operator_create_request;

typedef struct ksj_execution_context_descriptor {
  ksj_provider_abi_header abi;
  uint32_t numa_node;
  uint32_t device_ordinal;
  uint64_t execution_context_id;
  uint64_t resource_domain_id;
  uint64_t max_backend_concurrency;
  const ksj_host_services_v1* host_services;
} ksj_execution_context_descriptor;

typedef struct ksj_key_state_descriptor {
  ksj_provider_abi_header abi;
  ksj_byte_view semantic_key;
  uint64_t placement_key;
  uint64_t key_state_generation;
  uint64_t home_shard;
} ksj_key_state_descriptor;

typedef struct ksj_scan_start_descriptor {
  ksj_provider_abi_header abi;
  ksj_utf8_view run_id;
  ksj_utf8_view scan_id;
  ksj_digest256 normalized_scan_facts_digest;
  ksj_digest256 execution_plan_digest;
  uint64_t terminal_epoch;
} ksj_scan_start_descriptor;

typedef struct ksj_payload_view {
  ksj_provider_abi_header abi;
  const void* data;
  uint64_t byte_count;
  uint32_t memory_domain;
  uint32_t alignment;
  ksj_type_descriptor_view type;
} ksj_payload_view;

typedef struct ksj_input_item_view {
  ksj_provider_abi_header abi;
  ksj_payload_view payload;
  ksj_byte_view metadata;
  uint64_t semantic_key_hash;
  uint64_t order_key;
  uint64_t item_ordinal;
} ksj_input_item_view;

typedef struct ksj_input_batch_view {
  ksj_provider_abi_header abi;
  const ksj_input_item_view* items;
  uint32_t item_count;
  uint32_t input_port;
  uint64_t batch_id;
  uint64_t order_domain;
} ksj_input_batch_view;

typedef struct ksj_scratch_view {
  ksj_provider_abi_header abi;
  void* data;
  uint64_t byte_count;
  uint32_t memory_domain;
  uint32_t alignment;
  uint64_t resource_occurrence_id;
} ksj_scratch_view;

typedef struct ksj_key_state_view {
  ksj_provider_abi_header abi;
  void* data;
  uint64_t byte_count;
  uint64_t key_state_generation;
  uint64_t slot_generation;
} ksj_key_state_view;

typedef struct ksj_firing_lease_info {
  ksj_provider_abi_header abi;
  uint64_t resource_occurrence_id;
  uint64_t slot_generation;
  uint64_t terminal_epoch;
  uint32_t input_batch_count;
  uint32_t output_grant_count;
  uint64_t reserved_output_bytes;
  uint64_t reserved_scratch_bytes;
} ksj_firing_lease_info;

typedef struct ksj_retention_request {
  ksj_provider_abi_header abi;
  uint64_t requested_bytes;
  uint64_t retention_class;
  uint64_t maximum_lifetime_epoch;
} ksj_retention_request;

typedef struct ksj_async_registration {
  ksj_provider_abi_header abi;
  uint64_t resource_occurrence_id;
  uint64_t slot_generation;
  uint64_t terminal_epoch;
  uint64_t reserved_output_bytes;
  uint64_t reserved_scratch_bytes;
  uint64_t completion_deadline_ns;
  uint32_t maximum_completion_callbacks;
  uint32_t reserved0;
} ksj_async_registration;

typedef struct ksj_async_completion {
  ksj_provider_abi_header abi;
  ksj_provider_process_outcome outcome;
  uint32_t sealed_output_count;
  uint64_t completion_sequence;
  uint64_t terminal_epoch;
} ksj_async_completion;

typedef struct ksj_cancellation_view {
  ksj_provider_abi_header abi;
  ksj_provider_cancellation_state state;
  uint32_t reserved0;
  uint64_t terminal_epoch;
  uint64_t cancellation_generation;
} ksj_cancellation_view;

typedef struct ksj_output_seal_descriptor {
  ksj_provider_abi_header abi;
  uint32_t output_port;
  uint32_t produced_item_count;
  uint64_t produced_byte_count;
  uint64_t semantic_key_hash;
  uint64_t order_key;
  ksj_type_descriptor_view type;
  ksj_byte_view metadata;
} ksj_output_seal_descriptor;

typedef struct ksj_output_grant_callbacks_v1 ksj_output_grant_callbacks_v1;

typedef ksj_status(KSJ_PROVIDER_CALL* ksj_output_grant_map_fn)(void* host_context, ksj_output_grant* grant,
                                                               ksj_mutable_payload_view* out_payload,
                                                               ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_output_grant_seal_fn)(void* host_context, ksj_output_grant* grant,
                                                                const ksj_output_seal_descriptor* descriptor,
                                                                ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_output_grant_release_fn)(void* host_context, ksj_output_grant* grant,
                                                                   ksj_error_view* out_error);

/*
 * An output grant cannot publish directly.  The provider maps it, fills its
 * bounded capacity, seals it once, and returns control to the host; the host
 * validates type/layout/item/byte bounds and atomically commits fan-out.
 */
struct ksj_output_grant_callbacks_v1 {
  ksj_provider_abi_header abi;
  void* host_context;
  ksj_output_grant_map_fn map_mutable_payload;
  ksj_output_grant_seal_fn seal;
  ksj_output_grant_release_fn release;
};

typedef struct ksj_firing_lease_callbacks_v1 ksj_firing_lease_callbacks_v1;

typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_get_info_fn)(void* host_context, const ksj_firing_lease* lease,
                                                             ksj_firing_lease_info* out_info,
                                                             ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_get_input_batch_fn)(void* host_context, const ksj_firing_lease* lease,
                                                                    uint32_t batch_index,
                                                                    ksj_input_batch_view* out_batch,
                                                                    ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_get_scratch_fn)(void* host_context, const ksj_firing_lease* lease,
                                                                ksj_scratch_view* out_scratch,
                                                                ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_get_key_state_fn)(void* host_context, const ksj_firing_lease* lease,
                                                                  ksj_key_state* key_state,
                                                                  ksj_key_state_view* out_key_state,
                                                                  ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_acquire_output_fn)(void* host_context, ksj_firing_lease* lease,
                                                                   uint32_t output_slot, ksj_output_grant** out_grant,
                                                                   ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_retain_input_fn)(void* host_context, ksj_firing_lease* lease,
                                                                 uint32_t batch_index, uint32_t item_index,
                                                                 const ksj_retention_request* request,
                                                                 ksj_retention_handle** out_retention,
                                                                 ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_release_retention_fn)(void* host_context,
                                                                      ksj_retention_handle* retention,
                                                                      ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_register_async_fn)(void* host_context, ksj_firing_lease* lease,
                                                                   const ksj_async_registration* registration,
                                                                   ksj_async_token** out_token,
                                                                   ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_complete_async_fn)(void* host_context, ksj_async_token* token,
                                                                   const ksj_async_completion* completion,
                                                                   ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_release_async_fn)(void* host_context, ksj_async_token* token,
                                                                  ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_lease_get_cancellation_fn)(void* host_context, const ksj_firing_lease* lease,
                                                                     ksj_cancellation_view* out_cancellation,
                                                                     ksj_error_view* out_error);

/*
 * This callback table is host-owned and valid only during process/on_scan_end,
 * except that an async token explicitly retains the narrow state it names.
 * Retention and async registration are bounded host decisions: a provider has
 * no ABI route to keep arbitrary input, scratch, output, or callback pointers.
 */
struct ksj_firing_lease_callbacks_v1 {
  ksj_provider_abi_header abi;
  void* host_context;
  const ksj_output_grant_callbacks_v1* output_grants;
  ksj_lease_get_info_fn get_info;
  ksj_lease_get_input_batch_fn get_input_batch;
  ksj_lease_get_scratch_fn get_scratch;
  ksj_lease_get_key_state_fn get_key_state;
  ksj_lease_acquire_output_fn acquire_output_grant;
  ksj_lease_retain_input_fn retain_input;
  ksj_lease_release_retention_fn release_retention;
  ksj_lease_register_async_fn register_async;
  ksj_lease_complete_async_fn complete_async;
  ksj_lease_release_async_fn release_async;
  ksj_lease_get_cancellation_fn get_cancellation;
};

typedef struct ksj_process_result {
  ksj_provider_abi_header abi;
  ksj_provider_process_outcome outcome;
  uint32_t sealed_output_count;
  uint64_t consumed_input_item_count;
  uint64_t terminal_epoch;
  ksj_async_token* async_token;
} ksj_process_result;

typedef struct ksj_scan_end_descriptor {
  ksj_provider_abi_header abi;
  ksj_provider_scan_end_kind kind;
  uint32_t reserved0;
  uint64_t terminal_epoch;
  uint64_t completed_input_item_count;
} ksj_scan_end_descriptor;

typedef struct ksj_cancel_context {
  ksj_provider_abi_header abi;
  ksj_provider_scan_end_kind kind;
  uint32_t reserved0;
  uint64_t terminal_epoch;
  uint64_t cancellation_generation;
  ksj_utf8_view reason;
} ksj_cancel_context;

typedef ksj_status(KSJ_PROVIDER_CALL* ksj_operator_create_fn)(const ksj_operator_create_request* request,
                                                              ksj_provider_operator** out_operator,
                                                              ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_execution_context_create_fn)(
  ksj_provider_operator* operator_handle, const ksj_execution_context_descriptor* descriptor,
  ksj_execution_context** out_context, ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_key_state_init_fn)(ksj_provider_operator* operator_handle,
                                                             ksj_execution_context* context,
                                                             const ksj_key_state_descriptor* descriptor,
                                                             ksj_key_state** out_key_state, ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_operator_on_start_fn)(ksj_provider_operator* operator_handle,
                                                                ksj_execution_context* context,
                                                                ksj_key_state* key_state,
                                                                const ksj_scan_start_descriptor* descriptor,
                                                                ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_operator_process_batch_fn)(
  ksj_provider_operator* operator_handle, ksj_execution_context* context, ksj_key_state* key_state,
  ksj_firing_lease* lease, const ksj_firing_lease_callbacks_v1* lease_callbacks, ksj_process_result* out_result,
  ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_operator_on_scan_end_fn)(
  ksj_provider_operator* operator_handle, ksj_execution_context* context, ksj_key_state* key_state,
  const ksj_scan_end_descriptor* descriptor, ksj_firing_lease* terminal_lease,
  const ksj_firing_lease_callbacks_v1* lease_callbacks, ksj_process_result* out_result, ksj_error_view* out_error);
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_operator_on_cancel_fn)(ksj_provider_operator* operator_handle,
                                                                 ksj_execution_context* context,
                                                                 ksj_key_state* key_state,
                                                                 const ksj_cancel_context* context_descriptor,
                                                                 ksj_error_view* out_error);
typedef void(KSJ_PROVIDER_CALL* ksj_key_state_reset_fn)(ksj_provider_operator* operator_handle,
                                                        ksj_execution_context* context, ksj_key_state* key_state);
typedef void(KSJ_PROVIDER_CALL* ksj_execution_context_destroy_fn)(ksj_provider_operator* operator_handle,
                                                                  ksj_execution_context* context);
typedef void(KSJ_PROVIDER_CALL* ksj_operator_destroy_fn)(ksj_provider_operator* operator_handle);

/*
 * Provider-side lifecycle table returned by ksj_provider_query.  A provider
 * must leave an unavailable optional capability callback null and clear the
 * corresponding descriptor capability bit.  The host never invents a call
 * that was not made admissible by the resolved OperatorContract.
 */
typedef struct ksj_provider_api_v1 {
  ksj_provider_abi_header abi;
  ksj_operator_create_fn operator_create;
  ksj_execution_context_create_fn execution_context_create;
  ksj_key_state_init_fn key_state_init;
  ksj_operator_on_start_fn operator_on_start;
  ksj_operator_process_batch_fn operator_process_batch;
  ksj_operator_on_scan_end_fn operator_on_scan_end;
  ksj_operator_on_cancel_fn operator_on_cancel;
  ksj_key_state_reset_fn key_state_reset;
  ksj_execution_context_destroy_fn execution_context_destroy;
  ksj_operator_destroy_fn operator_destroy;
} ksj_provider_api_v1;

/*
 * Dynamic-provider entry symbol: "ksj_provider_query".  The provider fills
 * caller-owned output storage; the descriptor's immutable arrays/strings are
 * provider-owned and remain valid while the module is loaded.  A host must not
 * unload a provider module while any of its handles, callbacks or async tokens
 * can still exist.
 */
typedef ksj_status(KSJ_PROVIDER_CALL* ksj_provider_query_fn)(const ksj_provider_query_request* request,
                                                             ksj_provider_descriptor* out_descriptor,
                                                             ksj_provider_api_v1* out_api, ksj_error_view* out_error);

KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* request,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api_v1* out_api,
                                                                   ksj_error_view* out_error);

/*
 * Terminal protocol (normative):
 *   1. on_scan_end is called only for a normal drain and receives a normal
 *      terminal lease.  Its outputs still use OutputGrant -> seal -> host
 *      validation/commit.
 *   2. on_cancel is called for cancellation/failure only.  It receives no
 *      firing lease, no output-grant table and no ordinary-data publication
 *      capability.  It must only stop/release provider work; emitting MRI data
 *      from on_cancel is a contract violation.
 *   3. A process result of YIELD is valid only if the provider consumed no
 *      input, modified no persistent key state, registered no async work and
 *      sealed no output.  The host otherwise fails the occurrence/scan.
 *   4. Once a provider callback has run, StructuredFailure or
 *      ContractViolation is not retried as an input rollback.  The host aborts
 *      the scan and releases uncommitted grants.
 *   5. AsyncPending is valid only after a successful bounded register_async.
 *      complete_async or release_async consumes that token exactly once;
 *      stale generation or terminal epoch is a contract violation.
 */

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KSPACEJET_PROVIDER_V1_PROVIDER_H_
