#!/usr/bin/env python3
"""Generate the public KSpaceJet type-registry convenience headers.

The registry deliberately keeps human-readable payload and metadata semantics
next to a compact, exact structural descriptor.  A type identity is the
SHA-256 digest of the structural descriptor only, prefixed by the fixed
domain-separation string below.  Changing prose never changes an existing
identity: a semantic change must allocate a distinct TypeRef instead.

This tool intentionally uses only the Python standard library so it is usable
from the repository developer environment and from a minimal release build.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, NoReturn


IDENTITY_DOMAIN = "kspacejet.type-identity"
IDENTITY_PREFIX = (IDENTITY_DOMAIN + "\0").encode("ascii")
REGISTRY_KIND = "TypeRegistry"
MAXIMUM_C_ABI_RANK = 8
SEMANTIC_CHANGE_POLICY = (
    "A semantic change requires a distinct type_ref. Semantic documentation never mutates an existing type identity."
)
EXPECTED_TYPE_REFS = (
    "ksj.kspace-frame",
    "ksj.noise-calibration-frame",
    "ksj.noise-model",
    "ksj.phase-reference-frame",
    "ksj.phase-model",
    "ksj.coil-compression-basis",
    "ksj.coil-image-frame",
    "ksj.complex-image-frame",
    "ksj.sensitivity-map",
    "ksj.noncartesian-kspace-frame",
    "ksj.trajectory-frame",
    "ksj.image-frame",
    "ksj.control-message",
    "ismrmrd.acquisition",
    "ismrmrd.waveform",
    "ismrmrd.image",
)
IDENTIFIER_PATTERN = re.compile(r"^[a-z](?:[a-z0-9._-]*[a-z0-9])?$")

PAYLOAD_KINDS = {
    "buffer_handle": "PayloadKind::buffer_handle",
    "message_handle": "PayloadKind::message_handle",
    "control_token": "PayloadKind::control_token",
    "opaque_handle": "PayloadKind::opaque_handle",
}
ELEMENT_TYPES = {
    "none": "ElementType::none",
    "uint8": "ElementType::uint8",
    "int16": "ElementType::int16",
    "uint16": "ElementType::uint16",
    "int32": "ElementType::int32",
    "uint32": "ElementType::uint32",
    "float32": "ElementType::float32",
    "float64": "ElementType::float64",
    "complex_int16": "ElementType::complex_int16",
    "complex_float32": "ElementType::complex_float32",
    "complex_float64": "ElementType::complex_float64",
}
LAYOUTS = {
    "canonical_contiguous": "LayoutKind::canonical_contiguous",
    "channel_major_contiguous": "LayoutKind::channel_major_contiguous",
    "row_major_contiguous": "LayoutKind::row_major_contiguous",
    "column_major_contiguous": "LayoutKind::column_major_contiguous",
    "opaque": "LayoutKind::opaque",
}
STRIDES = {
    "canonical": "StrideKind::canonical",
    "explicit_byte_strides": "StrideKind::explicit_byte_strides",
}
MUTABILITY = {
    "immutable_after_publish": "PayloadMutability::immutable_after_publish",
    "mutable_exclusive": "PayloadMutability::mutable_exclusive",
}
OPAQUE_MESSAGE_TYPE_REFS = {
    "ksj.control-message",
    "ismrmrd.acquisition",
    "ismrmrd.waveform",
    "ismrmrd.image",
}
MEMORY_DOMAINS = {
    "host_normal": "TypeMemoryDomain::host_normal",
    "host_pinned": "TypeMemoryDomain::host_pinned",
    "host_hugepage": "TypeMemoryDomain::host_hugepage",
    "shared_host": "TypeMemoryDomain::shared_host",
    "cuda_device": "TypeMemoryDomain::cuda_device",
}
MEMORY_DOMAIN_ORDER = tuple(MEMORY_DOMAINS)

C_PAYLOAD_KINDS = {
    "buffer_handle": "KSJ_PAYLOAD_KIND_BUFFER_HANDLE",
    "message_handle": "KSJ_PAYLOAD_KIND_MESSAGE_HANDLE",
    "control_token": "KSJ_PAYLOAD_KIND_CONTROL_TOKEN",
    "opaque_handle": "KSJ_PAYLOAD_KIND_OPAQUE_HANDLE",
}
C_ELEMENT_TYPES = {
    "none": "KSJ_ELEMENT_TYPE_NONE",
    "uint8": "KSJ_ELEMENT_TYPE_UINT8",
    "int16": "KSJ_ELEMENT_TYPE_INT16",
    "uint16": "KSJ_ELEMENT_TYPE_UINT16",
    "int32": "KSJ_ELEMENT_TYPE_INT32",
    "uint32": "KSJ_ELEMENT_TYPE_UINT32",
    "float32": "KSJ_ELEMENT_TYPE_FLOAT32",
    "float64": "KSJ_ELEMENT_TYPE_FLOAT64",
    "complex_int16": "KSJ_ELEMENT_TYPE_COMPLEX_INT16",
    "complex_float32": "KSJ_ELEMENT_TYPE_COMPLEX_FLOAT32",
    "complex_float64": "KSJ_ELEMENT_TYPE_COMPLEX_FLOAT64",
}
C_LAYOUTS = {
    "canonical_contiguous": "KSJ_TYPE_LAYOUT_CANONICAL_CONTIGUOUS",
    "channel_major_contiguous": "KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS",
    "row_major_contiguous": "KSJ_TYPE_LAYOUT_ROW_MAJOR_CONTIGUOUS",
    "column_major_contiguous": "KSJ_TYPE_LAYOUT_COLUMN_MAJOR_CONTIGUOUS",
    "opaque": "KSJ_TYPE_LAYOUT_OPAQUE",
}
C_STRIDES = {
    "canonical": "KSJ_TYPE_STRIDES_CANONICAL",
    "explicit_byte_strides": "KSJ_TYPE_STRIDES_EXPLICIT_BYTE",
}
C_MUTABILITY = {
    "immutable_after_publish": "KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH",
    "mutable_exclusive": "KSJ_PAYLOAD_MUTABILITY_MUTABLE_EXCLUSIVE",
}
C_MEMORY_DOMAINS = {
    "host_normal": "KSJ_PROVIDER_MEMORY_HOST_PAGEABLE",
    "host_pinned": "KSJ_PROVIDER_MEMORY_HOST_PINNED",
    "shared_host": "KSJ_PROVIDER_MEMORY_SHARED",
    "cuda_device": "KSJ_PROVIDER_MEMORY_DEVICE",
}


class RegistryError(ValueError):
    """A registry source or generated-output validation failure."""


def fail(location: str, message: str) -> NoReturn:
    raise RegistryError(f"{location}: {message}")


def object_without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RegistryError(f"JSON object contains duplicate key {key!r}.")
        result[key] = value
    return result


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=object_without_duplicate_keys)
    except FileNotFoundError:
        fail(path.as_posix(), "file does not exist")
    except OSError as error:
        fail(path.as_posix(), f"cannot read file: {error}")
    except json.JSONDecodeError as error:
        fail(path.as_posix(), f"invalid JSON at line {error.lineno}, column {error.colno}: {error.msg}")
    raise AssertionError("unreachable")


def require_object(value: Any, location: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(location, "must be a JSON object")
    return value


def require_list(value: Any, location: str) -> list[Any]:
    if not isinstance(value, list):
        fail(location, "must be a JSON array")
    return value


def require_string(value: Any, location: str) -> str:
    if not isinstance(value, str):
        fail(location, "must be a string")
    return value


def require_integer(value: Any, location: str, *, minimum: int = 0) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        fail(location, f"must be an integer greater than or equal to {minimum}")
    return value


def require_exact_keys(value: dict[str, Any], expected: set[str], location: str) -> None:
    actual = set(value)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing:
        fail(location, f"is missing required field(s): {', '.join(missing)}")
    if unexpected:
        fail(location, f"contains unknown field(s): {', '.join(unexpected)}")


def require_identifier(value: Any, location: str) -> str:
    text = require_string(value, location)
    if not IDENTIFIER_PATTERN.fullmatch(text):
        fail(location, "must be a lower-case identifier containing only [a-z0-9._-]")
    return text


def parse_type_ref(value: Any, location: str) -> str:
    type_ref = require_string(value, location)
    if not IDENTIFIER_PATTERN.fullmatch(type_ref):
        fail(location, "must be a canonical lower-case identifier containing only [a-z0-9._-]")
    return type_ref


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def identity_digest(structural_descriptor: dict[str, Any]) -> str:
    payload = canonical_json(structural_descriptor).encode("utf-8")
    return "sha256:" + hashlib.sha256(IDENTITY_PREFIX + payload).hexdigest()


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def symbol_name(type_ref: str) -> str:
    local_type_id = type_ref.removeprefix("ksj.")
    symbol = re.sub(r"[^a-z0-9]+", "_", local_type_id.lower()).strip("_")
    if not symbol:
        raise AssertionError("validated type_ref produced no C/C++ symbol")
    return symbol


def pascal_name(symbol: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in symbol.split("_") if part)


@dataclass(frozen=True)
class RegisteredType:
    type_ref: str
    symbol: str
    pascal: str
    summary: str
    payload_semantics: dict[str, Any]
    metadata_semantics: dict[str, Any]
    structural_descriptor: dict[str, Any]
    digest: str

    @property
    def payload(self) -> dict[str, Any]:
        return {
            "kind": self.structural_descriptor["payload_kind"],
            "element_type": self.structural_descriptor["element_type"],
            "rank": self.structural_descriptor["rank"],
            "dimensions": self.structural_descriptor["dimensions"],
            "layout": self.structural_descriptor["layout"],
            "strides": self.structural_descriptor["strides"],
            "explicit_byte_strides": self.structural_descriptor["explicit_byte_strides"],
        }

    @property
    def memory(self) -> dict[str, Any]:
        return {
            "allowed_domains": self.structural_descriptor["allowed_memory_domains"],
            "min_alignment_bytes": self.structural_descriptor["min_alignment_bytes"],
        }

    @property
    def mutability(self) -> str:
        return self.structural_descriptor["mutability"]


def validate_payload(payload_value: Any, location: str) -> dict[str, Any]:
    payload = require_object(payload_value, location)
    require_exact_keys(
        payload,
        {
            "kind",
            "element_type",
            "rank",
            "dimensions",
            "layout",
            "strides",
            "explicit_byte_strides",
        },
        location,
    )
    kind = require_string(payload["kind"], f"{location}.kind")
    if kind not in PAYLOAD_KINDS:
        fail(f"{location}.kind", f"must be one of: {', '.join(PAYLOAD_KINDS)}")
    element_type = require_string(payload["element_type"], f"{location}.element_type")
    if element_type not in ELEMENT_TYPES:
        fail(f"{location}.element_type", f"must be one of: {', '.join(ELEMENT_TYPES)}")
    rank = require_integer(payload["rank"], f"{location}.rank")
    if rank > MAXIMUM_C_ABI_RANK:
        fail(f"{location}.rank", f"must not exceed Provider ABI maximum rank {MAXIMUM_C_ABI_RANK}")
    dimensions = require_list(payload["dimensions"], f"{location}.dimensions")
    if len(dimensions) != rank:
        fail(f"{location}.dimensions", "count must equal rank")
    validated_dimensions = [
        require_identifier(dimension, f"{location}.dimensions[{index}]")
        for index, dimension in enumerate(dimensions)
    ]
    if len(set(validated_dimensions)) != len(validated_dimensions):
        fail(f"{location}.dimensions", "must not contain duplicate names")
    layout = require_string(payload["layout"], f"{location}.layout")
    if layout not in LAYOUTS:
        fail(f"{location}.layout", f"must be one of: {', '.join(LAYOUTS)}")
    strides = require_string(payload["strides"], f"{location}.strides")
    if strides not in STRIDES:
        fail(f"{location}.strides", f"must be one of: {', '.join(STRIDES)}")
    explicit_byte_strides = require_list(payload["explicit_byte_strides"], f"{location}.explicit_byte_strides")
    validated_strides = [
        require_integer(stride, f"{location}.explicit_byte_strides[{index}]", minimum=1)
        for index, stride in enumerate(explicit_byte_strides)
    ]
    if strides == "canonical" and validated_strides:
        fail(f"{location}.explicit_byte_strides", "must be empty for canonical strides")
    if strides == "explicit_byte_strides" and (rank == 0 or len(validated_strides) != rank):
        fail(f"{location}.explicit_byte_strides", "must contain one positive stride for each dimension")
    if kind == "buffer_handle" and element_type == "none":
        fail(f"{location}.element_type", "must not be none for buffer_handle payloads")
    if kind == "control_token" and element_type != "none":
        fail(f"{location}.element_type", "must be none for control_token payloads")
    return {
        "kind": kind,
        "element_type": element_type,
        "rank": rank,
        "dimensions": validated_dimensions,
        "layout": layout,
        "strides": strides,
        "explicit_byte_strides": validated_strides,
    }


def validate_memory(memory_value: Any, location: str) -> dict[str, Any]:
    memory = require_object(memory_value, location)
    require_exact_keys(memory, {"allowed_domains", "min_alignment_bytes"}, location)
    domains = require_list(memory["allowed_domains"], f"{location}.allowed_domains")
    if not domains:
        fail(f"{location}.allowed_domains", "must not be empty")
    validated_domains: list[str] = []
    for index, domain_value in enumerate(domains):
        domain = require_string(domain_value, f"{location}.allowed_domains[{index}]")
        if domain not in MEMORY_DOMAINS:
            fail(f"{location}.allowed_domains[{index}]", f"must be one of: {', '.join(MEMORY_DOMAINS)}")
        validated_domains.append(domain)
    if len(set(validated_domains)) != len(validated_domains):
        fail(f"{location}.allowed_domains", "must not contain duplicates")
    canonical_domains = sorted(validated_domains, key=MEMORY_DOMAIN_ORDER.index)
    if validated_domains != canonical_domains:
        fail(f"{location}.allowed_domains", "must use canonical TypeMemoryDomain order")
    alignment = require_integer(memory["min_alignment_bytes"], f"{location}.min_alignment_bytes", minimum=1)
    if alignment & (alignment - 1):
        fail(f"{location}.min_alignment_bytes", "must be a power of two")
    return {"allowed_domains": canonical_domains, "min_alignment_bytes": alignment}


def validate_payload_semantics(value: Any, dimensions: list[str], location: str) -> dict[str, Any]:
    semantics = require_object(value, location)
    allowed = {"payload_role", "numeric_encoding", "dimension_meanings", "completion", "intensity", "message_contract"}
    required = {"payload_role", "numeric_encoding", "dimension_meanings"}
    missing = sorted(required - set(semantics))
    if missing:
        fail(location, f"is missing required field(s): {', '.join(missing)}")
    unexpected = sorted(set(semantics) - allowed)
    if unexpected:
        fail(location, f"contains unknown field(s): {', '.join(unexpected)}")
    payload_role = require_identifier(semantics["payload_role"], f"{location}.payload_role")
    numeric_encoding = require_identifier(semantics["numeric_encoding"], f"{location}.numeric_encoding")
    dimension_meanings = require_object(semantics["dimension_meanings"], f"{location}.dimension_meanings")
    if set(dimension_meanings) != set(dimensions):
        fail(f"{location}.dimension_meanings", "must define exactly the declared payload dimensions")
    validated_dimension_meanings = {
        dimension: require_identifier(dimension_meanings[dimension], f"{location}.dimension_meanings.{dimension}")
        for dimension in dimensions
    }
    result: dict[str, Any] = {
        "payload_role": payload_role,
        "numeric_encoding": numeric_encoding,
        "dimension_meanings": validated_dimension_meanings,
    }
    for optional_field in ("completion", "intensity", "message_contract"):
        if optional_field in semantics:
            result[optional_field] = require_string(semantics[optional_field], f"{location}.{optional_field}")
            if not result[optional_field]:
                fail(f"{location}.{optional_field}", "must not be empty")
    return result


def validate_metadata_semantics(value: Any, location: str) -> dict[str, Any]:
    semantics = require_object(value, location)
    require_exact_keys(semantics, {"kind", "meaning"}, location)
    kind = require_string(semantics["kind"], f"{location}.kind")
    if kind != "none":
        fail(f"{location}.kind", "the registry currently supports only kind 'none'")
    meaning = require_string(semantics["meaning"], f"{location}.meaning")
    if not meaning:
        fail(f"{location}.meaning", "must not be empty")
    return {"kind": kind, "meaning": meaning}


def validate_registry(registry: Any, registry_path: Path, schema_path: Path) -> list[RegisteredType]:
    schema = require_object(load_json(schema_path), schema_path.as_posix())
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        fail(schema_path.as_posix(), "must declare the draft 2020-12 meta-schema")
    if schema.get("$id") != "https://schemas.kspacejet.org/type-registry.schema.json":
        fail(schema_path.as_posix(), "must declare the canonical type-registry schema id")

    root = require_object(registry, registry_path.as_posix())
    require_exact_keys(
        root,
        {"$schema", "kind", "identity_hash_domain", "semantic_change_policy", "types"},
        registry_path.as_posix(),
    )
    if root["$schema"] != "https://json-schema.org/draft/2020-12/schema":
        fail(f"{registry_path}.$schema", "must declare the draft 2020-12 meta-schema")
    if root["kind"] != REGISTRY_KIND:
        fail(f"{registry_path}.kind", f"must be {REGISTRY_KIND!r}")
    if root["identity_hash_domain"] != IDENTITY_DOMAIN:
        fail(f"{registry_path}.identity_hash_domain", f"must be {IDENTITY_DOMAIN!r}")
    if root["semantic_change_policy"] != SEMANTIC_CHANGE_POLICY:
        fail(f"{registry_path}.semantic_change_policy", "must state the immutable type-identity policy")

    raw_types = require_list(root["types"], f"{registry_path}.types")
    if not raw_types:
        fail(f"{registry_path}.types", "must contain at least one executable type")
    result: list[RegisteredType] = []
    seen_refs: set[str] = set()
    seen_symbols: set[str] = set()
    for index, raw_entry in enumerate(raw_types):
        location = f"{registry_path}.types[{index}]"
        entry = require_object(raw_entry, location)
        require_exact_keys(
            entry,
            {"type_ref", "summary", "identity", "payload_semantics", "metadata_semantics"},
            location,
        )
        type_ref = parse_type_ref(entry["type_ref"], f"{location}.type_ref")
        if type_ref in seen_refs:
            fail(f"{location}.type_ref", f"duplicates type_ref {type_ref!r}")
        seen_refs.add(type_ref)
        summary = require_string(entry["summary"], f"{location}.summary")
        if not summary:
            fail(f"{location}.summary", "must not be empty")

        identity = require_object(entry["identity"], f"{location}.identity")
        require_exact_keys(identity, {"payload", "memory", "mutability"}, f"{location}.identity")
        payload = validate_payload(identity["payload"], f"{location}.identity.payload")
        memory = validate_memory(identity["memory"], f"{location}.identity.memory")
        unsupported_c_domains = [domain for domain in memory["allowed_domains"] if domain not in C_MEMORY_DOMAINS]
        if unsupported_c_domains:
            fail(
                f"{location}.identity.memory.allowed_domains",
                "cannot be represented by the Provider C ABI: " + ", ".join(unsupported_c_domains),
            )
        mutability = require_string(identity["mutability"], f"{location}.identity.mutability")
        if mutability not in MUTABILITY:
            fail(f"{location}.identity.mutability", f"must be one of: {', '.join(MUTABILITY)}")

        payload_semantics = validate_payload_semantics(
            entry["payload_semantics"], payload["dimensions"], f"{location}.payload_semantics"
        )
        metadata_semantics = validate_metadata_semantics(entry["metadata_semantics"], f"{location}.metadata_semantics")
        if type_ref == "ksj.kspace-frame" and "completion" not in payload_semantics:
            fail(f"{location}.payload_semantics", "ksj.kspace-frame requires completion semantics")
        if type_ref == "ksj.image-frame" and "intensity" not in payload_semantics:
            fail(f"{location}.payload_semantics", "ksj.image-frame requires intensity semantics")
        if type_ref in OPAQUE_MESSAGE_TYPE_REFS:
            if "message_contract" not in payload_semantics:
                fail(f"{location}.payload_semantics", f"{type_ref} requires opaque message-contract semantics")
            expected_opaque_message_payload = {
                "kind": "message_handle",
                "element_type": "none",
                "rank": 0,
                "dimensions": [],
                "layout": "opaque",
                "strides": "canonical",
                "explicit_byte_strides": [],
            }
            if payload != expected_opaque_message_payload:
                fail(f"{location}.identity.payload", f"{type_ref} must use the frozen opaque rank-0 message payload shape")
            if memory != {"allowed_domains": ["host_normal"], "min_alignment_bytes": 8}:
                fail(f"{location}.identity.memory", f"{type_ref} must use immutable host_normal alignment-8 message storage")
            if mutability != "immutable_after_publish":
                fail(f"{location}.identity.mutability", f"{type_ref} must be immutable_after_publish")

        structural_descriptor = {
            "type_ref": type_ref,
            "payload_kind": payload["kind"],
            "element_type": payload["element_type"],
            "rank": payload["rank"],
            "dimensions": payload["dimensions"],
            "layout": payload["layout"],
            "strides": payload["strides"],
            "explicit_byte_strides": payload["explicit_byte_strides"],
            "allowed_memory_domains": memory["allowed_domains"],
            "min_alignment_bytes": memory["min_alignment_bytes"],
            "mutability": mutability,
        }
        symbol = symbol_name(type_ref)
        if symbol in seen_symbols:
            fail(f"{location}.type_ref", f"generates duplicate C/C++ symbol {symbol!r}")
        seen_symbols.add(symbol)
        result.append(
            RegisteredType(
                type_ref=type_ref,
                symbol=symbol,
                pascal=pascal_name(symbol),
                summary=summary,
                payload_semantics=payload_semantics,
                metadata_semantics=metadata_semantics,
                structural_descriptor=structural_descriptor,
                digest=identity_digest(structural_descriptor),
            )
        )

    actual_refs = tuple(item.type_ref for item in result)
    if actual_refs != EXPECTED_TYPE_REFS:
        fail(
            f"{registry_path}.types",
            "must contain exactly the current executable types in canonical order: " + ", ".join(EXPECTED_TYPE_REFS),
        )
    return result


def header_comment_lines(registered_type: RegisteredType, prefix: str = "// ") -> list[str]:
    payload = registered_type.payload
    semantics = canonical_json(registered_type.payload_semantics)
    metadata = canonical_json(registered_type.metadata_semantics)
    return [
        f"{prefix}{registered_type.type_ref}: {registered_type.summary}",
        f"{prefix}Structural payload: {payload['element_type']} {payload['layout']} rank {payload['rank']} "
        f"[{', '.join(payload['dimensions'])}].",
        f"{prefix}Payload semantics: {semantics}",
        f"{prefix}Metadata semantics: {metadata}",
    ]


def generate_model_header(types: list[RegisteredType]) -> str:
    lines = [
        "// Generated by tools/type_registry/generate.py from types/registry.json. DO NOT EDIT.",
        "// Type identities are derived from the canonical structural descriptor; a semantic change needs a distinct TypeRef.",
        "#pragma once",
        "",
        '#include "kspacejet/recon/type_descriptor.hpp"',
        "",
        "#include <string>",
        "#include <string_view>",
        "",
        "namespace ksj::recon::types {",
        "",
        "// clang-format off",
        "",
    ]
    for registered_type in types:
        lines.extend(header_comment_lines(registered_type))
        lines.append(
            f"inline constexpr std::string_view k{registered_type.pascal}TypeRef = {c_string(registered_type.type_ref)};"
        )
        lines.append("")
        lines.append(f"[[nodiscard]] inline Result<TypeDescriptor> {registered_type.symbol}() {{")
        lines.append("  TypeDescriptorSpec specification;")
        lines.append(f"  specification.type_ref = std::string(k{registered_type.pascal}TypeRef);")
        lines.append(f"  specification.payload_kind = {PAYLOAD_KINDS[registered_type.payload['kind']]};")
        lines.append(f"  specification.element_type = {ELEMENT_TYPES[registered_type.payload['element_type']]};")
        lines.append(f"  specification.rank = {registered_type.payload['rank']}U;")
        dimensions = ", ".join(c_string(dimension) for dimension in registered_type.payload["dimensions"])
        lines.append(f"  specification.dimensions = {{{dimensions}}};")
        lines.append(f"  specification.layout = {LAYOUTS[registered_type.payload['layout']]};")
        lines.append(f"  specification.strides = {STRIDES[registered_type.payload['strides']]};")
        explicit_strides = ", ".join(f"{stride}U" for stride in registered_type.payload["explicit_byte_strides"])
        lines.append(f"  specification.explicit_byte_strides = {{{explicit_strides}}};")
        memory_domains = ", ".join(
            MEMORY_DOMAINS[domain] for domain in registered_type.memory["allowed_domains"]
        )
        lines.append(f"  specification.allowed_memory_domains = {{{memory_domains}}};")
        lines.append(f"  specification.min_alignment_bytes = {registered_type.memory['min_alignment_bytes']}U;")
        lines.append(f"  specification.mutability = {MUTABILITY[registered_type.mutability]};")
        lines.append("  return TypeDescriptor::create(specification);")
        lines.append("}")
        lines.append("")
    lines.append("[[nodiscard]] inline Result<TypeDescriptor> resolve(const std::string_view type_ref) {")
    for registered_type in types:
        lines.append(f"  if (type_ref == k{registered_type.pascal}TypeRef) {{")
        lines.append(f"    return {registered_type.symbol}();")
        lines.append("  }")
    lines.append(
        "  return Status::NotFound(\"No executable TypeDescriptor is registered for type_ref '\" + "
        "std::string(type_ref) + \"'.\");"
    )
    lines.append("}")
    lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    lines.append("} // namespace ksj::recon::types")
    lines.append("")
    return "\n".join(lines)


def abi_header_initializer(struct_type: str) -> str:
    return (
        "{sizeof(" + struct_type + "), UINT32_C(0), UINT64_C(0), "
        "{UINT64_C(0), UINT64_C(0)}}"
    )


def generate_digest_initializer(digest: str) -> list[str]:
    raw_digest = bytes.fromhex(digest.removeprefix("sha256:"))
    rows = [raw_digest[index : index + 8] for index in range(0, len(raw_digest), 8)]
    lines = ["{"]
    lines.append(f"  {abi_header_initializer('ksj_digest256')},")
    lines.append("  {")
    for row_index, row in enumerate(rows):
        suffix = "," if row_index + 1 < len(rows) else ""
        values = ", ".join(f"UINT8_C(0x{byte:02x})" for byte in row)
        lines.append(f"    {values}{suffix}")
    lines.append("  }")
    lines.append("}")
    return lines


def generate_sdk_header(types: list[RegisteredType]) -> str:
    lines = [
        "/* Generated by tools/type_registry/generate.py from types/registry.json. DO NOT EDIT. */",
        "/* A semantic change requires a distinct TypeRef; prose never mutates an existing identity. */",
        "#ifndef KSPACEJET_PROVIDER_TYPE_REGISTRY_H_",
        "#define KSPACEJET_PROVIDER_TYPE_REGISTRY_H_",
        "",
        '#include "kspacejet/provider/provider.h"',
        "",
        "// clang-format off",
        "",
    ]
    for registered_type in types:
        comment = [line.replace("// ", "/* ", 1) + " */" for line in header_comment_lines(registered_type)]
        lines.extend(comment)
        prefix = f"ksj_type_registry_{registered_type.symbol}"
        lines.append(f"static const char {prefix}_type_ref_storage[] = {c_string(registered_type.type_ref)};")
        lines.append(f"static const ksj_utf8_view {prefix}_type_ref = {{")
        lines.append(f"  {abi_header_initializer('ksj_utf8_view')},")
        lines.append(f"  {prefix}_type_ref_storage,")
        lines.append(f"  sizeof({prefix}_type_ref_storage) - 1U",
        )
        lines.append("};")
        dimensions = registered_type.payload["dimensions"]
        if dimensions:
            lines.append(f"static const ksj_utf8_view {prefix}_dimension_names[] = {{")
            for dimension in dimensions:
                lines.append("  {")
                lines.append(f"    {abi_header_initializer('ksj_utf8_view')},")
                lines.append(f"    {c_string(dimension)},")
                lines.append(f"    sizeof({c_string(dimension)}) - 1U",
                )
                lines.append("  },")
            lines.append("};")
        lines.append(f"static const uint64_t {prefix}_stride_bytes[8] = {{")
        for index in range(8):
            if index < len(registered_type.payload["explicit_byte_strides"]):
                stride = registered_type.payload["explicit_byte_strides"][index]
                lines.append(f"  UINT64_C({stride}),")
            else:
                lines.append("  UINT64_C(0),")
        lines.append("};")
        lines.append(f"static const ksj_digest256 {prefix}_identity_digest =")
        lines.extend(generate_digest_initializer(registered_type.digest))
        lines.append(";")
        lines.append("")
        lines.append(f"static inline ksj_type_descriptor_view {prefix}(void) {{")
        lines.append("  ksj_type_descriptor_view descriptor = {0};")
        lines.append("  descriptor.abi = ksj_provider_abi_header_make((uint32_t)sizeof(descriptor), UINT64_C(0));")
        lines.append(f"  descriptor.type_ref = {prefix}_type_ref;")
        lines.append(f"  descriptor.type_identity_digest = {prefix}_identity_digest;")
        lines.append(f"  descriptor.payload_kind = {C_PAYLOAD_KINDS[registered_type.payload['kind']]};")
        lines.append(f"  descriptor.element_type = {C_ELEMENT_TYPES[registered_type.payload['element_type']]};")
        lines.append(f"  descriptor.rank = UINT32_C({registered_type.payload['rank']});")
        if dimensions:
            lines.append(f"  descriptor.dimension_names = {prefix}_dimension_names;")
        layout_flags = " | ".join(
            (C_LAYOUTS[registered_type.payload["layout"]], C_STRIDES[registered_type.payload["strides"]])
        )
        lines.append(f"  descriptor.layout_flags = {layout_flags};")
        for index in range(8):
            lines.append(f"  descriptor.stride_bytes[{index}U] = {prefix}_stride_bytes[{index}U];")
        memory_flags = " | ".join(
            C_MEMORY_DOMAINS[domain] for domain in registered_type.memory["allowed_domains"]
        )
        lines.append(f"  descriptor.allowed_memory_domains = {memory_flags};")
        lines.append(f"  descriptor.minimum_alignment = UINT32_C({registered_type.memory['min_alignment_bytes']});")
        lines.append(f"  descriptor.mutability = {C_MUTABILITY[registered_type.mutability]};")
        lines.append("  return descriptor;")
        lines.append("}")
        lines.append("")
        matcher = f"ksj_type_registry_matches_{registered_type.symbol}"
        lines.append(f"static inline int {matcher}(const ksj_type_descriptor_view* descriptor) {{")
        lines.append("  uint32_t index;")
        lines.append("  if (descriptor == NULL || descriptor->type_ref.data == NULL ||")
        lines.append(f"      descriptor->type_ref.size != {prefix}_type_ref.size ||")
        lines.append(f"      descriptor->payload_kind != {C_PAYLOAD_KINDS[registered_type.payload['kind']]} ||")
        lines.append(f"      descriptor->element_type != {C_ELEMENT_TYPES[registered_type.payload['element_type']]} ||")
        lines.append(f"      descriptor->rank != UINT32_C({registered_type.payload['rank']}) ||")
        lines.append(f"      descriptor->layout_flags != ({layout_flags}) ||")
        lines.append(f"      descriptor->allowed_memory_domains != ({memory_flags}) ||")
        lines.append(f"      descriptor->minimum_alignment != UINT32_C({registered_type.memory['min_alignment_bytes']}) ||")
        lines.append(f"      descriptor->mutability != {C_MUTABILITY[registered_type.mutability]}) {{")
        lines.append("    return 0;")
        lines.append("  }")
        lines.append("  for (index = UINT32_C(0); index < descriptor->type_ref.size; ++index) {")
        lines.append(f"    if (descriptor->type_ref.data[index] != {prefix}_type_ref.data[index]) {{")
        lines.append("      return 0;")
        lines.append("    }")
        lines.append("  }")
        lines.append("  for (index = UINT32_C(0); index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {")
        lines.append(f"    if (descriptor->type_identity_digest.bytes[index] != {prefix}_identity_digest.bytes[index]) {{")
        lines.append("      return 0;")
        lines.append("    }")
        lines.append("  }")
        if dimensions:
            lines.append("  if (descriptor->dimension_names == NULL) {")
            lines.append("    return 0;")
            lines.append("  }")
            lines.append(f"  for (index = UINT32_C(0); index < UINT32_C({len(dimensions)}); ++index) {{")
            lines.append("    uint64_t character_index;")
            lines.append("    if (descriptor->dimension_names[index].data == NULL ||")
            lines.append(f"        descriptor->dimension_names[index].size != {prefix}_dimension_names[index].size) {{")
            lines.append("      return 0;")
            lines.append("    }")
            lines.append("    for (character_index = UINT64_C(0);")
            lines.append("         character_index < descriptor->dimension_names[index].size; ++character_index) {")
            lines.append(
                f"      if (descriptor->dimension_names[index].data[character_index] != {prefix}_dimension_names[index].data[character_index]) {{"
            )
            lines.append("        return 0;")
            lines.append("      }")
            lines.append("    }")
            lines.append("  }")
        lines.append("  for (index = UINT32_C(0); index < UINT32_C(8); ++index) {")
        lines.append(f"    if (descriptor->stride_bytes[index] != {prefix}_stride_bytes[index]) {{")
        lines.append("      return 0;")
        lines.append("    }")
        lines.append("  }")
        lines.append("  return 1;")
        lines.append("}")
        lines.append("")
    lines.append("// clang-format on")
    lines.append("")
    lines.append("#endif /* KSPACEJET_PROVIDER_TYPE_REGISTRY_H_ */")
    lines.append("")
    return "\n".join(lines)


def check_or_write(path: Path, content: str, check: bool) -> None:
    if check:
        try:
            existing = path.read_text(encoding="utf-8")
        except FileNotFoundError:
            fail(path.as_posix(), "generated file does not exist; run tools/type_registry/generate.py")
        except OSError as error:
            fail(path.as_posix(), f"cannot read generated file: {error}")
        if existing != content:
            fail(path.as_posix(), "is stale; run tools/type_registry/generate.py")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    default_root = Path(__file__).resolve().parents[2]
    parser.add_argument("--project-root", type=Path, default=default_root, help="repository root")
    parser.add_argument("--registry", type=Path, default=Path("types/registry.json"), help="registry JSON path")
    parser.add_argument(
        "--schema", type=Path, default=Path("schemas/type-registry.schema.json"), help="registry JSON Schema path"
    )
    parser.add_argument(
        "--model-header",
        type=Path,
        default=Path("libs/recon/kspacejet-recon-model/include/kspacejet/recon/type_registry.hpp"),
        help="generated C++ model convenience header",
    )
    parser.add_argument(
        "--sdk-header",
        type=Path,
        default=Path("libs/recon/kspacejet-provider-sdk/include/kspacejet/provider/type_registry.h"),
        help="generated Provider SDK C convenience header",
    )
    parser.add_argument("--check", action="store_true", help="validate source and require generated outputs to be current")
    return parser.parse_args()


def source_path(project_root: Path, value: Path) -> Path:
    return value if value.is_absolute() else project_root / value


def main() -> int:
    arguments = parse_arguments()
    project_root = arguments.project_root.resolve()
    registry_path = source_path(project_root, arguments.registry)
    schema_path = source_path(project_root, arguments.schema)
    model_header_path = source_path(project_root, arguments.model_header)
    sdk_header_path = source_path(project_root, arguments.sdk_header)
    try:
        registered_types = validate_registry(load_json(registry_path), registry_path, schema_path)
        check_or_write(model_header_path, generate_model_header(registered_types), arguments.check)
        check_or_write(sdk_header_path, generate_sdk_header(registered_types), arguments.check)
    except RegistryError as error:
        print(f"type-registry: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
