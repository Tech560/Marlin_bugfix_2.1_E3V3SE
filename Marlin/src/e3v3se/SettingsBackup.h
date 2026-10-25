/** E3V3SE settings backup: the Z-offset, bed mesh and machine settings as a
 *  file on the SD card, restorable after a firmware update. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_SETTINGS_BACKUP)

  #define SETTINGS_BACKUP_FILE "settings.bak"

  namespace settingsBackup {

    enum class Result : uint8_t {
      Saved,       // the file was written
      Restored,    // the file was read and applied
      NotMounted,  // no card in the reader
      Busy,        // a print is running or paused
      NoBackup,    // no backup file on the card
      Empty,       // the file holds no settings command
      Failed       // the card refused to write or read
    };

    /// Write the current settings to the backup file. Refused while printing.
    Result save();

    /// Apply the backup file and store the result in EEPROM.
    Result restore();

    /// True when the result means the file was written or applied.
    bool ok(const Result result);

    /// One line for the LCD title and the serial log.
    const char *result_text(const Result result);

  } // namespace settingsBackup

#endif // DWIN_SETTINGS_BACKUP
