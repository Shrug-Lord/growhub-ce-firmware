#pragma once
#define GPIO_MODE_OUTPUT 1
void gpio_reset_pin(int pin);
void gpio_set_direction(int pin, int mode);
void gpio_set_level(int pin, int level);
