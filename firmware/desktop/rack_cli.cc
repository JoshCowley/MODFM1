// rack_cli: plays a trace (engine calls with frame times) through the engine on a computer.
//
//   rack_cli <trace> [--wav DIR] [--json OUT]
//
// Trace format, one item per line:
//   patch <name>                start a patch: the frame counter goes back to 0
//   <frame> <function> <args>   call an engine function at that frame (48 kHz)
//   end <frames>                render to that frame, then report the patch
// For each patch it prints its level, a 100 ms loudness envelope, the time it took, and (FM-1 build)
// the arena use and voices. tests/compare.mjs compares two of these reports.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <chrono>
#include <string>
#include <vector>
#include "../engine/rack.h"

static void wav_write(const char* path, const std::vector<float>& l, const std::vector<float>& r) {
  FILE* f = fopen(path, "wb"); if (!f) { perror(path); return; }
  uint32_t n = (uint32_t)l.size(), bytes = n * 4, sr = 48000, br = sr * 4;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(1); u16(2); u32(sr); u32(br); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(bytes);
  for (uint32_t i = 0; i < n; ++i) {
    float a[2] = { l[i], r[i] };
    for (float x : a) { x = x > 1 ? 1 : x < -1 ? -1 : x; int16_t s = (int16_t)lrintf(x * 32767.0f); fwrite(&s, 2, 1, f); }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: rack_cli <trace> [--wav DIR] [--json OUT]\n"); return 2; }
  const char* wav_dir = nullptr; const char* json_path = nullptr;
  for (int i = 2; i < argc; ++i) {
    if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_dir = argv[++i];
    else if (!strcmp(argv[i], "--json") && i + 1 < argc) json_path = argv[++i];
  }
  FILE* in = fopen(argv[1], "r"); if (!in) { perror(argv[1]); return 1; }
  FILE* js = json_path ? fopen(json_path, "w") : nullptr;
  if (js) fprintf(js, "{\n");
  bool first = true;
  init();
  std::string name; long frame = 0; double ms = 0;
  std::vector<float> L, R;
  const int CH = 128;
  auto render_to = [&](long target) {
    while (frame < target) {
      int n = (int)((target - frame) < CH ? (target - frame) : CH);
      auto t0 = std::chrono::steady_clock::now();
      render(n);
      ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      float* l = out_l(); float* r = out_r();
      for (int i = 0; i < n; ++i) { L.push_back(l[i]); R.push_back(r[i]); }
      frame += n;
    }
  };
  char line[4096];
  while (fgets(line, sizeof line, in)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0] || line[0] == '#') continue;
    if (!strncmp(line, "patch ", 6)) { name = line + 6; frame = 0; ms = 0; L.clear(); R.clear(); continue; }
    if (!strncmp(line, "end ", 4)) {
      render_to(atol(line + 4));
      double sum = 0, peak = 0; bool bad = false;
      std::vector<double> env;
      const int W = 4800;
      for (size_t i = 0; i < L.size(); ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true;
        sum += (double)L[i] * L[i] + (double)R[i] * R[i];
        peak = fmax(peak, fmax(fabs(L[i]), fabs(R[i])));
        if (i % W == W - 1) { double s = 0; for (size_t k = i + 1 - W; k <= i; ++k) s += (double)L[k] * L[k] + (double)R[k] * R[k]; env.push_back(sqrt(s / (2 * W))); }
      }
      double rms = sqrt(sum / (2.0 * (L.size() ? L.size() : 1)));
      double secs = L.size() / 48000.0;
#ifdef RACK_FM1
      printf("%-14s rms %.4f peak %.3f  cpu %5.1f%%  arena %6d/%d  voices %d/%d  unloaded 0x%x%s\n", name.c_str(), rms, peak,
             ms / (secs * 10.0), fm1_status(0), fm1_status(1), fm1_status(3), fm1_status(2), fm1_status(4), bad ? "  NaN!" : "");
#else
      printf("%-14s rms %.4f peak %.3f  cpu %5.1f%%%s\n", name.c_str(), rms, peak, ms / (secs * 10.0), bad ? "  NaN!" : "");
#endif
      if (js) {
        fprintf(js, "%s \"%s\": {\"rms\": %.6f, \"peak\": %.6f, \"bad\": %s, \"cpu_pct\": %.3f, \"env\": [", first ? "" : ",\n", name.c_str(), rms, peak, bad ? "true" : "false", ms / (secs * 10.0));
        for (size_t i = 0; i < env.size(); ++i) fprintf(js, "%s%.6f", i ? "," : "", env[i]);
        fprintf(js, "]");
#ifdef RACK_FM1
        fprintf(js, ", \"arena\": %d, \"arena_size\": %d, \"voices\": %d, \"fit\": %d, \"unloaded\": %d", fm1_status(0), fm1_status(1), fm1_status(3), fm1_status(2), fm1_status(4));
#endif
        fprintf(js, "}");
        first = false;
      }
      if (wav_dir) {
        std::string p = std::string(wav_dir) + "/";
        for (char c : name) p += (c == ' ' || c == '/') ? '_' : c;
        p += ".wav"; wav_write(p.c_str(), L, R);
      }
      continue;
    }
    // "<frame> <fn> <args>"
    char* save = nullptr; char* tok = strtok_r(line, " ", &save); if (!tok) continue;
    long at = atol(tok); render_to(at);
    char* fn = strtok_r(nullptr, " ", &save); if (!fn) continue;
    double a[8] = { 0 }; int na = 0;
    while (na < 8 && (tok = strtok_r(nullptr, " ", &save))) a[na++] = atof(tok);
    std::string f = fn;
    if (f == "init") init();
    else if (f == "set_module") set_module((int)a[0], (int)a[1]);
    else if (f == "set_param") set_param((int)a[0], (int)a[1], (float)a[2]);
    else if (f == "set_meta") set_meta((int)a[0], (int)a[1], (float)a[2], (float)a[3], (float)a[4]);
    else if (f == "set_route") set_route((int)a[0], (int)a[1], (int)a[2], (int)a[3], (float)a[4], (int)a[5]);
    else if (f == "set_macro") set_macro((int)a[0], (float)a[1]);
    else if (f == "set_master") set_master((float)a[0]);
    else if (f == "set_master_fx") set_master_fx((float)a[0], (float)a[1], (float)a[2], (float)a[3]);
    else if (f == "set_voices") set_voices((int)a[0]);
    else if (f == "set_legato") set_legato((int)a[0]);
    else if (f == "set_vintage") set_vintage((float)a[0]);
    else if (f == "set_tune") set_tune((int)a[0], (float)a[1]);
    else if (f == "set_bend") set_bend((float)a[0]);
    else if (f == "set_ext") set_ext((int)a[0], (float)a[1]);
    else if (f == "set_ext_slew") set_ext_slew((int)a[0], (float)a[1]);
    else if (f == "note_on") note_on((float)a[0], (float)a[1]);
    else if (f == "note_off") note_off((float)a[0]);
    else if (f == "all_off") all_off();
    else if (f == "reset_clock") reset_clock();
    else if (f == "set_hw") { /* browser preview switch; the FM-1 build is always FM-1 */ }
    else fprintf(stderr, "%s: unknown call %s\n", name.c_str(), fn);
  }
  if (js) { fprintf(js, "\n}\n"); fclose(js); }
  fclose(in);
  return 0;
}
