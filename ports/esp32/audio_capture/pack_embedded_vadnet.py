from __future__ import annotations

import argparse
import pathlib
import struct


FIELD_BYTES = 32
MODEL_NAME = "vadnet1_medium"
MODEL_FILES = ("_MODEL_INFO_", "vadn1_data", "vadn1_index")


def padded_ascii(value: str) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) > FIELD_BYTES:
        raise ValueError(f"field is longer than {FIELD_BYTES} bytes: {value}")
    return encoded.ljust(FIELD_BYTES, b"\0")


def pack_model(model_dir: pathlib.Path) -> bytes:
    files = [(name, (model_dir / name).read_bytes()) for name in MODEL_FILES]
    header_bytes = 4 + FIELD_BYTES + 4 + len(files) * (FIELD_BYTES + 4 + 4)
    header = bytearray(struct.pack("<I", 1))
    header.extend(padded_ascii(MODEL_NAME))
    header.extend(struct.pack("<I", len(files)))

    offset = header_bytes
    payload = bytearray()
    for name, data in files:
        header.extend(padded_ascii(name))
        header.extend(struct.pack("<II", offset, len(data)))
        payload.extend(data)
        offset += len(data)

    if len(header) != header_bytes:
        raise AssertionError(f"packed header mismatch: {len(header)} != {header_bytes}")
    return bytes(header + payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    packed = pack_model(args.model_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != packed:
        args.output.write_bytes(packed)


if __name__ == "__main__":
    main()
