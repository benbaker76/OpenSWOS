/* Single translation unit that compiles the Tile Map Editor runtime
 * (third_party/tilemapeditor, see its OPENSWOS.md): a .tmj pitch map's loader and
 * player. SDL only -- docs/PITCH_MAPS.md. It shares the engine's easing curves
 * rather than carrying a second copy. */
#include "easing.h"
#define TA_EASE(kind, t) Ease((EaseKind)(kind), (t))
#include "../third_party/tilemapeditor/tilemapeditor.c"
