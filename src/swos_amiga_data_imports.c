/*
 * src/swos_amiga_data_imports.c — auto-generated from SWOS2.s
 * Real numeric tables for symbols that were previously zero-stubbed.
 *
 * Only definitions whose SWOS2.s body is uniform dc.w / dc.l with literal
 * numeric operands are imported here. Pointer tables and mixed-size data
 * still live as stubs in swos_amiga_data_stubs.c (or as hand-written
 * definitions in swos_globals.c).
 */
#include "swos_types.h"
#include "game_types.h"
#include "menu_system.h"
/* display.h is the single source of `#define AGA`. Without it the #ifdef AGA
 * guards below silently take the OCS #else branch, compiling _gamePalette as
 * [16] instead of [256] — the AGA renderer then reads 240 colours off the end
 * of the array (into _playerFacesTable / the ball-delta tables), which shows as
 * garbage/magenta palette entries on the pitch and scoreboard. */
#include "display.h"
uint16_t _selFilesColumnStartY[1] = { 0x0000 };
uint16_t _hilRightTeamGoals[1] = { 0x0000 };
uint16_t _bottomTeamCoachNo[1] = { 0x0000 };
uint16_t _kInjuryRecoveryTime[8] = { 0,60,70,80,90,100,110,130 };
uint16_t _selectedOldJobOfferOrdinal[1] = { 0x0000 };
TeamGeneralInfo *_lastTeamPlayedBeforeBreak = (TeamGeneralInfo *)0;
uint16_t _cameraXSaved0[1] = { 0x0000 };
uint16_t _numNationalTeams[1] = { 0x003F };
uint16_t _cameraXSaved1[1] = { 0x0000 };
uint16_t _lastFrameTicks[1] = { 0x0000 };
uint16_t _pl1SkipFireCounter[1] = { 0x0000               };
uint16_t _aiRandomRotateTable[2] = { -32,32 };
uint16_t _team1PenaltyGoals[1] = { 0x0000 };
uint16_t _kNormalKickBallSpeed[1] = { 0x0A00               };
uint16_t _bigNumbersColorConversionTable[87] = { 0,1,2,3,4,5,6,7    , 8,9,0,0,12,13,14,15, 0,1,2,3,4,5,6,7, 8,9,0,11,12,13,14,15, 0,1,2,3,4,5,6,7, 8,9,0,11,12,13,14,15, 6,6,5,5,4,4,13,3, 9,3,12,3,-1,6,6,5, 5,4,4,13,6,9,5,12, 4,-1,6,4,5,4,4,3, 13,3,9,3,12,3,-1 };
uint16_t _replaySelected[1] = { 0x0000 };
uintptr_t _exchangeFmtLine1a[1] = { 0 };
uint32_t _kHeaderLowJumpHeight[1] = { 0x00020000           };
uintptr_t _exchangeFmtLine1b[1] = { 0 };
uint16_t _controlMask[1] = { 0x0000               };
uint16_t _diyCupPitchTypeOrSeasonSelection[1] = { 0x0000 };
uint16_t _kJumpHeaderSpeed[12] = { 0x0800,0x0600,0x0600,0x0700,0x0800,0x0700,0x0000,0x2000 , 0x0030,0x0001,0x0000,0x0800 };
uint16_t _pl2FireCounter[1] = { 0x0000 };
/* Pointer-storage globals — retyped from uint32_t to uintptr_t so the
 * upper 32 bits of 64-bit pointers aren't truncated at the writer's
 * (uint32_t)<ptr> cast. See [[project_uint32_pointer_storage_trap]]. */
uintptr_t _benchTeamGame[1] = { 0 };
uint16_t _playerMarketFilterAgeGroup[1] = { 0x0000 };
uint16_t _selectedPlayerTacticsPosition[1] = { 0x0000 };
uint16_t _kGoalkeeperMediumDeflectBallSpeed[1] = { 0x0400 };
uintptr_t _offerFmtSinglePlayer[1] = { 0 };
uint16_t _forceLeftTeam[1] = { 0x0002 };
uint32_t _jobOfferFunds[1] = { 0x00000000           };
uint16_t _cameraCoordinatesValid[1] = { 0x0001 };
uint16_t _subsFireTimer[1] = { 0x0000               };
uint16_t _team1GoalkeeperReplaced[1] = { 0x0000 };
uint16_t _editTacticsLastBallLocation[1] = { 0x0000               };
uint16_t _titleTextPalette[16] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _bench1Called[1] = { 0x0000               };
/* 11 rows: purple (10, the Mod's purple.py) clashes with purple and maroon, maroon with purple */
uint16_t _conflictingBasicTeamColorsTable[11] = { 0x01A1,0x0002,0x0164,0x0218,0x0058,0x00A5,0x0454,0x00A1 , 0x0105,0x0208,
                                                  0x0440 };
uint16_t _buyPlayerRedrawList[3] = { 5,7,-1              };
uint16_t _gotCountryName[1] = { 0x0000 };
uint16_t _injuriesForever[7] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000  };
uint16_t _team2NumAllowedInjuries[1] = { 0x0000 };
uint16_t _topTeamCoachNo[1] = { 0x0000 };
uint16_t _diyCupMenuArg0Copy[1] = { 0x0000 };
uint16_t _teamsLowerBound[1] = { 0x0000               };
uint16_t _pl2BenchBlockWhileHoldingDirection[1] = { 0x0000 };
uint16_t _currentSpriteIndex[1] = { -1 };
uintptr_t _savedCurrentGoalPtr[1] = { 0 };
uint16_t _useAltBaseFlag[1] = { 0x0000 };
uint16_t _euroCupTitleHolderTeamId[1] = { 0x0000 };
uint16_t _kPenaltyBallDestDelta[16] = { 0,-1000,500,-1000,1000,0,500,1000, 0,1000,-500,1000,-1000,0,-500,-1000 };
uint16_t _diyCupStartMonthIndex[1] = { 0x0000 };
uint16_t _diyInitFocusOverride[1] = { 0x0000               };
uint16_t _cachedTertiaryQualifiedTeamId[1] = { 0x0000 };
uint16_t _gameCanceled[1] = { 0x0000 };
uintptr_t _nextGoalPtr[1] = { 0 };
uint16_t _penaltiesState[1] = { 0x0000 };
uint16_t _cupWinnersCupTitleHolderTeamId[1] = { 0x0000 };
uint16_t _selTeamsListBottomY[1] = { 0x0000 };
uint16_t _showClubTeams[1] = { 0x0000               };
uint16_t _numMatchedFiles[1] = { 0x0000 };
uint32_t _seasonRunningCostsAccum2[1] = { 0x00000000           };
uint16_t _kStaticHeaderBaseBallSpeed[1] = { 0x0700 };
uint32_t _kNormalKickDeltaZ[1] = { 0x00016000           };
uintptr_t _squadTeamFilePtr[1] = { 0 };
uint16_t _cglmVar3[1] = { 0x0000               };
uint16_t _fadeState[1] = { 0x0000 };
uint16_t _saveHighlightScene[1] = { 0x0000               };
uint16_t _playerAreas[35] = { 0,1,2,3,4,5,6,7    , 8,9,10,11,12,13,14,15, 16,17,18,19,20,21,22,23, 24,25,26,27,28,29,30,31, 32,33,34 };
/* stadiumMenuLittlePlayer / littlePlayerSpriteGeneric / littlePlayersColorTable
 * are adjacent on the Amiga (the MENU_CMD_SPRITE_COPY handler reads
 * src=+0, dst=+2, colorTable=+4..+35 from a single pointer). On SDL
 * they live in a shared backing buffer in swos_globals.c with asm
 * symbol aliases so the SPRITE_COPY path sees a contiguous 36-byte
 * block; definition here moved there. */
uint16_t _kBallPlOffsets[16] = { 0,-1,1,-1,1,0,1,1  , 0,1,-1,1,-1,0,-1,-1 };
uintptr_t _assetLoaderCacheFlag[1] = { 0 };
uint16_t _showFansCounter[1] = { 0x0000 };
uint16_t _kBallSpeedDeltaWhenControlled[8] = { 130,116,102,88,74,60,46,32 };
uintptr_t _diyStatsName[1] = { 0 };
uint16_t _cameraYVelocity[1] = { 0x0000               };
uint16_t _team2NumSubs[1] = { 0x0000 };
uint16_t _kSubstitutedPlayerSpeed[1] = { 0x0600               };
uint16_t _playerHadBall[1] = { 0                   };
uintptr_t _continueText[1] = { 0 };
uint16_t _squadChangesAllowed[1] = { 0x0000               };
uint16_t _cameraXVelocity[1] = { 0x0000               };
uint16_t _team1ScorerSpritesWidths[8] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _kDefaultDestinations[16] = { 0,-1000,1000,-1000,1000,0,1000,1000 , 0,1000,-1000,1000,-1000,0,-1000,-1000 };
uint16_t _selectedTeamsStack[10] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 , 0x0000,0x0000 };
uint16_t _diySelected[1] = { 0x0000               };
uint16_t _crowdChantsOn[1] = { 1 };
uint16_t _savingFile[1] = { 0x0000               };
uint16_t _kPassingSpinFactor[32] = { -16,0,16,0,0,-11,11,0 , 0,-16,0,16,11,0,0,11, 16,0,-16,0,0,11,-11,0, 0,16,0,-16,-11,0,0,-11 };
uint16_t _subsPrevPl1Direction[1] = { -1 };
uint16_t _diyPitchTypeOrSeason_1[1] = { 0x0000 };
uint16_t _chooseTeamsFriendly_leftListNumEntries[1] = { 0x0000 };
uint16_t _diyTournamentSelectTeams[1] = { 0x0000 };
uint16_t _seasonMatchReceiptsCount[1] = { 0x0000               };
uint16_t _diyCompetitionType[1] = { 0x0000 };
uint16_t _playMatchButtonVisibilityState[2] = { 0x0000,0x0000 };
uint32_t _careerIntlPlayersWrongTypeMask[1] = { 0x00000000           };
uint16_t _viewSquadSelectedTeam[1] = { 0x0000 };
uint16_t _diyCupMatchListScrollIndex[1] = { 0x0000 };
uint32_t _seasonWagesAccum[1] = { 0x00000000           };
uint16_t _team2PenaltyShooterIndex[1] = { 0x0000 };
uint16_t _diyTournamentEndMonthPage[1] = { 0x0000 };
uint16_t _audioActive[1] = { 0x0000 };
uint16_t _currentSeasonNumber[1] = { 0x0000               };
/* Phase 2 Wave 1: retyped from uint32_t to proper pointer type. */
uint8_t *_diyTeamsBufferPtr = (uint8_t *)0;
uint16_t _gameSeconds[1] = { 0x0000               };
uint32_t _allowShorterMenuItemsWithFrames[1] = { 0x00000000           };
uint16_t _playerCardChance[1] = { 0x0000               };
uint16_t _kLeftThrowInBallDestDelta[16] = { 250,-1000,1000,-1000,1000,0,1000,1000, 250,1000,-1000,1000,-1000,0,-1000,-1000 };
uintptr_t _plPositions[1] = { 0 };
uint16_t _ballInLowerPenaltyArea[1] = { 0x0000 };
uint16_t _kShotAtGoalMinumumSpeed[1] = { 0x0200 };
uint16_t _kGoalkeeperCatchSpeed[1] = { 0x0300               };
uint16_t _bigSIndex[1] = { 0x0001               };
uint16_t _goalScored[1] = { 0x0000               };
uint16_t _cameraMoveToShowerLatch[1] = { -1 };
uint16_t _teamStarting[1] = { 0x0001               };
uint16_t _selTeamsListTopY[1] = { 0x0000 };
uint16_t _pitchBallSpeedFactor[1] = { 0x0000               };
uint16_t _dstYStartLinesSaved0[1] = { 0x0000 };
uint8_t *_advert3Pointers[8] = { 0 };
uint16_t _dstYStartLinesSaved1[1] = { 0x0000 };
uint16_t _team1NameLength[1] = { 0x0000 };
uint16_t _plgD0Param[1] = { 0x0000 };
TeamFileEx *_playMatchTeam2Ptr = (TeamFileEx *)0;
uint8_t *_advert2Pointers[8] = { 0 };
uint16_t _ballRow[1] = { 0x0000               };
uint16_t _exitFlag[1] = { 0x0000               };
uint16_t _kKeeperPenaltySaveDistanceNear[17] = { 12,2048,176,168,152,128,96,56, 16,0,2560,512,448,384,320,2560, 2560 };
uint16_t _team1PenaltyAttempts[1] = { 0x0000               };
uint16_t _gamePitchType[1] = { 0x0000 };
uint8_t *_advert1Pointers[8] = { 0 };
uint16_t _squadRowMode[1] = { 0x0000 };
uint16_t _saveCustomTeams[1] = { 0x0000               };
uint16_t _longFireFlag[1] = { 0x0000 };
uint16_t _kGoalKeeperRecoveryStepAlt[1] = { 0x00C0               };
uint32_t _viewSquadMaskHi[1] = { 0x00000000           };
uint16_t _benchColors[9] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 , 0x0000 };
uint16_t _currentMessage[1] = { 0x0000               };
uint32_t _viewSquadMaskLo[1] = { 0x00000000           };
uint16_t _diyCupEndMonthIndex[1] = { 0x0000 };
uint16_t _careerGameListTeamControl[1] = { 0x0000               };
uint16_t _abortSelectTeams[1] = { 0x0000               };
uint16_t _cachedViewResultTeam2Goals[1] = { 0x0000 };
uint16_t _kPlayerHeaderSpeedIncrease[8] = { -336,-288,-240,-192,-144,-96,-48,0  };
uint16_t _ballNotHighX[1] = { 0x0000 };
uint16_t _ballNotHighY[1] = { 0x0000 };
uint16_t _ballYQuadrantDead[1] = { 0x0000 };
uint16_t _oldCameraXSaved0[1] = { 0x0000 };
uint16_t _offerPrimaryPositionMask[1] = { 0x0000 };
uint16_t _teamWillingToSellOrExchange[1] = { 0x0000               };
uint16_t _ballNotHighZ[1] = { 0x0000               };
uint16_t _kUpperLeftCornerBallDestDelta[16] = { 0,-1000,1000,-1000,1000,150,1000,300, 250,1000,-1000,1000,-1000,0,-1000,-1000 };
uint16_t _oldCameraXSaved1[1] = { 0x0000 };
uint16_t _diyTournamentStartMonthPage[1] = { 0x0000 };
uint16_t _teamIsImported[1] = { 0x0000               };
uint16_t _gamePitchTypeOrSeason[1] = { 0x0000 };
uint16_t _lastCameraDirection[1] = { 0x0000 };
MenuEntry *_editTacticsSelectedPlayerMenuEntryPtr = (MenuEntry *)0;
uint16_t _kPassingSpeed_2500_10000[1] = { 1664                };
uint16_t _ballXQuadrantCoordinates[5] = { 12,38,64,90,116 };
uint16_t _sfxQueueCount[1] = { 0x0000 };
uint16_t _kGoalkeeperFarJumpSlowerSpeed[1] = { 0x0500               };
uint16_t _leagueNumber[1] = { 0x0000 };
uint32_t _kGoalkeeperDeflectDeltaZ[1] = { 0x0000C000           };
uint32_t _kHighKickDeltaZ[1] = { 0x00020000           };
uint16_t _team1MostFrequentFace[1] = { 0x0000 };
uint16_t _isContestContinentalCup[1] = { 0x0000 };
TeamGeneralInfo *_lastTeamBooked = (TeamGeneralInfo *)0;
uint16_t _nat_teams_rand_limit1[1] = { 0x0029 };
uintptr_t _careerGameTopTeamName[1] = { 0 };
uint16_t _patternStatic[1] = { 0x0000 };
uint16_t _diyTournamentLookOnly[1] = { 0x0000 };
uint16_t _team1StatsData[7] = { 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000               };
uint16_t _kPlayerSpeedsGameInProgress[8] = { 928,974,1020,1066,1112,1158,1204,1250  };
uint16_t _subsBlockFire[1] = { 0x0000               };
uint16_t _offerExtraPositionMask[1] = { 0x0000 };
uint16_t _diyCupLookOnly[1] = { 0x0000 };
uint16_t _statsTeam1GoalsCopy2[1] = { 0x0000 };
uint16_t _playerMarketVisibleItemCount[1] = { 0x0000 };
uint16_t _gameListY[1] = { 0x0000 };
uint16_t _ballYQuadrantLimits[6] = { 0x00DC,0x0138,0x0193,0x01EF,0x024A,0x02A6 };
uint16_t _kRightThrowInBallDestDelta[16] = { -250,-1000,1000,-1000,1000,0,1000,1000, -250,1000,-1000,1000,-1000,0,-1000,-1000 };
uint32_t _seasonGateReceiptsAccum[1] = { 0x00000000           };
uint32_t _kGravityConstant[1] = { 0x00001200           };
uint16_t _cachedExtraTimeTeam1Goals[1] = { -1 };
uint16_t _transferCompleted[1] = { 0x0000               };
uint16_t _diyTournamentViewOnly[1] = { 0x0000               };
uint16_t _playing2ndGame[1] = { 0x0000 };
uint16_t _goToSubsTimer[1] = { 0x0000               };
uint16_t _totalSkillsFactorSum[1] = { 0x0000 };
uint16_t _deadVar03[1] = { 0x0000 };
uint16_t _foulYCoordinate[1] = { 0x0000 };
uint16_t _kUpperRightCornerBallDestDelta[16] = { 0,-1000,1000,-1000,1000,0,1000,1000, -250,1000,-1000,350,-1000,150,-1000,-1000 };
uint16_t _team1GoalsDigit1[1] = { 0x0000 };
TeamFileEx *_playMatchTeam1Ptr = (TeamFileEx *)0;
uint16_t _team1GoalsDigit2[1] = { 0x0000 };
uint16_t _stayInSelectTeams[1] = { 0x0000 };
uint16_t _hilStarted[1] = { 0x0000 };
uint16_t _playingPenalties[1] = { 0x0000               };
uint16_t _kPassingSpeed_40000_62500[1] = { 1962                };
uint16_t _currentKey[1] = { 0x0000 };
uint16_t _playMatchControlFlags[1] = { 0x0000               };
uint16_t _diyFoundTeamIndex[1] = { 0x0000 };
uint16_t _offerSwapValueBaseSum[1] = { 0x0000 };
uint16_t _subsTeamsSwapped[1] = { 0x0000 };
uint16_t _endOfHalfWriteOnly[1] = { 0x0000 };
uint16_t _topTeamPlayerNo[1] = { 0x0000 };
uint16_t _diyUnknownWord_2C[1] = { 0x0000 };
uint16_t _pitchNumberAndType[1] = { 0x0000 };
uint16_t _ballBounceFactor[1] = { 0x0000               };
uint16_t _playerYQuadrantOffset[1] = { 0x0000 };
uint16_t _allowExtraCharsFlag[1] = { 0x0000               };
uint16_t _viewSelectedTeams[1] = { 0x0000               };
uintptr_t _bottomTeamPtr[1] = { 0 };
uint16_t _team2Computer[1] = { 0x0000 };
uint16_t _squadDisplayCount[1] = { 0x0000               };
uint16_t _plSubstitutedX[1] = { 0x0000 };
uint16_t _plSubstitutedY[1] = { 0x0000 };
uint16_t _team1Computer[1] = { 0x0000 };
uintptr_t _gameTypeStr[1] = { 0 };
uint16_t _diyCupMenuArg0[1] = { 0x0000 };
uint16_t _diyCupMenuArg1[1] = { 0x0000 };
uint16_t _diyCupMenuArg2[1] = { 0x0000 };
uintptr_t _exchangeFmtLine2a[1] = { 0 };
uint16_t _deadVarAlways0[1] = { 0x0000 };
uint16_t _squadSelectedPlayerIndex[1] = { 0x0000 };
uint16_t _playerSelected[1] = { 0x0000 };
uint16_t _showNationalTeams[1] = { 0x0000 };
uint16_t _menuStatus[1] = { 0x0000               };
uint16_t _cameraYMod320[1] = { 0x0000 };
uint16_t _crowdChantVolume[1] = { 0x0000 };
uint16_t _gameState[1] = { 0x0000               };
uint16_t _diyFixtureViewMode[1] = { 0x0000               };
uint16_t _offerNegotiationStep2Amount[1] = { 0x0000 };
uint16_t _exitGameFlag[1] = { 0x0000               };
uint16_t _goalkeeperDiveBallHeights[28] = { 7,13,11,8,5,2,20,2 , 5,8,11,13,7,20,5,11, 9,6,3,0,20,0,3,6, 9,11,5,20 };
uint16_t _diyTournamentUseSeasonalPitchEffective[1] = { 0x0000 };
uintptr_t _euroCupTeamListPtr[1] = { 0 };
uint16_t _assetLoaderAltPathReady[1] = { 0x0000 };
uint16_t _diyLeftTeamResult[1] = { 0x0000 };
uint16_t _editTacticsSelectedPlayerNameIndex[1] = { 0x0000 };
uint16_t _menuFade[1] = { 0x0000               };
uint16_t _careerNationalTeamHasPendingAddPlayer[1] = { 0x0000 };
uint16_t _cameraDirection[1] = { 0x0000               };
uint16_t _gameRandValue[1] = { 0x0000 };
uint16_t _playerXQuadrantOffset[1] = { 0x0000               };
uint16_t _isCountryNumber[1] = { 0x0000 };
uintptr_t _curCountryStr[1] = { 0 };
uint16_t _menuTextBlinkCounter[1] = { 0x0000               };
uintptr_t _currentScorer[1] = { 0 };
uint16_t _goalCounter[1] = { 0x0000               };
uint16_t _teamsType[1] = { 0x0000               };
uint16_t _kKeeperSaveDistance[1] = { 24                  };
uint16_t _kStaticHeaderPlayerSpeed[1] = { 0x0100               };
uint16_t _playRefereeWhistle[1] = { 0x0000 };
uint16_t _colorTableReserves[32] = { 0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007 , 0x0008,0x0009,0x000A,0x0000,0x000C,0x000D,0x000E,0x0000, 0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007, 0x0008,0x0009,0x0000,0x000B,0x000C,0x000D,0x000E,0x000F };
uint16_t _teamNoOfferingJob[1] = { 0x0000 };
uint32_t _seasonRunningCostsAccum[1] = { 0x00000000           };
uint16_t _aiCounter[1] = { 0                   };
uintptr_t _savedNextGoalPtr[1] = { 0 };
uint16_t _instantReplayFlag[1] = { 0x0000               };
Sprite *_substitutedPlSprite = (Sprite *)0;
uint16_t _kPassingSpeed_90000_122500[1] = { 2133                };
uint16_t _totalNumberOfFiles[1] = { 0x0000               };
Sprite *_lastPlayerScored = (Sprite *)0;
uint16_t _playerMarketFilterRatingIndex[1] = { 0x0000 };
uint16_t _gameMaxSubstitutes[1] = { 0x0000 };
uint16_t _showStats[1] = { 0x0000               };
uint32_t _kHilFileSig[1] = { 0x0000000C           };
uint16_t _pl1FireCounter[1] = { 0x0000 };
uint16_t _playMatchExcludeFlags[1] = { 0x0000               };
uint16_t _kGoalKeeperRecoveryStepFar[1] = { 0x0080               };
uintptr_t _careerGameBottomTeamName[1] = { 0 };
uint16_t _usePlaneOffsetFlag[8] = { 0,376,429,155,370,24,0,1 };
uint16_t _chooseTeamsFriendly_rightListNumEntries[1] = { 0x0000 };
uint16_t _deadCameraBreakVar01[1] = { 0x0000 };
uint16_t _lastGameTick[1] = { 0x0000 };
uint16_t _editTacticsPlayerX[1] = { 0x0000 };
uint16_t _viewSquadSelectedPlayer[1] = { 0x0000 };
uint16_t _deadCameraBreakVar02[1] = { 0x1234 };
uint16_t _deadCameraBreakVar03[1] = { 0x1234 };
uint16_t _pl2Direction[1] = { 0x0000 };
uint16_t _deadCameraBreakVar04[1] = { 0x0000 };
uint16_t _kGoalkeeperFarJumpSpeed[1] = { 0x0800 };
uint16_t _subsWriteOnly[1] = { 0x0000 };
uint16_t _breakState[1] = { 0x0000               };
uint16_t _playerMarketFilterPosition[1] = { 0x0000 };
uint16_t _subsPrevDirection[1] = { 0x0000               };
uint16_t _playerMarketTotalMatchCount[1] = { 0x0000 };
uint16_t _selFilesColumnEndY[1] = { 0x0000 };
uint16_t _seasonWeeksDeducted[1] = { 0x0000               };
uint16_t _kBallKickingSpeed[1] = { 0x08A0               };
uint16_t _countryNumber[1] = { 0x0000 };
uintptr_t _currentSelectedPlayerIndexPtr[1] = { 0 };
uint16_t _deadInputTextVar[1] = { 0x0000 };
uint16_t _chooseTeamsFriendly_numTeamsInLeagueList[1] = { 0x0000 };
uint16_t _animPatternsIndex[1] = { 0x0000 };
uint16_t _team2PlayersColorTable[16] = { 0x0000,0x0001,0x0002,0x0003,0x0000,0x0000,0x0000,0x0000, 0x0008,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _fansSidesSwapped[1] = { 0x0000 };
uint16_t _team1NumSubs[1] = { 0x0000 };
uint16_t _forceDrawTransparentMenuItems[1] = { 0x0000               };
uintptr_t _plNameForMessage[1] = { 0 };
uint16_t _kBallSpeedKicking[8] = { -384,-270,-162,-54,54,162,270,384 };
uint16_t _playHighlightFlag[1] = { 0x0000               };
uint16_t _nobodysBallLastFrame[1] = { 0x0000 };
uint16_t _deadBenchVar[1] = { 0x0000 };
uint16_t _kPassingSpeed_10000_22500[1] = { 1792                };
uint16_t _saveFileBuffer[1] = { 0x0000               };
void *_leaguesPtrTable[10] = { 0 };
uint16_t _flipOnOff[1] = { 0x0000               };
uint16_t _teamSelectionKeyCache_ListIndex[1] = { 0x0000 };
uint16_t _cameraSnapToPlayerCount[1] = { 0x0000 };
uint16_t _ballXQuadrantLimits[5] = { 0x00B7,0x011D,0x0183,0x01E9,0x0081 };
uint16_t _statsTeam2Goals[1] = { 0x0000 };
uint16_t _normalReplayRunning[1] = { 0x0000               };
uintptr_t _uefaCupTeamListWritePtr[1] = { 0 };
uint16_t _kBallFriction[64] = { 0,-1000,199,-1000,414,-1000,668,-1000 , 1000,-1000,1000,-668,1000,-414,1000,-199, 1000,0,1000,199,1000,414,1000,668, 1000,1000,668,1000,414,1000,199,1000, 0,1000,-199,1000,-414,1000,-668,1000, -1000,1000,-1000,668,-1000,414,-1000,199, -1000,0,-1000,-199,-1000,-414,-1000,-668, -1000,-1000,-668,-1000,-414,-1000,-199,-1000 };
uint16_t _pl1BenchTimeoutCounter[1] = { 0x0000               };
uint32_t _kHeaderHighJumpHeight[1] = { 0x00024000           };
uint16_t _maxSubstitutesEntries[1] = { 0x0000               };
uint16_t _kGoalKeeperRecoveryStepBase[1] = { 0x0070 };
uint16_t _titleLogoPalette[16] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _spinBigS[1] = { 0 };
uint16_t _nextChantTimer[1] = { 0x0000 };
uint16_t _overlayFlagBackup[1] = { 0x0000 };
uintptr_t _savedCurrentHilPtr[1] = { 0 };
uint16_t _savedTeam2Goals[1] = { 0x0000 };
uint16_t _kInjuriesSpeedHandicap[8] = { 0,-96,-128,-160,-192,-224,-256,-288 };
uint16_t _selTeamsUpperBound[1] = { 0x0000 };
uint16_t _competitionTeamIdScratchList4[4] = { 0x0000,0x0000,0x0000,0x0000 };
uint16_t _numPlayersAtQuadrant[1] = { 0x0000 };
uint16_t _copyTimeSprite[1] = { 0x0000               };
uint16_t _cachedPenaltyShootoutTeam2Score[1] = { 0x0000 };
uint16_t _turnDirCopiedFromCameraCount[1] = { 0x0000 };
uint16_t _currentCrowdSample[1] = { 0x0000 };
uintptr_t _editTacticsPlayerNumbers[1] = { 0 };
uint16_t _oldCameraX[1] = { 0x0000               };
uint16_t _teamPlOfs24Table[30] = { 0x0008              , 0x0400              , 0x0070              , 0x0320              , 0x0090              , 0x0007              , 0x0007              , 0x0007              , 0x0003              , 0x0004              , 0x0005              , 0x0006              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0001              , 0x0006              , 0x0009              , 0x0004              , 0x000C              , 0x0001              , 0x0006              , 0x0009              , 0x0004               };
uint16_t _secondLeg[1] = { 0x0000 };
uint16_t _oldCameraY[1] = { 0x0000 };
uint16_t _kBallGroundConstant[1] = { 0x0010               };
uint16_t _ballYQuadrantCoordinates[7] = { 12,40,69,97,126,154,181 };
uint8_t *_editTeamsSelectedTeam = (uint8_t *)0;
uint16_t _leavingSubsMenu[1] = { 0x0000               };
uint16_t _subsMenuHighlightColor[1] = { 0x0000               };
uint16_t _viewTeamsStartY[1] = { 0x0000               };
uint16_t _selectNationalTeamMenuResult[1] = { 0x0000 };
uint16_t _numberOfLeagues[1] = { 0x0000 };
uint16_t _friendlySeason[1] = { 0x0000               };
uint16_t _cachedViewResultTeam1Goals[1] = { -1 };
uint16_t _aiAttackHalf[1] = { 0                   };
uint16_t _oldJobOfferOrdinal[1] = { 0x0000 };
uint16_t _contestLeagueNumber[1] = { 0x0000               };
uint16_t _inGameCounter[1] = { 0x0000               };
uint16_t _crowdEventFlags[1] = { 0x0000 };
uint16_t _aiCounterWriteOnly[1] = { 0 };
uint16_t _diyCupLastMatchScoreTeam1[1] = { 0x0000 };
uint16_t _diyCupLastMatchScoreTeam2[1] = { 0x0000 };
uint16_t _kBallSpeedFinishing[8] = { -288,-160,-32,96,224,352,480,608  };
uint16_t _drawTrainingTopHalfOfTheBench[1] = { 0x0000               };
uint16_t _teamSelected[1] = { 0x0000               };
uint16_t _plgD3Param[1] = { 0x0000 };
uint16_t _showingCpuTeams[1] = { 0x0000               };
TeamFileEx *_chooseTacticsTeamPtr = (TeamFileEx *)0;
uint16_t _team2ScorerSpritesWidths[8] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
MenuEntry *_editTacticsSelectedPairMenuEntryPtr = (MenuEntry *)0;
uint16_t _substitutedPlX[1] = { 0x0000 };
uintptr_t _savedContestRoundPtr[1] = { 0 };
uint16_t _substitutedPlY[1] = { 0x0000 };
uint16_t _editTacticsBallLocation[1] = { 0x0000               };
uint16_t _careerSquadScreenDidSelectPlayerFlag[1] = { 0x0000 };
uint16_t _kPlayerTacklingDownTime[8] = { 30,27,24,21,18,15,12,9 };
uint16_t _gameLengthInGame[1] = { 0x0000 };
uint16_t _hilSceneNumber[1] = { 0x0000               };
uint16_t _viewFutureTeamModeFlag[1] = { 0x0000 };
uint16_t _team1PlayersColorTable[16] = { 0x0000,0x0001,0x0002,0x0003,0x0000,0x0000,0x0000,0x0000, 0x0008,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _gameLenSecondsTable[4] = { 30,18,12,9 };
uint16_t _skipLeagueNumbers[1] = { 0x0000 };
uint16_t _editTacticsSelectedPairSlotIndex[1] = { 0x0000 };
uint16_t _choosingPreset[1] = { 0x0000               };
uintptr_t _currentMenuPtr[1] = { 0 };
uint16_t _team2AppPercent[1] = { 0x0000 };
uint16_t _diyCupMatchListDrawModeFlag[1] = { 0x0000 };
uintptr_t _chooseTeamsHeader1stPart[1] = { 0 };
uint16_t _squadPrevSelectedBreakPos[1] = { 0x0000 };
uint16_t _ballNextYGroundY[1] = { 0x0000 };
uint16_t _plComingX[1] = { 0x0000 };
uint16_t _pitchTypeOrSeason[1] = { 0x0000               };
uint16_t _plComingY[1] = { 0x0000 };
uint16_t _fire[1] = { 0x0000               };
uint16_t _careerNationalTeamAddPlayerType[1] = { 0x0000 };
uint16_t _numSelectedTeamsCopy[1] = { 0x0000 };
uintptr_t _goalBasePtr[1] = { 0 };
uint16_t _editTacticsDisableLittlePlayersAndBallDraw[1] = { 0x0000 };
uint16_t _extraPlayerValueRandom[1] = { 0x0000               };
uint16_t _kGoalKeeperClaimingBallHeight[7] = { 17,17,17,10,8,5,255 };
uint16_t _trainingGame[1] = { 0x0000 };
uint16_t _kGoalkeeperNearJumpSpeed[1] = { 1024 };
uint16_t _kPassingSpeedCloserThan2500[1] = { 1536                };
uint16_t _teamsCountryNumbers[256] = { 0,16,26,44,60,72,86,102, 114,207,207,221,233,245,287,333, 358,379,392,407,424,475,485,497, 513,528,564,580,601,628,654,676, 699,711,751,771,819,838,854,878, 896,918,928,944,984,1035,1049,1073, 1085,1101,1118,1132,1178,1178,1178,1178, 1206,1206,1207,1207,1208,1248,1248,1278, 1278,1290,1306,1316,1328,1388,1404,1405, 1431,1479,1497,1521,1527,1545,1567,1579, 1587,1636,1670,1680,1695,1726,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730, 1730,1730,1730,1730,1730,1730,1730,1730 };
uint16_t _errorPromptsEnabled[1] = { 0x0000               };
uint16_t _chooseCompetitionOnly[1] = { 0x0000               };
uint16_t _plLeavingX[1] = { 0x0000 };
uint16_t _diyNumberOfTeams[1] = { 0x0000 };
uint16_t _diyRightTeamResult[1] = { 0x0000 };
uint16_t _kGoalkeeperGameSpeed[1] = { 0x0400               };
uint16_t _strikeDestX[1] = { 0x0000               };
uint16_t _strikeDestY[1] = { 0x0000 };
uint16_t _spaceReplayTimer[1] = { 0x0000               };
uint16_t _strikeDestZ[1] = { 0x0000 };
uintptr_t _offerFmtLine1a[1] = { 0 };
uintptr_t _offerFmtLine1b[1] = { 0 };
uint16_t _goalOut[1] = { 0x0000               };
uint16_t _fadeInChantTimer[1] = { 0x0000 };
uint16_t _fireBlocked[1] = { 0x0000               };
uintptr_t _exchangeFmtSinglePlayer[1] = { 0 };
uint16_t _stoppageEventTimer[1] = { 0x0000               };
uint16_t _keyCount[1] = { 0x0000               };
uint16_t _isGoalAttempt[1] = { 0x0000               };
uint16_t _fadeCommand[1] = { 0x03E7 };
uint16_t _statsTimer[1] = { 0x0000               };
uint16_t _aiLongKickTable[3] = { 0,-999,4 };
uint16_t _ballNextZDeadVar[1] = { 0x0000               };
uint16_t _maxSubstitutes[1] = { 0x0000 };
uint16_t _importMenuSelectedTactics[1] = { 0x0000               };
uint16_t _cameraXMod320[1] = { 0x0000 };
uint16_t _playSeason[1] = { 0x0000               };
/* offerNegotiationStep1Amount: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uintptr_t _menuTitle[1] = { 0 };
uint16_t _blockAdvertisements[1] = { 0x0000               };
uint16_t _careerAutoAdvanceInProgress[1] = { 0x0000 };
uint16_t _fireResetFlag[1] = { 0x0000 };
uint16_t _loaderModeFlag[1] = { 0x0000 };
uint16_t _editTacticsPlayerPositions[12] = { -1,0,1,2,3,4,5,6, 7,8,9,10 };
uint16_t _is_A_team[1] = { 0x0000 };
uint16_t _ballControlTurnThresholds[8] = { 4,5,6,8,11,14,17,21  };
uint32_t _careerIntlPlayersRemoveMask[1] = { 0x00000000           };
uint16_t _selectedTeamIndex[1] = { 0x0000               };
uint16_t _selectedPlayerPosition[1] = { 0x0000 };
uint16_t _team2PenaltyGoals[1] = { 0x0000 };
uint16_t _audioBankLoaded[1] = { 0x0000 };
uint16_t _secondsSwitchAccumulator[1] = { 0x0000               };
/* Sized to hold up to 10 4-byte pointers to the visible market entries
 * (one per displayed list row). The auto-extractor only emitted the
 * first 2 bytes (`dc.w $0000` at SWOS2.s line 103049) because the
 * Amiga relied on pad_145364's 38 trailing bytes for the rest of the
 * array — see the "Adjacent-globals BSS pattern" memory. On SDL each
 * symbol lives in its own .o, so writes past entry 0 stomp adjacent
 * BSS and reads past entry 0 return garbage. Sized as
 * uint32_t[10] = 40 bytes, matching the original 2 + 38 footprint
 * exactly. */
/* Stores up to 10 pointers into homeMarketPlayers/foreignMarketPlayers
 * entries. Amiga stored these as 32-bit values; on 64-bit the slot must
 * be uintptr_t or the writer's `(uint32_t)a1` cast truncates the upper
 * 32 bits and the reader SIGSEGVs in PlayerMarketItemBeforeDraw.
 * Same trap shape as [[project_uint32_pointer_storage_trap]]. */
uintptr_t _playerMarketVisibleItems[10] = { 0 };
uint16_t _diyLeagueTournament_viewOptionsAvailable[1] = { 0x0000 };
uint16_t _hilLeftTeamGoals[1] = { 0x0000 };
uint16_t _team1TotalGoals[1] = { 0x0000 };
uintptr_t _assetLoaderWorkPtr[1] = { 0 };
uint16_t _showCupsAndOther[1] = { 0x0000               };
uint16_t _kKeeperPenaltySaveDistanceFar[1] = { 0x0014               };
uint16_t _pl2Tactics[1] = { 0x0000 };
uint16_t _uefaCupTitleHolderTeamId[1] = { 0x0000 };
uint16_t _ballXQuadrantDead[1] = { 0x0000 };
uint16_t _showingUserStats[1] = { 0x0000               };
uint16_t _plToEnterGameIndex[1] = { 0x0000               };
uint16_t _diyLeagueSelectTeams[1] = { 0x0000 };
Sprite *_lastPlayerBeforeGoalkeeper = (Sprite *)0;
uint16_t _animPatternsState[1] = { 0x0000 };
uint16_t _longFireTime[1] = { 0x0000 };
uint16_t _disallowedTurnFlagsCounter[1] = { 0x0000 };
uint16_t _kFreePassReleasingBallSpeed[1] = { 1792                };
uint16_t _titleScreenPalette[16] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _diyLeagueTournament_fixtureRowHeight[1] = { 0x0000 };
uint16_t _team1NumAllowedInjuries[1] = { 0x0000 };
uint16_t _hideBall[1] = { 0x0000               };
uint16_t _initialSubsState[1] = { 0x0000 };
uint16_t _contAbortResult[1] = { 0x0000               };
uint16_t _careerAddIntlPlayerSelectedTeamNumber[1] = { 0x0000 };
uint16_t _conflictingShortsTeamColorsTable[11] = { 0x01A3,0x0283,0x0164,0x0218,0x0058,0x00E5,0x0474,0x00A3 , 0x0105,0x020A,
                                                   0x0440 };
uint16_t _contAbortDefault[1] = { 0x0000               };
uint16_t _AI_maxStoppageTime[1] = { 0x0000 };
uint16_t _initialKickWriteOnlyTicks[1] = { 0x0000 };
uint16_t _editTacticsPitchStatus[1] = { 0x0000               };
uintptr_t _whatIsChanging[1] = { 0 };
uint16_t _diyLeagueLookOnly[1] = { 0x0000 };
uint16_t _statsTeam2GoalsCopy[1] = { 0x0000 };
uint16_t _subsMenuEntryFrameColor[1] = { 0x0000               };
uint16_t _fadeInitFlag[1] = { 0x03E7 };
uint16_t _writeOnly01[1] = { 0x0001 };
uint16_t _editTacticsInputRepeatCounter[1] = { 0x0000 };
uint16_t _diyQualifiedTeamIndices[16] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 , 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
/* cameraX / cameraY / cameraXFP / cameraYFP: manual definitions in
 * swos_globals.c. The Amiga build aliases `cameraX` (int16 high word)
 * and `cameraXFP` (int32 full FP) onto the same 4-byte storage, but
 * that only works on BE — on LE the int16 reads the fraction half.
 * Split the storage so each half is individually correct. */
uint16_t _chooseTeamsFriendly_menuTopY[1] = { 0x0000 };
TeamGeneralInfo *_lastTeamScored = (TeamGeneralInfo *)0;
uint16_t _tacticCursorFrames[4] = { 0x0095,0x0096,0x0097,0x0096 };
uint16_t _pl1Direction[1] = { 0x0000 };
uint16_t _xQuadrantToCoordinates[15] = { 3,12,21,29,38,47,55,64 , 73,81,90,99,107,116,125 };
void *_teamNames[10] = { 0 };
uint16_t _currentGame[1] = { 0x0000               };
uint16_t _diyTournamentSelectedStageIndex1Based[1] = { 0x0000 };
uint16_t _statsTeam2GoalsCopy2[1] = { 0x0000 };
uint16_t _cachedSecondaryQualifiedTeamIds[3] = { 0x0000,0x0000,0x0000 };
uint16_t _subsBlockDirections[1] = { 0x0000               };
TeamGame *_teamScoredGamePtr = (TeamGame *)0;
uint16_t _statsTeam1Goals[1] = { 0x0000 };
uint16_t _diyLeagueStartingMonth[1] = { 0x0000 };
uint16_t _selectTeamsNoLimit[1] = { 0x0000               };
uint16_t _paused[1] = { 0x0000               };
uint16_t _keyDelay[1] = { 0x0000 };
uint16_t _selectedLeagueIndex[1] = { 0x0000               };
uint16_t _subsTrainingTopTeam[1] = { 0x0000               };
uint32_t _seasonGateReceiptsAccum2[1] = { 0x00000000           };
uintptr_t _pmSelectedPlayerEntry[1] = { 0 };
TeamGeneralInfo *_teamScoredDataPtr = (TeamGeneralInfo *)0;
uint16_t _patternsGoalCounter[1] = { 0x0000 };
uintptr_t _topTeamPtr[1] = { 0 };
uint16_t _avgTeamValueBases[92] = { 0,5,10,15,20,25,30,35, 40,45,50,55,60,65,70,75, 80,85,90,95,100,110,120,130, 140,150,160,170,180,190,200,225, 250,275,300,350,400,450,500,550, 600,650,700,750,800,850,900,950, 1000,1100,1200,1300,1400,1500,1600,1700, 1800,1900,2000,2250,2500,2750,3000,3500, 4000,4500,5000,6000,7000,8000,9000,10000, 11000,12000,13000,14000,15000,16000,17000,18000, 19000,20000,21000,22000,23000,24000,25000,26000, 27000,28000,29000,30000 };
uint16_t _editTacticsNumPlayers[1] = { 0x0000 };
uint16_t _ballQuadrantIndex[1] = { 0x0000               };
uint16_t _savedTeam1Goals[1] = { 0x0000 };
uint16_t _thirdPartyOfferResult[1] = { 0x0000               };
uint16_t _jobOfferOrdinal[1] = { 0x0000               };
uint16_t _dontShowScorers[1] = { 0x0000               };
uint16_t _cachedPenaltyShootoutTeam1Score[1] = { -1 };
uint16_t _team2GoalkeeperReplaced[1] = { 0x0000 };
uint16_t _editTacticsBallColumn[1] = { 0x0000 };
uint16_t _subsDirection[1] = { 0x0000               };
uint16_t _stoppageAuxFlag[1] = { 0x0000 };
uint16_t _playerYQuadrantCoordinates[16] = { 0x0095,0x00BD,0x00E5,0x010D,0x0135,0x015D,0x0185,0x01AD, 0x01D5,0x01FD,0x0225,0x024D,0x0275,0x029D,0x02C5,0x02ED };
uintptr_t _entryText2[1] = { 0 };
uint16_t _animatedPatterns[8] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000 };
uint16_t _avgPlayerPriceHalfClamped[1] = { 0x0000 };
uint8_t *_diyNameOfCompetitionPtr = (uint8_t *)0;
uint16_t _careerAddIntlPlayerResult[1] = { 0x0000 };
uint16_t _numScenesInHilFile[1] = { 0x0000 };
uint16_t _team2NameLength[1] = { 0x0000 };
uint16_t _ballOutOfGameTimer[1] = { 0x0000               };
uint16_t _lastPlayerTurnFlags[1] = { 0x0000 };
uint16_t _halfNumber[1] = { 0x0000               };
uint16_t _cachedPrimaryQualifiedTeamIds[9] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000 };
uint16_t _goalkeeperSkipFlag[1] = { 0x0000               };
uint16_t _pl2SkipFireCounter[1] = { 0x0000               };
uintptr_t _benchTeam[1] = { 0 };
uint32_t _kBallJumpHeaderDeltaZ[1] = { 0x0000A000           };
uint16_t _throwInPassOrKick[1] = { 0x0000               };
uint16_t _kLowerLeftCornerBallDestDelta[16] = { 250,-1000,1000,-350,1000,-150,1000,1000, 0,1000,-1000,1000,-1000,0,-1000,-1000 };
uint16_t _leagueNumbersTable[10] = { 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000, 0x0000,0x0000 };
uint16_t _kPassingSpeed_62500_90000[1] = { 2048                };
uint16_t _team2MostFrequentFace[1] = { 0x0000 };
/* offerSwapPlayerOrdinal: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uint16_t _lastTeamScoredNumber[1] = { 0x0000               };
uint16_t _fireCounter[1] = { 0x0000               };
uint16_t _resultTimer[1] = { 0x0000 };
uint16_t _currentPlayerIndicatorPosition[1] = { 0x0004 };
uint16_t _savedContestListIndex[1] = { 0x0000               };
uint16_t _substituteInProgress[1] = { 0x0000               };
uint16_t _diyLeagueEndingMonth[1] = { 0x0000 };
uint16_t _numTeamsColumn1[1] = { 0x0000               };
uint16_t _numTeamsColumn2[1] = { 0x0000 };
uint16_t _numTeamsColumn3[1] = { 0x0000 };
uint16_t _numTeamsCountryBuffer[1] = { 0x0000 };
uint16_t _refTimer[1] = { 0x0000               };
uintptr_t _teamThatSubstitutes[1] = { 0 };
uintptr_t _savedGoalBasePtr[1] = { 0 };
uint16_t _teamNumThatScored[1] = { 0x0000               };
uintptr_t _viewSquadTitle[1] = { 0 };
uint16_t _kPlayerGroundConstant[1] = { 0x0060               };
uint16_t _kBallSpeedBounceFactorTable[7] = { 24,80,80,72,64,40,32 };
uintptr_t _savedContestEntryPtr[1] = { 0 };
uintptr_t _euroCupTeamListWritePtr[1] = { 0 };
uint16_t _pl2BenchTimeoutCounter[1] = { 0x0000 };
Sprite *_lastPlayerPlayed = (Sprite *)0;
uintptr_t _cupWinnersCupTeamListPtr[1] = { 0 };
uint16_t _exitSelectTeams[1] = { 0x0000 };
uint16_t _subsPl2TapCount[1] = { 0x0000 };
uint16_t _advertSlideTimer[1] = { 0x0000 };
uint16_t _plToBeSubstitutedPos[1] = { 0x0000               };
uint16_t _subsPl1TapCount[1] = { 0x0000 };
uint16_t _lastControls[1] = { 0x0000               };
uint16_t _goalCameraMode[1] = { 0x0000               };
uint16_t _kAIFailedPassChance[8] = { 6,4,3,2,1,0,0,0     };
uint16_t _kPlayerWithBallOffsets[16] = { 0,1,-1,1,-1,0,-1,-1, 0,-1,1,-1,1,0,1,1 };
uintptr_t _chooseTeamMenuName[1] = { 0 };
uint16_t _ballInUpperPenaltyArea[1] = { 0x0000 };
uintptr_t _exchangeFmtTwoPlayers[1] = { 0 };
uintptr_t _offerFmtTwoPlayers[1] = { 0 };
uintptr_t _playWhatStr[1] = { 0 };
uint16_t _playerFacesTable[3] = { 0x049B,0x049C,0x049D   };
uint16_t _playerNumThatStarted[1] = { 0x0000               };
#ifdef AGA
/* AGA 256-colour game/pitch palette (Amiga 12-bit 0RGB). Converted from the DOS
 * "Game" palette via SwosGfx ToAmiga12. WriteCopperPitchPalette overlays 9
 * pitch-condition entries (paletteIndices) onto this before pushing all 256 to
 * the copper. Regenerate: SwosGfx -palettes -pal-color=amiga12 -pal-format=c
 * -pal-count=256 from DOS_Palettes/Game.txt. First 16 ≈ the OCS #else values. */
uint16_t _gamePalette[256] = {
    0x0750, 0x0999, 0x0FFF, 0x0000, 0x0620, 0x0B40, 0x0F60, 0x0650,
    0x0020, 0x0550, 0x0F00, 0x000F, 0x0602, 0x099F, 0x00D0, 0x0FF0,
    0x09C0, 0x0AAA, 0x0FFF, 0x0111, 0x0720, 0x0D40, 0x0F83, 0x08C0,
    0x0030, 0x0CC0, 0x0F66, 0x077F, 0x0702, 0x0BBF, 0x00F0, 0x0FFB,
    0x0680, 0x0777, 0x0DDD, 0x0000, 0x0410, 0x0720, 0x0B40, 0x0580,
    0x0000, 0x0880, 0x0C00, 0x000C, 0x0301, 0x077C, 0x00A0, 0x0CC0,
    0x0000, 0x0111, 0x0222, 0x0444, 0x0555, 0x0777, 0x0888, 0x0AAA,
    0x0BBB, 0x0DDD, 0x0EEE, 0x0FFF, 0x0FDB, 0x0FB8, 0x0F84, 0x0F60,
    0x0E50, 0x0C40, 0x0B40, 0x0930, 0x0820, 0x0620, 0x0000, 0x0007,
    0x0008, 0x0009, 0x000A, 0x022C, 0x066E, 0x099E, 0x0CBA, 0x0B70,
    0x0420, 0x0530, 0x0EEE, 0x0EDD, 0x0DCC, 0x0DBB, 0x0DAA, 0x0D99,
    0x0C88, 0x0B77, 0x0966, 0x0755, 0x0533, 0x0322, 0x0111, 0x0000,
    0x0FED, 0x0EDB, 0x0DB9, 0x0CA8, 0x0B96, 0x0975, 0x0764, 0x0653,
    0x0543, 0x0211, 0x0530, 0x0B97, 0x0000, 0x0000, 0x0000, 0x0000,
    0x009F, 0x0555, 0x0777, 0x0222, 0x0F00, 0x0F77, 0x0888, 0x0999,
    0x0AAA, 0x0BBB, 0x0FBB, 0x0CCC, 0x0DDD, 0x0EEE, 0x0FFF, 0x0F44,
    0x0320, 0x0555, 0x0CCC, 0x0000, 0x0200, 0x0720, 0x0C40, 0x0210,
    0x0000, 0x0110, 0x0C00, 0x000C, 0x0000, 0x077C, 0x00A0, 0x0CC0,
    0x0680, 0x0777, 0x0CCC, 0x0000, 0x0410, 0x0930, 0x0C62, 0x0580,
    0x0000, 0x0880, 0x0C55, 0x055C, 0x0000, 0x099C, 0x00B0, 0x0CC9,
    0x0340, 0x0333, 0x0999, 0x0000, 0x0000, 0x0310, 0x0720, 0x0240,
    0x0000, 0x0440, 0x0900, 0x0009, 0x0000, 0x0559, 0x0070, 0x0980,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0222, 0x0333, 0x0444, 0x0666,
    0x0777, 0x0999, 0x0AAA, 0x0CCC, 0x0CA9, 0x0C86, 0x0C63, 0x0C40,
    0x0A30, 0x0830, 0x0720, 0x0510, 0x0410, 0x0200, 0x0000, 0x0003,
    0x0004, 0x0005, 0x0006, 0x0118, 0x044A, 0x067A, 0x0887, 0x0740,
    0x0000, 0x0210, 0x0AAA, 0x0A99, 0x0A99, 0x0988, 0x0977, 0x0966,
    0x0966, 0x0755, 0x0533, 0x0322, 0x0111, 0x0000, 0x0000, 0x0000,
    0x0CB9, 0x0A98, 0x0986, 0x0875, 0x0764, 0x0543, 0x0432, 0x0211,
    0x0110, 0x0000, 0x0110, 0x0765, 0x0000, 0x0000, 0x0000, 0x0000,
    0x007C, 0x0111, 0x0333, 0x0000, 0x0B00, 0x0B55, 0x0444, 0x0555,
    0x0666, 0x0777, 0x0B88, 0x0888, 0x0999, 0x0AAA, 0x0BBB, 0x0C33
};
#else
uint16_t _gamePalette[16] = { 0x0360,0x0999,0x0FFF,0x0000,0x0721,0x0A40,0x0F71,0x0250, 0x0030,0x0370,0x0F00,0x000F,0x0702,0x088F,0x0380,0x0FF0 };
#endif
uint16_t _editTacticsNearestSelectPitchWidth[1] = { 0x0000 };
uintptr_t _selectedTeamText[1] = { 0 };
uint16_t _pmMenuNumPlayers[1] = { 0x0000               };
uint16_t _kGoalkeeperWeakDeflectBallSpeed[1] = { 0x0200 };
uint16_t _startingTeamIndex[1] = { 0x0000 };
uint16_t _benchXLimit[1] = { 0x0000               };
uintptr_t _squadTeamPtr[1] = { 0 };
uint16_t _teamSelectionKeyCache_ListCount[1] = { 0x0000 };
uint16_t _careerJobOfferMode[1] = { 0x0000 };
uint16_t _otherTeamScored[1] = { 0x0000 };
uintptr_t _offerFmtLine2a[1] = { 0 };
uintptr_t _offerFmtLine2b[1] = { 0 };
uint16_t _teamPlayingUp[1] = { 0x0001               };
uint16_t _editTacticsFlipPairColorTable[5] = { 14,10,12,6,11 };
uint16_t _maxSubsPerTeam[1] = { 0x0000 };
uint16_t _controlsHeldTimer[1] = { 0x0000               };
uint16_t _aiTurnDirection[1] = { 0x0001 };
uint8_t *_diyCupMatchTeamNamePtr1 = (uint8_t *)0;
uint8_t *_diyCupMatchTeamNamePtr2 = (uint8_t *)0;
uint8_t *_spriteGraphicsPtr = (uint8_t *)0;
uint16_t _team2GoalsFirstLeg[1] = { 0x0000 };
uint16_t _gameNotInProgressCounterWriteOnly[1] = { 0x0000 };
uintptr_t _promptHeader[1] = { 0 };
TeamFileEx *_selectedTeamPtr = (TeamFileEx *)0;
uint16_t _kPassingSpeed_22500_40000[1] = { 1877                };
uint16_t _substitutedPlDestX[1] = { 0x0000 };
uint16_t _kSpinMultiplierFactor[10] = { 5,4,3,2,2,2,2,1    , 1,1 };
uint16_t _substitutedPlDestY[1] = { 0x0000 };
uintptr_t _errorText[1] = { 0 };
uint16_t _diyAllMatchesPlayed[1] = { 0x0000               };
uint16_t _pl2Coach[1] = { 0x0000 };
uintptr_t _winningTeamPtr[1] = { 0 };
uint16_t _columnMarker[1] = { 0x0000               };
uint16_t _natTeamsRandLimit2[1] = { 0x008E };
uint16_t _savedContestGameOffset[1] = { 0x0000               };
uint16_t _kRefereeSpeed[2] = { 0x0400,0x0500 };
uint16_t _kPlayerAirConstant[1] = { 0x0048               };
uint16_t _penalty[1] = { 0x0000               };
uint16_t _editTacticsSelectedPlayerSlotIndex[1] = { 0x0000 };
uint16_t _isNationalSquad[1] = { 0x0000               };
uint16_t _ballColumn[1] = { 0x0000               };
uint16_t _diyTournamentPrevValueBackup[1] = { 0x0000 };
uint16_t _extraTimeState[1] = { 0x0000 };
uintptr_t _buyOtherPlayersTeamPtr[1] = { 0 };
uint16_t _isSquadViewMode[1] = { 0x0000 };
uint16_t _minSubstitutes[1] = { 0x0000 };
uintptr_t _extension[1] = { 0 };
uint16_t _showPrereleaseCounter[1] = { 0x0000 };
uint16_t _kPlayerSpeedsGameStopped[8] = { 1136,1152,1168,1184,1200,1216,1232,1248 };
uint16_t _ingameTeamPlayerOffsets[16] = { 0,61,122,183,244,305,366,427 , 488,549,610,671,732,793,854,915 };
uint16_t _jobOffersFromRetired[1] = { 0x0000               };
uint16_t _findTeamInDIY_SuppressSideEffectsFlag[1] = { 0x0000 };
uint16_t _squadPrevSelectedPlayerIndex[1] = { 0x0000 };
uint16_t _gameSeason[1] = { 0x0000 };
uintptr_t _teamOfferingJobPtr[1] = { 0 };
uint16_t _ballNextX[1] = { 0x0000 };
uint16_t _ballNextY[1] = { 0x0000 };
uint16_t _teamDepthLevel[1] = { 0x0000               };
uint16_t _frameCount[1] = { 0x0000 };
void *_countryNames[64] = { 0 };
uint16_t _cameraYSaved0[1] = { 0x0000 };
uint16_t _cameraYSaved1[1] = { 0x0000 };
uint16_t _aiRand[1] = { 0x0000               };
uint16_t _stoppageTimerTotal[1] = { 0x0000               };
uint32_t _viewSquadFlags[1] = { 0x00000000           };
uint16_t _teamSkillDiffAfterChange[1] = { 0x0000 };
uint16_t _team2PenaltyAttempts[1] = { 0x0000 };
uint16_t _pl1Tactics[1] = { 0x0000 };
uintptr_t _team2Filename[1] = { 0 };
uint16_t _lastVBlankServiceTick[1] = { 0x0000 };
uint16_t _aiRotateRightTable[3] = { 1,2,3 };
uint16_t _timeDelta[1] = { 0x0000               };
uint16_t _numPlayersToShowInMenu[1] = { 0x0000 };
/* offerExtraPlayerOrdinal: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uint16_t _kPitchBallSpeedInfluence[7] = { -2,2,3,0,0,-1,-1    };
uint16_t _uefaCupQualifiedTeamCount[1] = { 0x0000 };
uintptr_t _team1Filename[1] = { 0 };
uint16_t _timeVar[1] = { 0x0000 };
uintptr_t _careerAddIntlPlayerSelectedSrcPlayerNamePtr[1] = { 0 };
uint16_t _offerStatus[1] = { 0x0000               };
uint16_t _viewSquadArg2[1] = { 0x0000 };
uint16_t _viewSquadArg3[1] = { 0x0000 };
uint16_t _kInjuryForeverSpeed[1] = { 0x0300 };
uint16_t _selTeamsColumn1[1] = { 0x0000               };
uint16_t _selTeamsColumn2[1] = { 0x0000 };
uint16_t _waitForPlayerToGoInTimer[1] = { 0x0000               };
uint16_t _cardsDisallowed[1] = { 0x0000               };
uint16_t _careerAddIntlPlayerSelectedPlayerOrdinal[1] = { 0x0000 };
uint16_t _selTeamsColumn3[1] = { 0x0000 };
/* selectedContest stores a Competition* pointer (Amiga binary uses a
 * 32-bit pointer slot; on 64-bit it must be uintptr_t or the high half
 * is truncated by the `(uint32_t)contest` cast at the writer site).
 * Wrong-size storage caused VIEW WORLD panics because the truncated
 * pointer dereferenced to zero-page memory and read all zeros. */
uintptr_t _selectedContest[1] = { 0 };
uintptr_t _abortText[1] = { 0 };
uint16_t _friendlyOk[1] = { 0x0000               };
uint16_t _whichCard[1] = { 0x0000               };
uint16_t _markedPlayerNumber[1] = { 0x0000               };
uint16_t _viewTeamsAbortY[1] = { 0x0000 };
uint16_t _stoppageTimerActive[1] = { 0x0000               };
uint16_t _lastKickSampleTick[1] = { -1 };
TeamFileEx *_diyCupLastMatchTeam1Ptr = (TeamFileEx *)0;
uint16_t _aiLeftSpinTable[3] = { -1,-2,-3 };
uint16_t _bench2Called[1] = { 0x0000 };
uint16_t _showOrigCustomTeams[1] = { 0x0000 };
uint16_t _swapPlayerValueRandom[1] = { 0x0000               };
uint16_t _viewTeamsColumn1[1] = { 0x0000               };
uint16_t _viewTeamsColumn2[1] = { 0x0000 };
uint16_t _changeTeams[1] = { 0x0000               };
uint16_t _viewTeamsColumn3[1] = { 0x0000 };
uint16_t _editTacticsTransitionActive[1] = { 0x0000               };
uint16_t _kGoalkeeperMoveToBallSpeed[1] = { 0x0400               };
uint16_t _careerTeamCoachOnly[1] = { 0x0000               };
uintptr_t _uefaCupTeamListPtr[1] = { 0 };
uint16_t _subsMenuX[1] = { 0x0000               };
uint16_t _kPlayerTacklingSpeed[1] = { 0x0700               };
uint16_t _pl1BenchBlockWhileHoldingDirection[1] = { 0x0000               };
uint16_t _foulXCoordinate[1] = { 0x0000               };
uint16_t _viewingOpponentTeam[1] = { 0x0000 };
uint16_t _currentGameTick[1] = { 0x0000 };
uint16_t _currentAnimatedAdLine[1] = { 0x0000               };
uint16_t _loadTeam[1] = { 0x0000               };
uintptr_t _chooseTeamsFunc[1] = { 0 };
uintptr_t _squadSelectedPlayerPtr[1] = { 0 };
uint16_t _kComputerTacklingDownTime[8] = { 3,3,3,3,3,3,3,3 };
uint16_t _ballMovingFrameIndices[5] = { 1068,1067,1066,1065,-1 };
uint16_t _kGoalkeeperStrongDeflectBallSpeed[1] = { 0x0600 };
uint32_t _kBallKickingDeltaZ[1] = { 0x00014000           };
uint16_t _didSwapThisFrame[1] = { 0x0000 };
TeamFileEx *_diyCupLastMatchTeam2Ptr = (TeamFileEx *)0;
uint16_t _transferPlayerMenuResult[1] = { 0x0000               };
uint16_t _topOfStackMark[1] = { 0x0000               };
uintptr_t _buyOtherPlayersTitlePtr[1] = { 0 };
uint16_t _statsTeam1GoalsCopy[1] = { 0x0000 };
uint16_t _plToBeSubstitutedOrd[1] = { 0x0000               };
uint16_t _selectedTeamCountryNumber[1] = { 0x0000               };
uint16_t _sfxQueueWriteIdx[1] = { 0x0000 };
uint16_t _paletteIndex[1] = { 0x0000 };
uint16_t _benchOff[1] = { 0x0000               };
uint16_t _team1GoalsFirstLeg[1] = { 0x0000 };
Sprite *_bookedPlayer = (Sprite *)0;
uint16_t _kBallSpeedPassingIncrease[8] = { 0,48,96,144,192,256,320,384  };
uint16_t _refState[1] = { 0x0000               };
uint16_t _bottomTeamPlayerNo[1] = { 0x0000 };
uint16_t _isFirstPlayerChoosingTeam[1] = { 0x0000               };
uint16_t _kBallBounceFactorTable[7] = { 88,112,104,104,96,88,80 };
uint16_t _playGame[1] = { 0x0000 };
uint16_t _playerMarketFilterCountry[1] = { 0x0000 };
TeamGeneralInfo *_lastTeamPlayed = (TeamGeneralInfo *)0;
uint32_t _kGoalkeeperDiveDeltas[8] = { 0x00030000,0x00038000,0x00040000,0x00048000,0x00050000,0x00058000,0x00060000,0x00068000  };
uintptr_t _cupWinnersCupTeamListWritePtr[1] = { 0 };
uint16_t _offerTargetValueBase[1] = { 0x0000 };
uint16_t _similarityFactor[1] = { 0x0000               };
uint16_t _ballSpeedBounceFactor[1] = { 0x0000               };
uint16_t _tacticsPositionDefensiveness[11] = { 0,0,0,0,0,0,0,0, 0,0,0 };
uint16_t _diyCupSelectTeams[1] = { 0x0000 };
uint16_t _breakCameraMode[1] = { 0x0000               };
/* offerAmount: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
Sprite *_lastKeeperPlayed = (Sprite *)0;
uint16_t _inSubstitutesMenu[1] = { 0x0000               };
uint16_t _kBallAirConstant[1] = { 0x000A               };
uint16_t _showDomesticPlayerMarket[1] = { 0x0000               };
uint16_t _goIntoHighlightsLoop[1] = { 0x0000               };
uint16_t _ballInGoalkeeperArea[1] = { 0x0000 };
uint16_t _menuX[1] = { 0x0000 };
uint16_t _menuY[1] = { 0x0000 };
uint16_t _currentTick[1] = { 0 };
uint16_t _penaltiesTimer[1] = { 0x0000               };
uint16_t _editTacticsLastTick[1] = { 0x0000               };
/* offerPrimaryPlayerOrdinal: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uintptr_t _careerAddIntlPlayerSelectedSrcPlayerPtr[1] = { 0 };
uint8_t *_diyTournamentSelectedStagePtr = (uint8_t *)0;
uint16_t _chooseTeamsResult[1] = { 0x0000               };
uint16_t _shortFire[1] = { 0x0000 };
uint16_t _team1PenaltyShooterIndex[1] = { 0x0000 };
uint16_t _yQuadrantToCoordinates[16] = { 5,14,24,36,50,64,79,93, 107,121,136,150,164,176,186,195 };
uint16_t _selFilesColumn3NumItems[1] = { 0x0000 };
uint16_t _selFilesColumn2NumItems[1] = { 0x0000 };
uint16_t _dstXByteOffsetSaved0[1] = { 0x0000 };
uint16_t _gameStatePl[1] = { 0x0000               };
uint16_t _dstXByteOffsetSaved1[1] = { 0x0000 };
uint16_t _subsState[1] = { 0x0000               };
uint16_t _littlePlayersConvertTable[16] = { 0,1,2,3,4,5,6,7, 8,9,-1,-1,12,13,-1,-1 };
uint16_t _selFilesColumn1NumItems[1] = { 0x0000 };
uint16_t _benchPlayerIndex[1] = { 0x0000               };
uint16_t _deadThrowInDirectionVar[1] = { 0x0000 };
uint16_t _tacticsChangedFlag[1] = { 0x0000               };
uint16_t _offerValueBaseSum[1] = { 0x0000 };
uint16_t _diyLeagueTournament_fixtureRowYStep[1] = { 0x0000 };
uint16_t _pendingSampleId3_4[1] = { 0x0000 };
uint16_t _chooseTeamsFriendly_menuBottomY[1] = { 0x0000 };
uint16_t _kHighKickBallSpeed[1] = { 0x0A80               };
uint16_t _plComingYTmp[1] = { 0x0000 };
uint16_t _benchY[1] = { 0x0000 };
uint16_t _currentDiyLine[1] = { 0x0000               };
uint16_t _fadeOutChantTimer[1] = { 0x0000 };
uint16_t _exitY[1] = { 0x0000 };
uint16_t _numLeaguesInCountry[1] = { 0x0000               };
uint16_t _kLowerRightCornerBallDestDelta[16] = { -250,-1000,1000,-1000,1000,0,1000,1000, 0,1000,-1000,1000,-1000,-150,-1000,-350 };
uint16_t _crowdSamples[4] = { 0x0037,0x0038,0x003A,0x003E };
uint16_t _nobodysBallTimer[1] = { 0x0000 };
uint16_t _playerXQuadrantsCoordinates[15] = { 0x0062,0x0084,0x00A6,0x00C8,0x00EA,0x010C,0x012E,0x0150, 0x0172,0x0194,0x01B6,0x01D8,0x01FA,0x021C,0x023E };
uint16_t _editTacticsBallRow[1] = { 0x0000 };
uint16_t _trophySprites[10] = { 0x00DE,0x00DF,0x00DB,0x00DD,0x00DC,0x00D7,0x00D8,0x00D9, 0x00DA,0x00D8 };
uint16_t _team2StatsData[7] = { 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000              , 0x0000               };
uint16_t _team2GoalsDigit1[1] = { 0x0000 };
uint16_t _kKeeperEaseBallTable[8] = { 1,4,3,3,3,2,2,2 };
uint16_t _menuControlsDirection[1] = { 0x0000               };
uint16_t _team2TotalGoals[1] = { 0x0000 };
uint16_t _team2GoalsDigit2[1] = { 0x0000 };
uint16_t _kPassingSpeedFurtherThan122500[1] = { 2218                };
uint16_t _numNeededTeams[1] = { 0x0000               };
uint16_t _aiResumePlayTimer[1] = { 0x0000               };
TeamFileEx *_diyLeftTeamPtr = (TeamFileEx *)0;
uint16_t _inEditTactics[1] = { 0x0000               };
uint16_t _playerMarketListScrollOffset[1] = { 0x0000 };
TeamFile *_gameTeam1 = (TeamFile *)0;
uint16_t _friendlyPitchType[1] = { 0x0000               };
TeamFile *_gameTeam2 = (TeamFile *)0;
uint16_t _diyTournamentType[1] = { 0x0000               };
uintptr_t _squadPrevHighlightedMenuEntryPtr[1] = { 0 };
uint16_t _advertIndex[1] = { 0x0000               };
uint16_t _chosenTactics[1] = { 0x0000               };
uint16_t _ballStaticFrameIndices[3] = { 1068,1068,-1 };
uint16_t _contestEntryHeight[1] = { 0x0000 };
uint16_t _team1AppPercent[1] = { 0x0000               };
/* numExtraPlayersWanted: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uint16_t _subsPrevPl2Direction[1] = { -1 };
uint16_t _oldCameraYSaved0[1] = { 0x0000 };
uint16_t _diyCompHasGroupStage[1] = { 0x0000               };
uint16_t _oldCameraYSaved1[1] = { 0x0000 };
uint16_t _isGameFriendly[1] = { 0x0000 };
uint32_t _careerIntlPlayersSelectableMask[1] = { 0x00000000           };
uint16_t _chooseTeamsFriendly_middleListNumEntries[1] = { 0x0000 };
uint16_t _subsMovementDelayTimer[1] = { 0x0000 };
uint16_t _overlayActiveFlag[1] = { 0x0000 };
uint16_t _kGoalkeeperBurstSpeed[2] = { 512,1024 };
uint16_t _chooseTeamsNumTeams[1] = { 0x0000               };
uint16_t _headerYPlusHeight[1] = { 0x0000 };
uint16_t _fadeAndSaveHighlightScene[1] = { 0x0000               };
uint16_t _useIndividualPlayerSkinColor[1] = { 0x0000               };
uint16_t _menuControlsDirection2[1] = { 0x0000               };
uint16_t _writeOnlyVar04[1] = { 0x0000 };
uint16_t _waitingInterrupt[1] = { 0 };
uint16_t _ballDefensiveX[1] = { 0x0000               };
uint16_t _ballDefensiveY[1] = { 0x0000 };
uint16_t _opponentBenchY[1] = { 0x0000 };
uint16_t _ballDefensiveZ[1] = { 0x0000 };
uint16_t _ballNextGroundX[1] = { 0x0000 };
uint16_t _penaltyGoalieSkillTable[30] = { 0x0001              , 0x0400              , 0x00B0              , 0x0400              , 0x0100              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0007              , 0x0002              , 0x0000              , 0x000E              , 0x000D              , 0x0003              , 0x000A              , 0x0005              , 0x0001              , 0x0008               };
uint16_t _lastSelectedTeam[1] = { 0x0000               };
uint16_t _subsMenuBackgroundColor[1] = { 0x0000               };
uint16_t _cupWinnersCupQualifiedTeamCount[1] = { 0x0000 };
uint16_t _cachedExtraTimeTeam2Goals[1] = { 0x0000 };
Sprite *_penaltyShooterSprite = (Sprite *)0;
/* teamOffering: now a field of OfferOrdinals offerOrdinals in swos_globals.c */
uint16_t _euroCupQualifiedTeamCount[1] = { 0x0000 };
uint16_t _numDiyResults[1] = { 0x0000               };
uint16_t _runSlower[1] = { 0x0000               };
uint16_t _prevSelectedTeamEntry[1] = { 0x0000 };
uintptr_t _playerKickerPtr[1] = { 0 };
uint16_t _pl1Coach[1] = { 0x0000 };
uint16_t _cameraLeavingSubsTimer[1] = { 0x0000               };
uint16_t _benchWriteOnly01[1] = { 0x0000 };
uint16_t _avgSelectedTeamsPlayerPrice[1] = { 0x0000               };
uint16_t _benchWriteOnly02[1] = { 0x0000 };
uint16_t _selectedPlayerInSubs[1] = { 0x0000               };
TeamFileEx *_diyRightTeamPtr = (TeamFileEx *)0;
uint16_t _endGameCounter[1] = { 0x0001               };
