#!/usr/bin/env python3
"""Write the signed update envelope for the fork's GitHub release notes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re

from update_version_manifest import sign_update


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("installer", type=Path)
    args = parser.parse_args()

    version = args.version
    filename = f"crossdesk-win-x64-{version}.exe"
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){2,3}", version):
        parser.error("expected a numeric fork release version")
    if args.installer.name != filename:
        parser.error(f"expected installer named {filename}")
    size = args.installer.stat().st_size
    if not 0 < size <= 2 * 1024**3:
        parser.error("installer size is outside the updater's supported range")
    with args.installer.open("rb") as installer:
        checksum = hashlib.file_digest(installer, "sha256").hexdigest()

    payload = json.dumps(
        {
            "schema": 1,
            "platform": "windows-x64",
            "version": version,
            "filename": filename,
            "url": (
                "https://github.com/fortruelove/crossdesk/releases/download/"
                f"v{version}/{filename}"
            ),
            "size": size,
            "sha256": checksum,
        },
        separators=(",", ":"),
    )
    envelope = sign_update(
        payload,
        os.environ["CROSSDESK_UPDATE_PRIVATE_KEY"],
        os.environ["CROSSDESK_UPDATE_PUBLIC_KEY"],
    )
    print(f"<!-- crossdesk-windows-update: {json.dumps(envelope, separators=(',', ':'))} -->")


if __name__ == "__main__":
    main()
