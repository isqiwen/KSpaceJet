#!/usr/bin/env python3
"""Validate the planning-only Provider/Operator catalog cross references.

This intentionally uses only the Python standard library.  Draft 2020-12
schema validation remains available to external schema tooling; this test adds
repository-specific checks that JSON Schema cannot express, including catalog
identity, filesystem paths, contract/interface correspondence, and the rule
that planned entries are absent from CMake and runtime source.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
from typing import Any


CATALOG_RELATIVE_PATH = Path("providers/catalog.json")
INTERFACE_ROOT = Path("providers/interfaces")
TYPE_REGISTRY_RELATIVE_PATH = Path("types/registry.json")
SCHEMA_RELATIVE_PATHS = {
    Path("schemas/operator-contract.schema.json"): "https://schemas.kspacejet.org/operator-contract.schema.json",
    Path("schemas/operator-interface.schema.json"): "https://schemas.kspacejet.org/operator-interface.schema.json",
    Path("schemas/provider-operator-catalog.schema.json"): "https://schemas.kspacejet.org/provider-operator-catalog.schema.json",
}
SCHEMA_URI = "https://json-schema.org/draft/2020-12/schema"
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9._-]*$")
LOWER_IDENTIFIER_PATTERN = re.compile(r"^[a-z](?:[a-z0-9._-]*[a-z0-9])?$")
TYPE_REFERENCE_PATTERN = re.compile(r"^[a-z](?:[a-z0-9._-]*[a-z0-9])?$")
PROVIDER_SLUG_PATTERN = re.compile(r"^[a-z](?:[a-z0-9-]*[a-z0-9])?$")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cmake"}
PROVIDER_SOURCE_FILES = (
    "provider_entry.cpp",
    "provider_api.hpp",
    "provider_api.cpp",
    "provider_state.hpp",
)


class CatalogValidator:
    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self.errors: list[str] = []
        self.cataloged_interfaces: set[Path] = set()
        self.cataloged_contracts: set[Path] = set()
        self.planned_identifiers: set[str] = set()
        self.planned_provider_slugs: set[str] = set()
        self.registered_type_refs: set[str] = set()
        self.implemented_operator_stems: dict[Path, set[str]] = {}

    def error(self, location: str, message: str) -> None:
        self.errors.append(f"{location}: {message}")

    def load_json(self, path: Path) -> Any | None:
        try:
            with path.open(encoding="utf-8") as stream:
                return json.load(stream)
        except FileNotFoundError:
            self.error(path.as_posix(), "file does not exist")
        except json.JSONDecodeError as error:
            self.error(path.as_posix(), f"invalid JSON at line {error.lineno}, column {error.colno}: {error.msg}")
        except OSError as error:
            self.error(path.as_posix(), f"cannot read file: {error}")
        return None

    def expect_mapping(self, value: Any, location: str) -> dict[str, Any] | None:
        if not isinstance(value, dict):
            self.error(location, "must be a JSON object")
            return None
        return value

    def expect_list(self, value: Any, location: str) -> list[Any] | None:
        if not isinstance(value, list):
            self.error(location, "must be a JSON array")
            return None
        return value

    def expect_string(self, value: Any, location: str) -> str | None:
        if not isinstance(value, str):
            self.error(location, "must be a string")
            return None
        return value

    def expect_exact_keys(self, value: dict[str, Any], allowed: set[str], location: str) -> None:
        unexpected = sorted(set(value).difference(allowed))
        if unexpected:
            self.error(location, f"contains unknown field(s): {', '.join(unexpected)}")

    def artifact_path(self, path_text: Any, location: str) -> Path | None:
        path_string = self.expect_string(path_text, location)
        if path_string is None:
            return None

        pure_path = PurePosixPath(path_string)
        if pure_path.is_absolute() or ".." in pure_path.parts:
            self.error(location, "must be a repository-relative path without '..'")
            return None

        candidate = (self.root / Path(*pure_path.parts)).resolve()
        try:
            candidate.relative_to(self.root)
        except ValueError:
            self.error(location, "must remain below the repository root")
            return None

        if not candidate.is_file():
            self.error(location, f"referenced file does not exist: {path_string}")
            return None
        return candidate

    def validate_schemas(self) -> None:
        for relative_path, expected_id in SCHEMA_RELATIVE_PATHS.items():
            path = self.root / relative_path
            schema = self.expect_mapping(self.load_json(path), relative_path.as_posix())
            if schema is None:
                continue
            if schema.get("$schema") != SCHEMA_URI:
                self.error(relative_path.as_posix(), f"$schema must be {SCHEMA_URI!r}")
            if schema.get("$id") != expected_id:
                self.error(relative_path.as_posix(), f"$id must be {expected_id!r}")
            if schema.get("type") != "object":
                self.error(relative_path.as_posix(), "root schema type must be 'object'")

    def validate_type_registry(self) -> None:
        registry = self.expect_mapping(
            self.load_json(self.root / TYPE_REGISTRY_RELATIVE_PATH), TYPE_REGISTRY_RELATIVE_PATH.as_posix()
        )
        if registry is None:
            return
        if registry.get("kind") != "TypeRegistry":
            self.error(TYPE_REGISTRY_RELATIVE_PATH.as_posix(), "kind must be 'TypeRegistry'")
        if registry.get("identity_hash_domain") != "kspacejet.type-identity":
            self.error(TYPE_REGISTRY_RELATIVE_PATH.as_posix(), "identity_hash_domain must be 'kspacejet.type-identity'")
        policy = registry.get("semantic_change_policy")
        if not isinstance(policy, str) or not policy:
            self.error(TYPE_REGISTRY_RELATIVE_PATH.as_posix(), "semantic_change_policy must be a non-empty string")

        types = self.expect_list(registry.get("types"), f"{TYPE_REGISTRY_RELATIVE_PATH.as_posix()}#/types")
        if types is None:
            return
        if not types:
            self.error(f"{TYPE_REGISTRY_RELATIVE_PATH.as_posix()}#/types", "must contain at least one registered type")
        for index, raw_type in enumerate(types):
            location = f"{TYPE_REGISTRY_RELATIVE_PATH.as_posix()}#/types/{index}"
            registered_type = self.expect_mapping(raw_type, location)
            if registered_type is None:
                continue
            type_ref = registered_type.get("type_ref")
            if not isinstance(type_ref, str) or not TYPE_REFERENCE_PATTERN.fullmatch(type_ref):
                self.error(location, "type_ref must be a lower identifier")
            elif type_ref in self.registered_type_refs:
                self.error(location, f"duplicate type_ref {type_ref!r}")
            else:
                self.registered_type_refs.add(type_ref)

    def validate_interface(self, path: Path, provider_id: str, operator_id: str) -> None:
        relative_path = path.relative_to(self.root).as_posix()
        interface = self.expect_mapping(self.load_json(path), relative_path)
        if interface is None:
            return

        self.expect_exact_keys(
            interface,
            {
                "$schema",
                "kind",
                "availability",
                "provider_id",
                "operator_id",
                "summary",
                "ports",
                "configuration",
                "notes",
            },
            relative_path,
        )
        if interface.get("$schema", SCHEMA_URI) != SCHEMA_URI:
            self.error(relative_path, f"$schema, when present, must be {SCHEMA_URI!r}")
        if interface.get("kind") != "OperatorInterface":
            self.error(relative_path, "kind must be 'OperatorInterface'")
        if interface.get("availability") != "planned":
            self.error(relative_path, "availability must be 'planned'")
        if interface.get("provider_id") != provider_id:
            self.error(relative_path, f"provider_id must match catalog Provider {provider_id!r}")
        if interface.get("operator_id") != operator_id:
            self.error(relative_path, f"operator_id must match catalog Operator {operator_id!r}")

        for field_name in ("summary", "notes"):
            if not isinstance(interface.get(field_name), str) or not interface[field_name]:
                self.error(relative_path, f"{field_name} must be a non-empty string")

        ports = self.expect_list(interface.get("ports"), f"{relative_path}#/ports")
        if ports is not None:
            if not ports:
                self.error(f"{relative_path}#/ports", "must contain at least one port")
            seen_port_names: set[str] = set()
            for index, raw_port in enumerate(ports):
                port_location = f"{relative_path}#/ports/{index}"
                port = self.expect_mapping(raw_port, port_location)
                if port is None:
                    continue
                self.expect_exact_keys(
                    port,
                    {"name", "direction", "semantic_type"},
                    port_location,
                )
                name = port.get("name")
                if not isinstance(name, str) or not IDENTIFIER_PATTERN.fullmatch(name):
                    self.error(port_location, "name must be an identifier")
                elif name in seen_port_names:
                    self.error(port_location, f"duplicate port name {name!r}")
                else:
                    seen_port_names.add(name)
                if port.get("direction") not in {"input", "output"}:
                    self.error(port_location, "direction must be 'input' or 'output'")
                semantic_type = port.get("semantic_type")
                if not isinstance(semantic_type, str) or not LOWER_IDENTIFIER_PATTERN.fullmatch(semantic_type):
                    self.error(port_location, "semantic_type must be a lower identifier")

        configuration = self.expect_mapping(interface.get("configuration"), f"{relative_path}#/configuration")
        if configuration is None:
            return
        configuration_location = f"{relative_path}#/configuration"
        self.expect_exact_keys(configuration, {"kind", "required", "optional"}, configuration_location)
        if configuration.get("kind") != "object":
            self.error(configuration_location, "kind must be 'object'")
        required = self.expect_list(configuration.get("required"), f"{configuration_location}/required")
        optional = self.expect_list(configuration.get("optional"), f"{configuration_location}/optional")
        if required is None or optional is None:
            return

        required_names = self.validate_configuration_keys(required, f"{configuration_location}/required")
        optional_names = self.validate_configuration_keys(optional, f"{configuration_location}/optional")
        overlap = sorted(required_names.intersection(optional_names))
        if overlap:
            self.error(configuration_location, f"required and optional overlap: {', '.join(overlap)}")

    def validate_configuration_keys(self, values: list[Any], location: str) -> set[str]:
        keys: set[str] = set()
        for index, value in enumerate(values):
            item_location = f"{location}/{index}"
            if not isinstance(value, str) or not IDENTIFIER_PATTERN.fullmatch(value):
                self.error(item_location, "must be an identifier")
            elif value in keys:
                self.error(item_location, f"duplicate configuration key {value!r}")
            else:
                keys.add(value)
        return keys

    def validate_contract(self, path: Path, operator_id: str) -> None:
        relative_path = path.relative_to(self.root).as_posix()
        contract = self.expect_mapping(self.load_json(path), relative_path)
        if contract is None:
            return
        self.expect_exact_keys(contract, {"kind", "operator_id", "ports"}, relative_path)
        if contract.get("kind") != "OperatorContract":
            self.error(relative_path, "implemented entry must point to kind 'OperatorContract'")
        if contract.get("operator_id") != operator_id:
            self.error(relative_path, f"operator_id must match catalog Operator {operator_id!r}")
        ports = self.expect_list(contract.get("ports"), f"{relative_path}#/ports")
        if ports is None:
            return
        if not ports:
            self.error(f"{relative_path}#/ports", "must contain at least one port")
        for index, raw_port in enumerate(ports):
            port_location = f"{relative_path}#/ports/{index}"
            port = self.expect_mapping(raw_port, port_location)
            if port is None:
                continue
            self.expect_exact_keys(
                port,
                {
                    "name",
                    "type_ref",
                    "direction",
                },
                port_location,
            )
            type_ref = port.get("type_ref")
            if not isinstance(type_ref, str) or not TYPE_REFERENCE_PATTERN.fullmatch(type_ref):
                self.error(port_location, "type_ref must be a lower identifier such as 'ksj.image-frame'")
            elif type_ref not in self.registered_type_refs:
                self.error(port_location, f"type_ref {type_ref!r} is not defined by types/registry.json")
            if "type_descriptor" in port:
                self.error(port_location, "OperatorContract port must use type_ref, not type_descriptor")
            direction = port.get("direction")
            if direction not in {"input", "output"}:
                self.error(port_location, "direction must be 'input' or 'output'")

        def find_forbidden_type_fields(value: Any, location: str) -> None:
            if isinstance(value, dict):
                for field_name, child in value.items():
                    child_location = f"{location}/{field_name}"
                    if field_name in {
                        "type_id",
                        "revision",
                        "descriptor_digest",
                        "abi_descriptor_digest",
                        "payload_schema_digest",
                        "metadata_schema_digest",
                        "type_identity_digest",
                    }:
                        self.error(child_location, "OperatorContract must reference registry types instead of a digest field")
                    find_forbidden_type_fields(child, child_location)
            elif isinstance(value, list):
                for child_index, child in enumerate(value):
                    find_forbidden_type_fields(child, f"{location}/{child_index}")

        find_forbidden_type_fields(contract, relative_path)

    def validate_catalog(self) -> None:
        catalog_path = self.root / CATALOG_RELATIVE_PATH
        catalog = self.expect_mapping(self.load_json(catalog_path), CATALOG_RELATIVE_PATH.as_posix())
        if catalog is None:
            return

        self.expect_exact_keys(
            catalog,
            {
                "$schema",
                "kind",
                "runtime_resolution_policy",
                "scope",
                "providers",
            },
            CATALOG_RELATIVE_PATH.as_posix(),
        )
        if catalog.get("$schema") != SCHEMA_URI:
            self.error(CATALOG_RELATIVE_PATH.as_posix(), f"$schema must be {SCHEMA_URI!r}")
        if catalog.get("kind") != "ProviderOperatorCatalog":
            self.error(CATALOG_RELATIVE_PATH.as_posix(), "kind must be 'ProviderOperatorCatalog'")
        if catalog.get("runtime_resolution_policy") != "catalog-is-not-runtime-resolvable":
            self.error(CATALOG_RELATIVE_PATH.as_posix(), "catalog must be explicitly non-runtime-resolvable")
        if not isinstance(catalog.get("scope"), str) or not catalog["scope"]:
            self.error(CATALOG_RELATIVE_PATH.as_posix(), "scope must be a non-empty string")

        providers = self.expect_list(catalog.get("providers"), f"{CATALOG_RELATIVE_PATH.as_posix()}#/providers")
        if providers is None:
            return
        if not providers:
            self.error(f"{CATALOG_RELATIVE_PATH.as_posix()}#/providers", "must contain at least one Provider")
            return

        provider_ids: set[str] = set()
        provider_slugs: set[str] = set()
        operator_ids: set[str] = set()
        for index, raw_provider in enumerate(providers):
            self.validate_provider(raw_provider, index, provider_ids, provider_slugs, operator_ids)

    def validate_provider(
        self,
        raw_provider: Any,
        index: int,
        provider_ids: set[str],
        provider_slugs: set[str],
        operator_ids: set[str],
    ) -> None:
        location = f"{CATALOG_RELATIVE_PATH.as_posix()}#/providers/{index}"
        provider = self.expect_mapping(raw_provider, location)
        if provider is None:
            return

        self.expect_exact_keys(
            provider,
            {
                "provider_id",
                "provider_slug",
                "availability",
                "responsibility",
                "lifecycle_boundary",
                "implementation",
                "operators",
            },
            location,
        )
        provider_id = provider.get("provider_id")
        if not isinstance(provider_id, str) or not LOWER_IDENTIFIER_PATTERN.fullmatch(provider_id):
            self.error(location, "provider_id must be a lower identifier")
            return
        if not provider_id.startswith("org.kspacejet."):
            self.error(location, "in-tree Provider IDs must begin with 'org.kspacejet.'")
        if provider_id in provider_ids:
            self.error(location, f"duplicate provider_id {provider_id!r}")
        provider_ids.add(provider_id)

        provider_slug = provider.get("provider_slug")
        if not isinstance(provider_slug, str) or not PROVIDER_SLUG_PATTERN.fullmatch(provider_slug):
            self.error(location, "provider_slug must be a lower hyphenated identifier")
            return
        if provider_slug in provider_slugs:
            self.error(location, f"duplicate provider_slug {provider_slug!r}")
        provider_slugs.add(provider_slug)

        availability = provider.get("availability")
        if availability not in {"implemented-development", "planned"}:
            self.error(location, "availability must be 'implemented-development' or 'planned'")
            return
        for field_name in ("responsibility", "lifecycle_boundary"):
            if not isinstance(provider.get(field_name), str) or not provider[field_name]:
                self.error(location, f"{field_name} must be a non-empty string")

        implementation = provider.get("implementation")
        source_directory = self.root / "providers" / f"kspacejet-{provider_slug}"
        if availability == "implemented-development":
            self.validate_implementation(implementation, location, provider_slug, source_directory)
        else:
            self.planned_provider_slugs.add(provider_slug)
            if implementation is not None:
                self.error(location, "planned Provider must not declare an implementation")
            if source_directory.exists():
                self.error(location, f"planned Provider must not have a module directory: {source_directory.relative_to(self.root)}")

        operators = self.expect_list(provider.get("operators"), f"{location}/operators")
        if operators is None:
            return
        if not operators:
            self.error(f"{location}/operators", "must contain at least one Operator")
            return
        for operator_index, raw_operator in enumerate(operators):
            self.validate_operator(raw_operator, f"{location}/operators/{operator_index}", provider_id, provider_slug,
                                   availability, operator_ids)

    def validate_implementation(
        self, raw_implementation: Any, location: str, provider_slug: str, source_directory: Path
    ) -> None:
        implementation = self.expect_mapping(raw_implementation, f"{location}/implementation")
        if implementation is None:
            return
        self.expect_exact_keys(implementation, {"source_directory", "artifact_basename"}, f"{location}/implementation")
        expected_directory = f"providers/kspacejet-{provider_slug}"
        if implementation.get("source_directory") != expected_directory:
            self.error(f"{location}/implementation", f"source_directory must be {expected_directory!r}")
        expected_artifact = f"ksj-{provider_slug}"
        if implementation.get("artifact_basename") != expected_artifact:
            self.error(f"{location}/implementation", f"artifact_basename must be {expected_artifact!r}")
        if not source_directory.is_dir() or not (source_directory / "CMakeLists.txt").is_file():
            self.error(f"{location}/implementation", f"implemented Provider source is missing: {expected_directory}")
            return

        source_root = source_directory / "src"
        for filename in PROVIDER_SOURCE_FILES:
            if not (source_root / filename).is_file():
                self.error(
                    f"{location}/implementation",
                    f"implemented Provider must contain src/{filename}",
                )
        for legacy_filename in ("provider_internal.hpp", "provider_common.hpp", "provider_common.cpp"):
            if (source_root / legacy_filename).exists():
                self.error(
                    f"{location}/implementation",
                    f"implemented Provider must not use generic src/{legacy_filename}; "
                    "use provider_state, provider_api, an Operator header, or a named support unit",
                )
        for source_path in source_root.rglob("*"):
            if not source_path.is_file() or source_path.suffix not in {".cpp", ".hpp"}:
                continue
            source_text = source_path.read_text(encoding="utf-8", errors="ignore")
            for legacy_identifier in ("provider_internal", "provider_common"):
                if re.search(rf"\b{legacy_identifier}\b", source_text):
                    self.error(
                        source_path.relative_to(self.root).as_posix(),
                        f"implemented Provider must not use generic {legacy_identifier} naming; "
                        "use a functional namespace or support-unit name",
                    )
        operator_directory = source_root / "operators"
        if not operator_directory.is_dir():
            self.error(f"{location}/implementation", "implemented Provider must contain src/operators")
        self.implemented_operator_stems.setdefault(operator_directory, set())

    def validate_operator(
        self,
        raw_operator: Any,
        location: str,
        provider_id: str,
        provider_slug: str,
        provider_availability: str,
        operator_ids: set[str],
    ) -> None:
        operator = self.expect_mapping(raw_operator, location)
        if operator is None:
            return
        self.expect_exact_keys(
            operator,
            {"operator_id", "availability", "summary", "contract_path", "interface_path"},
            location,
        )
        operator_id = operator.get("operator_id")
        if not isinstance(operator_id, str) or not IDENTIFIER_PATTERN.fullmatch(operator_id):
            self.error(location, "operator_id must be an identifier")
            return
        if operator_id in operator_ids:
            self.error(location, f"operator_id is globally reserved and appears more than once: {operator_id!r}")
        operator_ids.add(operator_id)
        if not isinstance(operator.get("summary"), str) or not operator["summary"]:
            self.error(location, "summary must be a non-empty string")

        availability = operator.get("availability")
        if availability not in {"implemented-development", "planned"}:
            self.error(location, "availability must be 'implemented-development' or 'planned'")
            return
        if provider_availability == "planned" and availability != "planned":
            self.error(location, "a planned Provider cannot contain an implemented Operator")

        contract_path = operator.get("contract_path")
        interface_path = operator.get("interface_path")
        if availability == "implemented-development":
            if interface_path is not None:
                self.error(location, "implemented Operator must not retain a planned interface_path")
            expected_contract_path = f"providers/kspacejet-{provider_slug}/contracts/{operator_id}.json"
            if contract_path != expected_contract_path:
                self.error(location, f"contract_path must be {expected_contract_path!r}")
            path = self.artifact_path(contract_path, f"{location}/contract_path")
            if path is not None:
                self.cataloged_contracts.add(path)
                self.validate_contract(path, operator_id)
            operator_directory = self.root / "providers" / f"kspacejet-{provider_slug}" / "src" / "operators"
            self.implemented_operator_stems.setdefault(operator_directory, set()).add(operator_id)
            return

        self.planned_identifiers.add(operator_id)
        if contract_path is not None:
            self.error(location, "planned Operator must not declare a contract_path")
        expected_interface_path = f"providers/interfaces/{provider_id}/{operator_id}.json"
        if interface_path != expected_interface_path:
            self.error(location, f"interface_path must be {expected_interface_path!r}")
        path = self.artifact_path(interface_path, f"{location}/interface_path")
        if path is not None:
            self.cataloged_interfaces.add(path)
            self.validate_interface(path, provider_id, operator_id)

    def validate_catalog_coverage(self) -> None:
        actual_interfaces = {path.resolve() for path in (self.root / INTERFACE_ROOT).rglob("*.json")}
        unreferenced_interfaces = sorted(actual_interfaces.difference(self.cataloged_interfaces))
        missing_interfaces = sorted(self.cataloged_interfaces.difference(actual_interfaces))
        for path in unreferenced_interfaces:
            self.error(path.relative_to(self.root).as_posix(), "planned interface is not referenced by providers/catalog.json")
        for path in missing_interfaces:
            self.error(path.relative_to(self.root).as_posix(), "catalog references a missing planned interface")

        actual_contracts = {path.resolve() for path in self.root.glob("providers/kspacejet-*/contracts/*.json")}
        unreferenced_contracts = sorted(actual_contracts.difference(self.cataloged_contracts))
        for path in unreferenced_contracts:
            self.error(path.relative_to(self.root).as_posix(), "implemented contract is not referenced by providers/catalog.json")

        for operator_directory, expected_stems in self.implemented_operator_stems.items():
            if not operator_directory.is_dir():
                continue
            actual_cpp_stems = {path.stem for path in operator_directory.glob("*.cpp")}
            actual_hpp_stems = {path.stem for path in operator_directory.glob("*.hpp")}
            relative_directory = operator_directory.relative_to(self.root).as_posix()
            for stem in sorted(expected_stems):
                if stem not in actual_hpp_stems:
                    self.error(f"{relative_directory}/{stem}.hpp", "implemented Operator must own a private declaration header")
                if stem not in actual_cpp_stems:
                    self.error(f"{relative_directory}/{stem}.cpp", "implemented Operator must own its implementation source")
            for stem in sorted(actual_cpp_stems.union(actual_hpp_stems).difference(expected_stems)):
                self.error(
                    f"{relative_directory}/{stem}",
                    "Operator source/header is not represented by an implemented catalog Operator",
                )
            for stem in sorted(actual_cpp_stems.symmetric_difference(actual_hpp_stems)):
                self.error(
                    f"{relative_directory}/{stem}",
                    "Operator declarations and implementation must be a matched .hpp/.cpp pair",
                )

    def validate_planned_entries_are_not_built_or_loaded(self) -> None:
        provider_cmake = self.root / "providers/CMakeLists.txt"
        if provider_cmake.is_file():
            provider_cmake_text = provider_cmake.read_text(encoding="utf-8")
            for provider_slug in sorted(self.planned_provider_slugs):
                subdirectory = f"add_subdirectory(kspacejet-{provider_slug})"
                if subdirectory in provider_cmake_text:
                    self.error(
                        provider_cmake.relative_to(self.root).as_posix(),
                        f"planned Provider {provider_slug!r} is registered as a CMake module",
                    )

        for path in self.iter_build_or_runtime_sources():
            relative_path = path.relative_to(self.root).as_posix()
            text = path.read_text(encoding="utf-8", errors="ignore")
            for identifier in sorted(self.planned_identifiers):
                if re.search(rf"(?<![A-Za-z0-9._-]){re.escape(identifier)}(?![A-Za-z0-9._-])", text):
                    self.error(relative_path, f"planned Operator {identifier!r} appears in build/runtime source")
            for provider_slug in sorted(self.planned_provider_slugs):
                module_name = f"kspacejet-{provider_slug}"
                if module_name in text:
                    self.error(relative_path, f"planned Provider module {module_name!r} appears in build/runtime source")

    def iter_build_or_runtime_sources(self) -> list[Path]:
        paths: list[Path] = []
        for relative_root in (Path("apps"), Path("libs"), Path("providers"), Path("tests")):
            root = self.root / relative_root
            if not root.is_dir():
                continue
            for path in root.rglob("*"):
                if not path.is_file():
                    continue
                relative_path = path.relative_to(self.root)
                if INTERFACE_ROOT in relative_path.parents:
                    continue
                if path.name == "CMakeLists.txt" or path.suffix in SOURCE_SUFFIXES:
                    paths.append(path)
        return paths

    def run(self) -> int:
        self.validate_schemas()
        self.validate_type_registry()
        self.validate_catalog()
        self.validate_catalog_coverage()
        self.validate_planned_entries_are_not_built_or_loaded()
        if self.errors:
            for error in self.errors:
                print(f"error: {error}", file=sys.stderr)
            return 1

        print(
            "Provider/Operator catalog validation passed: "
            f"{len(self.cataloged_contracts)} implemented contracts, "
            f"{len(self.cataloged_interfaces)} planned interfaces."
        )
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="KSpaceJet repository root (defaults to the current working directory).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return CatalogValidator(args.project_root).run()


if __name__ == "__main__":
    sys.exit(main())
