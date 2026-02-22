/**
 * Persist saved RF buttons (name + code) to LittleFS.
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <ArduinoJson.h>
#include <vector>

struct RcProtocol {
  bool inverted;
  int pulseLengthUs;
  int zeroHigh;
  int zeroLow;
  int oneHigh;
  int oneLow;
};

struct SavedButton {
  String id;
  String name;
  String type;   // "raw" | "rc"
  // For raw: JSON array of pulse lengths (positive = high, negative = low)
  String rawPulses;  // e.g. "[470,-444,1071,...]"
  // For rc:
  String rcCode;     // binary string e.g. "001001111000..."
  RcProtocol rcProtocol;
};

bool storageBegin();
std::vector<SavedButton> storageLoadButtons();
bool storageSaveButtons(const std::vector<SavedButton>& buttons);
bool storageAddButton(const SavedButton& btn);
bool storageRemoveButton(const String& id);
bool storageUpdateButton(const String& id, const SavedButton& btn);

#endif // STORAGE_H
