#pragma once

#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/result.hpp"
#include "kspacejet/recon/run_record.hpp"

#include <string>
#include <string_view>

namespace ksj::recon {

// The AdmissionRecord and RunRecord schemas permit this decoration for JSON
// Schema tooling.  It is accepted by the parsers below, but deliberately
// omitted by the canonical serializers: it is not part of either record's
// semantic payload.
inline constexpr std::string_view kJsonSchemaDraft202012 = "https://json-schema.org/draft/2020-12/schema";

// Serialize the complete immutable AdmissionRecord payload in the v1
// canonical JSON domain.  The record has no self-digest field; its plan and
// verification digests remain ordinary detached artifact references.
[[nodiscard]] Result<std::string> serialize_admission_record_canonical_json(const AdmissionRecord& record);

// Strictly parse the public AdmissionRecord v1 JSON form.  Parsing rejects
// duplicate decoded keys, floats, values outside the exact v1 integer range,
// unknown members, and schema-invalid shapes before forwarding the resulting
// value to AdmissionRecord::create().  This control-plane boundary is also
// bounded to a 1 MiB document, 16 container levels, 4,096 array elements, 64
// object members, and 16 KiB UTF-8 strings; the serializer enforces the same
// limits so a successful serialization always remains parseable here.
[[nodiscard]] Result<AdmissionRecord> parse_admission_record_json(std::string_view document);

// Serialize the complete immutable RunRecord payload in the v1 canonical JSON
// domain.  The plan, verification, and admission record identities are
// detached references; no record self-digest is synthesized or emitted.
[[nodiscard]] Result<std::string> serialize_run_record_canonical_json(const RunRecord& record);

// Strictly parse the public RunRecord v1 JSON form with the same canonical
// JSON safety guarantees as parse_admission_record_json().
[[nodiscard]] Result<RunRecord> parse_run_record_json(std::string_view document);

} // namespace ksj::recon
