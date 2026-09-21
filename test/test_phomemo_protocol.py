#!/usr/bin/env python3
"""Small host check for BLE transfer and scan policy."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include <assert.h>
#include "services/phomemo_m_series_protocol.h"
int main() {
  assert(PHOMEMO_PREFERRED_MTU == 185);
  assert(phomemoWriteChunk(PHOMEMO_PREFERRED_MTU) == 128);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "check"
    result = subprocess.run(
        ["g++", "-std=c++11", f"-I{root / 'src'}", "-x", "c++", "-", "-o", str(binary)],
        input=source, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
