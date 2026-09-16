#pragma once

struct FilaManPrintPending {
  bool pending = false;
  int spool_id = 0;
  int preset_id = 0;

  bool request(int spool, int preset) {
    if (pending) return false;
    spool_id = spool;
    preset_id = preset;
    pending = true;
    return true;
  }
  void cancel() { pending = false; }
  bool take(int* spool, int* preset) {
    if (!pending) return false;
    pending = false;
    *spool = spool_id;
    *preset = preset_id;
    return true;
  }
};
