# SPDX-License-Identifier: GPL-3.0-or-later
"""Smoke tests for scripts/hex-to-cpparray.py.

Per D-03 (Phase 9 context): the script consumes the usbrply JSON shape,
NOT raw .pcap bytes. These tests synthesise a 3-packet usbrply document
in-memory — no real capture is required for the test suite to pass.

Coverage matrix:
1. happy path emits valid C++ header with expected identifier
2. --packet-index selects the right packet from the filtered list
3. --type filter narrows by usbrply packet type
4. --device with shell-special chars is rejected (Pitfall 32 + T-09-13)
5. --capture missing causes argparse non-zero exit
6. empty packs list rejected
7. emitted header is syntactically valid C++20 (g++ -fsyntax-only)
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "hex-to-cpparray.py"

SYNTHETIC_DOC = {
    "packs": [
        {"type": "controlWrite", "data": "020b00000000000044"},
        {"type": "interruptIn", "data": "0000000000000007"},
        {"type": "interruptOut", "data": "ff00"},
    ]
}


def _write_doc(tmp_path: Path, doc: dict) -> Path:
    path = tmp_path / "cap.json"
    path.write_text(json.dumps(doc), encoding="utf-8")
    return path


def _run(args: list[str], **kwargs) -> subprocess.CompletedProcess:
    # All inputs are test-supplied constants; bandit S603 doesn't apply.
    return subprocess.run(  # noqa: S603
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
        check=False,
        **kwargs,
    )


def test_happy_path_emits_valid_cpp_header(tmp_path: Path) -> None:
    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run(
        [
            str(cap),
            "--device",
            "akp03_variant_3004",
            "--capture",
            "image-upload-first-chunk",
        ]
    )
    assert result.returncode == 0, result.stderr
    out = result.stdout

    # Boilerplate header lines.
    assert "#pragma once" in out
    assert "#include <array>" in out
    assert "#include <cstdint>" in out
    assert "namespace ajazz::tests::fixtures" in out

    # Identifier shape and array declaration.
    assert "inline constexpr std::array<std::uint8_t," in out
    assert "AKP03_VARIANT_3004_IMAGE_UPLOAD_FIRST_CHUNK_BYTES" in out

    # First-3-bytes proof: default --packet-index=0 + default --type=any
    # picks the first packet, whose data starts with 02 0b 00.
    assert "0x02, 0x0b, 0x00," in out

    # Source metadata line preserves the original (unnormalised) inputs.
    assert "device=akp03_variant_3004" in out
    assert "label=image-upload-first-chunk" in out

    # End-of-file-fixer compatibility: exactly one trailing newline.
    assert out.endswith("\n")
    assert not out.endswith("\n\n")


def test_packet_index_selection(tmp_path: Path) -> None:
    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run(
        [
            str(cap),
            "--device",
            "ak980pro",
            "--capture",
            "second-packet",
            "--packet-index",
            "1",
        ]
    )
    assert result.returncode == 0, result.stderr
    # Second packet is interruptIn 0000000000000007 -> 8 bytes ending in 0x07.
    assert "0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07," in result.stdout
    assert "std::array<std::uint8_t, 8>" in result.stdout


def test_type_filter(tmp_path: Path) -> None:
    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run(
        [
            str(cap),
            "--device",
            "ak980pro",
            "--capture",
            "out-only",
            "--type",
            "interruptOut",
            "--packet-index",
            "0",
        ]
    )
    assert result.returncode == 0, result.stderr
    # interruptOut packet is ff00 -> two bytes 0xff, 0x00.
    assert "0xff, 0x00," in result.stdout
    assert "std::array<std::uint8_t, 2>" in result.stdout


def test_invalid_codename_rejected(tmp_path: Path) -> None:
    # Pitfall 32: non-ASCII / shell-special chars must be rejected upfront.
    # Threat T-09-13: --device with shell-specials would corrupt the header.
    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run(
        [
            str(cap),
            "--device",
            "AKP03!Variant",
            "--capture",
            "image-upload",
        ]
    )
    assert result.returncode != 0
    stderr_lc = result.stderr.lower()
    # Error must reference device/codename/ASCII or the snake-case regex.
    assert (
        "device" in stderr_lc
        or "codename" in stderr_lc
        or "ascii" in stderr_lc
        or "snake_case" in stderr_lc
    ), result.stderr


def test_missing_capture_label_rejected(tmp_path: Path) -> None:
    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run([str(cap), "--device", "akp03_variant_3004"])
    # argparse exits 2 on missing required args.
    assert result.returncode != 0


def test_empty_packs_rejected(tmp_path: Path) -> None:
    cap = _write_doc(tmp_path, {"packs": []})
    result = _run(
        [
            str(cap),
            "--device",
            "akp03_variant_3004",
            "--capture",
            "should-fail",
        ]
    )
    assert result.returncode != 0
    stderr_lc = result.stderr.lower()
    assert "empty" in stderr_lc or "no packets" in stderr_lc, result.stderr


def test_raw_pcap_extension_rejected(tmp_path: Path) -> None:
    # CAPTURE-01 policy: never accept raw capture files as input.
    # Even if the bytes inside happen to be JSON, the .pcap/.pcapng suffix
    # is reserved and must be rejected with a pointer to CAPTURING.md.
    fake_pcap = tmp_path / "evil.pcap"
    fake_pcap.write_text(json.dumps(SYNTHETIC_DOC), encoding="utf-8")
    result = _run(
        [
            str(fake_pcap),
            "--device",
            "akp03_variant_3004",
            "--capture",
            "should-fail",
        ]
    )
    assert result.returncode != 0
    stderr_lc = result.stderr.lower()
    assert "pcap" in stderr_lc or "usbrply" in stderr_lc, result.stderr


def test_emitted_header_is_syntactically_valid_cpp(tmp_path: Path) -> None:
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not on PATH; clang-only CI is acceptable")

    cap = _write_doc(tmp_path, SYNTHETIC_DOC)
    result = _run(
        [
            str(cap),
            "--device",
            "akp03_variant_3004",
            "--capture",
            "image-upload-first-chunk",
        ]
    )
    assert result.returncode == 0, result.stderr

    # Pipe the emitted header into g++ -fsyntax-only via stdin.
    # gpp comes from shutil.which (absolute path); inputs are constants.
    syntax_check = subprocess.run(  # noqa: S603
        [gpp, "-fsyntax-only", "-std=c++20", "-x", "c++", "-"],
        input=result.stdout,
        capture_output=True,
        text=True,
        check=False,
    )
    # g++ emits a benign #pragma-once-in-main-file warning when reading
    # the header as a stdin translation unit; the exit code is what we
    # actually assert.
    assert syntax_check.returncode == 0, syntax_check.stderr
