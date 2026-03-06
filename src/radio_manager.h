/**
 * RadioManager: SX127x mode switching between OOK (direct mode) and LoRa (packet mode).
 *
 * The SX1278 can only operate in one mode at a time. This layer provides a
 * borrow/release API so modules can temporarily switch the radio mode.
 *
 * In standalone mode, the radio stays in OOK (existing behavior, no change).
 * In gateway/remote mode, the default is LoRa RX. OOK operations temporarily
 * borrow the radio and release it back to LoRa when done.
 */

#ifndef RADIO_MANAGER_H
#define RADIO_MANAGER_H

#include <RadioLib.h>
#include "config.h"

enum RadioMode : uint8_t {
  RADIO_UNINIT,
  RADIO_STANDBY,
  RADIO_OOK_RX,
  RADIO_OOK_TX,
  RADIO_LORA_RX,
  RADIO_LORA_TX,
};

bool        radioInit();
SX1278*     radioGetHal();
RadioMode   radioGetMode();

bool radioSwitchToOOK();
bool radioSwitchToLoRa();

void radioSetStandby();
void radioSetOokRx();
void radioSetOokTx();
bool radioStartLoRaRx();

bool radioIsOk();

#endif // RADIO_MANAGER_H
