"""Register native DLL directories for the project-local OSGeo4W runtime."""

from __future__ import annotations

import os
from pathlib import Path


_DLL_DIRECTORY_HANDLES = []

if os.name == "nt":
    osgeo_root = Path(os.environ.get("OSGEO4W_ROOT", ""))
    for dll_dir in (
        osgeo_root / "apps" / "Qt6" / "bin",
        osgeo_root / "bin",
        osgeo_root / "apps" / "gdal-dev" / "bin",
    ):
        if dll_dir.is_dir():
            _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(dll_dir))
