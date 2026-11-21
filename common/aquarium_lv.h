// Aquarium-Tablet: LVGL-Flags setzen und lesen, ohne Warnungen.
// Neuere LVGL-Versionen (Build-Server) markieren lv_obj_add_flag / lv_obj_remove_flag /
// lv_obj_has_flag als veraltet und bieten lv_obj_set_hidden() usw.; ältere (9.5) kennen
// diese Setter noch nicht. Die Hilfsfunktionen hier bauen mit beiden ohne Warnung.
#pragma once
#include "lvgl.h"

namespace aqlv {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
inline void flag(lv_obj_t *o, lv_obj_flag_t f, bool on) {
  if (on) lv_obj_add_flag(o, f);
  else lv_obj_remove_flag(o, f);
}
inline bool has(const lv_obj_t *o, lv_obj_flag_t f) { return lv_obj_has_flag(o, f); }
#pragma GCC diagnostic pop

inline void hidden(lv_obj_t *o, bool h) { flag(o, LV_OBJ_FLAG_HIDDEN, h); }
inline bool is_hidden(const lv_obj_t *o) { return has(o, LV_OBJ_FLAG_HIDDEN); }
inline void clickable(lv_obj_t *o, bool c) { flag(o, LV_OBJ_FLAG_CLICKABLE, c); }
inline void scrollable(lv_obj_t *o, bool s) { flag(o, LV_OBJ_FLAG_SCROLLABLE, s); }

}  // namespace aqlv
