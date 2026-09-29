#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H
#include <Arduino.h>

void lcd_init();
void lcd_update();
void lcd_overlay(const char* a, const char* b);
void lcd_rechargeOk(int32_t mp);
void lcd_service();

#endif