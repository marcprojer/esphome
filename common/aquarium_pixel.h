// Aquarium als animierter 8-Bit-Würfel für die Startseite des Aquarium-Tablets.
//
// Portierung von render() aus der HA-Karte www/aquarium-pixel-card.js (HA-Repo).
// Die statische Szene (Voxel, Glas, Lampe, Boden) kommt aus aquarium_pixel_data.h,
// erzeugt mit tools/aquarium_pixel_tablet_export.js im HA-Repo. Hier läuft nur die
// Animation: Garnelen (nachts ruhig, fressen am Futter; im Garnelenbecken zwei zusätzliche,
// die schwimmen), drei Rote Rennschnecken (Garnelenbecken), CO2-Bläschen, Wasseroberfläche,
// Mondlicht, Trübung, "!", Sanduhr bei Wartung bzw. Fütterungspause, Futter nach dem Füttern.
//
// Ausgabe: RGB565, doppelt vergrössert (1 Szenenpixel = 2x2 Displaypixel).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace aqpx {

struct TankData {
  uint16_t W, H, OX, OY;
  uint8_t X, Y, ZW;
  float LY;
  bool mirror;
  float bsx, bsy, bz0;      // CO2-Diffusor (Voxel) und Höhe des Austritts
  uint8_t fx, fy;           // Stelle, an der das Futter liegt (Voxel)
  const uint32_t *base;     // ARGB, A = 0 oder 255
  const int16_t *depth;     // Tiefe pro Pixel (-32768 = leer)
  const uint8_t *mask;      // Bit 0-1 Wasser (1) / Oberfläche (2), 4 Lampe, 8 Lampe-Trefferfläche,
                            // 16 Diffusor, 32 Diffusor-Trefferfläche
  const uint32_t *ov;       // Glas und Lampe, ARGB
  const uint8_t *ground;    // Höhe des Bodens pro Voxel (X*Y)
  uint8_t nshrimp;
  const int8_t (*shrimps)[4];  // x, y, Richtung, Farbe (0 rot, 1 blau)
};

enum { HL_NONE = 0, HL_LAMP = 1, HL_CO2 = 2 };
enum { FOOD_NONE = 0, FOOD_STICKS = 1, FOOD_GEMUESE = 2, FOOD_PULVER = 3, FOOD_TABLETTE = 4 };
static constexpr int BORDER = 4;  // Rand um das Becken (wie in der Karte)
static constexpr int SCALE = 2;

struct Rgb { uint8_t r, g, b; };
using PackFn = uint16_t (*)(uint8_t r, uint8_t g, uint8_t b);

class Tank {
 public:
  int CW = 0, CH = 0;  // Grösse inkl. Rand, in Szenenpixeln

  void init(const TankData *d) {
    d_ = d;
    W_ = d->W; H_ = d->H; N_ = W_ * H_;
    CW = W_ + 2 * BORDER; CH = H_ + 2 * BORDER;
    fr_ = (uint8_t *) alloc(N_ * 4);
    fd_ = (int16_t *) alloc(N_ * 2);
    bg_ = (uint8_t *) alloc(CW * CH * 3);
    ts_ = d->mirror ? 0 : 3;
    sp_.clear();
    for (int i = 0; i < d->nshrimp; i++)
      sp_.push_back({d->shrimps[i][0], d->shrimps[i][1], d->shrimps[i][2], d->shrimps[i][3], 0, 0});
    // Becken mit Garnelen: zusätzlich je eine rote und blaue schwimmende Garnele und drei
    // Rote Rennschnecken (hinten an der Scheibe, vorne links an der Scheibe, am Boden)
    sn_.clear();
    if (d->nshrimp > 0) {
      for (int i = 0; i < 2; i++) {
        Shrimp w{0, 0, 0, i, 0, 0};
        w.swim = true; w.fx = d->X * (.6f + .2f * i); w.fy = d->Y * (.75f - .1f * i); w.fz = d->ZW * .6f;
        sp_.push_back(w);
      }
      uint32_t q = 977;
      auto sr = [&q]() { q = q * 1664525u + 1013904223u; return q / 4294967296.f; };
      for (int i = 0; i < 3; i++) {
        Snail n{};
        n.art = i;
        if (i == SN_BODEN) { n.x = 6 + sr() * (d->X - 12); n.y = 6 + sr() * (d->Y - 12); n.a = sr() * 6.28f; }
        else {
          n.u = 4 + sr() * (d->X - 8); n.z = (i == SN_HINTEN ? d->ZW * .55f : 3) + sr() * (d->ZW * .35f); n.a = sr() * 6.28f;
          for (auto &g : n.algen) { g.u = 1 + sr() * (d->X - 2); g.z = 1 + sr() * (d->ZW - 2); g.weg = 0; }
        }
        sn_.push_back(n);
      }
    }
    // Hintergrund (gedithert)
    static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = 0; y < CH; y++)
      for (int x = 0; x < CW; x++) {
        float dx = (x - CW * .5f) / CW, dy = (y - CH * .45f) / CH;
        float v = std::max(0.f, 1 - sqrtf(dx * dx * 1.1f + dy * dy * 1.4f) * 1.7f);
        v *= v;
        float q = floorf(v * 5 + bayer[(y & 3) * 4 + (x & 3)] / 16.f) / 5;
        uint8_t *o = &bg_[(y * CW + x) * 3];
        o[0] = lroundf(5 + 10 * q); o[1] = lroundf(6 + 14 * q); o[2] = lroundf(10 + 21 * q);
      }
  }
  bool ready() const { return d_ != nullptr && fr_ && fd_ && bg_; }

  // Zielzustand; die Übergänge laufen weich wie in der Karte (~2 s)
  void set_target(bool light, bool co2, bool pump, bool murk, bool warn) {
    tn_ = light ? 0 : 1; tf_ = pump ? 1 : 0; tm_ = murk ? 1 : 0; co2_ = co2; warn_ = warn;
    if (first_) { night_ = tn_; flow_ = tf_; murk_ = tm_; first_ = false; }
  }
  // Wartung bzw. Fütterungspause: verbleibender Anteil 0..1 (Sanduhr), < 0 = keine Pause
  void set_pause(float f) { pause_ = f; }
  // Futter im Becken: Art (FOOD_*), verbleibender Anteil 0..1 (30 min nach dem Füttern),
  // neu = gerade gefüttert, dann sinkt es von der Oberfläche ab
  void set_food(int kind, float rest, bool neu) {
    if (kind == FOOD_NONE || rest <= 0) { food_ = FOOD_NONE; return; }
    if (food_ == FOOD_NONE || kind != food_) fz_ = neu ? d_->ZW - .5f : d_->ground[d_->fy * d_->X + d_->fx] + 1;
    food_ = kind; food_rest_ = std::min(1.f, rest);
  }
  // Hervorhebung: während des Drückens wachsend, nach dem Umschalten kurz aufblitzen
  void press(int k, uint32_t now) { press_k_ = k; press_t0_ = now; }
  void release() { press_k_ = HL_NONE; }
  void flash(int k, uint32_t now) { press_k_ = HL_NONE; flash_k_ = k; flash_t0_ = now; }

  // Treffer an einer Stelle der Ausgabe (Displaypixel relativ zur Bildecke)
  int hit(int px, int py) const {
    if (!d_) return HL_NONE;
    int x = px / SCALE - BORDER, y = py / SCALE - BORDER;
    if (x < 0 || y < 0 || x >= W_ || y >= H_) return HL_NONE;
    uint8_t m = d_->mask[y * W_ + x];
    if (m & 32) return HL_CO2;
    if (m & 8) return HL_LAMP;
    return HL_NONE;
  }

  // now in ms, dt in s. out: RGB565, CW*SCALE x CH*SCALE, stride in Pixeln
  void render(uint32_t now, float dt, uint16_t *out, int stride, PackFn pack) {
    if (!ready()) return;
    const TankData &d = *d_;
    float k = std::min(1.f, dt * 1.5f);
    night_ += (tn_ - night_) * k; flow_ += (tf_ - flow_) * k; murk_ += (tm_ - murk_) * k * .5f;
    int hl_k = HL_NONE; float hl_a = 0;
    if (press_k_) { hl_k = press_k_; hl_a = .2f + .55f * std::min(1.f, (now - press_t0_) / 500.f); }
    else if (flash_k_ && now - flash_t0_ < 700) { hl_k = flash_k_; hl_a = .85f * (1 - (now - flash_t0_) / 700.f); }

    for (int i = 0; i < N_; i++) {
      uint32_t c = d.base[i];
      uint8_t *o = &fr_[i * 4];
      o[0] = c >> 16; o[1] = c >> 8; o[2] = c; o[3] = c >> 24;
    }
    memcpy(fd_, d.depth, N_ * 2);

    bool eat = false;
    if (food_ != FOOD_NONE) {
      float gz0 = gz(d.fx, d.fy) + 1;
      fz_ = std::max(gz0, fz_ - dt * 2.2f);
      draw_food();
      eat = fz_ <= gz0 + .01f;
    }
    for (auto &s : sp_) {
      if (s.swim) step_swimmer(s, now, dt);
      else step_shrimp(s, now, eat);
      draw_shrimp(s, now);
    }
    for (auto &n : sn_) {
      step_snail(n, dt, now);
      if (n.art == SN_BODEN) draw_ground_snail(n);
      else if (n.art == SN_HINTEN) { draw_algen(n); draw_pane_snail(n, now); }
    }

    // CO2-Bläschen
    if (co2_ && (int32_t) (now - next_bubble_) > 0) {
      bubbles_.push_back({d.bsx + rnd() * .6f - .3f, d.bsy + rnd() * .6f - .3f, d.bz0, rnd() * 6});
      next_bubble_ = now + 160 + (uint32_t) (rnd() * 320);
    }
    float wob = .12f + .28f * flow_;
    static const Rgb B0 = {215, 245, 255}, B1 = {150, 205, 228}, B2 = {95, 150, 175};
    for (auto &b : bubbles_) {
      b.z += dt * 5;
      float x = b.x + sinf(b.z * .8f + b.ph) * wob;
      int sx = rnd_(d.OX + 2 * (x - b.y)), sy = rnd_(d.OY + x + b.y - 3 * b.z);
      int key = (rnd_(x) + rnd_(b.y) + rnd_(b.z)) * 64 + 99;
      pset(sx, sy, B0, key);
      if (b.z > d.bz0 + 2) { pset(sx + 1, sy, B1, key); pset(sx, sy + 1, B1, key); pset(sx + 1, sy + 1, B2, key); }
    }
    bubbles_.erase(std::remove_if(bubbles_.begin(), bubbles_.end(), [&](const Bubble &b) { return b.z >= d.ZW + .5f; }),
                   bubbles_.end());
    // Schnecke vorne an der Scheibe (Unterseite) vor allem anderen
    for (auto &n : sn_)
      if (n.art == SN_VORNE) { draw_algen(n); draw_pane_snail(n, now); }

    // Wasser, Glas/Lampe, Mondlicht, Hervorhebung; dann auf den Hintergrund
    ts_ += dt * (.15f + .85f * flow_);
    float day = 1 - night_, mk = murk_ * .3f, nk = night_ * .85f;
    bool blink = warn_ && (now / 600) % 2 == 0;
    for (int y = 0; y < CH; y++) {
      uint16_t *row = out + (size_t) y * SCALE * stride;
      for (int x = 0; x < CW; x++) {
        const uint8_t *bg = &bg_[(y * CW + x) * 3];
        float r = bg[0], g = bg[1], b = bg[2];
        int tx = x - BORDER, ty = y - BORDER;
        if (tx >= 0 && ty >= 0 && tx < W_ && ty < H_) {
          int i = ty * W_ + tx;
          uint8_t *o = &fr_[i * 4];
          float pr = o[0], pg = o[1], pb = o[2], pa = o[3];
          uint8_t m = d.mask[i], wm = m & 3;
          if (wm) {
            pr = pr * .86f + 6 * .14f; pg = pg * .88f + 38 * .12f; pb = pb * .86f + 62 * .14f;
            if (mk > 0) { pr += (46 - pr) * mk; pg += (60 - pg) * mk; pb += (24 - pb) * mk; }
            if (wm == 2) {
              float u = (tx - d.OX) / 2.f, v = ty - d.OY + 3 * (d.ZW - 1);
              float wx = (u + v) / 2, wy = (v - u) / 2;
              float q = sinf(wx * .9f + ts_ * 1.6f) + sinf(wy * 1.15f - ts_ * 1.25f) + sinf((wx - wy) * .55f + ts_ * .8f);
              float e = ((d.mirror ? wx : wy) - d.LY) / 5;
              float gl = expf(-e * e) * 10 * day;
              pr = clamp(pr + gl * .6f); pg = clamp(pg + gl); pb = clamp(pb + gl);
              float a = (q > 2.45f ? .4f : q > 2.0f ? .13f : 0) * (.25f + .75f * day);
              if (a > 0) { pr += (170 - pr) * a; pg += (225 - pg) * a; pb += (240 - pb) * a; }
            }
          }
          uint32_t ov = d.ov[i];
          if (ov >> 24) {
            float a = (ov >> 24) / 255.f, cr = (ov >> 16) & 255, cg = (ov >> 8) & 255, cb = ov & 255;
            pr += (cr - pr) * a; pg += (cg - pg) * a; pb += (cb - pb) * a;
            if (pa < 255) { pr = cr; pg = cg; pb = cb; pa = std::max(pa, (float) (ov >> 24)); }
          }
          if (pa > 0) {
            pr = clamp(pr); pg = clamp(pg); pb = clamp(pb);
            if (nk > .00085f) {
              float l = pr * .3f + pg * .59f + pb * .11f;
              pr += (l * .2f - pr) * nk; pg += (l * .36f - pg) * nk; pb += (l * .7f + 10 - pb) * nk;
            }
            if (hl_k && hl_a > .01f && (m & (hl_k == HL_LAMP ? 4 : 16))) {
              pr += (255 - pr) * hl_a; pg += (240 - pg) * hl_a; pb += (160 - pb) * hl_a;
            }
            float a = pa / 255.f;
            r += (pr - r) * a; g += (pg - g) * a; b += (pb - b) * a;
          }
        }
        // blinkendes "!" links oben (Warnung oder Werte ausser Soll); tiefer als in der
        // HA-Karte, weil darüber auf dem Tablet die Temperatur steht
        if (blink && x >= 6 && x < 8 && ((y >= 21 && y < 27) || (y >= 29 && y < 31))) { r = 255; g = 122; b = 61; }
        uint16_t c = pack(clamp(r), clamp(g), clamp(b));
        uint16_t *p = row + x * SCALE;
        p[0] = c; p[1] = c; p[stride] = c; p[stride + 1] = c;
      }
    }
    // Sanduhr rechts neben dem "!" während Wartung oder Fütterungspause
    if (pause_ >= 0) sanduhr(11, 22, now, out, stride, pack);
  }

 private:
  struct Shrimp {
    int x, y, d, col;
    uint32_t next, pause;
    bool swim = false;                    // schwimmt (sonst am Boden)
    float fx = 0, fy = 0, fz = 0, tx = 0, ty = 0, tz = 0, sz = -1;
    uint32_t tt = 0;                      // nächstes Ziel
  };
  struct Alga { float u, z; uint32_t weg; };
  enum { SN_HINTEN = 0, SN_VORNE = 1, SN_BODEN = 2 };
  struct Snail { int art; float u, z, x, y, a; Alga algen[35]; };
  struct Bubble { float x, y, z, ph; };

  const TankData *d_ = nullptr;
  int W_ = 0, H_ = 0, N_ = 0;
  uint8_t *fr_ = nullptr, *bg_ = nullptr;
  int16_t *fd_ = nullptr;
  std::vector<Shrimp> sp_;
  std::vector<Snail> sn_;
  std::vector<Bubble> bubbles_;
  uint32_t next_bubble_ = 0, seed_ = 0x2545F491;
  float ts_ = 0, night_ = 0, flow_ = 1, murk_ = 0, tn_ = 0, tf_ = 1, tm_ = 0;
  bool co2_ = true, warn_ = false, first_ = true;
  float pause_ = -1;
  int food_ = FOOD_NONE;
  float food_rest_ = 0, fz_ = 0;
  int press_k_ = HL_NONE, flash_k_ = HL_NONE;
  uint32_t press_t0_ = 0, flash_t0_ = 0;

  static void *alloc(size_t n) {
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return malloc(n);
  }
  static uint8_t clamp(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t) (v + .5f); }
  static int rnd_(float v) { return (int) floorf(v + .5f); }  // wie Math.round
  float rnd() {  // xorshift, 0..1
    seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
    return (seed_ >> 8) / 16777216.f;
  }
  int gz(int x, int y) const {
    x = std::max(0, std::min(d_->X - 1, x)); y = std::max(0, std::min(d_->Y - 1, y));
    return d_->ground[y * d_->X + x];
  }
  void pset(int x, int y, const Rgb &c, int dep) {
    if (x < 0 || y < 0 || x >= W_ || y >= H_) return;
    int i = y * W_ + x;
    if (dep < fd_[i]) return;
    fd_[i] = dep;
    uint8_t *o = &fr_[i * 4];
    o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255;
  }
  static Rgb shade(const Rgb &c, float f) {
    return {(uint8_t) std::min(255.f, c.r * f), (uint8_t) std::min(255.f, c.g * f), (uint8_t) std::min(255.f, c.b * f)};
  }
  void voxel(int x, int y, int z, const Rgb &c) {
    int sx = d_->OX + 2 * (x - y), sy = d_->OY + (x + y) - 3 * z, k = (x + y + z) * 64 + z;
    Rgb L = shade(c, .74f), R = shade(c, .52f);
    for (int r = 0; r < 2; r++)
      for (int q = -2; q < 2; q++) pset(sx + q, sy + r, c, k);
    for (int r = 2; r < 5; r++) {
      pset(sx - 2, sy + r, L, k); pset(sx - 1, sy + r, L, k);
      pset(sx, sy + r, R, k); pset(sx + 1, sy + r, R, k);
    }
  }
  // Sanduhr (7 x 9 Szenenpixel), gleiche Zeichnung wie sanduhr() in der HA-Karte
  // www/aquarium-pixel-card.js: Deckel orange, Glas, Sand oben (füllt von unten) und unten
  void sanduhr(int x0, int y0, uint32_t now, uint16_t *out, int stride, PackFn pack) const {
    static const int8_t GLAS[14][2] = {{1, 1}, {5, 1}, {1, 2}, {5, 2}, {2, 3}, {4, 3}, {2, 4},
                                       {4, 4}, {2, 5}, {4, 5}, {1, 6}, {5, 6}, {1, 7}, {5, 7}};
    static const int8_t OBEN[7][2] = {{3, 3}, {2, 2}, {3, 2}, {4, 2}, {2, 1}, {3, 1}, {4, 1}};
    static const int8_t UNTEN[7][2] = {{2, 7}, {3, 7}, {4, 7}, {2, 6}, {3, 6}, {4, 6}, {3, 5}};
    const uint16_t deckel = pack(255, 122, 61), glas = pack(127, 166, 184), sand = pack(240, 200, 90),
                   leer = pack(22, 34, 44);
    auto px = [&](int x, int y, uint16_t c) {
      x += x0; y += y0;
      if (x < 0 || y < 0 || x >= CW || y >= CH) return;
      uint16_t *p = out + (size_t) y * SCALE * stride + x * SCALE;
      p[0] = c; p[1] = c; p[stride] = c; p[stride + 1] = c;
    };
    int oben = (int) ceilf(std::max(0.f, std::min(1.f, pause_)) * 7);
    for (int x = 0; x < 7; x++) { px(x, 0, deckel); px(x, 8, deckel); }
    for (auto &g : GLAS) px(g[0], g[1], glas);
    for (int i = 0; i < 7; i++) {
      px(OBEN[i][0], OBEN[i][1], i < oben ? sand : leer);
      px(UNTEN[i][0], UNTEN[i][1], i < 7 - oben ? sand : leer);
    }
    px(3, 4, oben > 0 && (now / 300) % 2 == 0 ? sand : leer);  // rieselndes Korn
  }
  // Voxel mit Höhe als Kommazahl (absinkendes Futter), wie drawVoxel in der Karte
  void voxelf(int x, int y, float z, const Rgb &c) {
    int sx = d_->OX + 2 * (x - y), sy = (int) floorf(d_->OY + (x + y) - 3 * z + .5f), k = (int) ((x + y + z) * 64 + z);
    Rgb L = shade(c, .74f), R = shade(c, .52f);
    for (int r = 0; r < 2; r++)
      for (int q = -2; q < 2; q++) pset(sx + q, sy + r, c, k);
    for (int r = 2; r < 5; r++) {
      pset(sx - 2, sy + r, L, k); pset(sx - 1, sy + r, L, k);
      pset(sx, sy + r, R, k); pset(sx + 1, sy + r, R, k);
    }
  }
  static constexpr int DIRS[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
  static int sgn(int v) { return (v > 0) - (v < 0); }
  static int dir_of(int dx, int dy) {
    for (int i = 0; i < 8; i++)
      if (DIRS[i][0] == dx && DIRS[i][1] == dy) return i;
    return 0;
  }
  // Nachts (night_) seltener und langsamer unterwegs. eat: Futter liegt am Boden ->
  // hinlaufen und daneben fressen (Fühler wackeln). Wie stepShrimp in der HA-Karte.
  void step_shrimp(Shrimp &s, uint32_t now, bool eat) {
    if ((int32_t) (now - s.next) < 0) return;
    float n = eat ? 0 : night_;
    if (eat) {
      int ex = d_->fx - s.x, ey = d_->fy - s.y;
      if (std::max(abs(ex), abs(ey)) <= 2) {
        s.d = dir_of(sgn(ex), sgn(ey));
        s.pause = now + 700 + (uint32_t) (rnd() * 900); s.next = s.pause; return;
      }
      if (rnd() < .75f) s.d = dir_of(sgn(ex), sgn(ey));
    } else {
      if (rnd() < .07f + .45f * n) { s.pause = now + (uint32_t) ((1200 + rnd() * 3000) * (1 + 2 * n)); s.next = s.pause; return; }
      if (rnd() < .3f) s.d = (s.d + (rnd() < .5f ? 1 : 7)) % 8;
    }
    int nx = s.x + DIRS[s.d][0], ny = s.y + DIRS[s.d][1];
    if (nx < 2 || ny < 2 || nx > d_->X - 3 || ny > d_->Y - 3 || abs(gz(nx, ny) - gz(s.x, s.y)) > 1) {
      s.d = (s.d + (rnd() < .5f ? 2 : 6)) % 8; s.next = now + 120; return;
    }
    s.x = nx; s.y = ny; s.next = now + (uint32_t) ((170 + rnd() * 200) * (1 + 3 * n));
  }
  // Futter wie drawFood in der HA-Karte: Sticks 3 Voxel, Gemüse/Tablette 2x2, Pulver als
  // Wolke (beim Absinken auseinander) bzw. Staub; wird mit food_rest_ kleiner
  void draw_food() {
    static const Rgb C[5] = {{0, 0, 0}, {150, 95, 45}, {125, 195, 70}, {110, 200, 110}, {104, 92, 60}};
    const Rgb &c = C[food_];
    int fx = d_->fx, fy = d_->fy;
    if (food_ == FOOD_PULVER) {
      static const int8_t ST[12][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {2, 0}, {0, 2}, {-2, 1}, {1, -2}};
      float gz0 = gz(fx, fy) + 1, auf = std::min(1.f, (fz_ - gz0) / 3), sc = 1 + auf * 1.5f;
      int n = (int) ceilf(food_rest_ * 12);
      Rgb dark = shade(c, .7f);
      for (int i = 0; i < n; i++) {
        float u = ST[i][0] * sc, v = ST[i][1] * sc;
        int sx = rnd_(d_->OX + 2 * (fx + u - fy - v)), sy = rnd_(d_->OY + fx + u + fy + v - 3 * fz_ + 3);
        pset(sx, sy, i % 3 ? c : dark, (fx + fy + rnd_(fz_)) * 64 + 90);
      }
      return;
    }
    static const int8_t STICK[3][2] = {{0, 0}, {1, 0}, {2, 0}}, QUAD[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    bool st = food_ == FOOD_STICKS;
    int total = st ? 3 : 4, n = std::max(1, (int) ceilf(food_rest_ * total));
    for (int i = 0; i < n; i++) {
      const int8_t *o = st ? STICK[i] : QUAD[i];
      voxelf(fx + o[0], fy + o[1], fz_, c);
    }
  }
  // Schwimmende Garnele: gleitet im vorderen Teil zu wechselnden Zielen in mittlerer bis oberer
  // Höhe, nachts langsamer und tiefer (wie stepSwimmer in der HA-Karte)
  void step_swimmer(Shrimp &s, uint32_t now, float dt) {
    const TankData &d = *d_;
    if ((int32_t) (now - s.tt) > 0) {
      s.tx = d.X * (.45f + rnd() * .45f); s.ty = d.Y * (.45f + rnd() * .45f);
      s.tz = d.ZW * ((.3f + rnd() * .55f) * (1 - .5f * night_)); s.tt = now + 3000 + (uint32_t) (rnd() * 4000);
    }
    float vx = s.tx - s.fx, vy = s.ty - s.fy, vz = s.tz - s.fz, l = sqrtf(vx * vx + vy * vy + vz * vz);
    if (l > .3f) {
      float v = std::min(l, dt * 1.6f * (1 - .6f * night_)) / l;
      s.fx += vx * v; s.fy += vy * v; s.fz += vz * v;
      float hl = hypotf(vx, vy);
      if (hl > .3f) {
        int sx = sgn(rnd_(vx / hl * 1.4f)), sy = sgn(rnd_(vy / hl * 1.4f));
        if (sx || sy) s.d = dir_of(sx, sy);
      }
    }
    s.x = rnd_(s.fx); s.y = rnd_(s.fy);
    s.sz = roundf((s.fz + sinf((now % 43982u) / 700.f + s.col * 3) * .25f) * 3) / 3;  // auf ganze Bildpixel
  }

  // ---- Rote Rennschnecken (wie in der HA-Karte): pro Bildpixel rückwärts gerechnet
  // (Bildpixel -> Stelle der Schnecke), Mitte auf ganze Pixel, Richtung in 16 Stufen
  static constexpr float SK = 1.15f;  // Grösse
  // Scheiben: vorne links (y = Y) und hinten (y = 0); u längs, z Höhe
  void pane_fwd(int art, float u, float z, float &x, float &y) const {
    if (art == SN_VORNE) { x = d_->OX + 2 * (u - d_->Y); y = d_->OY + u + d_->Y - 3 * z; }
    else { x = d_->OX + 2 * u; y = d_->OY + u - 3 * z; }
  }
  void pane_inv(int art, float x, float y, float &u, float &z) const {
    if (art == SN_VORNE) { u = (x - d_->OX) / 2 + d_->Y; z = (d_->OY + u + d_->Y - y) / 3; }
    else { u = (x - d_->OX) / 2; z = (d_->OY + u - y) / 3; }
  }
  void step_snail(Snail &n, float dt, uint32_t now) {
    if (rnd() < dt * .3f) n.a += (rnd() - .5f) * 1.6f;
    float v = .35f * dt;
    if (n.art != SN_BODEN) {
      float lo = n.art == SN_HINTEN ? d_->ZW * .4f : 1.5f, hi = d_->ZW - 2;
      n.u += cosf(n.a) * v; n.z += sinf(n.a) * v * .7f;
      if (n.u < 2 || n.u > d_->X - 2) { n.a = (float) M_PI - n.a; n.u = std::max(2.f, std::min(d_->X - 2.f, n.u)); }
      if (n.z < lo || n.z > hi) { n.a = -n.a; n.z = std::max(lo, std::min(hi, n.z)); }
      for (auto &g : n.algen) {
        if (!g.weg && fabsf(g.u - n.u) < 1.6f && fabsf(g.z - n.z) < 1.3f) g.weg = now + 600000 + (uint32_t) (rnd() * 600000) + 1;
        else if (g.weg && (int32_t) (now - g.weg) > 0) g.weg = 0;
      }
    } else {
      float nx = n.x + cosf(n.a) * v * .7f, ny = n.y + sinf(n.a) * v * .7f;
      if (nx < 2.5f || ny < 2.5f || nx > d_->X - 3.5f || ny > d_->Y - 3.5f || abs(gz(rnd_(nx), rnd_(ny)) - gz(rnd_(n.x), rnd_(n.y))) > 1)
        n.a += (float) M_PI * (.5f + rnd());
      else { n.x = nx; n.y = ny; }
    }
  }
  static Rgb rgbf(float r, float g, float b) { return {clamp(r), clamp(g), clamp(b)}; }
  // Gehäuse von oben/aussen: oranges Gehäuse, Reihen schwarzer Flecken, Glanz, Spitze hinten
  static bool shell(float s, float t, Rgb &c) {
    float ns = (s + .2f) / 1.6f, nt = t / 1.3f, e = ns * ns + nt * nt;
    if (e > 1) return false;
    float f = 1.05f - .55f * e + .35f * std::max(0.f, -nt - ns * .3f) * (1 - e);
    if (hypotf(s + 1.25f, t) < .35f) { c = rgbf(235 * f, 120 * f, 60 * f); return true; }
    int reihe = (int) floorf((s + 2) * 1.4f);
    float a = fmodf((s + 2) * 1.4f, 1), b = fmodf((t + 2 + (reihe % 2) * .35f) * 1.6f, 1);
    if (fabsf(a - .5f) < .17f && fabsf(b - .5f) < .19f && e < .85f) c = {34, 18, 12};
    else c = rgbf(214 * f, 92 * f, 34 * f);
    return true;
  }
  static bool foot(float s, float t, Rgb &c) {
    float ef = (s - .4f) / 2 * ((s - .4f) / 2) + (t / 1.1f) * (t / 1.1f);
    if (ef > 1) return false;
    c = ef > .7f ? Rgb{140, 132, 118} : Rgb{188, 178, 160};
    return true;
  }
  static bool feeler(float s, float t, Rgb &c) {
    float k = (s - 2.5f) / .25f;
    if (k < -.3f || k > 3.3f) return false;
    for (int side = -1; side <= 1; side += 2)
      if (fabsf(t - side * (.3f + k * .18f)) < .16f) { c = {95, 85, 75}; return true; }
    return false;
  }
  // Unterseite durchs Glas: Gehäuserand, Fuss mit wandernden Wellen, Kopf mit Maul (geht auf und zu)
  static bool under(float s, float t, uint32_t now, Rgb &c) {
    float hx = s - 1.9f;
    if (hypotf(hx, t) < .6f) {
      float auf = (sinf((now % 1068142u) / 170.f) + 1) / 2;
      c = hypotf(hx - .1f, t) < .12f + .16f * auf ? Rgb{70, 40, 34} : Rgb{176, 164, 148};
      return true;
    }
    float ef = ((s - .2f) / 1.7f) * ((s - .2f) / 1.7f) + (t / .95f) * (t / .95f);
    if (ef <= 1) {
      float w = .9f + .1f * sinf(s * 5 + (now % 1633628u) / 260.f);
      c = ef > .75f ? rgbf(150 * w, 140 * w, 124 * w) : rgbf(205 * w, 196 * w, 180 * w);
      return true;
    }
    float es = ((s + .1f) / 1.85f) * ((s + .1f) / 1.85f) + (t / 1.4f) * (t / 1.4f);
    if (es <= 1) { c = es > .8f ? Rgb{120, 50, 22} : Rgb{168, 72, 30}; return true; }
    return feeler(s, t, c);
  }
  static float dir16(float a) { const float st = (float) M_PI / 8; return roundf(a / st) * st; }
  void draw_pane_snail(const Snail &n, uint32_t now) {
    float a = dir16(n.a), ca = cosf(a), sa = sinf(a), fx, fy, u0, z0;
    pane_fwd(n.art, n.u, n.z, fx, fy);
    int cx = rnd_(fx), cy = rnd_(fy);
    pane_inv(n.art, cx, cy, u0, z0);
    for (int y = cy - 9; y <= cy + 9; y++)
      for (int x = cx - 10; x <= cx + 10; x++) {
        if (x < 0 || y < 0 || x >= W_ || y >= H_) continue;
        float u, z;
        pane_inv(n.art, x, y, u, z);
        float du = (u - u0) / SK, dz = (z - z0) / (.7f * SK), ls = du * ca + dz * sa, lt = -du * sa + dz * ca;
        Rgb c;
        bool hit = n.art == SN_VORNE ? under(ls, lt, now, c) : (shell(ls, lt, c) || foot(ls, lt, c) || feeler(ls, lt, c));
        if (!hit) continue;
        if (n.art == SN_HINTEN) pset(x, y, c, (int) ((u + z + 1) * 64));
        else { int i = y * W_ + x; uint8_t *o = &fr_[i * 4]; o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = 255; fd_[i] = 32767; }
      }
  }
  // Am Boden: Kuppel (Höhe bis 1.3 Voxel), pro Bildpixel von oben nach unten die Höhe suchen
  void draw_ground_snail(const Snail &n) {
    float a = dir16(n.a), ca = cosf(a), sa = sinf(a);
    float gx0 = roundf(n.x * 2) / 2, gy0 = roundf(n.y * 2) / 2;
    int g = gz(rnd_(n.x), rnd_(n.y)) + 1;
    int cx = rnd_(d_->OX + 2 * (gx0 - gy0)), cy = rnd_(d_->OY + gx0 + gy0 - 3 * g);
    for (int y = cy - 12; y <= cy + 8; y++)
      for (int x = cx - 10; x <= cx + 10; x++) {
        if (x < 0 || y < 0 || x >= W_ || y >= H_) continue;
        for (int hi = 13; hi >= 0; hi--) {
          float h = hi * .1f, dd = (x - d_->OX) / 2.f, q = y - d_->OY + 3 * (g + h), gx = (dd + q) / 2, gy = (q - dd) / 2;
          float dx = (gx - gx0) / SK, dy = (gy - gy0) / SK, ls = dx * ca + dy * sa, lt = -dx * sa + dy * ca;
          Rgb c;
          bool hit = false;
          if (hi > 0) {
            float ns = (ls + .2f) / 1.6f, nt = lt / 1.3f, e = ns * ns + nt * nt;
            hit = e <= 1 && 1.3f * sqrtf(1 - e) >= h && shell(ls, lt, c);
          } else hit = foot(ls, lt, c) || feeler(ls, lt, c);
          if (hit) { pset(x, y, c, (int) ((gx + gy + g + h) * 64 + g + 2)); break; }
        }
      }
  }
  // Algenpunkte auf der Scheibe der Schnecke (dezent, hinten von Pflanzen verdeckt)
  void draw_algen(const Snail &n) {
    for (auto &g : n.algen) {
      if (g.weg) continue;
      float fx, fy;
      pane_fwd(n.art, g.u, g.z, fx, fy);
      int x = rnd_(fx), y = rnd_(fy);
      if (x < 0 || y < 0 || x >= W_ || y >= H_) continue;
      int i = y * W_ + x;
      if (n.art == SN_HINTEN && fd_[i] > (int) ((g.u + g.z + 1) * 64)) continue;
      uint8_t *o = &fr_[i * 4];
      o[0] = clamp(o[0] + (60 - o[0]) * .22f); o[1] = clamp(o[1] + (105 - o[1]) * .22f); o[2] = clamp(o[2] + (55 - o[2]) * .22f);
    }
  }

  void draw_shrimp(const Shrimp &s, uint32_t now) {
    // Farben wie in der Karte: Kopf, Mitte, Schwanz, Fühler
    static const Rgb C[2][4] = {{{255, 42, 54}, {255, 20, 36}, {168, 12, 24}, {255, 120, 110}},
                                {{58, 134, 255}, {31, 98, 255}, {15, 58, 168}, {120, 180, 255}}};
    const Rgb *c = C[s.col & 1];
    int dx = DIRS[s.d][0], dy = DIRS[s.d][1];
    for (int k = 0; k < 3; k++) {
      int x = s.x - dx * k, y = s.y - dy * k;
      if (x >= 0 && y >= 0 && x < d_->X && y < d_->Y) {
        if (s.swim) voxelf(x, y, s.sz, c[k]);
        else voxel(x, y, gz(x, y) + 1, c[k]);
      }
    }
    float z = s.swim ? s.sz : gz(s.x, s.y) + 1, hx = d_->OX + 2 * (s.x - s.y), hy = d_->OY + s.x + s.y - 3 * z;
    int key = (int) ((s.x + s.y + z) * 64 + z + 1);
    float ux = 2 * (dx - dy), uy = dx + dy, l = hypotf(ux, uy);
    if (l == 0) l = 1;
    ux /= l; uy /= l;
    float wig = s.swim || (int32_t) (now - s.pause) < 0 ? sinf((now % 56549u) / 90.f) * .6f : 0;  // 56549 ms ≈ 100 Perioden
    for (int side = -1; side <= 1; side += 2)
      for (int k = 2; k <= 5; k++) {
        float px = hx - .5f + ux * k - uy * side * (k * .35f + wig), py = hy + .5f + uy * k + ux * side * (k * .35f + wig) - k * .25f;
        pset(rnd_(px), rnd_(py), k < 4 ? c[3] : c[2], key);
      }
  }
};

}  // namespace aqpx

// Die beiden Becken des Tablets (0 = Garnelen, 1 = Plants)
inline aqpx::Tank aq_tank[2];
