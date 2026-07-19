#include <M5Cardputer.h>
#include "audio_out.hpp"
#include "config.hpp"

static AudioOut audioOut;

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setCursor(4, 4);
  M5Cardputer.Display.println("cardputer_asv_mp3");

  if (audioOut.begin()) {
    M5Cardputer.Display.println("audio: ok");
    if (cfg::kBootBeep) audioOut.playTestBeep(440, 300);
  } else {
    M5Cardputer.Display.println("audio: FAIL");
  }
}

void loop() {
  M5Cardputer.update();
  audioOut.updateAmpFromHp();
  delay(50);
}
