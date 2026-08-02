#include <EPaperDisplay.h>
#include <EPD_1in54g.h>

void EPaperDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if ((x > WIDTH - 1) || (y > HEIGHT - 1) || (x < 0) || (y < 0)) return;

  int byteIndex = y * (WIDTH/4) + x / 4;
  int shift = (3 - (x % 4)) * 2;
  uint8_t mask = ~(0b11 << shift);

  uint8_t cleanedBits = buffer[byteIndex] & mask;

  buffer[byteIndex] = cleanedBits | (color << shift);
}

void EPaperDisplay::display() {
  EPD_1IN54G_Display(buffer);
}