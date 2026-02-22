/**
 * RF handler: SX127x OOK control + GPIO32 capture/replay.
 *
 * Capture strategy:
 *   - ISR records raw edge timestamps into a ring buffer
 *   - After capture stops (or timeout), post-process:
 *     find the longest contiguous run of "sane" pulses (100-15000 us)
 *     separated by frame gaps (~9000 us which ARE kept as negative values)
 *     and discard leading/trailing noise.
 */

#include "rf_handler.h"
#include "config.h"
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static SPIClass* g_spi = nullptr;
static SX1278* g_radio = nullptr;
static bool g_radioOk = false;

// --- Raw edge ring buffer (ISR writes, main reads after stop) ---
struct EdgeRecord {
  unsigned long timeUs;
  uint8_t level;
};

static const unsigned int EDGE_BUF_SIZE = 2048;
static volatile EdgeRecord g_edges[EDGE_BUF_SIZE];
static volatile unsigned int g_edgeHead = 0;
static volatile bool g_captureRunning = false;
static volatile unsigned long g_captureStartUs = 0;

// Processed output
static std::vector<int32_t> g_capturePulses;

static void IRAM_ATTR rfCaptureIsr() {
  if (!g_captureRunning) return;
  unsigned int head = g_edgeHead;
  if (head >= EDGE_BUF_SIZE) return;
  g_edges[head].timeUs = micros();
  g_edges[head].level = (uint8_t)digitalRead(RF_DATA_PIN);
  g_edgeHead = head + 1;
}

// Post-process edges into clean pulses
static void processEdges() {
  g_capturePulses.clear();
  unsigned int count = g_edgeHead;
  if (count < 2) return;

  // Build raw pulse list from edges
  std::vector<int32_t> raw;
  raw.reserve(count);
  for (unsigned int i = 1; i < count; i++) {
    long dt = (long)(g_edges[i].timeUs - g_edges[i - 1].timeUs);
    if (dt <= 0) continue;
    uint8_t prevLevel = g_edges[i - 1].level;
    int32_t pulse = prevLevel ? (int32_t)dt : -(int32_t)dt;
    raw.push_back(pulse);
  }

  if (raw.size() < SIGNAL_MIN_PULSES) return;

  // Find the best contiguous segment of "real" pulses.
  // A "real" pulse has abs value between SIGNAL_MIN_PULSE_US and SIGNAL_MAX_PULSE_US,
  // OR is a frame gap (abs value between SIGNAL_MAX_PULSE_US and SIGNAL_GAP_US).
  // A gap > SIGNAL_GAP_US means dead air (not part of the signal).

  int bestStart = -1, bestLen = 0;
  int curStart = -1, curLen = 0;

  for (int i = 0; i < (int)raw.size(); i++) {
    int32_t absVal = raw[i] < 0 ? -raw[i] : raw[i];
    bool isSignalPulse = (absVal >= SIGNAL_MIN_PULSE_US && absVal <= SIGNAL_MAX_PULSE_US);
    bool isFrameGap = (absVal > SIGNAL_MAX_PULSE_US && absVal <= SIGNAL_GAP_US);

    if (isSignalPulse || isFrameGap) {
      if (curStart < 0) curStart = i;
      curLen++;
    } else {
      if (curLen > bestLen) { bestStart = curStart; bestLen = curLen; }
      curStart = -1;
      curLen = 0;
    }
  }
  if (curLen > bestLen) { bestStart = curStart; bestLen = curLen; }

  if (bestStart < 0 || bestLen < (int)SIGNAL_MIN_PULSES) return;

  // Copy the best segment, capped at MAX_RAW_PULSES
  int end = bestStart + bestLen;
  if (end - bestStart > MAX_RAW_PULSES) end = bestStart + MAX_RAW_PULSES;
  for (int i = bestStart; i < end; i++)
    g_capturePulses.push_back(raw[i]);
}

bool rfBegin() {
  g_spi = new SPIClass(HSPI);
  g_spi->begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SX127X_CS_PIN);
  SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
  Module* mod = new Module(SX127X_CS_PIN, SX127X_DIO0_PIN, SX127X_RST_PIN, SX127X_DIO1_PIN, *g_spi, spiSettings);
  g_radio = new SX1278(mod);

  int st = g_radio->beginFSK();
  if (st != RADIOLIB_ERR_NONE) { g_radioOk = false; return false; }

  st = g_radio->setFrequency(SX127X_FREQ_MHZ);
  if (st != RADIOLIB_ERR_NONE) { g_radioOk = false; return false; }

  st = g_radio->setOOK(true);
  if (st != RADIOLIB_ERR_NONE) { g_radioOk = false; return false; }

  st = g_radio->setBitRate(5.0f);
  if (st != RADIOLIB_ERR_NONE) { /* non-fatal */ }

  st = g_radio->setRxBandwidth(SX127X_BANDWIDTH_KHZ);
  if (st != RADIOLIB_ERR_NONE) { /* non-fatal */ }

  // OOK peak mode with floor threshold for reliable decoding
  g_radio->setOokThresholdType(RADIOLIB_SX127X_OOK_THRESH_PEAK);
  g_radio->setOokFixedOrFloorThreshold(0x0C);

  g_radio->standby();
  g_radioOk = true;
  pinMode(RF_DATA_PIN, INPUT);
  return true;
}

void rfSetStandby() { if (g_radioOk) g_radio->standby(); }
void rfSetRx()      { if (g_radioOk) g_radio->receiveDirect(); }
void rfSetTx()      { if (g_radioOk) g_radio->transmitDirect(); }
bool rfIsOk()       { return g_radioOk; }

void rfCaptureStart() {
  g_capturePulses.clear();
  g_edgeHead = 0;
  g_captureRunning = true;
  g_captureStartUs = micros();
  rfSetRx();
  pinMode(RF_DATA_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RF_DATA_PIN), rfCaptureIsr, CHANGE);
}

void rfCaptureStop() {
  g_captureRunning = false;
  detachInterrupt(digitalPinToInterrupt(RF_DATA_PIN));
  rfSetStandby();
  processEdges();
}

bool rfCaptureRunning() { return g_captureRunning; }

bool rfCaptureShouldAutoStop() {
  if (!g_captureRunning) return false;
  unsigned int count = g_edgeHead;
  if (count < SIGNAL_MIN_PULSES * 2) return false;
  unsigned long lastEdge = g_edges[count - 1].timeUs;
  unsigned long now = micros();
  unsigned long elapsed = now - lastEdge;
  return (elapsed > (unsigned long)(SIGNAL_GAP_US * 3));
}

void rfCaptureGetPulses(std::vector<int32_t>& out) {
  out = g_capturePulses;
}

unsigned int rfCaptureCount() {
  if (g_captureRunning) return g_edgeHead;
  return (unsigned int)g_capturePulses.size();
}

void rfReplayRaw(const std::vector<int32_t>& pulses) {
  if (pulses.empty()) return;
  rfReplayRaw(pulses.data(), (unsigned int)pulses.size());
}

void rfReplayRaw(const int32_t* pulses, unsigned int len) {
  if (!len) return;
  rfSetStandby();
  pinMode(RF_DATA_PIN, OUTPUT);
  digitalWrite(RF_DATA_PIN, LOW);
  rfSetTx();
  delayMicroseconds(500);
  for (unsigned int i = 0; i < len; i++) {
    int32_t p = pulses[i];
    int us = (int)(p < 0 ? -p : p);
    if (us > 0) {
      digitalWrite(RF_DATA_PIN, p > 0 ? HIGH : LOW);
      delayMicroseconds(us);
    }
  }
  digitalWrite(RF_DATA_PIN, LOW);
  rfSetStandby();
  rfSetRx();
  pinMode(RF_DATA_PIN, INPUT);
}

static void pushRcPulses(std::vector<int32_t>& out, const char* code,
  int pulseLengthUs, bool inverted, int zH, int zL, int oH, int oL) {
  for (const char* p = code; *p; p++) {
    int highMul = (*p == '1') ? oH : zH;
    int lowMul = (*p == '1') ? oL : zL;
    if (inverted) { int t = highMul; highMul = lowMul; lowMul = t; }
    int highUs = highMul * pulseLengthUs;
    int lowUs = lowMul * pulseLengthUs;
    if (highUs > 0) out.push_back((int32_t)highUs);
    if (lowUs > 0) out.push_back(-(int32_t)lowUs);
  }
}

void rfReplayRc(const char* code, int pulseLengthUs, bool inverted,
  int zeroHigh, int zeroLow, int oneHigh, int oneLow, int repeat) {
  std::vector<int32_t> pulses;
  for (int r = 0; r < repeat; r++) {
    pushRcPulses(pulses, code, pulseLengthUs, inverted,
      zeroHigh, zeroLow, oneHigh, oneLow);
    if (r < repeat - 1)
      pulses.push_back(-(int32_t)(2 * pulseLengthUs));
  }
  if (!pulses.empty()) rfReplayRaw(pulses);
}

void rfReplayButton(const SavedButton& btn) {
  if (btn.type == "raw" && btn.rawPulses.length() > 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, btn.rawPulses);
    if (!err && doc.is<JsonArray>()) {
      std::vector<int32_t> pulses;
      for (JsonVariant v : doc.as<JsonArray>())
        pulses.push_back((int32_t)v.as<int>());
      rfReplayRaw(pulses);
    }
  } else if (btn.type == "rc" && btn.rcCode.length() > 0) {
    const RcProtocol& p = btn.rcProtocol;
    int pl = p.pulseLengthUs > 0 ? p.pulseLengthUs : 305;
    rfReplayRc(btn.rcCode.c_str(), pl, p.inverted,
      p.zeroHigh, p.zeroLow, p.oneHigh, p.oneLow, 10);
  }
}
