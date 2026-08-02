#include <Adafruit_GFX.h>

class EPaperDisplay : public Adafruit_GFX {
public:
  EPaperDisplay() : Adafruit_GFX(WIDTH, HEIGHT) {}
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void display();
private:
  static const int WIDTH = 200;
  static const int HEIGHT = 200;
  static const int BYTES_PER_ROW = WIDTH / 4;
  static const int BUFFER_SIZE = ((HEIGHT - 1) * BYTES_PER_ROW + (WIDTH - 1) / 4) + 1;
  uint8_t buffer[BUFFER_SIZE];
};