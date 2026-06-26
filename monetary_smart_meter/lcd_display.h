#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>

// ============================================================================
// PUBLIC SCREEN CONTROLLER PROTOTYPES
// ============================================================================

/**
 * @brief Initializes the physical 16x2 I2C Liquid Crystal display panel,
 * turns on the background backlight, and displays a temporary system boot screen.
 */
void lcd_init();

/**
 * @brief Cycles through multiple info layout dashboards (Wallet Balance,
 * Grid Voltage/Current, Real Power/Power Factor, and Live Shadow Time components).
 * This runs inside a non-blocking background task scheduler reading metrics purely out of RAM.
 */
void lcd_updateParameters();

/**
 * @brief Asynchronously flashes a temporary confirmation layout indicating
 * that an over-the-air recharge transaction was successful.
 * @param newBalance The updated wallet amount to show to the consumer.
 */
void lcd_showCardAccepted(float newBalance);

#endif