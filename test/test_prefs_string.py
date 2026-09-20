#!/usr/bin/env python3
"""Run the shared preferences writer against Arduino's ambiguous empty-string result."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'hardware').mkdir()
    (tmp / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  bool isEmpty() const { return empty(); }
};
inline unsigned long millis() { return 1; }
struct SerialClass { template<class... T> void printf(const char*, T...) {} };
extern SerialClass Serial;
''')
    (tmp / 'hardware/sd_logger.h').write_text('#pragma once\ninline void logSDf(const char*, ...) {}\n')
    prefs_header = '''#pragma once
#include <Arduino.h>
#include <cassert>
#include <map>
#include <cstring>
extern std::map<std::string, std::string> stored;
extern bool fail_put, fail_read;
extern int opens, closes, fail_open_at;
class Preferences {
  bool active=false;
 public:
  bool begin(const char*, bool) { assert(!active); active=(++opens != fail_open_at); return active; }
  void end() { assert(active); active=false; ++closes; }
  bool isKey(const char* key) { assert(active); return stored.count(key); }
  size_t putString(const char* key, const char* value) {
    assert(active);
    if (fail_put) return 0;
    stored[key]=value;
    return strlen(value); // Arduino core 2.0.17 returns zero for a successful empty write.
  }
  String getString(const char* key, const char* fallback="") {
    assert(active);
    return !fail_read && stored.count(key) ? String(stored[key]) : String(fallback);
  }
'''
    for name, ctype in [('Float', 'float'), ('Int', 'int'), ('UInt', 'uint32_t'),
                        ('UChar', 'uint8_t'), ('Bool', 'bool')]:
        prefs_header += f'  {ctype} get{name}(const char*, {ctype} fallback) {{ return fallback; }}\n'
        prefs_header += f'  size_t put{name}(const char*, {ctype}) {{ return 1; }}\n'
    (tmp / 'Preferences.h').write_text(prefs_header + '};\n')
    source = r'''
#include <Preferences.h>
#include "services/prefs_store.h"
SerialClass Serial;
std::map<std::string, std::string> stored;
bool fail_put=false, fail_read=false;
int opens=0, closes=0, fail_open_at=0;
int main() {
  // An unsuccessful clear must not be reported as a success.
  stored["key"]="old"; fail_put=true;
  assert(!prefsPutString("key", ""));
  assert(stored["key"]=="old");
  stored.clear();
  assert(!prefsPutString("key", ""));
  assert(!stored.count("key"));

  fail_put=false;
  int before=opens;
  assert(prefsPutString("key", ""));
  assert(opens==before+2 && opens==closes); // Verify after closing and reopening.
  assert(stored.count("key") && stored["key"].empty());
  assert(prefsGetString("key", "fallback").isEmpty()); // Stored empty is not absent.

  before=opens;
  assert(prefsPutString("key", "new"));
  assert(stored["key"]=="new" && opens==before+1);
  fail_put=true; before=opens;
  assert(!prefsPutString("key", "replacement"));
  assert(stored["key"]=="new" && opens==before+1);

  fail_put=false; fail_read=true;
  assert(!prefsPutString("key", "")); // A read error cannot verify the write.
  fail_read=false; fail_open_at=opens+2;
  assert(!prefsPutString("key", "")); // Nor can a failed verification reopen.
}
'''
    result = subprocess.run(
        ['g++', '-std=c++11', f'-I{tmp}', f'-I{root / "src"}', '-x', 'c++', '-',
         str(root / 'src/services/prefs_store.cpp'), '-o', str(tmp / 'check')],
        input=source, text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(tmp / 'check')], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    print('preferences string checks passed')
