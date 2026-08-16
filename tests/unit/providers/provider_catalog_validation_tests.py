#!/usr/bin/env python3
"""Focused regressions for Provider catalog structural validation."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from provider_catalog_validation import CatalogValidator


PROJECT_ROOT = Path(__file__).resolve().parents[3]
LEGACY_PORT_FIELDS_FIXTURE = (
    PROJECT_ROOT / "tests/unit/providers/fixtures/invalid/operator-contract-port-legacy-capabilities.json"
)
LEGACY_PLANNING_FIELDS_FIXTURE = (
    PROJECT_ROOT / "tests/unit/libs/recon/fixtures/invalid/operator-contract-overflow.json"
)


class ProviderCatalogValidationTests(unittest.TestCase):
    def test_moved_planning_fields_are_unknown(self) -> None:
        validator = CatalogValidator(PROJECT_ROOT)
        validator.registered_type_refs.add("ksj.kspace-frame")

        validator.validate_contract(LEGACY_PLANNING_FIELDS_FIXTURE, "legacy_planning_probe")

        self.assertEqual(
            [
                "tests/unit/libs/recon/fixtures/invalid/operator-contract-overflow.json: "
                "contains unknown field(s): execution"
            ],
            validator.errors,
        )

    def test_removed_port_capability_fields_are_unknown(self) -> None:
        validator = CatalogValidator(PROJECT_ROOT)
        validator.registered_type_refs.add("ksj.image-frame")

        validator.validate_contract(LEGACY_PORT_FIELDS_FIXTURE, "legacy_capability_probe")

        self.assertEqual(
            [
                "tests/unit/providers/fixtures/invalid/operator-contract-port-legacy-capabilities.json#/ports/0: "
                "contains unknown field(s): layout_capabilities, metadata_capabilities, required"
            ],
            validator.errors,
        )

    def test_provider_source_layout_rejects_generic_internal_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_directory = root / "providers/kspacejet-example"
            source_root = source_directory / "src"
            operator_directory = source_root / "operators"
            operator_directory.mkdir(parents=True)
            (source_directory / "CMakeLists.txt").touch()
            for filename in ("provider_entry.cpp", "provider_api.hpp", "provider_api.cpp", "provider_state.hpp"):
                (source_root / filename).touch()
            (source_root / "provider_internal.hpp").touch()

            validator = CatalogValidator(root)
            validator.validate_implementation(
                {
                    "source_directory": "providers/kspacejet-example",
                    "artifact_basename": "ksj-example",
                },
                "catalog/provider",
                "example",
                source_directory,
            )

            self.assertEqual(
                [
                    "catalog/provider/implementation: implemented Provider must not use generic "
                    "src/provider_internal.hpp; use provider_state, provider_api, an Operator header, "
                    "or a named support unit"
                ],
                validator.errors,
            )

    def test_operator_sources_require_matching_private_header_and_implementation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            operator_directory = root / "providers/kspacejet-example/src/operators"
            operator_directory.mkdir(parents=True)
            (operator_directory / "example_operator.cpp").touch()

            validator = CatalogValidator(root)
            validator.implemented_operator_stems[operator_directory] = {"example_operator"}
            validator.validate_catalog_coverage()

            self.assertEqual(
                [
                    "providers/kspacejet-example/src/operators/example_operator.hpp: implemented Operator must own "
                    "a private declaration header",
                    "providers/kspacejet-example/src/operators/example_operator: Operator declarations and "
                    "implementation must be a matched .hpp/.cpp pair",
                ],
                validator.errors,
            )


if __name__ == "__main__":
    unittest.main()
