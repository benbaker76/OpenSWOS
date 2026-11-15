/* pl_mpeg_impl.c -- pl_mpeg's implementation, once (third_party/pl_mpeg/OPENSWOS.md): the MPEG-1
 * decoder of the PC cutscenes' upscaled clips (src/mod_dosvideos.c). SDL builds only. */
#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../third_party/pl_mpeg/pl_mpeg.h"
