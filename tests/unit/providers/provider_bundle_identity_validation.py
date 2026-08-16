#!/usr/bin/env python3
"""Validate reproducible development bundle identities for every executable Provider.

This checks every catalogued implemented-development Provider using the
kspacejet.provider-bundle convention. It is not a signed bundle-manifest
validator: it keeps the catalog, descriptor source, descriptor order, and
README derivation command in lockstep.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


CATALOG_PATH = Path("providers/catalog.json")
PROVIDER_BUNDLE_DOMAIN = "kspacejet.provider-bundle"
HEX_DIGEST_PATTERN = r"[0-9a-f]{64}"


@dataclass(frozen=True)
class CatalogOperator:
    operator_id: str
    contract_path: Path


@dataclass(frozen=True)
class ProviderIdentity:
    provider_id: str
    provider_slug: str
    source_directory: Path
    operators: tuple[CatalogOperator, ...]
    bundle_digest: str


class BundleIdentityValidator:
    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self.errors: list[str] = []

    def error(self, path: Path | str, message: str) -> None:
        if isinstance(path, Path):
            try:
                location = path.resolve().relative_to(self.root).as_posix()
            except ValueError:
                location = path.as_posix()
        else:
            location = path
        self.errors.append(f"{location}: {message}")

    def read_text(self, path: Path) -> str | None:
        try:
            return path.read_text(encoding="utf-8")
        except FileNotFoundError:
            self.error(path, "file does not exist")
        except OSError as exc:
            self.error(path, f"cannot read file: {exc}")
        return None

    def load_json_object(self, path: Path) -> dict[str, Any] | None:
        text = self.read_text(path)
        if text is None:
            return None
        try:
            value = json.loads(text)
        except json.JSONDecodeError as exc:
            self.error(path, f"invalid JSON at line {exc.lineno}, column {exc.colno}: {exc.msg}")
            return None
        if not isinstance(value, dict):
            self.error(path, "must be a JSON object")
            return None
        return value

    def catalog_provider(self, catalog: dict[str, Any], provider_slug: str) -> ProviderIdentity | None:
        providers = catalog.get("providers")
        if not isinstance(providers, list):
            self.error(CATALOG_PATH, "providers must be a JSON array")
            return None

        matching: list[tuple[int, dict[str, Any]]] = []
        for index, raw_provider in enumerate(providers):
            if isinstance(raw_provider, dict) and raw_provider.get("provider_slug") == provider_slug:
                matching.append((index, raw_provider))
        catalog_location = f"{CATALOG_PATH.as_posix()}#/providers"
        if len(matching) != 1:
            self.error(catalog_location, f"must contain exactly one {provider_slug!r} Provider")
            return None

        index, provider = matching[0]
        location = f"{catalog_location}/{index}"
        provider_id = provider.get("provider_id")
        if not isinstance(provider_id, str) or not provider_id:
            self.error(location, "provider_id must be a non-empty string")
            return None
        if provider.get("availability") != "implemented-development":
            self.error(location, f"{provider_slug!r} must be implemented-development")
            return None

        implementation = provider.get("implementation")
        if not isinstance(implementation, dict):
            self.error(location, "implemented Provider must declare implementation")
            return None
        source_directory_text = implementation.get("source_directory")
        if not isinstance(source_directory_text, str) or not source_directory_text:
            self.error(location, "implementation.source_directory must be a non-empty string")
            return None
        source_directory = self.root / source_directory_text
        if not source_directory.is_dir():
            self.error(location, f"implementation source directory does not exist: {source_directory_text}")
            return None

        raw_operators = provider.get("operators")
        if not isinstance(raw_operators, list):
            self.error(location, "operators must be a JSON array")
            return None
        operators: list[CatalogOperator] = []
        for operator_index, raw_operator in enumerate(raw_operators):
            operator_location = f"{location}/operators/{operator_index}"
            if not isinstance(raw_operator, dict):
                self.error(operator_location, "must be a JSON object")
                continue
            if raw_operator.get("availability") != "implemented-development":
                continue
            operator_id = raw_operator.get("operator_id")
            contract_path_text = raw_operator.get("contract_path")
            if not isinstance(operator_id, str) or not operator_id:
                self.error(operator_location, "implemented Operator must have a non-empty operator_id")
                continue
            if not isinstance(contract_path_text, str) or not contract_path_text:
                self.error(operator_location, "implemented Operator must have a contract_path")
                continue
            contract_path = self.root / contract_path_text
            if not contract_path.is_file():
                self.error(operator_location, f"contract_path does not exist: {contract_path_text}")
                continue
            contract = self.load_json_object(contract_path)
            if contract is not None:
                if contract.get("kind") != "OperatorContract":
                    self.error(contract_path, "must be an OperatorContract before deriving a bundle identity")
                if contract.get("operator_id") != operator_id:
                    self.error(contract_path, f"operator_id must match catalog value {operator_id!r}")
            operators.append(CatalogOperator(operator_id, contract_path))

        if not operators:
            self.error(location, "must contain at least one implemented-development Operator")
            return None
        return ProviderIdentity(
            provider_id=provider_id,
            provider_slug=provider_slug,
            source_directory=source_directory,
            operators=tuple(operators),
            bundle_digest="",
        )

    def extract_one(self, text: str, pattern: str, path: Path, label: str) -> str | None:
        matches = re.findall(pattern, text, flags=re.MULTILINE | re.DOTALL)
        if len(matches) != 1:
            self.error(path, f"must declare exactly one {label}")
            return None
        return matches[0]

    def descriptor_operator_order(self, identity: ProviderIdentity, provider_api: Path, text: str) -> list[str] | None:
        qualified_name = r"(?:(?:[A-Za-z_][A-Za-z0-9_]*::)*)"
        single_operator_matches = re.findall(
            rf"descriptor\.operators\s*=\s*&{qualified_name}([A-Za-z_][A-Za-z0-9_]*)_descriptor\(\)\s*;", text
        )
        if single_operator_matches:
            if len(single_operator_matches) != 1:
                self.error(provider_api, "simple descriptor form must expose exactly one Operator")
                return None
            return single_operator_matches

        list_match = re.search(
            r"(?P<list_name>[A-Za-z_][A-Za-z0-9_]*)_operators\(\)\s+noexcept\s*\{.*?"
            r"implementations\s*\{(?P<operators>.*?)\}\s*;",
            text,
            flags=re.DOTALL,
        )
        if list_match is None:
            self.error(provider_api, "cannot find a descriptor-order Operator declaration")
            return None
        order = re.findall(rf"&{qualified_name}([A-Za-z_][A-Za-z0-9_]*)_operator\(\)", list_match.group("operators"))
        if not order:
            self.error(provider_api, "descriptor-order implementation list must not be empty")
            return None

        metadata_match = re.search(
            r"ProviderMetadata\(\)\s+noexcept\s*:\s*operator_descriptors\{(?P<descriptors>.*?)\},\s*"
            r"descriptor\(",
            text,
            flags=re.DOTALL,
        )
        if metadata_match is None:
            self.error(provider_api, "cannot find descriptor construction")
            return None
        list_name = list_match.group("list_name")
        descriptor_indexes = [
            int(index)
            for index in re.findall(
                rf"{re.escape(list_name)}_operators\(\)\[(\d+)U\]", metadata_match.group("descriptors")
            )
        ]
        if descriptor_indexes != list(range(len(order))):
            self.error(provider_api, "descriptor construction must preserve the implementation-list declaration order")
            return None
        return order

    @staticmethod
    def development_bundle_digest(fields: list[str]) -> str:
        payload = b"".join(field.encode("utf-8") + b"\0" for field in fields)
        return hashlib.sha256(payload).hexdigest()

    def validate_readme(
        self, identity: ProviderIdentity, readme: Path, expected_fields: list[str], expected_digest: str
    ) -> None:
        text = self.read_text(readme)
        if text is None:
            return
        if "including the final NUL" not in text:
            self.error(readme, "must explicitly document that the final NUL is included")
        if "src/provider_api.cpp" not in text:
            self.error(readme, "must identify src/provider_api.cpp as the descriptor literal source")

        command_blocks = [
            match
            for match in re.finditer(r"(?:```|~~~)sh\n(?P<command>.*?)\n(?:```|~~~)", text, flags=re.DOTALL)
            if "printf '%s\\0'" in match.group("command") and "sha256sum" in match.group("command")
        ]
        if len(command_blocks) != 1:
            self.error(readme, "must contain exactly one printf '%s\\0' bundle derivation command")
            return
        command = command_blocks[0].group("command")
        if re.search(r"\|\s*\n\s*sha256sum\b", command) is None:
            self.error(readme, "bundle command must pipe its fields to sha256sum")
        quoted_fields = re.findall(r"'([^']*)'", command)
        if not quoted_fields or quoted_fields[0] != r"%s\0":
            self.error(readme, "bundle command must begin with printf '%s\\0'")
            return
        if quoted_fields[1:] != expected_fields:
            self.error(
                readme,
                "documented NUL-delimited fields must be domain, catalog Provider ID, then "
                "descriptor-order Operator IDs",
            )

        result_match = re.search(
            rf"Its result is\s+`({HEX_DIGEST_PATTERN})`\.",
            text[command_blocks[0].end() :],
            flags=re.DOTALL,
        )
        if result_match is None:
            self.error(readme, "must state the expected lowercase SHA-256 result after the derivation command")
        elif result_match.group(1) != expected_digest:
            self.error(
                readme,
                f"documented SHA-256 result must be {expected_digest}, got {result_match.group(1)}",
            )

    def validate_provider(self, catalog: dict[str, Any], provider_slug: str) -> ProviderIdentity | None:
        catalog_identity = self.catalog_provider(catalog, provider_slug)
        if catalog_identity is None:
            return None
        provider_api = catalog_identity.source_directory / "src/provider_api.cpp"
        provider_api_text = self.read_text(provider_api)
        if provider_api_text is None:
            return None

        source_provider_id = self.extract_one(
            provider_api_text,
            r'constexpr\s+char\s+kProviderId\[\]\s*=\s*"([^"]*)"\s*;',
            provider_api,
            "kProviderId literal",
        )
        if source_provider_id is None:
            return None
        if source_provider_id != catalog_identity.provider_id:
            self.error(
                provider_api,
                f"kProviderId must match catalog Provider ID {catalog_identity.provider_id!r}, got {source_provider_id!r}",
            )

        source_bundle_digest = self.extract_one(
            provider_api_text,
            rf'constexpr\s+char\s+kProviderBundleDigestHex\[\]\s*=\s*"({HEX_DIGEST_PATTERN})"\s*;',
            provider_api,
            "lowercase kProviderBundleDigestHex literal",
        )
        if source_bundle_digest is None:
            return None
        if "descriptor.bundle_digest = make_bundle_digest_from_hex(kProviderBundleDigestHex);" not in provider_api_text:
            self.error(provider_api, "provider descriptor must use kProviderBundleDigestHex")

        source_order = self.descriptor_operator_order(catalog_identity, provider_api, provider_api_text)
        if source_order is None:
            return None
        catalog_order = [operator.operator_id for operator in catalog_identity.operators]
        if source_order != catalog_order:
            self.error(
                provider_api,
                f"descriptor Operator order {source_order!r} must match catalog implemented order {catalog_order!r}",
            )
            return None

        fields = [PROVIDER_BUNDLE_DOMAIN, catalog_identity.provider_id]
        fields.extend(operator.operator_id for operator in catalog_identity.operators)
        expected_bundle_digest = self.development_bundle_digest(fields)
        if source_bundle_digest != expected_bundle_digest:
            self.error(
                provider_api,
                f"kProviderBundleDigestHex must be {expected_bundle_digest}, got {source_bundle_digest}",
            )

        identity = ProviderIdentity(
            provider_id=catalog_identity.provider_id,
            provider_slug=catalog_identity.provider_slug,
            source_directory=catalog_identity.source_directory,
            operators=catalog_identity.operators,
            bundle_digest=expected_bundle_digest,
        )
        self.validate_readme(identity, identity.source_directory / "README.md", fields, expected_bundle_digest)
        return identity

    def run(self) -> int:
        identities: list[ProviderIdentity] = []
        catalog = self.load_json_object(self.root / CATALOG_PATH)
        if catalog is not None:
            raw_providers = catalog.get("providers")
            provider_slugs: list[str] = []
            if isinstance(raw_providers, list):
                for raw_provider in raw_providers:
                    if not isinstance(raw_provider, dict) or raw_provider.get("availability") != "implemented-development":
                        continue
                    provider_slug = raw_provider.get("provider_slug")
                    if isinstance(provider_slug, str) and provider_slug:
                        provider_slugs.append(provider_slug)
            else:
                self.error(CATALOG_PATH, "providers must be a JSON array")
            for provider_slug in provider_slugs:
                identity = self.validate_provider(catalog, provider_slug)
                if identity is not None:
                    identities.append(identity)

        if self.errors:
            for error in self.errors:
                print(f"error: {error}", file=sys.stderr)
            return 1

        operator_count = sum(len(identity.operators) for identity in identities)
        print(
            "Provider bundle identity validation passed: "
            f"{len(identities)} Providers and {operator_count} descriptor-order Operators."
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
    return BundleIdentityValidator(args.project_root).run()


if __name__ == "__main__":
    sys.exit(main())
