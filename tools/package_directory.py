"""Reuse one producer package directory; never retain random build copies."""
from pathlib import Path
import re
import shutil


def package_directory(output, name, *inputs):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name) or ".." in name:
        raise ValueError("Invalid package directory name")
    root = Path(output).resolve()
    target = root / name
    if target.is_symlink():
        raise ValueError("Refusing to overwrite a linked SDK directory")
    for source in inputs:
        if source is not None and (Path(source).resolve().is_relative_to(target) or target.is_relative_to(Path(source).resolve())):
            raise ValueError("Package destination contains a build input")
    root.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if not target.is_dir():
            raise ValueError("Package destination is not a directory")
        shutil.rmtree(target)
    target.mkdir()
    return target
