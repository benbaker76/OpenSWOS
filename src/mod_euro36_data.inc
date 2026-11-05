/* mod_euro36_data.inc -- written by scripts/make_euro36.py: do not edit. */

static const TeamId kChampionsSeeds[36] = {
    { 20, 0 },   /* AC MILAN */
    { 20, 22 },   /* JUVENTUS */
    { 20, 31 },   /* PARMA */
    { 20, 23 },   /* LAZIO */
    { 8, 20 },   /* MANCHESTER UTD */
    { 8, 22 },   /* NEWCASTLE UNITED */
    { 8, 18 },   /* LIVERPOOL */
    { 8, 1 },   /* ASTON VILLA */
    { 14, 4 },   /* BOR. DORTMUND */
    { 14, 3 },   /* BAYERN MUNICH */
    { 14, 39 },   /* VFB STUTTGART */
    { 14, 44 },   /* WERDER BREMEN */
    { 35, 8 },   /* BARCELONA */
    { 35, 35 },   /* REAL MADRID */
    { 35, 16 },   /* DEP. LA CORUNA */
    { 35, 6 },   /* ATLETICO MADRID */
    { 13, 30 },   /* PARIS ST-GERMAIN */
    { 13, 23 },   /* MONACO */
    { 13, 1 },   /* AUXERRE */
    { 13, 19 },   /* LYON */
    { 29, 12 },   /* FC PORTO */
    { 29, 20 },   /* SPORTING LISBON */
    { 25, 0 },   /* AJAX */
    { 25, 21 },   /* PSV */
    { 33, 33 },   /* RANGERS */
    { 2, 0 },   /* ANDERLECHT */
    { 6, 8 },   /* SLAVIA PRAGUE */
    { 31, 16 },   /* SPARTAK MOSCOW */
    { 1, 5 },   /* RAPID VIENNA */
    { 30, 18 },   /* STEAUA BUCHAREST */
    { 15, 17 },   /* OLYMPIAKOS */
    { 36, 8 },   /* IFK GOTHENBURG */
    { 38, 9 },   /* FENERBAHCE */
    { 16, 5 },   /* FERENCVAROS */
    { 37, 5 },   /* GRASSHOPPERS */
    { 39, 2 },   /* DINAMO KIEV */
};
static const TeamId kEuropaSeeds[36] = {
    { 8, 4 },   /* BLACKBURN ROVERS */
    { 8, 0 },   /* ARSENAL */
    { 20, 21 },   /* INTER MILAN */
    { 8, 12 },   /* EVERTON */
    { 20, 40 },   /* ROMA */
    { 20, 18 },   /* FIORENTINA */
    { 35, 44 },   /* VALENCIA */
    { 25, 10 },   /* FEYENOORD */
    { 33, 9 },   /* CELTIC */
    { 29, 2 },   /* BENFICA */
    { 35, 10 },   /* BETIS */
    { 14, 2 },   /* BAYER LEVERKUSEN */
    { 35, 47 },   /* ZARAGOZA */
    { 14, 23 },   /* KARLSRUHE */
    { 2, 4 },   /* CLUB BRUGES */
    { 14, 25 },   /* MONCHENGLADBACH */
    { 2, 17 },   /* STANDARD LIEGE */
    { 33, 0 },   /* ABERDEEN */
    { 6, 10 },   /* SPARTA PRAGUE */
    { 13, 4 },   /* BORDEAUX */
    { 13, 27 },   /* NANTES */
    { 13, 20 },   /* MARSEILLE */
    { 25, 31 },   /* VITESSE */
    { 2, 1 },   /* ANTWERP */
    { 33, 15 },   /* DUNDEE UNITED */
    { 25, 28 },   /* TWENTE */
    { 1, 8 },   /* SV SALZBURG */
    { 15, 0 },   /* AEK ATHENS */
    { 15, 19 },   /* PANATHINAIKOS */
    { 38, 21 },   /* TRABZONSPOR */
    { 38, 5 },   /* BESIKTAS */
    { 38, 10 },   /* GALATASARAY */
    { 1, 9 },   /* TIROL INNSBRUCK */
    { 1, 7 },   /* STURM GRAZ */
    { 28, 7 },   /* LEGIA WARSAW */
    { 4, 2 },   /* HAJDUK SPLIT */
};
static const TeamId kConferenceSeeds[36] = {
    { 8, 38 },   /* TOTTENHAM H. */
    { 8, 8 },   /* CHELSEA */
    { 8, 16 },   /* LEEDS UNITED */
    { 20, 42 },   /* SAMPDORIA */
    { 20, 28 },   /* NAPOLI */
    { 35, 42 },   /* TENERIFE */
    { 14, 17 },   /* HAMBURG */
    { 35, 41 },   /* SPORTING GIJON */
    { 35, 4 },   /* ATHLETIC BILBAO */
    { 14, 7 },   /* COLOGNE */
    { 20, 47 },   /* UDINESE */
    { 14, 22 },   /* KAISERSLAUTERN */
    { 13, 37 },   /* STRASBOURG */
    { 13, 33 },   /* RENNES */
    { 13, 5 },   /* CAEN */
    { 25, 24 },   /* RODA JC */
    { 2, 3 },   /* CHARLEROI */
    { 2, 7 },   /* GENT */
    { 33, 24 },   /* HIBERNIAN */
    { 29, 3 },   /* BOAVISTA */
    { 33, 23 },   /* HEARTS */
    { 1, 0 },   /* AUSTRIA VIENNA */
    { 2, 10 },   /* KV MECHELEN */
    { 25, 18 },   /* MVV */
    { 27, 11 },   /* ROSENBORG */
    { 25, 13 },   /* GRONINGEN */
    { 1, 4 },   /* NIEDEROSTERREICH */
    { 29, 24 },   /* VIT. GUIMARAES */
    { 33, 28 },   /* MOTHERWELL */
    { 41, 7 },   /* RED STAR B'GRADE */
    { 36, 15 },   /* OSTER */
    { 7, 9 },   /* SILKEBORG */
    { 29, 11 },   /* FARENSE */
    { 19, 14 },   /* MACCABI TEL-AVIV */
    { 3, 14 },   /* SLAVIA SOFIA */
    { 37, 10 },   /* NEUCHATEL XAMAX */
};

/* {team file, CL, EL, UECL}: each country's clubs in each tier, from the second season */
static const uint8_t kPlaces[][4] = {
    { 1, 1, 3, 2 },
    { 2, 1, 3, 3 },
    { 3, 0, 0, 1 },
    { 4, 0, 1, 0 },
    { 6, 1, 1, 0 },
    { 7, 0, 0, 1 },
    { 8, 4, 3, 3 },
    { 13, 4, 3, 3 },
    { 14, 4, 3, 3 },
    { 15, 1, 2, 0 },
    { 16, 1, 0, 0 },
    { 19, 0, 0, 1 },
    { 20, 4, 3, 3 },
    { 25, 2, 3, 3 },
    { 27, 0, 0, 1 },
    { 28, 0, 1, 0 },
    { 29, 2, 1, 3 },
    { 30, 1, 0, 0 },
    { 31, 1, 0, 0 },
    { 33, 1, 3, 3 },
    { 35, 4, 3, 3 },
    { 36, 1, 0, 1 },
    { 37, 1, 0, 1 },
    { 38, 1, 3, 0 },
    { 39, 1, 0, 0 },
    { 41, 0, 0, 1 },
};

static const WorldUefaCountry kCountries[] = {
    { 0, { 164, 164, 164, 164, 164 } },
    { 1, { 2016, 2016, 2016, 2016, 2016 } },
    { 2, { 4172, 4172, 4172, 4172, 4172 } },
    { 3, { 938, 938, 938, 938, 938 } },
    { 4, { 938, 938, 938, 938, 938 } },
    { 5, { 328, 328, 328, 328, 328 } },
    { 6, { 1969, 1969, 1969, 1969, 1969 } },
    { 7, { 938, 938, 938, 938, 938 } },
    { 8, { 13711, 13711, 13711, 13711, 13711 } },
    { 10, { 94, 94, 94, 94, 94 } },
    { 11, { 94, 94, 94, 94, 94 } },
    { 12, { 445, 445, 445, 445, 445 } },
    { 13, { 4570, 4570, 4570, 4570, 4570 } },
    { 14, { 8133, 8133, 8133, 8133, 8133 } },
    { 15, { 1523, 1523, 1523, 1523, 1523 } },
    { 16, { 1102, 1102, 1102, 1102, 1102 } },
    { 17, { 211, 211, 211, 211, 211 } },
    { 18, { 141, 141, 141, 141, 141 } },
    { 19, { 680, 680, 680, 680, 680 } },
    { 20, { 15070, 15070, 15070, 15070, 15070 } },
    { 21, { 117, 117, 117, 117, 117 } },
    { 22, { 188, 188, 188, 188, 188 } },
    { 23, { 164, 164, 164, 164, 164 } },
    { 24, { 141, 141, 141, 141, 141 } },
    { 25, { 6727, 6727, 6727, 6727, 6727 } },
    { 26, { 188, 188, 188, 188, 188 } },
    { 27, { 750, 750, 750, 750, 750 } },
    { 28, { 984, 984, 984, 984, 984 } },
    { 29, { 5555, 5555, 5555, 5555, 5555 } },
    { 30, { 1406, 1406, 1406, 1406, 1406 } },
    { 31, { 1242, 1242, 1242, 1242, 1242 } },
    { 32, { 94, 94, 94, 94, 94 } },
    { 33, { 5109, 5109, 5109, 5109, 5109 } },
    { 34, { 492, 492, 492, 492, 492 } },
    { 35, { 12492, 12492, 12492, 12492, 12492 } },
    { 36, { 1125, 1125, 1125, 1125, 1125 } },
    { 37, { 1102, 1102, 1102, 1102, 1102 } },
    { 38, { 1688, 1688, 1688, 1688, 1688 } },
    { 39, { 938, 938, 938, 938, 938 } },
    { 40, { 94, 94, 94, 94, 94 } },
    { 41, { 844, 844, 844, 844, 844 } },
    { 76, { 328, 328, 328, 328, 328 } },
    { 78, { 656, 656, 656, 656, 656 } },
};

/* each association's ten strongest clubs, and every club of the tiers */
static const WorldUefaClub kClubs[] = {
    { 20, 0, 236 },   /* AC MILAN */
    { 35, 8, 198 },   /* BARCELONA */
    { 35, 35, 186 },   /* REAL MADRID */
    { 8, 20, 169 },   /* MANCHESTER UTD */
    { 20, 22, 168 },   /* JUVENTUS */
    { 8, 22, 166 },   /* NEWCASTLE UNITED */
    { 20, 31, 137 },   /* PARMA */
    { 8, 18, 134 },   /* LIVERPOOL */
    { 25, 0, 128 },   /* AJAX */
    { 14, 4, 120 },   /* BOR. DORTMUND */
    { 14, 3, 119 },   /* BAYERN MUNICH */
    { 8, 1, 116 },   /* ASTON VILLA */
    { 8, 4, 114 },   /* BLACKBURN ROVERS */
    { 8, 0, 109 },   /* ARSENAL */
    { 33, 33, 107 },   /* RANGERS */
    { 20, 23, 102 },   /* LAZIO */
    { 20, 21, 101 },   /* INTER MILAN */
    { 8, 12, 96 },   /* EVERTON */
    { 20, 40, 93 },   /* ROMA */
    { 29, 12, 90 },   /* FC PORTO */
    { 8, 38, 87 },   /* TOTTENHAM H. */
    { 8, 8, 87 },   /* CHELSEA */
    { 35, 16, 82 },   /* DEP. LA CORUNA */
    { 8, 16, 77 },   /* LEEDS UNITED */
    { 13, 30, 75 },   /* PARIS ST-GERMAIN */
    { 29, 20, 74 },   /* SPORTING LISBON */
    { 2, 0, 73 },   /* ANDERLECHT */
    { 25, 21, 71 },   /* PSV */
    { 20, 18, 70 },   /* FIORENTINA */
    { 35, 44, 67 },   /* VALENCIA */
    { 35, 6, 67 },   /* ATLETICO MADRID */
    { 25, 10, 64 },   /* FEYENOORD */
    { 33, 9, 59 },   /* CELTIC */
    { 14, 39, 59 },   /* VFB STUTTGART */
    { 29, 2, 57 },   /* BENFICA */
    { 20, 42, 54 },   /* SAMPDORIA */
    { 35, 10, 51 },   /* BETIS */
    { 20, 28, 50 },   /* NAPOLI */
    { 14, 44, 49 },   /* WERDER BREMEN */
    { 14, 23, 48 },   /* KARLSRUHE */
    { 14, 2, 48 },   /* BAYER LEVERKUSEN */
    { 35, 47, 48 },   /* ZARAGOZA */
    { 13, 23, 48 },   /* MONACO */
    { 14, 25, 45 },   /* MONCHENGLADBACH */
    { 2, 4, 45 },   /* CLUB BRUGES */
    { 35, 42, 44 },   /* TENERIFE */
    { 14, 17, 42 },   /* HAMBURG */
    { 35, 41, 40 },   /* SPORTING GIJON */
    { 35, 4, 39 },   /* ATHLETIC BILBAO */
    { 2, 17, 38 },   /* STANDARD LIEGE */
    { 13, 1, 38 },   /* AUXERRE */
    { 14, 7, 37 },   /* COLOGNE */
    { 20, 47, 35 },   /* UDINESE */
    { 14, 22, 34 },   /* KAISERSLAUTERN */
    { 6, 8, 34 },   /* SLAVIA PRAGUE */
    { 13, 19, 34 },   /* LYON */
    { 31, 16, 34 },   /* SPARTAK MOSCOW */
    { 1, 5, 33 },   /* RAPID VIENNA */
    { 30, 18, 32 },   /* STEAUA BUCHAREST */
    { 33, 0, 32 },   /* ABERDEEN */
    { 6, 10, 31 },   /* SPARTA PRAGUE */
    { 13, 27, 29 },   /* NANTES */
    { 13, 4, 29 },   /* BORDEAUX */
    { 13, 20, 28 },   /* MARSEILLE */
    { 13, 37, 28 },   /* STRASBOURG */
    { 13, 33, 26 },   /* RENNES */
    { 25, 31, 24 },   /* VITESSE */
    { 2, 1, 22 },   /* ANTWERP */
    { 13, 5, 22 },   /* CAEN */
    { 33, 15, 20 },   /* DUNDEE UNITED */
    { 25, 28, 19 },   /* TWENTE */
    { 15, 17, 19 },   /* OLYMPIAKOS */
    { 38, 9, 18 },   /* FENERBAHCE */
    { 15, 0, 18 },   /* AEK ATHENS */
    { 25, 24, 18 },   /* RODA JC */
    { 36, 8, 18 },   /* IFK GOTHENBURG */
    { 1, 8, 18 },   /* SV SALZBURG */
    { 16, 5, 18 },   /* FERENCVAROS */
    { 1, 9, 18 },   /* TIROL INNSBRUCK */
    { 38, 10, 18 },   /* GALATASARAY */
    { 15, 19, 18 },   /* PANATHINAIKOS */
    { 38, 21, 18 },   /* TRABZONSPOR */
    { 38, 5, 18 },   /* BESIKTAS */
    { 33, 24, 17 },   /* HIBERNIAN */
    { 37, 5, 17 },   /* GRASSHOPPERS */
    { 2, 3, 17 },   /* CHARLEROI */
    { 2, 7, 17 },   /* GENT */
    { 28, 7, 17 },   /* LEGIA WARSAW */
    { 1, 7, 17 },   /* STURM GRAZ */
    { 39, 2, 17 },   /* DINAMO KIEV */
    { 4, 2, 16 },   /* HAJDUK SPLIT */
    { 25, 18, 16 },   /* MVV */
    { 2, 8, 16 },   /* GERMINAL EKEREN */
    { 33, 23, 16 },   /* HEARTS */
    { 2, 10, 16 },   /* KV MECHELEN */
    { 29, 3, 16 },   /* BOAVISTA */
    { 78, 8, 16 },   /* SLOVAN BR'SLAVA */
    { 1, 0, 16 },   /* AUSTRIA VIENNA */
    { 27, 11, 16 },   /* ROSENBORG */
    { 25, 34, 15 },   /* WILLEM II */
    { 25, 29, 15 },   /* UTRECHT */
    { 25, 13, 15 },   /* GRONINGEN */
    { 2, 15, 15 },   /* RWD MOLENBEEK */
    { 2, 2, 14 },   /* CERCLE BRUGES */
    { 33, 28, 11 },   /* MOTHERWELL */
    { 29, 24, 11 },   /* VIT. GUIMARAES */
    { 41, 7, 11 },   /* RED STAR B'GRADE */
    { 36, 15, 11 },   /* OSTER */
    { 1, 4, 11 },   /* NIEDEROSTERREICH */
    { 33, 29, 10 },   /* PARTICK THISTLE */
    { 41, 8, 10 },   /* VOJVODINA N. SAD */
    { 7, 3, 10 },   /* BRONDBY */
    { 37, 10, 10 },   /* NEUCHATEL XAMAX */
    { 3, 1, 10 },   /* BOTEV PLOVDIV */
    { 41, 3, 10 },   /* PARTIZAN B'GRADE */
    { 7, 0, 10 },   /* AAB */
    { 33, 19, 10 },   /* FALKIRK */
    { 7, 9, 10 },   /* SILKEBORG */
    { 3, 14, 10 },   /* SLAVIA SOFIA */
    { 7, 2, 10 },   /* AGF */
    { 7, 8, 10 },   /* OB */
    { 37, 0, 10 },   /* AARAU */
    { 37, 9, 10 },   /* LUZERN */
    { 7, 4, 10 },   /* FC COPENHAGEN */
    { 29, 1, 10 },   /* BELENENSES */
    { 3, 9, 10 },   /* LOKOMOTIV SOFIA */
    { 37, 11, 10 },   /* SERVETTE */
    { 3, 2, 10 },   /* CSKA SOFIA */
    { 29, 16, 10 },   /* MARITIMO */
    { 6, 7, 10 },   /* SIGMA OLOMOUC */
    { 29, 11, 10 },   /* FARENSE */
    { 36, 0, 10 },   /* AIK */
    { 16, 8, 10 },   /* KISPEST-HONVED */
    { 16, 1, 10 },   /* BVSC */
    { 28, 2, 10 },   /* GKS KATOWICE */
    { 30, 20, 10 },   /* UNIV. CRAIOVA */
    { 30, 15, 10 },   /* RAPID BUCHAREST */
    { 15, 22, 10 },   /* PAOK */
    { 4, 1, 10 },   /* CROATIA ZAGREB */
    { 19, 10, 10 },   /* MACCABI HAIFA */
    { 19, 14, 10 },   /* MACCABI TEL-AVIV */
    { 3, 6, 9 },   /* LEVSKI SOFIA */
    { 37, 8, 9 },   /* LUGANO */
    { 15, 16, 9 },   /* OFI CRETE */
    { 37, 12, 9 },   /* SION */
    { 7, 7, 9 },   /* LYNGBY */
    { 34, 11, 9 },   /* OLIMPIJA L'JANA */
    { 36, 12, 9 },   /* MALMO */
    { 6, 0, 9 },   /* BANIK OSTRAVA */
    { 36, 9, 9 },   /* IFK NORRKOPING */
    { 28, 23, 9 },   /* WIDZEW LODZ */
    { 39, 3, 9 },   /* DNEPROPETROVSK */
    { 31, 3, 9 },   /* DINAMO MOSCOW */
    { 1, 2, 9 },   /* GRAZER AK */
    { 16, 9, 9 },   /* MTK */
    { 76, 2, 9 },   /* DINAMO MINSK */
    { 39, 1, 9 },   /* CHERNOMORETS */
    { 15, 1, 9 },   /* APOLLON ATHENS */
    { 33, 25, 9 },   /* KILMARNOCK */
    { 4, 7, 9 },   /* OSIJEK */
    { 30, 5, 8 },   /* DINAMO BUCHAREST */
    { 3, 13, 6 },   /* SHUMEN */
    { 29, 19, 6 },   /* SALGUEIROS */
    { 28, 3, 6 },   /* GORNIK ZABRZE */
    { 6, 6, 6 },   /* PETRA DRNOVICE */
    { 27, 20, 6 },   /* VIKING */
    { 19, 0, 5 },   /* BEITAR JERUSALEM */
    { 7, 1, 5 },   /* AB */
    { 27, 6, 5 },   /* LILLESTROM */
    { 37, 1, 5 },   /* BASEL */
    { 3, 8, 5 },   /* LOK. PLOVDIV */
    { 7, 5, 5 },   /* HERFOLGE */
    { 34, 7, 5 },   /* MARIBOR BRANIK */
    { 27, 1, 5 },   /* BRANN */
    { 15, 2, 5 },   /* ARIS */
    { 34, 9, 5 },   /* MURA M. SOBOTA */
    { 15, 13, 5 },   /* LARISSA */
    { 37, 7, 5 },   /* LAUSANNE */
    { 28, 25, 5 },   /* ZAGLEBIE LUBIN */
    { 41, 0, 5 },   /* BUDUCNOST */
    { 78, 3, 5 },   /* DUNAJSKA STREDA */
    { 37, 4, 5 },   /* FC ZURICH */
    { 78, 5, 5 },   /* INTER BRATISLAVA */
    { 41, 4, 5 },   /* PROLETER */
    { 3, 4, 5 },   /* ETAR V. TARNOVO */
    { 28, 6, 5 },   /* LECH POZNAN */
    { 36, 3, 5 },   /* DJURGARDEN */
    { 28, 8, 5 },   /* LKS LODZ */
    { 29, 0, 5 },   /* BEIRA MAR */
    { 36, 5, 5 },   /* HALMSTAD */
    { 36, 14, 5 },   /* ORGRYTE */
    { 36, 7, 5 },   /* HELSINGBORG */
    { 6, 2, 5 },   /* CESKE BUDEJOVICE */
    { 5, 3, 5 },   /* ANORTHOSIS */
    { 17, 5, 5 },   /* IA */
    { 28, 14, 5 },   /* RUCH CHORZOW */
    { 16, 6, 5 },   /* GYOR */
    { 6, 13, 5 },   /* UNION CHEB */
    { 16, 17, 5 },   /* VAC */
    { 31, 17, 5 },   /* SPARTAK VLAD. */
    { 36, 13, 5 },   /* OREBRO */
    { 1, 3, 5 },   /* LASK LINZ */
    { 31, 1, 5 },   /* CSKA MOSCOW */
    { 31, 10, 5 },   /* LOKOMOTIV MOSCOW */
    { 31, 19, 5 },   /* TORPEDO MOSCOW */
    { 4, 11, 5 },   /* VARTEKS VARAZDIN */
    { 12, 8, 5 },   /* MYPA */
    { 12, 1, 5 },   /* FINNPA */
    { 4, 6, 5 },   /* NK ZAGREB */
    { 5, 4, 5 },   /* APOEL NICOSIA */
    { 16, 16, 5 },   /* UJPESTI TE */
    { 39, 11, 5 },   /* SHAKHTYOR */
    { 38, 13, 5 },   /* ISTANBULSPOR */
    { 30, 8, 5 },   /* GLORIA BISTRITA */
    { 39, 4, 5 },   /* KARPATY LVOV */
    { 31, 18, 5 },   /* TEKSTILSCHIK */
    { 38, 17, 5 },   /* KOCAELISPOR */
    { 30, 12, 5 },   /* PETROLUL */
    { 1, 6, 5 },   /* RIED IM INNKREIS */
    { 15, 8, 5 },   /* IRAKLIS */
    { 12, 0, 5 },   /* FC JAZZ */
    { 27, 9, 5 },   /* MOLDE */
    { 7, 6, 5 },   /* HVIDOVRE */
    { 30, 7, 5 },   /* FARUL CONSTANTA */
    { 1, 1, 5 },   /* FC LINZ */
    { 22, 10, 4 },   /* ZALGIRIS VILNIUS */
    { 41, 9, 4 },   /* ZEMUN */
    { 12, 3, 4 },   /* HJK */
    { 27, 0, 4 },   /* BODO GLIMT */
    { 19, 5, 4 },   /* HAPOEL HAIFA */
    { 19, 2, 3 },   /* BNEI YEHUDA */
    { 6, 12, 3 },   /* UHERSKE HRADISTE */
    { 6, 5, 3 },   /* KAUCUK OPAVA */
    { 28, 19, 3 },   /* STALOWA WOLA */
    { 28, 0, 3 },   /* AMICA WRONKI */
    { 19, 7, 3 },   /* HAPOEL P-TIKVA */
    { 15, 3, 3 },   /* ATHINAIKOS */
    { 26, 12, 3 },   /* LINFIELD */
    { 12, 4, 2 },   /* ILVES */
    { 4, 0, 2 },   /* CIBALIA VINKOVCI */
    { 30, 0, 2 },   /* ARGES PITESTI */
    { 76, 6, 2 },   /* KIM VITEBSK */
    { 18, 10, 2 },   /* SHAMROCK ROVERS */
    { 30, 2, 2 },   /* CEAHLAUL P-N */
    { 38, 0, 2 },   /* ALTAY */
    { 18, 3, 2 },   /* CORK CITY */
    { 34, 5, 2 },   /* KOPER */
    { 41, 1, 2 },   /* HAJDUK KULA */
    { 0, 9, 2 },   /* PARTIZANI TIRANA */
    { 38, 2, 2 },   /* ANTALYASPOR */
    { 26, 9, 2 },   /* GLENAVON */
    { 41, 5, 2 },   /* RAD BELGRADE */
    { 3, 7, 2 },   /* LOK. GORNA ORIA. */
    { 78, 10, 2 },   /* TATRAN PRESOV */
    { 3, 0, 2 },   /* BEROE S. ZAGORA */
    { 41, 2, 2 },   /* NAPREDAK */
    { 22, 6, 2 },   /* ROMAR MAZEIKIAI */
    { 41, 6, 2 },   /* RADNICKI NIS */
    { 78, 0, 2 },   /* 1.FC KOSICE */
    { 17, 8, 2 },   /* KR */
    { 24, 13, 2 },   /* VALLETTA */
    { 78, 11, 2 },   /* ZILINA */
    { 21, 7, 2 },   /* SKONTO RIGA */
    { 5, 12, 2 },   /* OMONIA NICOSIA */
    { 5, 5, 2 },   /* APOLLON LIMASSOL */
    { 24, 3, 2 },   /* HIBERNIANS */
    { 39, 12, 2 },   /* TAVRIA */
    { 12, 6, 2 },   /* JARO */
    { 6, 1, 2 },   /* BOBY BRNO */
    { 16, 3, 2 },   /* DEBRECEN */
    { 39, 0, 2 },   /* BUKOVINA */
    { 39, 9, 2 },   /* NIVA TERNOPOL */
    { 23, 1, 2 },   /* AVENIR BEGGEN */
    { 12, 10, 2 },   /* TPS */
    { 27, 10, 2 },   /* MOSS */
    { 4, 8, 2 },   /* RIJEKA */
    { 27, 3, 2 },   /* HAMARKAMERATENE */
    { 19, 8, 2 },   /* HAPOEL TEL-AVIV */
    { 0, 4, 2 },   /* DINAMO TIRANA */
    { 16, 0, 2 },   /* BEKESCSABA */
    { 38, 6, 2 },   /* BURSASPOR */
    { 31, 0, 2 },   /* ASMARAL MOSCOW */
    { 31, 9, 2 },   /* LOK. N. NOVGOROD */
    { 12, 5, 2 },   /* INTER TURKU */
    { 27, 5, 2 },   /* KONGSVINGER */
    { 30, 1, 2 },   /* BRASOV */
    { 16, 2, 2 },   /* CSEPEL */
    { 39, 15, 2 },   /* VERES RIVNE */
    { 26, 15, 2 },   /* PORTADOWN */
    { 23, 7, 2 },   /* JEUNESSE ESCH */
    { 4, 3, 2 },   /* INKER ZAPRESIC */
    { 34, 13, 2 },   /* POTROSNIK */
    { 27, 7, 2 },   /* LYN */
    { 19, 3, 2 },   /* HAPOEL BEERSHEBA */
    { 76, 0, 2 },   /* DINAMO 93 MINSK */
    { 38, 1, 2 },   /* ANKARAGUCU */
    { 12, 9, 2 },   /* ROPS */
    { 4, 5, 2 },   /* MARSONIA S. BROD */
    { 34, 15, 2 },   /* PUBLIKUM CELJE */
    { 31, 6, 2 },   /* KAMAZ */
    { 12, 2, 2 },   /* HAKA */
    { 23, 2, 2 },   /* CS GREVENMACHER */
    { 0, 12, 2 },   /* SK TIRANA */
    { 23, 4, 1 },   /* ETZELLA */
    { 34, 1, 1 },   /* ELEKTROELEMENT */
    { 0, 5, 1 },   /* ELBASANI */
    { 11, 5, 1 },   /* IF */
    { 26, 5, 1 },   /* CLIFTONVILLE */
    { 18, 1, 1 },   /* BRAY WANDERERS */
    { 34, 3, 1 },   /* ISTRAGAS JADRAN */
    { 0, 7, 1 },   /* ILIRIA F-KRUJE */
    { 11, 7, 1 },   /* LIF */
    { 76, 8, 1 },   /* MOLDECHNO */
    { 22, 3, 1 },   /* GELEZINIS VILKAS */
    { 19, 4, 1 },   /* HAPOEL BEIT SHAN */
    { 0, 0, 1 },   /* ALBPETROL PATOS */
    { 11, 0, 1 },   /* B 36 */
    { 76, 1, 1 },   /* DINAMO BREST */
    { 11, 9, 1 },   /* SIF */
    { 26, 0, 1 },   /* ARDS */
    { 18, 5, 1 },   /* DROGHEDA UNITED */
    { 22, 5, 1 },   /* PANERYS VILNIUS */
    { 0, 2, 1 },   /* BESA KAVAJE */
    { 11, 2, 1 },   /* B 71 */
    { 76, 3, 1 },   /* DNEPR MOGILEV */
    { 26, 2, 1 },   /* BALLYMENA UNITED */
    { 18, 7, 1 },   /* GALWAY UNITED */
    { 34, 0, 1 },   /* AM COSMOS */
    { 22, 7, 1 },   /* SIAULIAI-SAKALAS */
    { 11, 4, 1 },   /* HB */
    { 10, 8, 1 },   /* NORMA TALLINN */
    { 26, 4, 1 },   /* CARRICK RANGERS */
    { 18, 0, 1 },   /* BOHEMIANS */
    { 22, 0, 1 },   /* ARAS KLAIPEDA */
    { 34, 2, 1 },   /* HIT GORICA */
    { 10, 1, 1 },   /* EP JOHVI */
    { 26, 6, 1 },   /* COLERAINE */
    { 18, 2, 1 },   /* COBH RAMBLERS */
    { 40, 8, 1 },   /* CWMBRAN TOWN */
    { 22, 2, 1 },   /* FBK KAUNAS */
    { 10, 3, 1 },   /* FLORA TALLINN */
    { 18, 4, 1 },   /* DERRY CITY */
    { 40, 1, 1 },   /* ABERYSTWYTH */
    { 22, 4, 1 },   /* INKARAS KAUNAS */
    { 21, 6, 1 },   /* RAF YELGAVA */
    { 10, 5, 1 },   /* KEEMIK K-J */
    { 26, 1, 1 },   /* BALLYCLARE C. */
    { 40, 3, 1 },   /* BANGOR CITY */
    { 21, 8, 1 },   /* VAIROGS REZEKNE */
    { 10, 7, 1 },   /* NIKOL TALLINN */
    { 40, 5, 1 },   /* CAERSWS */
    { 21, 1, 1 },   /* FC DAG RIGA */
    { 22, 8, 1 },   /* SIRIJUS KLAIPEDA */
    { 17, 6, 1 },   /* IBV */
    { 10, 0, 1 },   /* DUNAMO TALLINN */
    { 10, 9, 1 },   /* TALLINNA SADAM */
    { 40, 7, 1 },   /* CONWY UNITED */
    { 22, 1, 1 },   /* EKRANAS PANEVEZS */
    { 21, 3, 1 },   /* OLYMPIA LIEPAYA */
    { 10, 2, 1 },   /* ESDAG TARTU */
    { 32, 8, 1 },   /* MURATA */
    { 40, 0, 1 },   /* ABERGAVENNY */
    { 40, 9, 1 },   /* EBBW VALE */
    { 21, 5, 1 },   /* PARDAUGAVA RIGA */
    { 78, 2, 1 },   /* CHEMLON HUMENNE */
    { 17, 1, 1 },   /* FH */
    { 5, 8, 1 },   /* EVAGORAS PAPHOS */
    { 10, 4, 1 },   /* KALEV SILLAMAE */
    { 40, 2, 1 },   /* AFAN LIDO */
    { 78, 4, 1 },   /* FC NITRA */
    { 5, 1, 1 },   /* AEL LIMASSOL */
    { 17, 3, 1 },   /* FYLKIR */
    { 10, 6, 1 },   /* MERKUUR TARTU */
    { 32, 3, 1 },   /* FAETANO */
    { 24, 8, 1 },   /* RABAT AJAX */
    { 40, 4, 1 },   /* BRITON FERRY */
    { 21, 0, 1 },   /* AUSEKLIS */
    { 21, 9, 1 },   /* VIDUS RIGA */
    { 78, 6, 1 },   /* LOK. KOSICE */
    { 32, 5, 1 },   /* JUVENES */
    { 24, 1, 1 },   /* FLORIANA */
    { 40, 6, 1 },   /* CONNAH'S QUAY */
    { 21, 2, 1 },   /* GAUYA VALMIERA */
    { 17, 7, 1 },   /* KEFLAVIK */
    { 32, 7, 1 },   /* MONTEVITO */
    { 21, 4, 1 },   /* OLYMPIA RIGA */
    { 78, 1, 1 },   /* BANSKA BYSTRICA */
    { 17, 0, 1 },   /* BREIDABLIK */
    { 5, 7, 1 },   /* ETHNIKOS AKHNAS */
    { 17, 9, 1 },   /* LEIFTUR */
    { 32, 0, 1 },   /* CAILUNGO */
    { 32, 9, 1 },   /* TRE FIORI */
    { 24, 5, 1 },   /* MQABBA HAJDUKS */
    { 39, 5, 1 },   /* KREMENCHUK */
    { 23, 6, 1 },   /* FOLA ESCH */
    { 5, 0, 1 },   /* AEK LARNACA */
    { 17, 2, 1 },   /* FRAM */
    { 32, 2, 1 },   /* DOMAGNANO */
    { 24, 7, 1 },   /* PIETA HOTSPURS */
    { 23, 8, 1 },   /* RED BOYS D'DANGE */
    { 4, 4, 1 },   /* ISTRA PULA */
    { 5, 2, 1 },   /* ALKI LARNACA */
    { 17, 4, 1 },   /* GRINDAVIK */
    { 32, 4, 1 },   /* FOLGORE */
    { 24, 0, 1 },   /* BIRKIRKARA LUXOL */
    { 32, 6, 1 },   /* LIBERTAS */
    { 24, 2, 1 },   /* HAMRUN SPARTANS */
    { 23, 3, 1 },   /* CS PETANGE */
    { 5, 6, 1 },   /* ARIS LIMASSOL */
    { 24, 4, 1 },   /* MELLIEHA */
    { 76, 5, 1 },   /* GOMSELMASH GOMEL */
    { 23, 5, 1 },   /* F91 DUDELANGE */
    { 32, 1, 1 },   /* COSMOS */
    { 19, 1, 1 },   /* BEITAR TEL-AVIV */
    { 0, 6, 1 },   /* FLAMURTARI VLORE */
    { 11, 6, 1 },   /* KI */
    { 24, 6, 1 },   /* NAXXAR LIONS */
    { 76, 7, 1 },   /* LOK VITEBSK */
    { 11, 8, 1 },   /* NSI */
    { 76, 9, 1 },   /* NEMAN GRODNO */
    { 23, 0, 1 },   /* ARIS BONNEVOIE */
    { 23, 9, 1 },   /* RODANGE 91 */
    { 0, 1, 1 },   /* APOLONIA FIER */
    { 11, 1, 1 },   /* B 68 */
    { 18, 6, 1 },   /* DUNDALK */
    { 0, 3, 1 },   /* BESELIDHJA LEZHE */
    { 11, 3, 1 },   /* GI */
    { 76, 4, 1 },   /* FANDOK BOBRUISK */
    { 26, 3, 1 },   /* BANGOR */
    { 18, 8, 1 },   /* LIMERICK */
};
