/** E3V3SE SD card file management: guarded deletion from the LCD file browser.
 *
 * The check and the removal live here, away from the LCD, so they can be tested
 * on a host with a stubbed card reader.
 */
#include "../inc/MarlinConfig.h"

#include "SDFiles.h"

#include "../MarlinCore.h"
#include "../sd/cardreader.h"

#include <string.h>

namespace sdFiles {

  bool deletable_now() {
    if (!card.isMounted()) return false;
    // CardReader::isPaused() is also true for a file that is merely open for
    // reading, so ask the job state instead of the file state.
    return !card.isPrinting() && !marlin.printingIsActive();
  }

  DeleteResult remove_selected() {
    if (!card.isMounted()) return DeleteResult::NotMounted;
    if (!deletable_now()) return DeleteResult::Busy;
    if (card.flag.filenameIsDir) return DeleteResult::IsFolder;

    // removeFile() works on the working directory by name, so keep a copy of
    // the 8.3 name before the card reader moves it around.
    char dos[FILENAME_LENGTH];
    strncpy(dos, card.filename, sizeof(dos) - 1);
    dos[sizeof(dos) - 1] = '\0';
    if (!dos[0]) return DeleteResult::NoFile;

    card.removeFile(dos);
    return card.fileExists(dos) ? DeleteResult::Failed : DeleteResult::Deleted;
  }

  bool deleted(const DeleteResult result) { return result == DeleteResult::Deleted; }

  const char *result_text(const DeleteResult result) {
    switch (result) {
      case DeleteResult::Deleted:    return "File deleted";
      case DeleteResult::NotMounted: return "No SD card";
      case DeleteResult::Busy:       return "Printing: not deleted";
      case DeleteResult::IsFolder:   return "Not a file";
      case DeleteResult::NoFile:     return "Nothing selected";
      default:                       return "Could not delete";
    }
  }

} // namespace sdFiles
