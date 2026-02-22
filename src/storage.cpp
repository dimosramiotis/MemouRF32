/**
 * Storage implementation - LittleFS JSON for saved RF buttons.
 */

#include "storage.h"
#include "config.h"
#include <LittleFS.h>
#include <algorithm>

static std::vector<SavedButton> s_buttons;
static bool s_begun = false;

bool storageBegin() {
  if (s_begun) return true;
  if (!LittleFS.begin(true)) {
    return false;
  }
  s_begun = true;
  s_buttons = storageLoadButtons();
  return true;
}

std::vector<SavedButton> storageLoadButtons() {
  std::vector<SavedButton> out;
  if (!s_begun && !LittleFS.begin(true)) return out;

  if (!LittleFS.exists(BUTTONS_FILE)) return out;

  File f = LittleFS.open(BUTTONS_FILE, "r");
  if (!f) return out;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return out;

  JsonArray arr = doc["buttons"].as<JsonArray>();
  for (JsonObject o : arr) {
    SavedButton b;
    b.id = o["id"].as<String>();
    b.name = o["name"].as<String>();
    b.type = o["type"].as<String>();
    b.rawPulses = o["rawPulses"].as<String>();
    b.rcCode = o["rcCode"].as<String>();
    if (o["rcProtocol"].is<JsonObject>()) {
      JsonObject p = o["rcProtocol"];
      b.rcProtocol.inverted = p["inverted"];
      b.rcProtocol.pulseLengthUs = p["pulseLength"].as<int>();
      b.rcProtocol.zeroHigh = p["zeroHigh"].as<int>();
      b.rcProtocol.zeroLow = p["zeroLow"].as<int>();
      b.rcProtocol.oneHigh = p["oneHigh"].as<int>();
      b.rcProtocol.oneLow = p["oneLow"].as<int>();
    }
    if (b.id.length() && b.name.length()) out.push_back(b);
  }
  return out;
}

bool storageSaveButtons(const std::vector<SavedButton>& buttons) {
  if (!s_begun) return false;

  JsonDocument doc;
  JsonArray arr = doc["buttons"].to<JsonArray>();
  for (const auto& b : buttons) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = b.id;
    o["name"] = b.name;
    o["type"] = b.type;
    o["rawPulses"] = b.rawPulses;
    o["rcCode"] = b.rcCode;
    JsonObject p = o["rcProtocol"].to<JsonObject>();
    p["inverted"] = b.rcProtocol.inverted;
    p["pulseLength"] = b.rcProtocol.pulseLengthUs;
    p["zeroHigh"] = b.rcProtocol.zeroHigh;
    p["zeroLow"] = b.rcProtocol.zeroLow;
    p["oneHigh"] = b.rcProtocol.oneHigh;
    p["oneLow"] = b.rcProtocol.oneLow;
  }

  File f = LittleFS.open(BUTTONS_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  s_buttons = buttons;
  return true;
}

bool storageAddButton(const SavedButton& btn) {
  s_buttons = storageLoadButtons();
  for (const auto& b : s_buttons)
    if (b.id == btn.id) return false;
  s_buttons.push_back(btn);
  return storageSaveButtons(s_buttons);
}

bool storageRemoveButton(const String& id) {
  s_buttons = storageLoadButtons();
  s_buttons.erase(
    std::remove_if(s_buttons.begin(), s_buttons.end(),
      [&id](const SavedButton& b) { return b.id == id; }),
    s_buttons.end()
  );
  return storageSaveButtons(s_buttons);
}

bool storageUpdateButton(const String& id, const SavedButton& btn) {
  s_buttons = storageLoadButtons();
  for (size_t i = 0; i < s_buttons.size(); i++) {
    if (s_buttons[i].id == id) {
      s_buttons[i] = btn;
      s_buttons[i].id = id;
      return storageSaveButtons(s_buttons);
    }
  }
  return false;
}
