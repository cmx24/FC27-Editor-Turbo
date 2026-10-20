// FC 27 LE Turbo GUI - weak default of the Set-pieces card (see ui_setpiece_card.h): draws nothing. G-pickers' ui_setpiece_card.cpp replaces it.
#include "ui_setpiece_card.h"

namespace turbo {
namespace setpiece_card {

#if defined(__GNUC__)
__attribute__((weak))
#endif
void draw(App&, int64_t) {}

}  // namespace setpiece_card
}  // namespace turbo
