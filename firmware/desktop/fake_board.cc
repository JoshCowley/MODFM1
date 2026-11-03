// A pretend FM-1 for the computer: implements fm1_platform.h's fm1_hal_* functions, then drives the app the
// way the board would (64-frame audio blocks, key presses from the main context) and writes the result to
// a WAV file. It checks the app glue (call queue, audio conversion, keys, MIDI), not the hardware.
//   build/fake_board out.wav
#include "../hw/fm1_platform.h"
#include "../engine/rack.h"
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <vector>

extern "C" {
int fm1_hal_sample_rate(void) { return 48000; }
void fm1_hal_display_rect(int, int, int, int, const uint16_t*) {}
void fm1_hal_led(int, uint8_t) {}
void fm1_hal_midi_out(const uint8_t*, int) {}
int fm1_hal_flash_read(uint32_t, void*, uint32_t) { return -1; }
int fm1_hal_flash_write(uint32_t, const void*, uint32_t) { return -1; }
uint32_t fm1_hal_flash_size(void) { return 0; }
uint32_t fm1_hal_cycles(void) { return 0; }
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "fake_board.wav";
  const int BLOCK = 64, SECS = 4;
  std::vector<int16_t> out; int16_t buf[BLOCK * 2];
  fm1_app_init();
  double worst = 0, total = 0; int blocks = 0, peak = 0;
  for (int b = 0; b < SECS * 48000 / BLOCK; ++b) {
    double t = b * BLOCK / 48000.0;
    // the main context: a C major chord on the keys at 0 s (keys 7, 11, 14 = C4, E4, G4), released at 2 s,
    // and a MIDI note at 1 s
    if (b == 0) { fm1_app_key(7, 1, .8f); fm1_app_key(11, 1, .8f); fm1_app_key(14, 1, .8f); }
    if (b == (int)(1.0 * 48000 / BLOCK)) { const uint8_t on[3] = { 0x90, 72, 100 }; fm1_app_midi(on, 3); }
    if (b == (int)(2.0 * 48000 / BLOCK)) { fm1_app_key(7, 0, 0); fm1_app_key(11, 0, 0); fm1_app_key(14, 0, 0); const uint8_t off[3] = { 0x80, 72, 0 }; fm1_app_midi(off, 3); }
    fm1_app_ui((uint32_t)(t * 1000));
    // the audio interrupt
    auto t0 = std::chrono::steady_clock::now();
    fm1_app_audio(buf, BLOCK);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    worst = us > worst ? us : worst; total += us; ++blocks;
    for (int i = 0; i < BLOCK * 2; ++i) { out.push_back(buf[i]); int a = buf[i] < 0 ? -buf[i] : buf[i]; peak = a > peak ? a : peak; }
  }
  FILE* f = fopen(path, "wb");
  uint32_t bytes = (uint32_t)out.size() * 2, v;
  fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); uint16_t h[] = { 1, 2 }; fwrite(h, 2, 2, f); v = 48000; fwrite(&v, 4, 1, f); v = 192000; fwrite(&v, 4, 1, f);
  uint16_t h2[] = { 4, 16 }; fwrite(h2, 2, 2, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f); fwrite(out.data(), 2, out.size(), f); fclose(f);
  double period = BLOCK / 48000.0 * 1e6;
  printf("%s: %d blocks of %d frames, peak %d/32767, this computer: average %.1f us, worst %.1f us of %.0f us per block\n",
         path, blocks, BLOCK, peak, total / blocks, worst, period);
#ifdef RACK_FM1
  printf("FM-1 engine: arena %d of %d bytes, %d voices fit\n", fm1_status(0), fm1_status(1), fm1_status(2));
#endif
  return peak > 0 ? 0 : 1;
}
