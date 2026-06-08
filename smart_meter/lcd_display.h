#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include <MFRC522.h>

void lcd_init();
void lcd_showTapCard();
void lcd_updateParameters();

#endif