#!/usr/bin/env python3
"""Import local conservateur : originaux immuables, ZIP sûr et PDF page par page."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def preserve(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if sha256(source) != sha256(target):
            raise ValueError(f"Refus de remplacer un original différent : {target}")
        return
    shutil.copy2(source, target)
    target.chmod(stat.S_IREAD)


def safe_members(archive: zipfile.ZipFile):
    """Validate every entry before writing any entry; reject links and collisions."""
    names = set()
    total = 0
    for entry in archive.infolist():
        normalized = entry.filename.replace("\\", "/")
        p = PurePosixPath(normalized)
        if p.is_absolute() or not p.parts or any(x in (".", "..") or ":" in x for x in p.parts):
            raise ValueError(f"Chemin ZIP interdit : {entry.filename}")
        if stat.S_ISLNK(entry.external_attr >> 16):
            raise ValueError(f"Lien ZIP interdit : {entry.filename}")
        key = str(p).casefold().rstrip("/")
        if key in names:
            raise ValueError(f"Collision ZIP : {entry.filename}")
        names.add(key)
        total += entry.file_size
        if total > 512 * 1024 * 1024:
            raise ValueError("Archive trop volumineuse (>512 Mio décompressés)")
        yield entry, p


def extract_safe(source: Path, target: Path) -> list[dict]:
    records = []
    with zipfile.ZipFile(source) as archive:
        members = list(safe_members(archive))
        for entry, relative in members:
            destination = target.joinpath(*relative.parts)
            if entry.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            data = archive.read(entry)
            digest = hashlib.sha256(data).hexdigest()
            if destination.exists() and sha256(destination) != digest:
                raise ValueError(f"Refus de remplacer une extraction différente : {destination}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists():
                destination.write_bytes(data)
                destination.chmod(stat.S_IREAD)
            records.append({"entry": entry.filename, "bytes": len(data), "sha256": digest})
    return records


def extract_pdf(source: Path, target: Path) -> dict:
    converter = shutil.which("pdftotext")
    if not converter:
        raise ValueError("pdftotext absent ; installer Poppler pour indexer le PDF")
    result = subprocess.run([converter, "-layout", "-enc", "UTF-8", str(source), "-"],
                            check=True, capture_output=True)
    text = result.stdout.decode("utf-8")
    pages = text.split("\f")
    if pages and not pages[-1].strip():
        pages.pop()
    target.mkdir(parents=True, exist_ok=True)
    page_records = []
    for number, page in enumerate(pages, 1):
        output = target / f"page-{number:03d}.txt"
        output.write_text(page, encoding="utf-8")
        page_records.append({"page": number, "file": output.name, "sha256": sha256(output),
                             "characters": len(page)})
    version = subprocess.run([converter, "-v"], capture_output=True, text=True)
    index = {"schema_version": 1, "source_sha256": sha256(source),
             "parents": [{"file": source.name, "sha256": sha256(source)}],
             "tool": "tools/import/import_sources.py", "tool_sha256": sha256(Path(__file__)),
             "configuration": {"layout": True, "encoding": "UTF-8", "page_delimiter": "form_feed"},
             "method": "pdftotext -layout -enc UTF-8",
             "converter_version": (version.stderr or version.stdout).splitlines()[0],
             "page_count": len(pages), "pages": page_records, "status": "documented"}
    (target / "pages.json").write_text(json.dumps(index, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return index


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT)
    parser.add_argument("--reference-dir", type=Path, default=ROOT / "reference")
    parser.add_argument("--diagnostic", action="store_true")
    args = parser.parse_args(argv)
    try:
        records = []
        for source in sorted(args.source_dir.iterdir()):
            if not source.is_file() or source.suffix.lower() not in (".pdf", ".zip"):
                continue
            raw = args.reference_dir / "raw" / "local" / source.name
            preserve(source, raw)
            record = {"id": source.stem, "source_path": str(source.resolve()),
                      "raw_path": raw.relative_to(args.reference_dir).as_posix(),
                      "bytes": source.stat().st_size, "sha256": sha256(source),
                      "source_mtime_ns": source.stat().st_mtime_ns, "initial_url": None,
                      "final_url": None, "version": None, "license": "unknown / non redistribuable par défaut",
                      "acquisition": "copie locale conservatrice", "status": "documented"}
            if source.suffix.lower() == ".zip":
                extracted = args.reference_dir / "raw" / "extracted" / source.stem
                record["entries"] = extract_safe(raw, extracted)
                record["extracted_path"] = extracted.relative_to(args.reference_dir).as_posix()
            else:
                derived = args.reference_dir / "derived" / "pdf" / source.stem
                record["pdf_index"] = extract_pdf(raw, derived)
                record["derived_path"] = derived.relative_to(args.reference_dir).as_posix()
            records.append(record)
        manifest = {"schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
                    "tool": str(Path(__file__).relative_to(ROOT)), "tool_sha256": sha256(Path(__file__)),
                    "sources": records, "capture_count": 0,
                    "note": "Inventaires antérieurs importés, sans binaires audio ni captures de référence."}
        args.reference_dir.mkdir(parents=True, exist_ok=True)
        (args.reference_dir / "sources.local.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(json.dumps({"imported": len(records), "manifest": str(args.reference_dir / "sources.local.json")}, ensure_ascii=False))
        return 0
    except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        if args.diagnostic:
            raise
        print(f"Import impossible : {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
