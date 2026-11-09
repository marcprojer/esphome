// Tab "Pflege" des Aquarium-Tablets: Zusätze und Futter erfassen.
//
// Home Assistant liefert pro Becken (sensor.aquarium<s>_tablet_pflege, Attribute
// "zugaben" und "futter") je eine Zeile pro Zusatz bzw. Futtersorte:
//   name|einheit|einheit mehrzahl|schritt|info
// z. B. "Crusta Minerals|Löffel|Löffel|0.25|Pulver · alle 7 Tage · fällig in 3 Tagen".
// Die Infos rechnet HA; hier wird nur zerlegt und formatiert.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace aqcare {

struct Item {
  std::string name, unit1, unitn, info;
  float step;
};

inline std::vector<Item> parse(const std::string &s) {
  std::vector<Item> out;
  size_t a = 0;
  while (a <= s.size()) {
    size_t e = s.find('\n', a);
    if (e == std::string::npos) e = s.size();
    std::string line = s.substr(a, e - a);
    std::vector<std::string> f;
    size_t b = 0;
    while (true) {
      size_t p = line.find('|', b);
      f.push_back(line.substr(b, p == std::string::npos ? std::string::npos : p - b));
      if (p == std::string::npos) break;
      b = p + 1;
    }
    if (f.size() >= 5 && !f[0].empty()) {
      float st = strtof(f[3].c_str(), nullptr);
      out.push_back({f[0], f[1], f[2], f[4], st > 0 ? st : 0.5f});
    }
    a = e + 1;
  }
  return out;
}

inline int find(const std::vector<Item> &l, const std::string &name) {
  if (name.empty()) return -1;
  for (size_t i = 0; i < l.size(); i++)
    if (l[i].name == name) return i;
  return -1;
}

// Alle Namen für den Roller (eine Zeile pro Eintrag)
inline std::string options(const std::vector<Item> &l) {
  std::string o;
  for (auto &it : l) {
    if (!o.empty()) o += '\n';
    o += it.name;
  }
  return o.empty() ? std::string("--") : o;
}

// Schnellwahl je Einheit (5 Werte)
inline const float *chips(const std::string &unit) {
  static const float ml[5] = {0.5f, 1, 1.5f, 2, 3};
  static const float loeffel[5] = {0.25f, 0.5f, 1, 1.5f, 2};
  static const float stueck[5] = {0.5f, 1, 1.5f, 2, 3};
  if (unit == "ml") return ml;
  if (unit == "Löffel") return loeffel;
  return stueck;
}

// 1, 0.5, 1.25 (ohne überflüssige Nullen)
inline std::string fmt(float v) {
  char b[16];
  snprintf(b, sizeof(b), "%.2f", v);
  std::string s(b);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

}  // namespace aqcare
