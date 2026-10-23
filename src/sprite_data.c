/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "sprite_data.h"
#include "swos_platform.h"  /* _ASM_PFX for the teamSpritesTable .equ aliases (SDL force-includes this; the Amiga build does not) */

int16_t _jumpHeaderTeam1UpFrames[12]
    = { -5, 303, -5, 311, -5, 319, -5, 311, -5, 303, 227, -101 };
int16_t _jumpHeaderHitTeam1DownFrames[12]
    = { -5, 304, -5, 312, -5, 320, -5, 312, -5, 304, 230, -101 };
int16_t _jumpHeaderHitTeam1RightFrames[12]
    = { -5, 305, -5, 313, -5, 321, -5, 313, -5, 305, 233, -101 };
int16_t _jumpHeaderHitTeam1LeftFrames[12]
    = { -5, 306, -5, 314, -5, 322, -5, 314, -5, 306, 236, -101 };
int16_t _jumpHeaderHitTeam1UpRightFrames[12]
    = { -5, 310, -5, 318, -5, 326, -5, 318, -5, 310, 248, -101 };
int16_t _jumpHeaderHitTeam1UpLeftFrames[12]
    = { -5, 309, -5, 317, -5, 325, -5, 317, -5, 309, 245, -101 };
int16_t _jumpHeaderHitTeam1DownRightFrames[12]
    = { -5, 308, -5, 316, -5, 324, -5, 316, -5, 308, 242, -101 };
int16_t _jumpHeaderHitTeam1DownLeftFrames[12]
    = { -5, 307, -5, 315, -5, 323, -5, 315, -5, 307, 239, -101 };
int16_t _jumpHeaderHitTeam2UpFrames[12]
    = { -5, 606, -5, 614, -5, 622, -5, 614, -5, 606, 530, -101 };
int16_t _jumpHeaderHitTeam2DownFrames[12]
    = { -5, 607, -5, 615, -5, 623, -5, 615, -5, 607, 533, -101 };
int16_t _jumpHeaderHitTeam2RightFrames[12]
    = { -5, 608, -5, 616, -5, 624, -5, 616, -5, 608, 536, -101 };
int16_t _jumpHeaderHitTeam2LeftFrames[12]
    = { -5, 609, -5, 617, -5, 625, -5, 617, -5, 609, 539, -101 };
int16_t _jumpHeaderHitTeam2UpRightFrames[12]
    = { -5, 613, -5, 621, -5, 629, -5, 621, -5, 613, 551, -101 };
int16_t _jumpHeaderHitTeam2UpLeftFrames[12]
    = { -5, 612, -5, 620, -5, 628, -5, 620, -5, 612, 548, -101 };
int16_t _jumpHeaderHitTeam2DownRightFrames[12]
    = { -5, 611, -5, 619, -5, 627, -5, 619, -5, 611, 545, -101 };
int16_t _jumpHeaderHitTeam2DownLeftFrames[12]
    = { -5, 610, -5, 618, -5, 626, -5, 618, -5, 610, 542, -101 };
int16_t _jumpHeaderAttemptTeam1UpFrames[8]
    = { -5, 303, -5, 311, -10, 319, 289, -101 };
int16_t _jumpHeaderAttemptTeam1DownFrames[8]
    = { -5, 304, -5, 312, -10, 320, 290, -101 };
int16_t _jumpHeaderAttemptTeam1RightFrames[8]
    = { -5, 305, -5, 313, -10, 321, 292, -101 };
int16_t _jumpHeaderAttemptTeam1LeftFrames[8]
    = { -5, 306, -5, 314, -10, 322, 291, -101 };
int16_t _jumpHeaderAttemptTeam1UpRightFrames[8]
    = { -5, 310, -5, 318, -10, 326, 296, -101 };
int16_t _jumpHeaderAttemptTeam1UpLeftFrames[8]
    = { -5, 309, -5, 317, -10, 325, 295, -101 };
int16_t _jumpHeaderAttemptTeam1DownRightFrames[8]
    = { -5, 308, -5, 316, -10, 324, 294, -101 };
int16_t _jumpHeaderAttemptTeam1DownLeftFrames[8]
    = { -5, 307, -5, 315, -10, 323, 293, -101 };
int16_t _jumpHeaderAttemptTeam2UpFrames[8]
    = { -5, 606, -5, 614, -10, 622, 592, -101 };
int16_t _jumpHeaderAttemptTeam2DownFrames[8]
    = { -5, 607, -5, 615, -10, 623, 593, -101 };
int16_t _jumpHeaderAttemptTeam2RightFrames[8]
    = { -5, 608, -5, 616, -10, 624, 595, -101 };
int16_t _jumpHeaderAttemptTeam2LeftFrames[8]
    = { -5, 609, -5, 617, -10, 625, 594, -101 };
int16_t _jumpHeaderAttemptTeam2UpRightFrames[8]
    = { -5, 613, -5, 621, -10, 629, 599, -101 };
int16_t _jumpHeaderAttemptTeam2UpLeftFrames[8]
    = { -5, 612, -5, 620, -10, 628, 598, -101 };
int16_t _jumpHeaderAttemptTeam2DownRightFrames[8]
    = { -5, 611, -5, 619, -10, 627, 597, -101 };
int16_t _jumpHeaderAttemptTeam2DownLeftFrames[8]
    = { -5, 610, -5, 618, -10, 626, 596, -101 };
int16_t _team1LosingReactionFrames[6] = { -2, 230, -30, 301, 302, -101 };
int16_t _team2LosingReactionFrames[6] = { -2, 533, -30, 604, 605, -101 };
int16_t _team1WinningReactionFrames[5] = { -20, 254, -20, 230, -999 };
int16_t _team2WinningReactionFrames[5] = { -20, 557, -20, 533, -999 };
int16_t _team1PlGettingYellowCardFrames[6] = { -20, 230, -65, 301, 301, -101 };
int16_t _team2PlGettingYellowCardFrames[6] = { -20, 533, -65, 604, 604, -101 };
int16_t _team1PlayerGettingRedCardFrames[8]
    = { -20, 230, -70, 301, 301, -50, 302, -101 };
int16_t _team2PlayerGettingRedCardFrames[8]
    = { -20, 533, -70, 604, 604, -50, 605, -101 };
int16_t _team1PlGetting2ndYellowCardFrames[10]
    = { -20, 230, -80, 301, 301, 301, -75, 302, 302, -101 };
int16_t _team2PlGetting2ndYellowCardFrames[10]
    = { -20, 533, -80, 604, 604, 604, -75, 605, 605, -101 };
int16_t _playerRunningUpTeam1[5] = { 229, 227, 228, 227, -999 };
int16_t _playerRunningDownTeam1[5] = { 232, 230, 231, 230, -999 };
int16_t _playerRunningRightTeam1[5] = { 235, 233, 234, 233, -999 };
int16_t _playerRunningLeftTeam1[5] = { 238, 236, 237, 236, -999 };
int16_t _playerRunningUpRightTeam1[5] = { 250, 249, 248, 249, -999 };
int16_t _playerRunningUpLeftTeam1[5] = { 247, 246, 245, 246, -999 };
int16_t _playerRunningDownRightTeam1[5] = { 244, 243, 242, 243, -999 };
int16_t _playerRunningDownLeftTeam1[5] = { 241, 240, 239, 240, -999 };
int16_t _team1PlayerStandingFacingUpFrames[2] = { 227, -999 };
int16_t _team1PlayerStandingFacingBottomFrames[2] = { 230, -999 };
int16_t _team1PlayerStandingFacingRightFrames[2] = { 233, -999 };
int16_t _team1PlayerStandingFacingLeftFrames[2] = { 236, -999 };
int16_t _team1PlayerStandingFacingUpRightFrames[2] = { 249, -999 };
int16_t _team1PlayerStandingFacingTopLeftFrames[2] = { 246, -999 };
int16_t _team1PlayerStandingFacingBottomRightFrames[2] = { 243, -999 };
int16_t _team1PlayerStandingFacingBottomLeftFrames[2] = { 240, -999 };
int16_t _team1TacklingUpFrames[2] = { 281, -999 };
int16_t _team1TacklingDownFrames[2] = { 282, -999 };
int16_t _team1TacklingRightFrames[2] = { 284, -999 };
int16_t _team1TacklingLeftFrames[2] = { 283, -999 };
int16_t _team1TacklingUpRightFrames[2] = { 288, -999 };
int16_t _team1TacklingUpLeftFrames[2] = { 287, -999 };
int16_t _team1TacklingDownRightFrames[2] = { 286, -999 };
int16_t _team1TacklingDownLeftFrames[2] = { 285, -999 };
int16_t _team1TackledUpFrames[2] = { 289, -999 };
int16_t _team1TackledDownFrames[2] = { 290, -999 };
int16_t _team1TackledRightFrames[2] = { 292, -999 };
int16_t _team1TackledLeftFrames[2] = { 291, -999 };
int16_t _team1TackledUpRightFrames[2] = { 296, -999 };
int16_t _team1TackledUpLeftFrames[2] = { 295, -999 };
int16_t _team1TackledDownRightFrames[2] = { 294, -999 };
int16_t _team1TackledDownLeftFrames[2] = { 293, -999 };
int16_t _staticHeaderAttemptTeam1UpFrames[9]
    = { -10, 303, -15, 311, -5, 303, -10, 227, -101 };
int16_t _staticHeaderAttemptTeam1DownFrames[9]
    = { -10, 304, -15, 312, -5, 304, -10, 230, -101 };
int16_t _staticHeaderAttemptTeam1RightFrames[9]
    = { -10, 305, -15, 313, -5, 305, -10, 233, -101 };
int16_t _staticHeaderAttemptTeam1LeftFrames[9]
    = { -10, 306, -15, 314, -5, 306, -10, 236, -101 };
int16_t _staticHeaderAttemptTeam1UpRightFrames[9]
    = { -10, 310, -15, 318, -5, 310, -10, 248, -101 };
int16_t _staticHeaderAttemptTeam1UpLeftFrames[9]
    = { -10, 309, -15, 317, -5, 309, -10, 245, -101 };
int16_t _staticHeaderAttemptTeam1DownRightFrames[9]
    = { -10, 308, -15, 316, -5, 308, -10, 242, -101 };
int16_t _staticHeaderAttemptTeam1DownLeftFrames[9]
    = { -10, 307, -15, 315, -5, 307, -10, 239, -101 };
int16_t _staticHeaderAttemptTeam2UpFrames[9]
    = { -10, 606, -15, 614, -5, 606, -10, 530, -101 };
int16_t _staticHeaderAttemptTeam2DownFrames[9]
    = { -10, 607, -15, 615, -5, 607, -10, 533, -101 };
int16_t _staticHeaderAttemptTeam2RightFrames[9]
    = { -10, 608, -15, 616, -5, 608, -10, 536, -101 };
int16_t _staticHeaderAttemptTeam2LeftFrames[9]
    = { -10, 609, -15, 617, -5, 609, -10, 539, -101 };
int16_t _staticHeaderAttemptTeam2UpRightFrames[9]
    = { -10, 613, -15, 621, -5, 613, -10, 551, -101 };
int16_t _staticHeaderAttemptTeam2UpLeftFrames[9]
    = { -10, 612, -15, 620, -5, 612, -10, 548, -101 };
int16_t _staticHeaderAttemptTeam2DownRightFrames[9]
    = { -10, 611, -15, 619, -5, 611, -10, 545, -101 };
int16_t _staticHeaderAttemptTeam2DownLeftFrames[9]
    = { -10, 610, -15, 618, -5, 610, -10, 542, -101 };
int16_t _staticHeaderAimHitTeam1UpFrames[9]
    = { -10, 303, -15, 319, -5, 303, -10, 227, -101 };
int16_t _staticHeaderAimHitTeam1DownFrames[9]
    = { -10, 304, -15, 320, -5, 304, -10, 230, -101 };
int16_t _staticHeaderAimHitTeam1RightFrames[9]
    = { -10, 305, -15, 321, -5, 305, -10, 233, -101 };
int16_t _staticHeaderAimHitTeam1LeftFrames[9]
    = { -10, 306, -15, 322, -5, 306, -10, 236, -101 };
int16_t _staticHeaderAimHitTeam1UpRightFrames[9]
    = { -10, 310, -15, 326, -5, 310, -10, 248, -101 };
int16_t _staticHeaderAimHitTeam1UpLeftFrames[9]
    = { -10, 309, -15, 325, -5, 309, -10, 245, -101 };
int16_t _staticHeaderAimHitTeam1DownRightFrames[9]
    = { -10, 308, -15, 324, -5, 308, -10, 242, -101 };
int16_t _staticHeaderAimHitTeam1DownLeftFrames[9]
    = { -10, 307, -15, 323, -5, 307, -10, 239, -101 };
int16_t _staticHeaderAimHitTeam2UpFrames[9]
    = { -10, 606, -15, 622, -5, 606, -10, 530, -101 };
int16_t _staticHeaderAimHitTeam2DownFrames[9]
    = { -10, 607, -15, 623, -5, 607, -10, 533, -101 };
int16_t _staticHeaderAimHitTeam2RightFrames[9]
    = { -10, 608, -15, 624, -5, 608, -10, 536, -101 };
int16_t _staticHeaderAimHitTeam2LeftFrames[9]
    = { -10, 609, -15, 625, -5, 609, -10, 539, -101 };
int16_t _staticHeaderAimHitTeam2UpRightFrames[9]
    = { -10, 613, -15, 629, -5, 613, -10, 551, -101 };
int16_t _staticHeaderAimHitTeam2UpLeftFrames[9]
    = { -10, 612, -15, 628, -5, 612, -10, 548, -101 };
int16_t _staticHeaderAimHitTeam2DownRightFrames[9]
    = { -10, 611, -15, 627, -5, 611, -10, 545, -101 };
int16_t _staticHeaderAimHitTeam2DownLeftFrames[9]
    = { -10, 610, -15, 626, -5, 610, -10, 542, -101 };
int16_t _aboutToThrowInTeam1UpFrames[7] = { -90, 258, -20, 257, -20, 258, -104 };
int16_t _aboutToThrowInTeam1DownFrames[7]
    = { -90, 261, -20, 260, -20, 261, -104 };
int16_t _aboutToThrowInTeam1RightFrames[7]
    = { -90, 264, -20, 263, -20, 264, -104 };
int16_t _aboutToThrowInTeam1LeftFrames[7]
    = { -90, 267, -20, 266, -20, 267, -104 };
int16_t _aboutToThrowInTeam1UpRightFrames[7]
    = { -90, 279, -20, 278, -20, 279, -104 };
int16_t _aboutToThrowInTeam1UpLeftFrames[7]
    = { -90, 276, -20, 275, -20, 276, -104 };
int16_t _aboutToThrowInTeam1DownRightFrames[7]
    = { -90, 273, -20, 272, -20, 273, -104 };
int16_t _aboutToThrowInTeam1DownLeftFrames[7]
    = { -90, 270, -20, 269, -20, 270, -104 };
int16_t _throwInPassTeam1UpFrames[4] = { -5, 258, 259, -101 };
int16_t _throwInPassTeam1DownFrames[4] = { -5, 261, 262, -101 };
int16_t _throwInPassTeam1RightFrames[4] = { -5, 264, 265, -101 };
int16_t _throwInPassTeam1LeftFrames[4] = { -5, 267, 268, -101 };
int16_t _throwInPassTeam1UpRightFrames[4] = { -5, 279, 280, -101 };
int16_t _throwInPassTeam1UpLeftFrames[4] = { -5, 276, 277, -101 };
int16_t _throwInPassTeam1DownRightFrames[4] = { -5, 273, 274, -101 };
int16_t _throwInPassTeam1DownLeftFrames[4] = { -5, 270, 271, -101 };
int16_t _throwInKickTeam1UpFrames[6] = { -5, 258, 257, 258, 259, -101 };
int16_t _throwInKickTeam1DownFrames[6] = { -5, 261, 260, 261, 262, -101 };
int16_t _throwInKickTeam1RightFrames[6] = { -5, 264, 263, 264, 265, -101 };
int16_t _throwInKickTeam1LeftFrames[6] = { -5, 267, 266, 267, 268, -101 };
int16_t _throwInKickTeam1UpRightFrames[6] = { -5, 279, 278, 279, 280, -101 };
int16_t _throwInKickTeam1UpLeftFrames[6] = { -5, 276, 275, 276, 277, -101 };
int16_t _throwInKickTeam1DownRightFrames[6] = { -5, 273, 272, 273, 274, -101 };
int16_t _throwInKickTeam1DownLeftFrames[6] = { -5, 270, 269, 270, 271, -101 };
int16_t _team1InjuredPlayerTopRightTable[5] = { 297, -20, 298, -20, -999 };
int16_t _team1InjuredPlayerTopLeftTable[5] = { 299, -20, 300, -20, -999 };
int16_t _playerRunningUpTeam2[5] = { 532, 530, 531, 530, -999 };
int16_t _playerRunningDownTeam2[5] = { 535, 533, 534, 533, -999 };
int16_t _playerRunningRightTeam2[5] = { 538, 536, 537, 536, -999 };
int16_t _playerRunningLeftTeam2[5] = { 541, 539, 540, 539, -999 };
int16_t _playerRunningUpRightTeam2[5] = { 553, 551, 552, 551, -999 };
int16_t _playerRunningUpLeftTeam2[5] = { 550, 548, 549, 548, -999 };
int16_t _playerRunningDownRightTeam2[5] = { 547, 545, 546, 545, -999 };
int16_t _playerRunningDownLeftTeam2[5] = { 544, 542, 543, 542, -999 };
int16_t _team2PlayerStandingFacingUpFrames[2] = { 530, -999 };
int16_t _team2PlayerStandingFacingDownFrames[2] = { 533, -999 };
int16_t _team2PlayerStandingFacingRightFrames[2] = { 536, -999 };
int16_t _team2PlayerStandingFacingLeftFrames[2] = { 539, -999 };
int16_t _team2PlayerStandingFacingUpRightFrames[2] = { 552, -999 };
int16_t _team2PlayerStandingFacingUpLeftFrames[2] = { 549, -999 };
int16_t _team2PlayerStandingFacingDownRightFrames[2] = { 546, -999 };
int16_t _team2PlayerStandingFacingDownLeftFrames[2] = { 543, -999 };
int16_t _team2TacklingUpFrames[2] = { 584, -999 };
int16_t _team2TacklingDownFrames[2] = { 585, -999 };
int16_t _team2TacklingRightFrames[2] = { 587, -999 };
int16_t _team2TacklingLeftFrames[2] = { 586, -999 };
int16_t _team2TacklingUpRightFrames[2] = { 591, -999 };
int16_t _team2TacklingUpLeftFrames[2] = { 590, -999 };
int16_t _team2TacklingDownRightFrames[2] = { 589, -999 };
int16_t _team2TacklingDownLeftFrames[2] = { 588, -999 };
int16_t _team2TackledUpFrames[2] = { 592, -999 };
int16_t _team2TackledDownFrames[2] = { 593, -999 };
int16_t _team2TackledRightFrames[2] = { 595, -999 };
int16_t _team2TackledLeftFrames[2] = { 594, -999 };
int16_t _team2TackledUpRightFrames[2] = { 599, -999 };
int16_t _team2TackledUpLeftFrames[2] = { 598, -999 };
int16_t _team2TackledDownRightFrames[2] = { 597, -999 };
// Also contains 8 embedded team1 standing-recovery sequences after first -999
int16_t _team2TackledDownLeftFrames[26] = { 596, -999, -50, 303, -999, -50, 304,
    -999, -50, 305, -999, -50, 306, -999, -50, 310, -999, -50, 309, -999, -50,
    308, -999, -50, 307, -999 };
int16_t _aboutToThrowInTeam2UpFrames[7] = { -90, 561, -20, 560, -20, 561, -104 };
int16_t _aboutToThrowInTeam2DownFrames[7]
    = { -90, 564, -20, 563, -20, 564, -104 };
int16_t _aboutToThrowInTeam2RightFrames[7]
    = { -90, 567, -20, 566, -20, 567, -104 };
int16_t _aboutToThrowInTeam2LeftFrames[7]
    = { -90, 570, -20, 569, -20, 570, -104 };
int16_t _aboutToThrowInTeam2UpRightFrames[7]
    = { -90, 582, -20, 581, -20, 582, -104 };
int16_t _aboutToThrowInTeam2UpLeftFrames[7]
    = { -90, 579, -20, 578, -20, 579, -104 };
int16_t _aboutToThrowInTeam2DownRightFrames[7]
    = { -90, 576, -20, 575, -20, 576, -104 };
int16_t _aboutToThrowInTeam2DownLeftFrames[7]
    = { -90, 573, -20, 572, -20, 573, -104 };
int16_t _throwInPassTeam2UpFrames[4] = { -5, 561, 562, -101 };
int16_t _throwInPassTeam2DownFrames[4] = { -5, 564, 565, -101 };
int16_t _throwInPassTeam2RightFrames[4] = { -5, 567, 568, -101 };
int16_t _throwInPassTeam2LeftFrames[4] = { -5, 570, 571, -101 };
int16_t _throwInPassTeam2UpRightFrames[4] = { -5, 582, 583, -101 };
int16_t _throwInPassTeam2UpLeftFrames[4] = { -5, 579, 580, -101 };
int16_t _throwInPassTeam2DownRightFrames[4] = { -5, 576, 577, -101 };
int16_t _throwInPassTeam2DownLeftFrames[4] = { -5, 573, 574, -101 };
int16_t _throwInKickTeam2UpFrames[6] = { -5, 561, 560, 561, 562, -101 };
int16_t _throwInKickTeam2DownFrames[6] = { -5, 564, 563, 564, 565, -101 };
int16_t _throwInKickTeam2RightFrames[6] = { -5, 567, 566, 567, 568, -101 };
int16_t _throwInKickTeam2LeftFrames[6] = { -5, 570, 569, 570, 571, -101 };
int16_t _throwInKickTeam2UpRightFrames[6] = { -5, 582, 581, 582, 583, -101 };
int16_t _throwInKickTeam2UpLeftFrames[6] = { -5, 579, 578, 579, 580, -101 };
int16_t _throwInKickTeam2DownRightFrames[6] = { -5, 576, 575, 576, 577, -101 };
int16_t _throwInKickTeam2DownLeftFrames[6] = { -5, 573, 572, 573, 574, -101 };
int16_t _team2InjuredPlayerTopRightTable[5] = { 600, -20, 601, -20, -999 };
int16_t _team2InjuredPlayerTopLeftTable[5] = { 602, -20, 603, -20, -999 };
int16_t _goalkeeperRunningUpTeam1[5] = { 835, 833, 834, 833, -999 };
int16_t _goalkeeperRunningDownTeam1[5] = { 838, 836, 837, 836, -999 };
int16_t _goalkeeperRunningRightTeam1[5] = { 841, 839, 840, 839, -999 };
int16_t _goalkeeperRunningLeftTeam1[5] = { 844, 842, 843, 842, -999 };
int16_t _goalkeeperRunningUpRightTeam1[5] = { 856, 855, 854, 855, -999 };
int16_t _goalkeeperRunningUpLeftTeam1[5] = { 853, 852, 851, 852, -999 };
int16_t _goalkeeperRunningDownRightTeam1[5] = { 850, 849, 848, 849, -999 };
int16_t _goalkeeperRunningDownLeftTeam1[5] = { 847, 846, 845, 846, -999 };
int16_t _goalie1StandingFacingUpFrames[2] = { 833, -999 };
int16_t _goalie1StandingFacingDownFrames[2] = { 836, -999 };
int16_t _goalie1StandingFacingRightFrames[2] = { 839, -999 };
int16_t _goalie1StandingFacingLeftFrames[2] = { 842, -999 };
int16_t _goalie1StandingFacingUpRightFrames[2] = { 855, -999 };
int16_t _goalie1StandingFacingUpLeftFrames[2] = { 852, -999 };
int16_t _goalie1StandingFacingDownRightFrames[2] = { 849, -999 };
int16_t _goalie1StandingFacingDownLeftFrames[2] = { 846, -999 };
int16_t _goalie1CatchBallFacingUpFrames[10]
    = { -5, 885, 886, 887, -3, 886, -2, 885, 833, -101 };
int16_t _goalie1CatchBallFacingDownFrames[10]
    = { -5, 888, 889, 890, -3, 889, -2, 888, 836, -101 };
int16_t _goalie1GoalKickRightFrames[13]
    = { -2, 857, -2, 861, -2, 862, 862, -2, 862, -62, 862, 833, -101 };
int16_t _goalie1GoalKickLeftFrames[13]
    = { -2, 869, -2, 865, -2, 864, 864, -2, 864, -62, 864, 833, -101 };
int16_t _goalie1HighDiveHoldRightFrames[13]
    = { -2, 871, -2, 875, -2, 876, 876, -2, 876, -62, 876, 836, -101 };
int16_t _goalie1HighDiveHoldLeftFrames[13]
    = { -2, 883, -2, 879, -2, 878, 878, -2, 878, -62, 878, 836, -101 };
int16_t _goalie1LowDiveRightFaceUpFrames[13]
    = { -5, 857, -5, 861, -5, 862, 862, -2, 862, -50, 862, 833, -101 };
int16_t _goalie1LowDiveLeftFaceUpFrames[13]
    = { -5, 869, -5, 865, -5, 864, 864, -2, 864, -50, 864, 833, -101 };
int16_t _goalie1LowDiveRightFaceDownFrames[13]
    = { -5, 871, -5, 875, -5, 876, 876, -2, 876, -50, 876, 836, -101 };
int16_t _goalie1LowDiveLeftFaceDownFrames[13]
    = { -5, 883, -5, 879, -5, 878, 878, -2, 878, -50, 878, 836, -101 };
int16_t _goalie1HighDiveReachRightFaceUpFrames[13]
    = { -5, 857, -7, 858, -3, 859, 860, -2, 861, -50, 862, 833, -101 };
int16_t _goalie1HighDiveReachLeftFaceUpFrames[13]
    = { -5, 869, -7, 868, -3, 867, 866, -2, 865, -50, 864, 833, -101 };
int16_t _goalie1HighDiveReachRightFaceDownFrames[13]
    = { -5, 871, -7, 872, -3, 873, 874, -2, 875, -50, 876, 836, -101 };
int16_t _goalie1HighDiveReachLeftFaceDownFrames[13]
    = { -5, 883, -7, 882, -3, 881, 880, -2, 879, -50, 878, 836, -101 };
int16_t _goalie1DiveLandFacingUpFrames[6] = { -4, 886, -2, 885, 833, -101 };
int16_t _goalie1DiveLandFacingDownFrames[6] = { -4, 889, -2, 888, 836, -101 };
int16_t _goalie1DeflectBallFacingUpFrames[6] = { -4, 886, -2, 885, 833, -101 };
int16_t _goalie1DeflectBallFacingDownFrames[6] = { -8, 889, -4, 888, 836, -101 };
int16_t _goalkeeperRunningUpTeam2[5] = { 951, 949, 950, 949, -999 };
int16_t _goalkeeperRunningDownTeam2[5] = { 954, 952, 953, 952, -999 };
int16_t _goalkeeperRunningRightTeam2[5] = { 957, 955, 956, 955, -999 };
int16_t _goalkeeperRunningLeftTeam2[5] = { 960, 958, 959, 958, -999 };
int16_t _goalkeeperRunningUpRightTeam2[5] = { 972, 971, 970, 971, -999 };
int16_t _goalkeeperRunningUpLeftTeam2[5] = { 969, 968, 967, 968, -999 };
int16_t _goalkeeperRunningDownRightTeam2[5] = { 966, 965, 964, 965, -999 };
int16_t _goalkeeperRunningDownLeftTeam2[5] = { 963, 962, 961, 962, -999 };
int16_t _goalie2StandingFacingUpFrames[2] = { 949, -999 };
int16_t _goalie2StandingFacingDownFrames[2] = { 952, -999 };
int16_t _goalie2StandingFacingRightFrames[2] = { 955, -999 };
int16_t _goalie2StandingFacingLeftFrames[2] = { 958, -999 };
int16_t _goalie2StandingFacingUpRightFrames[2] = { 971, -999 };
int16_t _goalie2StandingFacingUpLeftFrames[2] = { 968, -999 };
int16_t _goalie2StandingFacingDownRightFrames[2] = { 965, -999 };
int16_t _goalie2StandingFacingDownLeftFrames[2] = { 962, -999 };
int16_t _goalie2CatchBallFacingUpFrames[10]
    = { -5, 1001, 1002, 1003, -3, 1002, -2, 1001, 949, -101 };
int16_t _goalie2CatchBallFacingDownFrames[10]
    = { -5, 1004, 1005, 1006, -3, 1005, -2, 1004, 952, -101 };
int16_t _goalie2GoalKickRightFrames[13]
    = { -2, 973, -2, 977, -2, 978, 978, -2, 978, -62, 978, 949, -101 };
int16_t _goalie2GoalKickLeftFrames[13]
    = { -2, 985, -2, 981, -2, 980, 980, -2, 980, -62, 980, 949, -101 };
int16_t _goalie2HighDiveHoldRightFrames[13]
    = { -2, 987, -2, 991, -2, 992, 992, -2, 992, -62, 992, 952, -101 };
int16_t _goalie2HighDiveHoldLeftFrames[13]
    = { -2, 999, -2, 995, -2, 994, 994, -2, 994, -62, 994, 952, -101 };
int16_t _goalie2LowDiveRightFaceUpFrames[13]
    = { -5, 973, -5, 977, -5, 978, 978, -2, 978, -50, 978, 949, -101 };
int16_t _goalie2LowDiveLeftFaceUpFrames[13]
    = { -5, 985, -5, 981, -5, 980, 980, -2, 980, -50, 980, 949, -101 };
int16_t _goalie2LowDiveRightFaceDownFrames[13]
    = { -5, 987, -5, 991, -5, 992, 992, -2, 992, -50, 992, 952, -101 };
int16_t _goalie2LowDiveLeftFaceDownFrames[13]
    = { -5, 999, -5, 995, -5, 994, 994, -2, 994, -50, 994, 952, -101 };
int16_t _goalie2HighDiveReachRightFaceUpFrames[13]
    = { -5, 973, -7, 974, -3, 975, 976, -2, 977, -50, 978, 949, -101 };
int16_t _goalie2HighDiveReachLeftFaceUpFrames[13]
    = { -5, 985, -7, 984, -3, 983, 982, -2, 981, -50, 980, 949, -101 };
int16_t _goalie2HighDiveReachRightFaceDownFrames[13]
    = { -5, 987, -7, 988, -3, 989, 990, -2, 991, -50, 992, 952, -101 };
int16_t _goalie2HighDiveReachLeftFaceDownFrames[13]
    = { -5, 999, -7, 998, -3, 997, 996, -2, 995, -50, 994, 952, -101 };
int16_t _goalie2DiveLandFacingUpFrames[6] = { -4, 1002, -2, 1001, 949, -101 };
int16_t _goalie2DiveLandFacingDownFrames[6] = { -4, 1005, -2, 1004, 952, -101 };
int16_t _goalie2DeflectBallFacingUpFrames[6] = { -4, 1002, -2, 1001, 949, -101 };
int16_t _goalie2DeflectBallFacingDownFrames[6]
    = { -4, 1005, -2, 1004, 952, -101 };
int16_t _refComingUpFrames[5] = { 1161, 1159, 1160, 1159, -999 };
int16_t _refComingDownFrames[5] = { 1164, 1162, 1163, 1162, -999 };
int16_t _refStandingFacingUpFrames[2] = { 1159, -999 };
int16_t _refStandingFacingDownFrames[2] = { 1162, -999 };
int16_t _refStandingFacingSideFrames[2] = { 1165, -999 };
int16_t _refYellowCardFrames[25]
    = { -10, 1165, -12, 1168, -12, 1169, -12, 1168, -12, 1169, -12, 1168, -12,
          1169, -12, 1168, -12, 1169, -20, 1165, -80, 1166, -50, 1165, -101 };
int16_t _refRedCardFrames[25]
    = { -10, 1165, -12, 1168, -12, 1169, -12, 1168, -12, 1169, -12, 1168, -12,
          1169, -12, 1168, -12, 1169, -20, 1165, -80, 1167, -50, 1165, -101 };
int16_t _refSecondYellowFrames[29] = { -10, 1165, -12, 1168, -12, 1169, -12,
    1168, -12, 1169, -12, 1168, -12, 1169, -12, 1168, -12, 1169, -20, 1165, -80,
    1166, -20, 1165, -80, 1167, -50, 1165, -101 };
AnimTable _staticHeaderAttemptAnimTable = { 5,
    { _staticHeaderAttemptTeam1UpFrames, _staticHeaderAttemptTeam1UpRightFrames,
        _staticHeaderAttemptTeam1RightFrames,
        _staticHeaderAttemptTeam1DownRightFrames,
        _staticHeaderAttemptTeam1DownFrames,
        _staticHeaderAttemptTeam1DownLeftFrames,
        _staticHeaderAttemptTeam1LeftFrames,
        _staticHeaderAttemptTeam1UpLeftFrames, _staticHeaderAttemptTeam2UpFrames,
        _staticHeaderAttemptTeam2UpRightFrames,
        _staticHeaderAttemptTeam2RightFrames,
        _staticHeaderAttemptTeam2DownRightFrames,
        _staticHeaderAttemptTeam2DownFrames,
        _staticHeaderAttemptTeam2DownLeftFrames,
        _staticHeaderAttemptTeam2LeftFrames,
        _staticHeaderAttemptTeam2UpLeftFrames, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0 } };
AnimTable _staticHeaderAimAnimTable = { 5,
    { _staticHeaderAimHitTeam1UpFrames, _staticHeaderAimHitTeam1UpRightFrames,
        _staticHeaderAimHitTeam1RightFrames,
        _staticHeaderAimHitTeam1DownRightFrames,
        _staticHeaderAimHitTeam1DownFrames,
        _staticHeaderAimHitTeam1DownLeftFrames,
        _staticHeaderAimHitTeam1LeftFrames, _staticHeaderAimHitTeam1UpLeftFrames,
        _staticHeaderAimHitTeam2UpFrames, _staticHeaderAimHitTeam2UpRightFrames,
        _staticHeaderAimHitTeam2RightFrames,
        _staticHeaderAimHitTeam2DownRightFrames,
        _staticHeaderAimHitTeam2DownFrames,
        _staticHeaderAimHitTeam2DownLeftFrames,
        _staticHeaderAimHitTeam2LeftFrames, _staticHeaderAimHitTeam2UpLeftFrames,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _staticHeaderHitAnimTable = { 5,
    { _staticHeaderAimHitTeam1UpFrames, _staticHeaderAimHitTeam1UpRightFrames,
        _staticHeaderAimHitTeam1RightFrames,
        _staticHeaderAimHitTeam1DownRightFrames,
        _staticHeaderAimHitTeam1DownFrames,
        _staticHeaderAimHitTeam1DownLeftFrames,
        _staticHeaderAimHitTeam1LeftFrames, _staticHeaderAimHitTeam1UpLeftFrames,
        _staticHeaderAimHitTeam2UpFrames, _staticHeaderAimHitTeam2UpRightFrames,
        _staticHeaderAimHitTeam2RightFrames,
        _staticHeaderAimHitTeam2DownRightFrames,
        _staticHeaderAimHitTeam2DownFrames,
        _staticHeaderAimHitTeam2DownLeftFrames,
        _staticHeaderAimHitTeam2LeftFrames, _staticHeaderAimHitTeam2UpLeftFrames,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _jumpHeaderAttemptAnimTable = { 5,
    { _jumpHeaderAttemptTeam1UpFrames, _jumpHeaderAttemptTeam1UpRightFrames,
        _jumpHeaderAttemptTeam1RightFrames,
        _jumpHeaderAttemptTeam1DownRightFrames, _jumpHeaderAttemptTeam1DownFrames,
        _jumpHeaderAttemptTeam1DownLeftFrames, _jumpHeaderAttemptTeam1LeftFrames,
        _jumpHeaderAttemptTeam1UpLeftFrames, _jumpHeaderAttemptTeam2UpFrames,
        _jumpHeaderAttemptTeam2UpRightFrames, _jumpHeaderAttemptTeam2RightFrames,
        _jumpHeaderAttemptTeam2DownRightFrames, _jumpHeaderAttemptTeam2DownFrames,
        _jumpHeaderAttemptTeam2DownLeftFrames, _jumpHeaderAttemptTeam2LeftFrames,
        _jumpHeaderAttemptTeam2UpLeftFrames, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0 } };
// Player hitting flying or lob headers (on ball contact)
AnimTable _jumpHeaderHitAnimTable = { 5,
    { _jumpHeaderTeam1UpFrames, _jumpHeaderHitTeam1UpRightFrames,
        _jumpHeaderHitTeam1RightFrames, _jumpHeaderHitTeam1DownRightFrames,
        _jumpHeaderHitTeam1DownFrames, _jumpHeaderHitTeam1DownLeftFrames,
        _jumpHeaderHitTeam1LeftFrames, _jumpHeaderHitTeam1UpLeftFrames,
        _jumpHeaderHitTeam2UpFrames, _jumpHeaderHitTeam2UpRightFrames,
        _jumpHeaderHitTeam2RightFrames, _jumpHeaderHitTeam2DownRightFrames,
        _jumpHeaderHitTeam2DownFrames, _jumpHeaderHitTeam2DownLeftFrames,
        _jumpHeaderHitTeam2LeftFrames, _jumpHeaderHitTeam2UpLeftFrames, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _playerRunningAnimTable = { 5,
    { _playerRunningUpTeam1, _playerRunningUpRightTeam1, _playerRunningRightTeam1,
        _playerRunningDownRightTeam1, _playerRunningDownTeam1,
        _playerRunningDownLeftTeam1, _playerRunningLeftTeam1,
        _playerRunningUpLeftTeam1, _playerRunningUpTeam2,
        _playerRunningUpRightTeam2, _playerRunningRightTeam2,
        _playerRunningDownRightTeam2, _playerRunningDownTeam2,
        _playerRunningDownLeftTeam2, _playerRunningLeftTeam2,
        _playerRunningUpLeftTeam2, _goalkeeperRunningUpTeam1,
        _goalkeeperRunningUpRightTeam1, _goalkeeperRunningRightTeam1,
        _goalkeeperRunningDownRightTeam1, _goalkeeperRunningDownTeam1,
        _goalkeeperRunningDownLeftTeam1, _goalkeeperRunningLeftTeam1,
        _goalkeeperRunningUpLeftTeam1, _goalkeeperRunningUpTeam2,
        _goalkeeperRunningUpRightTeam2, _goalkeeperRunningRightTeam2,
        _goalkeeperRunningDownRightTeam2, _goalkeeperRunningDownTeam2,
        _goalkeeperRunningDownLeftTeam2, _goalkeeperRunningLeftTeam2,
        _goalkeeperRunningUpLeftTeam2 } };
AnimTable _playerNormalStandingAnimTable = { 5,
    { _team1PlayerStandingFacingUpFrames, _team1PlayerStandingFacingUpRightFrames,
        _team1PlayerStandingFacingRightFrames,
        _team1PlayerStandingFacingBottomRightFrames,
        _team1PlayerStandingFacingBottomFrames,
        _team1PlayerStandingFacingBottomLeftFrames,
        _team1PlayerStandingFacingLeftFrames,
        _team1PlayerStandingFacingTopLeftFrames,
        _team2PlayerStandingFacingUpFrames,
        _team2PlayerStandingFacingUpRightFrames,
        _team2PlayerStandingFacingRightFrames,
        _team2PlayerStandingFacingDownRightFrames,
        _team2PlayerStandingFacingDownFrames,
        _team2PlayerStandingFacingDownLeftFrames,
        _team2PlayerStandingFacingLeftFrames,
        _team2PlayerStandingFacingUpLeftFrames, _goalie1StandingFacingUpFrames,
        _goalie1StandingFacingUpRightFrames, _goalie1StandingFacingRightFrames,
        _goalie1StandingFacingDownRightFrames, _goalie1StandingFacingDownFrames,
        _goalie1StandingFacingDownLeftFrames, _goalie1StandingFacingLeftFrames,
        _goalie1StandingFacingUpLeftFrames, _goalie2StandingFacingUpFrames,
        _goalie2StandingFacingUpRightFrames, _goalie2StandingFacingRightFrames,
        _goalie2StandingFacingDownRightFrames, _goalie2StandingFacingDownFrames,
        _goalie2StandingFacingDownLeftFrames, _goalie2StandingFacingLeftFrames,
        _goalie2StandingFacingUpLeftFrames } };
AnimTable _plTacklingAnimTable = { 5,
    { _team1TacklingUpFrames, _team1TacklingUpRightFrames,
        _team1TacklingRightFrames, _team1TacklingDownRightFrames,
        _team1TacklingDownFrames, _team1TacklingDownLeftFrames,
        _team1TacklingLeftFrames, _team1TacklingUpLeftFrames,
        _team2TacklingUpFrames, _team2TacklingUpRightFrames,
        _team2TacklingRightFrames, _team2TacklingDownRightFrames,
        _team2TacklingDownFrames, _team2TacklingDownLeftFrames,
        _team2TacklingLeftFrames, _team2TacklingUpLeftFrames, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable_20 _plTacklingAnimTable_2 = { 5,
    { _team1TacklingUpFrames, _team1TacklingUpRightFrames,
        _team1TacklingRightFrames, _team1TacklingDownRightFrames,
        _team1TacklingDownFrames, _team1TacklingDownLeftFrames,
        _team1TacklingLeftFrames, _team1TacklingUpLeftFrames,
        _team2TacklingUpFrames, _team2TacklingUpRightFrames,
        _team2TacklingRightFrames, _team2TacklingDownRightFrames,
        _team2TacklingDownFrames, _team2TacklingDownLeftFrames,
        _team2TacklingLeftFrames, _team2TacklingUpLeftFrames, 0, 0, 0, 0 } };
AnimTable_20 _playerTackledAnimTable = { 5,
    { _team1TackledUpFrames, _team1TackledUpRightFrames, _team1TackledRightFrames,
        _team1TackledDownRightFrames, _team1TackledDownFrames,
        _team1TackledDownLeftFrames, _team1TackledLeftFrames,
        _team1TackledUpLeftFrames, _team2TackledUpFrames,
        _team2TackledUpRightFrames, _team2TackledRightFrames,
        _team2TackledDownRightFrames, _team2TackledDownFrames,
        _team2TackledDownLeftFrames, _team2TackledLeftFrames,
        _team2TackledUpLeftFrames, 0, 0, 0, 0 } };
AnimTable _goalieCatchingBallAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1CatchBallFacingUpFrames, _goalie1CatchBallFacingUpFrames,
        _goalie1CatchBallFacingUpFrames, _goalie1CatchBallFacingDownFrames,
        _goalie1CatchBallFacingDownFrames, _goalie1CatchBallFacingDownFrames,
        _goalie1CatchBallFacingDownFrames, _goalie1CatchBallFacingUpFrames,
        _goalie2CatchBallFacingUpFrames, _goalie2CatchBallFacingUpFrames,
        _goalie2CatchBallFacingUpFrames, _goalie2CatchBallFacingDownFrames,
        _goalie2CatchBallFacingDownFrames, _goalie2CatchBallFacingDownFrames,
        _goalie2CatchBallFacingDownFrames, _goalie2CatchBallFacingUpFrames } };
AnimTable _aboutToThrowInAnimTable = { 5,
    { _aboutToThrowInTeam1UpFrames, _aboutToThrowInTeam1UpRightFrames,
        _aboutToThrowInTeam1RightFrames, _aboutToThrowInTeam1DownRightFrames,
        _aboutToThrowInTeam1DownFrames, _aboutToThrowInTeam1DownLeftFrames,
        _aboutToThrowInTeam1LeftFrames, _aboutToThrowInTeam1UpLeftFrames,
        _aboutToThrowInTeam2UpFrames, _aboutToThrowInTeam2UpRightFrames,
        _aboutToThrowInTeam2RightFrames, _aboutToThrowInTeam2DownRightFrames,
        _aboutToThrowInTeam2DownFrames, _aboutToThrowInTeam2DownLeftFrames,
        _aboutToThrowInTeam2LeftFrames, _aboutToThrowInTeam2UpLeftFrames, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _throwInPassAnimTable = { 5,
    { _throwInPassTeam1UpFrames, _throwInPassTeam1UpRightFrames,
        _throwInPassTeam1RightFrames, _throwInPassTeam1DownRightFrames,
        _throwInPassTeam1DownFrames, _throwInPassTeam1DownLeftFrames,
        _throwInPassTeam1LeftFrames, _throwInPassTeam1UpLeftFrames,
        _throwInPassTeam2UpFrames, _throwInPassTeam2UpRightFrames,
        _throwInPassTeam2RightFrames, _throwInPassTeam2DownRightFrames,
        _throwInPassTeam2DownFrames, _throwInPassTeam2DownLeftFrames,
        _throwInPassTeam2LeftFrames, _throwInPassTeam2UpLeftFrames, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _throwInKickAnimTable = { 5,
    { _throwInKickTeam1UpFrames, _throwInKickTeam1UpRightFrames,
        _throwInKickTeam1RightFrames, _throwInKickTeam1DownRightFrames,
        _throwInKickTeam1DownFrames, _throwInKickTeam1DownLeftFrames,
        _throwInKickTeam1LeftFrames, _throwInKickTeam1UpLeftFrames,
        _throwInKickTeam2UpFrames, _throwInKickTeam2UpRightFrames,
        _throwInKickTeam2RightFrames, _throwInKickTeam2DownRightFrames,
        _throwInKickTeam2DownFrames, _throwInKickTeam2DownLeftFrames,
        _throwInKickTeam2LeftFrames, _throwInKickTeam2UpLeftFrames, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _goalieGoalKickAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1GoalKickRightFrames, 0, 0, 0, _goalie1GoalKickLeftFrames, 0, 0, 0,
        _goalie2GoalKickRightFrames, 0, 0, 0, _goalie2GoalKickLeftFrames, 0 } };
AnimTable _leftGoalieJumpingHighAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1HighDiveReachRightFaceUpFrames, 0, 0, 0,
        _goalie1HighDiveReachLeftFaceUpFrames, 0, 0, 0,
        _goalie2HighDiveReachRightFaceUpFrames, 0, 0, 0,
        _goalie2HighDiveReachLeftFaceUpFrames, 0 } };
AnimTable _goalieDiveHighHoldAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1HighDiveHoldRightFrames, 0, 0, 0, _goalie1HighDiveHoldLeftFrames,
        0, 0, 0, _goalie2HighDiveHoldRightFrames, 0, 0, 0,
        _goalie2HighDiveHoldLeftFrames, 0 } };
AnimTable _rightGoalieJumpingHighAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1HighDiveReachRightFaceDownFrames, 0, 0, 0,
        _goalie1HighDiveReachLeftFaceDownFrames, 0, 0, 0,
        _goalie2HighDiveReachRightFaceDownFrames, 0, 0, 0,
        _goalie2HighDiveReachLeftFaceDownFrames, 0 } };
AnimTable _leftGoalieJumpingLowAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1LowDiveRightFaceUpFrames, 0, 0, 0,
        _goalie1LowDiveLeftFaceUpFrames, 0, 0, 0,
        _goalie2LowDiveRightFaceUpFrames, 0, 0, 0,
        _goalie2LowDiveLeftFaceUpFrames, 0 } };
AnimTable _rightGoalieJumpingLowAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1LowDiveRightFaceDownFrames, 0, 0, 0,
        _goalie1LowDiveLeftFaceDownFrames, 0, 0, 0,
        _goalie2LowDiveRightFaceDownFrames, 0, 0, 0,
        _goalie2LowDiveLeftFaceDownFrames, 0 } };
AnimTable _goalieDiveLandAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1DiveLandFacingUpFrames, _goalie1DiveLandFacingUpFrames,
        _goalie1DiveLandFacingUpFrames, _goalie1DiveLandFacingDownFrames,
        _goalie1DiveLandFacingDownFrames, _goalie1DiveLandFacingDownFrames,
        _goalie1DiveLandFacingDownFrames, _goalie1DiveLandFacingUpFrames,
        _goalie2DiveLandFacingUpFrames, _goalie2DiveLandFacingUpFrames,
        _goalie2DiveLandFacingUpFrames, _goalie2DiveLandFacingDownFrames,
        _goalie2DiveLandFacingDownFrames, _goalie2DiveLandFacingDownFrames,
        _goalie2DiveLandFacingDownFrames, _goalie2DiveLandFacingUpFrames } };
AnimTable _goalieDeflectBallAnimTable = { 5,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        _goalie1DeflectBallFacingUpFrames, _goalie1DeflectBallFacingUpFrames,
        _goalie1DeflectBallFacingUpFrames, _goalie1DeflectBallFacingDownFrames,
        _goalie1DeflectBallFacingDownFrames, _goalie1DeflectBallFacingDownFrames,
        _goalie1DeflectBallFacingDownFrames, _goalie1DeflectBallFacingUpFrames,
        _goalie2DeflectBallFacingUpFrames, _goalie2DeflectBallFacingUpFrames,
        _goalie2DeflectBallFacingUpFrames, _goalie2DeflectBallFacingDownFrames,
        _goalie2DeflectBallFacingDownFrames, _goalie2DeflectBallFacingDownFrames,
        _goalie2DeflectBallFacingDownFrames,
        _goalie2DeflectBallFacingUpFrames } };
AnimTable _plGettingYellowCardAnimTable = { 5,
    { _team1PlGettingYellowCardFrames, _team1PlGettingYellowCardFrames,
        _team1PlGettingYellowCardFrames, _team1PlGettingYellowCardFrames,
        _team1PlGettingYellowCardFrames, _team1PlGettingYellowCardFrames,
        _team1PlGettingYellowCardFrames, _team1PlGettingYellowCardFrames,
        _team2PlGettingYellowCardFrames, _team2PlGettingYellowCardFrames,
        _team2PlGettingYellowCardFrames, _team2PlGettingYellowCardFrames,
        _team2PlGettingYellowCardFrames, _team2PlGettingYellowCardFrames,
        _team2PlGettingYellowCardFrames, _team2PlGettingYellowCardFrames, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _plGettingRedCardAnimTable = { 5,
    { _team1PlayerGettingRedCardFrames, _team1PlayerGettingRedCardFrames,
        _team1PlayerGettingRedCardFrames, _team1PlayerGettingRedCardFrames,
        _team1PlayerGettingRedCardFrames, _team1PlayerGettingRedCardFrames,
        _team1PlayerGettingRedCardFrames, _team1PlayerGettingRedCardFrames,
        _team2PlayerGettingRedCardFrames, _team2PlayerGettingRedCardFrames,
        _team2PlayerGettingRedCardFrames, _team2PlayerGettingRedCardFrames,
        _team2PlayerGettingRedCardFrames, _team2PlayerGettingRedCardFrames,
        _team2PlayerGettingRedCardFrames, _team2PlayerGettingRedCardFrames, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _plGetting2ndYellowCardAnimTable = { 5,
    { _team1PlGetting2ndYellowCardFrames, _team1PlGetting2ndYellowCardFrames,
        _team1PlGetting2ndYellowCardFrames, _team1PlGetting2ndYellowCardFrames,
        _team1PlGetting2ndYellowCardFrames, _team1PlGetting2ndYellowCardFrames,
        _team1PlGetting2ndYellowCardFrames, _team1PlGetting2ndYellowCardFrames,
        _team2PlGetting2ndYellowCardFrames, _team2PlGetting2ndYellowCardFrames,
        _team2PlGetting2ndYellowCardFrames, _team2PlGetting2ndYellowCardFrames,
        _team2PlGetting2ndYellowCardFrames, _team2PlGetting2ndYellowCardFrames,
        _team2PlGetting2ndYellowCardFrames, _team2PlGetting2ndYellowCardFrames, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _plInjuredAnimTable = { 5,
    { _team1InjuredPlayerTopLeftTable, _team1InjuredPlayerTopLeftTable,
        _team1InjuredPlayerTopLeftTable, _team1InjuredPlayerTopLeftTable,
        _team1InjuredPlayerTopRightTable, _team1InjuredPlayerTopRightTable,
        _team1InjuredPlayerTopRightTable, _team1InjuredPlayerTopRightTable,
        _team2InjuredPlayerTopLeftTable, _team2InjuredPlayerTopLeftTable,
        _team2InjuredPlayerTopLeftTable, _team2InjuredPlayerTopLeftTable,
        _team2InjuredPlayerTopRightTable, _team2InjuredPlayerTopRightTable,
        _team2InjuredPlayerTopRightTable, _team2InjuredPlayerTopRightTable, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _playerLosingReactionAnimTable = { 5,
    { _team1LosingReactionFrames, _team1LosingReactionFrames,
        _team1LosingReactionFrames, _team1LosingReactionFrames,
        _team1LosingReactionFrames, _team1LosingReactionFrames,
        _team1LosingReactionFrames, _team1LosingReactionFrames,
        _team2LosingReactionFrames, _team2LosingReactionFrames,
        _team2LosingReactionFrames, _team2LosingReactionFrames,
        _team2LosingReactionFrames, _team2LosingReactionFrames,
        _team2LosingReactionFrames, _team2LosingReactionFrames, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable _playerWinningReactionAnimTable = { 5,
    { _team1WinningReactionFrames, _team1WinningReactionFrames,
        _team1WinningReactionFrames, _team1WinningReactionFrames,
        _team1WinningReactionFrames, _team1WinningReactionFrames,
        _team1WinningReactionFrames, _team1WinningReactionFrames,
        _team2WinningReactionFrames, _team2WinningReactionFrames,
        _team2WinningReactionFrames, _team2WinningReactionFrames,
        _team2WinningReactionFrames, _team2WinningReactionFrames,
        _team2WinningReactionFrames, _team2WinningReactionFrames, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
AnimTable_8 _refComingAnimTable = { 5,
    { _refComingUpFrames, _refComingUpFrames, _refComingUpFrames,
        _refComingDownFrames, _refComingDownFrames, _refComingDownFrames,
        _refComingDownFrames, _refComingUpFrames } };
AnimTable_12 _refWaitingAnimTable = { 5,
    { _refStandingFacingSideFrames, _refStandingFacingSideFrames,
        _refStandingFacingSideFrames, _refStandingFacingSideFrames,
        _refStandingFacingSideFrames, _refStandingFacingSideFrames,
        _refStandingFacingSideFrames, _refStandingFacingSideFrames,
        _refStandingFacingDownFrames, _refStandingFacingDownFrames,
        _refStandingFacingSideFrames, _refStandingFacingUpFrames } };
AnimTable_8 _refYellowCardAnimTable = { 5,
    { _refYellowCardFrames, _refYellowCardFrames, _refYellowCardFrames,
        _refYellowCardFrames, _refYellowCardFrames, _refYellowCardFrames,
        _refYellowCardFrames, _refYellowCardFrames } };
AnimTable_8 _refRedCardAnimTable = { 5,
    { _refRedCardFrames, _refRedCardFrames, _refRedCardFrames, _refRedCardFrames,
        _refRedCardFrames, _refRedCardFrames, _refRedCardFrames,
        _refRedCardFrames } };
AnimTable_8 _refSecondYellowAnimTable = { 5,
    { _refSecondYellowFrames, _refSecondYellowFrames, _refSecondYellowFrames,
        _refSecondYellowFrames, _refSecondYellowFrames, _refSecondYellowFrames,
        _refSecondYellowFrames, _refSecondYellowFrames } };
Sprite _big_S_Sprite = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0,
    { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 },
    1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _cornerFlagSprite
    = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
/* Widescreen-only second corner-flag sprite. The full pitch width shows both side corners
 * at once, but cornerFlagSprite can only track one; UpdateCornerFlagSprite drives this one
 * at the opposite corner. Hidden (imageIndex = -1) outside widescreen.
 *
 * Defined on the m68k target as well, though nothing there ever draws it: it is a region of
 * the shared match-state registry (src/sim_state.c), and a registry that differs by one
 * region gives the two builds different SimState_LayoutHash values -- which would mean a
 * save state written on one target is refused by the other (docs/CHALLENGE.md). The same
 * goes for the three grid extras below. */
Sprite _cornerFlagSprite2
    = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _advertInGameSprites[8] = {
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
        { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1, 0,
        0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 }
};
Sprite _refereeSprite
    = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _currentTimeSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _resultOnHalftimeSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 212,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _resultAfterTheGameSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 213,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridUpperRowLeftSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridUpperRowRightSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridMiddleRowLeftSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridMiddleRowRightSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridBottomRowLeftSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridBottomRowRightSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
/* Widescreen: the score panel is a 168-wide dither tile (sprite 138) repeated
 * across the row. Two tiles cover the old 336 box; a 16:9 HUD box is 480 wide,
 * so a THIRD tile per row is needed to reach the right edge (3 x 168 = 504,
 * clipped to HUD_WIDTH). Only widescreen shows these -- the Amiga is always 336 -- but
 * they are DEFINED on both targets, for the registry reason given at _cornerFlagSprite2. */
Sprite _gridUpperRowExtraSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridMiddleRowExtraSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _gridBottomRowExtraSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 138,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1NameSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2NameSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1GoalsDigit2Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1GoalsDigit1Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2GoalsDigit2Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2GoalsDigit1Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1TotalGoalsDigit2Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1TotalGoalsDigit1Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2TotalGoalsDigit2Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2TotalGoalsDigit1Sprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _dashSprite = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0,
    { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 },
    1, 195, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
/* Scorer sprites must be contiguous arrays — game code iterates them with pointer++ */
Sprite _team1ScorerSprite[8] = {
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 196,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 197,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 198,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 199,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 200,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 201,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 202,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 203,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
};
/* Scorer sprites must be contiguous arrays — game code iterates them with pointer++ */
Sprite _team2ScorerSprite[8] = {
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 204,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 205,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 206,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 207,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 208,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 209,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 210,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, 211,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 },
};
Sprite _goal1TopSprite = { 3, 0, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1, 5, 1,
    0, { 19660800 }, { 8454144 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 300, 129,
    { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, 1091, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0 };
Sprite _goal2BottomSprite = { 3, 0, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1, 5,
    1, 0, { 19660800 }, { 50987008 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 300,
    778, { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, 1092, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _ballSprite = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0,
    { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 },
    1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _ballShadowSprite
    = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _playerMarkSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _currentPlayerNameSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1CurPlayerNumSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2CurPlayerNumSprite
    = { 0, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _bookedPlayerCardOrNumberSprite
    = { 3, 0, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _goalie1Sprite
    = { 1, 1, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player2Sprite
    = { 1, 2, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player3Sprite
    = { 1, 3, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player4Sprite
    = { 1, 4, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player5Sprite
    = { 1, 5, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player6Sprite
    = { 1, 6, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player7Sprite
    = { 1, 7, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player8Sprite
    = { 1, 8, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player9Sprite
    = { 1, 9, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player10Sprite = { 1, 10, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1,
    5, 1, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0,
    { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0 };
Sprite _team1Player11Sprite = { 1, 11, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1,
    5, 1, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0,
    { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0 };
Sprite _goalie2Sprite
    = { 2, 1, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player2Sprite
    = { 2, 2, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player3Sprite
    = { 2, 3, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player4Sprite
    = { 2, 4, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player5Sprite
    = { 2, 5, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player6Sprite
    = { 2, 6, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player7Sprite
    = { 2, 7, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player8Sprite
    = { 2, 8, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player9Sprite
    = { 2, 9, 0, (AnimTable *)-1, -1, 255, 0, 0, 0, 0, -1, 5, 1, 0, { 0 }, { 0 },
          { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0, 0, 0, 0, 0, 1 }, 1, -1,
          0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player10Sprite = { 2, 10, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1,
    5, 1, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0,
    { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0 };
Sprite _team2Player11Sprite = { 2, 11, 0, (AnimTable *)-1, -1, 0xFF, 0, 0, 0, 0, -1,
    5, 1, 0, { 0 }, { 0 }, { 0 }, 0, 0, { 0 }, { 0 }, { 0 }, 0, 0,
    { 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 }, 1, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0 };
/* teamSpritesTable: 101 sprite pointers covering team1SpritesTable[11],
 * team2SpritesTable[11], and allSpritesArray[79] in that exact order.
 *
 * On Amiga BSS these were three adjacent `dc.l` blocks (SWOS2.s line
 * 42863 / 42877 / 42891). Bench/subs draw code reads
 * `spritesTable[d7]` for d7 = 11..15 (the substitute about to enter
 * the game) which is OOB for the 11-entry team table — Amiga code
 * relied on the next BSS table being right after. We mirror that
 * adjacency by overlaying all three names onto one initialized array:
 *
 *   team1SpritesTable = teamSpritesTable + 0     [11 entries]
 *   team2SpritesTable = teamSpritesTable + 44    [11 entries]
 *   allSpritesArray   = teamSpritesTable + 88    [79 entries]
 *
 * With this layout:
 *   team1SpritesTable[11..21] reads team2SpritesTable[0..10]
 *   team2SpritesTable[11..15] reads allSpritesArray[0..4]
 *
 * exactly as the Amiga did. `spr->injuryLevel == 0xFFFE` checks on
 * those OOB slots return false (the sprites pointed at aren't
 * "permanently injured"), so no UI affordance changes — just no
 * SIGSEGV.
 *
 * The `.equ` aliases must live in the same TU as the array definition
 * or GAS won't emit a global symbol for them (same gotcha as
 * careerFileBufferBuf — see swos_globals_career_overlay.c). */
/* UNSIZED ON PURPOSE. The final element is the (Sprite *)-1 sentinel that
 * InitDisplaySprites walks to; with an explicit size, adding a sprite silently
 * DROPS the trailing initializers — GCC only WARNS ("excess elements in array
 * initializer") — and losing the sentinel makes InitDisplaySprites run off the
 * end of the array and SIGSEGV at match start. That is exactly what happened
 * when the three widescreen grid tiles were added to a [102] declaration.
 * Letting the initializer size the array makes that failure impossible; the
 * assert below still enforces the minimum the overlapping views need. */
Sprite* _teamSpritesTable[] = {
    /* [0..10]  — team1SpritesTable */
    &_goalie1Sprite, &_team1Player2Sprite, &_team1Player3Sprite,
    &_team1Player4Sprite, &_team1Player5Sprite, &_team1Player6Sprite,
    &_team1Player7Sprite, &_team1Player8Sprite, &_team1Player9Sprite,
    &_team1Player10Sprite, &_team1Player11Sprite,
    /* [11..21] — team2SpritesTable */
    &_goalie2Sprite, &_team2Player2Sprite, &_team2Player3Sprite,
    &_team2Player4Sprite, &_team2Player5Sprite, &_team2Player6Sprite,
    &_team2Player7Sprite, &_team2Player8Sprite, &_team2Player9Sprite,
    &_team2Player10Sprite, &_team2Player11Sprite,
    /* [22..100] — allSpritesArray */
    &_ballShadowSprite, &_ballSprite, &_goal1TopSprite,
    &_goal2BottomSprite, &_goalie1Sprite, &_team1Player2Sprite,
    &_team1Player3Sprite, &_team1Player4Sprite, &_team1Player5Sprite,
    &_team1Player6Sprite, &_team1Player7Sprite, &_team1Player8Sprite,
    &_team1Player9Sprite, &_team1Player10Sprite, &_team1Player11Sprite,
    &_goalie2Sprite, &_team2Player2Sprite, &_team2Player3Sprite,
    &_team2Player4Sprite, &_team2Player5Sprite, &_team2Player6Sprite,
    &_team2Player7Sprite, &_team2Player8Sprite, &_team2Player9Sprite,
    &_team2Player10Sprite, &_team2Player11Sprite, &_team1CurPlayerNumSprite,
    &_team2CurPlayerNumSprite, &_playerMarkSprite,
    &_bookedPlayerCardOrNumberSprite, &_currentTimeSprite,
    &_resultOnHalftimeSprite, &_resultAfterTheGameSprite, &_gridUpperRowLeftSprite,
    &_gridUpperRowRightSprite, &_gridMiddleRowLeftSprite,
    &_gridMiddleRowRightSprite, &_gridBottomRowLeftSprite,
    &_gridBottomRowRightSprite, &_team1NameSprite, &_team2NameSprite, &_dashSprite,
    &_team1GoalsDigit1Sprite, &_team2GoalsDigit1Sprite, &_team1GoalsDigit2Sprite,
    &_team2GoalsDigit2Sprite, &_team1TotalGoalsDigit1Sprite,
    &_team2TotalGoalsDigit1Sprite, &_team1TotalGoalsDigit2Sprite,
    &_team2TotalGoalsDigit2Sprite,
    &_team1ScorerSprite[0], &_team2ScorerSprite[0],
    &_team1ScorerSprite[1], &_team2ScorerSprite[1],
    &_team1ScorerSprite[2], &_team2ScorerSprite[2],
    &_team1ScorerSprite[3], &_team2ScorerSprite[3],
    &_team1ScorerSprite[4], &_team2ScorerSprite[4],
    &_team1ScorerSprite[5], &_team2ScorerSprite[5],
    &_team1ScorerSprite[6], &_team2ScorerSprite[6],
    &_team1ScorerSprite[7], &_team2ScorerSprite[7],
    &_refereeSprite, &_cornerFlagSprite,
#ifdef SWOS_SDL_BUILD
    &_cornerFlagSprite2,
    &_gridUpperRowExtraSprite, &_gridMiddleRowExtraSprite,
    &_gridBottomRowExtraSprite,
#endif
    &_big_S_Sprite, &_currentPlayerNameSprite, _advertInGameSprites,
    &_advertInGameSprites[0x1], &_advertInGameSprites[0x2],
    &_advertInGameSprites[0x3], &_advertInGameSprites[0x4],
    &_advertInGameSprites[0x5], &_advertInGameSprites[0x6],
    &_advertInGameSprites[0x7], (Sprite*)-1,
};
/* UNTANGLED (.equ -> real pointer globals): team1/team2/allSprites are
 * views into the one contiguous teamSpritesTable[101] — team2 at slot 11,
 * allSprites at slot 22 (the old aliases used 44/88 B = 11/22 * sizeof(void*),
 * now expressed as plain array indices so they're 64-bit-correct without the
 * UINTPTR_MAX literal-offset macros). They are real `Sprite **` pointers, not
 * .equ symbols. The intentional OOB cross-reads still resolve because all
 * three index the same flat array: team1[11..21] = team2[0..10],
 * team2[11..15] = allSprites[0..4]. Consumers use `tableName[i]` and
 * pointer-decay assignment unchanged; only their extern decls become
 * `Sprite **` (matching these definitions — an array-form extern over a
 * pointer object would misread, so all are updated). */
_Static_assert(sizeof(_teamSpritesTable) / sizeof(*_teamSpritesTable) >= 101,
               "teamSpritesTable needs >= 101 slots: team1/team2/allSprites "
               "overlay it at 0/11/22 and the bench/subs code deliberately "
               "cross-reads past team1's and team2's own 11 entries");

Sprite **_team1SpritesTable = &_teamSpritesTable[0];
Sprite **_team2SpritesTable = &_teamSpritesTable[11];
Sprite **_allSpritesArray   = &_teamSpritesTable[22];
