#pragma once
// =============================================================
//  CRASH_LOGGER.H v1.1.4 — Post-mortem reboot diagnostics
//
//  WHAT IT DOES
//  ------------
//  1. On every boot, reads `esp_reset_reason()` and appends a line to
//     /boot.log (LittleFS, ring-buffered to last 50 boots).
//  2. If reset_reason indicates abnormal reboot (PANIC / INT_WDT /
//     TASK_WDT / BROWNOUT), reads coredump summary (faulting task,
//     PC, top-of-stack backtrace) from the `coredump` partition and
//     writes a human-readable file to /crashes/crash_NN.txt.
//     Then erases the coredump partition (so next crash overwrites
//     cleanly, instead of always reading the first one).
//  3. Provides cached boot state (lastReason, bootCount, crashCount)
//     for status display in S_BATTERY settings screen.
//  TODO: web UI endpoints (/crashes, /boot-log) for remote viewing.
//
//  WHY IT MATTERS
//  --------------
//  ESP32 loses Serial on reboot. Without persistent crash log we
//  only see crashes if a serial monitor is attached. With this we
//  can plug the device in *after* a crash and still inspect the cause.
//
//  STORAGE FOOTPRINT
//  -----------------
//  - /boot.log:        50 lines × ~80 B = ~4 KB
//  - /crashes/*.txt:   capped at 10 files × ~512 B = ~5 KB
//  Total ~10 KB on a ~9.875 MB LittleFS — negligible.
//
//  DEPENDENCIES
//  ------------
//  Requires LittleFS (mounted in setup() before crashLogger.init()).
//  Requires esp_core_dump.h from arduino-esp32 core 3.x.
//  Requires partition table with `coredump` partition (we have one
//  since v1.1.2 — 64 KB at 0x3F0000).
// =============================================================
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>
#include "esp_core_dump.h"
#include "config.h"
#include "error_handler.h"

// ── Tunables ─────────────────────────────────────────────────
#define CRASH_LOG_BOOT_FILE     "/boot.log"
#define CRASH_LOG_DIR           "/crashes"
#define CRASH_LOG_MAX_BOOTS     50
#define CRASH_LOG_MAX_CRASHES   10
#define CRASH_LOG_LINE_MAX      256

// ── Reset reason → human string ──────────────────────────────
static const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT_RST";
    case ESP_RST_SW:        return "SW_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// ── True if this reset was abnormal (we want a crash dump) ───
static bool isAbnormalReset(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC    ||
         r == ESP_RST_INT_WDT  ||
         r == ESP_RST_TASK_WDT ||
         r == ESP_RST_WDT      ||
         r == ESP_RST_BROWNOUT;
}

class CrashLogger {
public:
  // Cached at boot, queryable later (for S_BATTERY settings screen)
  esp_reset_reason_t lastReason = ESP_RST_UNKNOWN;
  bool   wasAbnormal      = false;
  uint16_t bootCount      = 0;
  uint16_t crashCount     = 0;

  // Call ONCE in setup(), AFTER LittleFS.begin() succeeds.
  void init() {
    lastReason = esp_reset_reason();
    wasAbnormal = isAbnormalReset(lastReason);

    Serial.printf("[BOOT] Reset reason: %s\n", resetReasonStr(lastReason));

    if (!LittleFS.begin(false)) {
      Serial.println("[CRASH_LOG] LittleFS not mounted, skipping");
      return;
    }

    _ensureDir();
    _appendBootLog();
    _countCrashes();

    if (wasAbnormal) {
      Serial.printf("[CRASH_LOG] Abnormal reset detected: %s\n",
                    resetReasonStr(lastReason));
      // Set persistent error flag so it shows up in error list until cleared
      gErrors.set(ERR_LAST_PANIC, SEV_WARN);

      _saveCoredumpSummary();
    }
  }

  // For settings screen: how many crash files exist
  uint16_t getCrashCount() const { return crashCount; }

  // Erase all crash files (called from settings or future web UI)
  void clearCrashes() {
    File dir = LittleFS.open(CRASH_LOG_DIR);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
      String path = String(CRASH_LOG_DIR) + "/" + f.name();
      f.close();
      LittleFS.remove(path);
      f = dir.openNextFile();
    }
    crashCount = 0;
    gErrors.clear(ERR_LAST_PANIC);
    Serial.println("[CRASH_LOG] All crashes cleared");
  }

private:
  void _ensureDir() {
    if (!LittleFS.exists(CRASH_LOG_DIR)) {
      LittleFS.mkdir(CRASH_LOG_DIR);
    }
  }

  // Append one line to /boot.log, rotating if too long.
  void _appendBootLog() {
    // Read existing log, count lines, take last N-1, append new line.
    String existing = "";
    File f = LittleFS.open(CRASH_LOG_BOOT_FILE, "r");
    uint16_t lineCount = 0;
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() > 0) {
          existing += line + "\n";
          lineCount++;
        }
      }
      f.close();
    }

    // Rotate: keep only last CRASH_LOG_MAX_BOOTS-1
    if (lineCount >= CRASH_LOG_MAX_BOOTS) {
      int keep = CRASH_LOG_MAX_BOOTS - 1;
      int firstNl = 0;
      int trim = lineCount - keep;
      while (trim > 0 && firstNl >= 0) {
        firstNl = existing.indexOf('\n');
        if (firstNl >= 0) existing.remove(0, firstNl + 1);
        trim--;
      }
    }
    bootCount = lineCount + 1;

    char line[CRASH_LOG_LINE_MAX];
    snprintf(line, sizeof(line),
             "[%04u] up=%lus reason=%s heap=%uKB psram=%uKB ver=%s\n",
             bootCount,
             (unsigned long)(millis() / 1000),
             resetReasonStr(lastReason),
             (unsigned)(ESP.getFreeHeap() / 1024),
             psramFound() ? (unsigned)(ESP.getFreePsram() / 1024) : 0,
             FW_VERSION);

    File w = LittleFS.open(CRASH_LOG_BOOT_FILE, "w");
    if (w) {
      w.print(existing);
      w.print(line);
      w.close();
    } else {
      Serial.println("[CRASH_LOG] Failed to write boot.log");
    }
  }

  void _countCrashes() {
    crashCount = 0;
    File dir = LittleFS.open(CRASH_LOG_DIR);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f) {
      crashCount++;
      f = dir.openNextFile();
    }
  }

  // Read coredump from partition and write a human-readable summary
  // to /crashes/crash_NN.txt, then erase the coredump partition.
  void _saveCoredumpSummary() {
    esp_err_t check = esp_core_dump_image_check();
    if (check != ESP_OK) {
      Serial.printf("[CRASH_LOG] No valid coredump (err=0x%x)\n", check);
      // Still record the abnormal reboot so we know it happened
      _writeCrashFile("(no coredump available)\n");
      return;
    }

    esp_core_dump_summary_t* summary =
      (esp_core_dump_summary_t*)heap_caps_malloc(
        sizeof(esp_core_dump_summary_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!summary) {
      Serial.println("[CRASH_LOG] OOM allocating summary");
      _writeCrashFile("(summary alloc failed)\n");
      return;
    }

    esp_err_t r = esp_core_dump_get_summary(summary);
    if (r != ESP_OK) {
      Serial.printf("[CRASH_LOG] get_summary failed 0x%x\n", r);
      _writeCrashFile("(summary read failed)\n");
      free(summary);
      return;
    }

    // Build human-readable text
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
      "Reset reason : %s\n"
      "Faulting task: %s\n"
      "Exception PC : 0x%08lx\n"
      "Backtrace    : ",
      resetReasonStr(lastReason),
      summary->exc_task,
      (unsigned long)summary->exc_pc);

    for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
      n += snprintf(buf + n, sizeof(buf) - n,
                    "0x%08lx ",
                    (unsigned long)summary->exc_bt_info.bt[i]);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "\n");

    _writeCrashFile(buf);
    free(summary);

    // Erase coredump partition so next crash starts fresh
    esp_core_dump_image_erase();
    Serial.println("[CRASH_LOG] Coredump saved and partition erased");
  }

  void _writeCrashFile(const char* content) {
    // Find next free crash_NN.txt slot
    if (crashCount >= CRASH_LOG_MAX_CRASHES) {
      // Overwrite oldest
      String oldest = String(CRASH_LOG_DIR) + "/crash_00.txt";
      LittleFS.remove(oldest);
      // Shift down: 01→00, 02→01, ...
      for (uint16_t i = 1; i < CRASH_LOG_MAX_CRASHES; i++) {
        char from[32], to[32];
        snprintf(from, sizeof(from), "%s/crash_%02u.txt", CRASH_LOG_DIR, i);
        snprintf(to,   sizeof(to),   "%s/crash_%02u.txt", CRASH_LOG_DIR, i-1);
        LittleFS.rename(from, to);
      }
      crashCount = CRASH_LOG_MAX_CRASHES - 1;
    }

    char path[32];
    snprintf(path, sizeof(path),
             "%s/crash_%02u.txt", CRASH_LOG_DIR, crashCount);
    File f = LittleFS.open(path, "w");
    if (f) {
      f.print(content);
      f.close();
      crashCount++;
      Serial.printf("[CRASH_LOG] Saved %s\n", path);
    } else {
      Serial.printf("[CRASH_LOG] Failed to open %s\n", path);
    }
  }
};

extern CrashLogger gCrash;
