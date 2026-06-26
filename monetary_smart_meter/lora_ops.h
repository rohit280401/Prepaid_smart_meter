#ifndef LORA_OPS_H
#define LORA_OPS_H

#include <Arduino.h>

// ============================================================================
// NETWORK CONTROLLER INTERFACE PROTOTYPES
// ============================================================================

/**
 * @brief Asynchronously extracts data out of the LoRa FIFO buffer following a hardware event.
 * Validates the signature, ensures uniqueness using the TXID index, processes balances/tariffs,
 * and replies with an immediate acknowledgment frame.
 */
void handleIncomingLoRaPacket();

/**
 * @brief Transmits standard tracking metrics (V, I, P, PF, Balance, dynamic rates, and uptime)
 * back to the central base station gateway using a non-blocking scheduler interval.
 */
void sendLoRaTelemetry();

#endif