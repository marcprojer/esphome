// Aquarium Monitor: helpers shared by both tanks.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include "esphome/core/time.h"

namespace aqmon {

// Seconds since 1970 from ISO 8601 with offset, e.g. 2026-10-14T13:00:00+02:00 (0 = invalid)
inline uint32_t iso_ts(const std::string &x) {
  int Y, M, D, h, m, s;
  if (sscanf(x.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) return 0;
  int offset = 0;
  size_t p = x.find_last_of("+-");
  if (p != std::string::npos && p > 18) {
    int oh = 0, om = 0;
    sscanf(x.c_str() + p + 1, "%d:%d", &oh, &om);
    offset = (x[p] == '-' ? -1 : 1) * (oh * 3600 + om * 60);
  }
  // days since 1970-01-01 (civil calendar)
  int y = Y - (M <= 2);
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t) era * 146097 + doe - 719468;
  return (uint32_t) (days * 86400 + h * 3600 + m * 60 + s - offset);
}

// New water change logged in HA (sensor.aquarium<s>_wasserwechsel_zeitpunkt)?
// 0 = nothing to do (same/older entry, HA restart, no time yet), 1 = fresh (capture the
// baseline), -1 = logged more than 24 h after the fact (clear the baseline).
// "stored" keeps the timestamp of the last handled change.
inline int water_change(const std::string &x, uint32_t &stored, esphome::ESPTime now) {
  uint32_t ts = iso_ts(x);
  if (ts == 0 || ts <= stored) return 0;
  if (!now.is_valid()) return 0;
  stored = ts;
  return now.timestamp > ts + 86400 ? -1 : 1;
}

}  // namespace aqmon
