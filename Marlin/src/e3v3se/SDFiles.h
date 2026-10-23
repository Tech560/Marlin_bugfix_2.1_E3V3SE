/** E3V3SE SD card file management: guarded deletion from the LCD file browser. */
#pragma once

#include "../inc/MarlinConfigPre.h"

namespace sdFiles {

  enum class DeleteResult : uint8_t {
    Deleted,      // the entry is gone
    NotMounted,   // no card in the reader
    Busy,         // a print is running or paused
    IsFolder,     // only files are deleted
    NoFile,       // nothing selected
    Failed        // the card refused the removal
  };

  /// True when the card is mounted and no job is printing or paused.
  bool deletable_now();

  /// Delete the entry the card reader has selected.
  DeleteResult remove_selected();

  /// True when the result means the file is gone.
  bool deleted(const DeleteResult result);

  /// One line for the LCD title and the serial log.
  const char *result_text(const DeleteResult result);

} // namespace sdFiles
