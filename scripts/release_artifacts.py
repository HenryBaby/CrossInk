#!/usr/bin/env python3
"""Create and validate provenance files for firmware promotion artifacts."""

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def metadata_for(binary: Path, *, commit: str, version: str, environment: str) -> dict:
    return {
        "github_sha": commit,
        "version": version,
        "environment": environment,
        "filename": binary.name,
        "sha256": sha256(binary),
    }


def write_metadata(binary: Path, output: Path, *, commit: str, version: str, environment: str) -> None:
    metadata = metadata_for(binary, commit=commit, version=version, environment=environment)
    output.mkdir(parents=True, exist_ok=True)
    (output / "provenance.json").write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n")
    (output / "SHA256SUMS").write_text(f"{metadata['sha256']}  {binary.name}\n")


def validate(directory: Path, *, commit: str, version: str, environment: str) -> Path:
    metadata_path = directory / "provenance.json"
    checksums_path = directory / "SHA256SUMS"
    metadata = json.loads(metadata_path.read_text())
    expected_name = f"firmware-{'x3-x4' if environment == 'default' else environment}-v{version}.bin"
    binary = directory / expected_name
    if not binary.is_file():
        raise ValueError(f"missing expected firmware: {expected_name}")
    binaries = sorted(directory.glob("*.bin"))
    if binaries != [binary]:
        raise ValueError(f"unexpected firmware files: {[item.name for item in binaries]}")
    expected = {
        "github_sha": commit,
        "version": version,
        "environment": environment,
        "filename": expected_name,
        "sha256": sha256(binary),
    }
    if metadata != expected:
        raise ValueError(f"provenance mismatch: expected {expected}, got {metadata}")
    if checksums_path.read_text() != f"{expected['sha256']}  {expected_name}\n":
        raise ValueError("checksum metadata mismatch")
    return binary


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    write = subparsers.add_parser("write")
    write.add_argument("--binary", type=Path, required=True)
    write.add_argument("--output", type=Path, required=True)
    write.add_argument("--commit", required=True)
    write.add_argument("--version", required=True)
    write.add_argument("--environment", required=True)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("--directory", type=Path, required=True)
    validate_parser.add_argument("--commit", required=True)
    validate_parser.add_argument("--version", required=True)
    validate_parser.add_argument("--environment", required=True)
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "write":
        write_metadata(
            args.binary,
            args.output,
            commit=args.commit,
            version=args.version,
            environment=args.environment,
        )
    else:
        validate(args.directory, commit=args.commit, version=args.version, environment=args.environment)


if __name__ == "__main__":
    main()
