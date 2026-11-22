#!/usr/bin/env python3
"""Procedural GLB asset generator (thin wrapper).

Implementation lives in tools/assetgen/ for maintainability.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from assetgen.cli import main

if __name__ == "__main__":
    main()
