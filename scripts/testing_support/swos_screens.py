#!/usr/bin/env python3
"""The SWOS screen graph — reusable domain DATA, not per-request scripts
(docs/TESTING_FLOWS_DESIGN.md). Screen identities come from the menus JSON
(bin/data_json/menus/*.json onInitPtr) and src/menus.c; the picker and memo
behavior from the proven start_fresh_career() route (scripts/swos_plugin.py).

Dynamic pickers are parameterized nodes: the team picker keeps ONE node and
its hop edge takes a label parameter — a new club is new request data, never
a new node or edge.
"""
from testing_support.graph import Edge, Screen, ScreenGraph

MAIN_MENU = "SWOSMainMenuInit"
CAREER_MENU = "CareerMenuInit"
TEAM_PICKER = "SelectTeamsInit"
CHAIRMAN_MEMO = "ChairmanMemosInit"
CAREER_HUB = "ContestMenuInit"
FRIENDLY_MENU = "FriendlyMenuInit"
CHOOSE_KIND = "ChooseTeamsInit"                      # CLUB/NATIONAL/CUSTOM/EXIT
FRIENDLY_TEAMLIST = "ChooseTeamsFriendlyInitFinish"  # team rows cycle control
SEASON_TEAMLIST = "ChooseTeamsFriendlyInit"          # season's team list: the
                                                     # league pre-selected CPU
PLAY_MATCH = "PlayMatchMenuInit"
MATCH_BOUNDARY = "GameLoop_Frame"                    # a FUNCTION, not a menu

SCREENS = [
    Screen("mainMenu", MAIN_MENU),
    Screen("careerMenu", CAREER_MENU),
    Screen("teamPicker", TEAM_PICKER,
           "career team picker: continents/countries/divisions/clubs all "
           "share this init; progress = changed rows, not changed init"),
    Screen("chairmanMemo", CHAIRMAN_MEMO),
    Screen("careerHub", CAREER_HUB),
    Screen("friendlyMenu", FRIENDLY_MENU),
    Screen("chooseKind", CHOOSE_KIND),
    Screen("friendlyTeamList", FRIENDLY_TEAMLIST,
           "fire on a team row cycles its control: 0 -> COMPUTER(1) -> "
           "PLAYER-COACH(2) -> COACH(3) -> 0 (include/play_match_menu.h)"),
    Screen("seasonTeamList", SEASON_TEAMLIST,
           "SEASON's team list: every league team pre-selected as "
           "COMPUTER(1); fire cycles control; PLAY SEASON proceeds"),
    Screen("playMatch", PLAY_MATCH,
           "pre-match squad screen: PlayMatchSelected gates on the RAW "
           "joystick fire (_pl1Fire), not the menu _shortFire"),
]

EDGES = [
    Edge("open career", "mainMenu", "careerMenu",
         {"type": "activate_callback", "symbol": "CareerMenu"},
         {"screen_is": CAREER_MENU}),
    Edge("choose team and start career", "careerMenu", "teamPicker",
         {"type": "activate_callback", "symbol": "ChooseTeamAndStartCareer"},
         {"screen_is": TEAM_PICKER}),
    Edge("picker hop", "teamPicker", "teamPicker",
         {"type": "activate_label", "label": "$label"},
         {"rows_or_screen_changed": True}),
    Edge("open friendly", "mainMenu", "friendlyMenu",
         {"type": "activate_callback", "symbol": "FriendlyMenu"},
         {"screen_is": FRIENDLY_MENU}),
]


def build_graph() -> ScreenGraph:
    return ScreenGraph(SCREENS, EDGES)
