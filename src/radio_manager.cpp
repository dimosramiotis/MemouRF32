/**
 * RadioManager implementation.
 * Manages the SX1278 radio shared between OOK (RF capture/replay) and LoRa (node comms).
 */

#include "radio_manager.h"
#include "config.h"

static SPIClass*  s_spi   = nullptr;
static SX1278*    s_radio = nullptr;
static bool       s_ok    = false;
static RadioMode  s_mode  = RADIO_UNINIT;

bool radioInit() {
  if (s_ok) return true;

  s_spi = new SPIClass(HSPI);
  s_spi->begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SX127X_CS_PIN);
  SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
  Module* mod = new Module(SX127X_CS_PIN, SX127X_DIO0_PIN, SX127X_RST_PIN,
                           SX127X_DIO1_PIN, *s_spi, spiSettings);
  s_radio = new SX1278(mod);

  int st = s_radio->beginFSK();
  if (st != RADIOLIB_ERR_NONE) { s_ok = false; return false; }

  st = s_radio->setFrequency(SX127X_FREQ_MHZ);
  if (st != RADIOLIB_ERR_NONE) { s_ok = false; return false; }

  s_radio->standby();
  s_ok   = true;
  s_mode = RADIO_STANDBY;
  return true;
}

SX1278* radioGetHal() { return s_radio; }
RadioMode radioGetMode() { return s_mode; }
bool radioIsOk() { return s_ok; }

bool radioSwitchToOOK() {
  if (!s_ok) return false;
  s_radio->standby();

  int st = s_radio->beginFSK();
  if (st != RADIOLIB_ERR_NONE) return false;

  st = s_radio->setFrequency(SX127X_FREQ_MHZ);
  if (st != RADIOLIB_ERR_NONE) return false;

  st = s_radio->setOOK(true);
  if (st != RADIOLIB_ERR_NONE) return false;

  s_radio->setBitRate(5.0f);
  s_radio->setRxBandwidth(SX127X_BANDWIDTH_KHZ);
  s_radio->setOokThresholdType(RADIOLIB_SX127X_OOK_THRESH_PEAK);
  s_radio->setOokFixedOrFloorThreshold(0x0C);
  s_radio->standby();

  s_mode = RADIO_STANDBY;
  return true;
}

bool radioSwitchToLoRa() {
  if (!s_ok) return false;
  s_radio->standby();

  int st = s_radio->begin(SX127X_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                          LORA_SYNC_WORD, LORA_TX_POWER_DBM, LORA_PREAMBLE, 0);
  if (st != RADIOLIB_ERR_NONE) return false;

  s_mode = RADIO_STANDBY;
  return true;
}

void radioSetStandby() {
  if (!s_ok) return;
  s_radio->standby();
  s_mode = RADIO_STANDBY;
}

void radioSetOokRx() {
  if (!s_ok) return;
  s_radio->receiveDirect();
  s_mode = RADIO_OOK_RX;
}

void radioSetOokTx() {
  if (!s_ok) return;
  s_radio->transmitDirect();
  s_mode = RADIO_OOK_TX;
}

bool radioStartLoRaRx() {
  if (!s_ok) return false;
  int st = s_radio->startReceive();
  if (st != RADIOLIB_ERR_NONE) return false;
  s_mode = RADIO_LORA_RX;
  return true;
}
