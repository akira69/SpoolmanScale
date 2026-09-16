#!/usr/bin/env python3
"""Behavioral response validation for a queued PC print request."""
import subprocess
from pathlib import Path

root = Path(__file__).parents[1]
source = r'''
#include <assert.h>
#include "services/filaman_print_request_parse.h"
int main() {
  int id = 0;
  assert(filamanParsePrintRequest("{\"id\":42,\"spool_id\":123,\"preset_id\":7}", 123, 7, &id) == 201 && id == 42);
  assert(filamanParsePrintRequest("{\"id\":43,\"spool_id\":123,\"preset_id\":null}", 123, 0, &id) == 201 && id == 43);
  assert(filamanParsePrintRequest("{\"id\":42,\"spool_id\":124,\"preset_id\":7}", 123, 7, &id) < 0 && id == 0);
  assert(filamanParsePrintRequest("{\"id\":0,\"spool_id\":123,\"preset_id\":7}", 123, 7, &id) < 0);
  assert(filamanParsePrintRequest("{\"id\":42,\"spool_id\":123,\"preset_id\":8}", 123, 7, &id) < 0);
  assert(filamanParsePrintRequest("{\"id\":42,\"spool_id\":123}", 123, 0, &id) < 0);
  assert(filamanParsePrintRequest("broken", 123, 7, &id) < 0);
}
'''
exe = "/tmp/test_filaman_print_request"
result = subprocess.run(["g++", "-std=c++11", f"-I{root / 'src'}", f"-I{root / '.pio/libdeps/wt32-sc01-plus/ArduinoJson/src'}", "-x", "c++", "-", "-o", exe], input=source, text=True, capture_output=True)
assert result.returncode == 0, result.stderr
result = subprocess.run([exe], capture_output=True, text=True)
assert result.returncode == 0, result.stderr
