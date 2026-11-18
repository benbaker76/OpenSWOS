#!/usr/bin/env python3
"""worldgen/nations.py -- World 2026-27's national teams: all 211 FIFA members (docs/WORLD_DATAGEN.md §15).

    python3 -I scripts/worldgen/nations.py [--world bin/data_json/worlds/2627] [--offline] [--report FILE]

Each nation's squad is its English Wikipedia article's current squad ({{nat fs g player}}: the
latest call-up, with number, GK/DF/MF/FW, caps and club), each player matched to the same man in
the world's own clubs (its teams/*.json: his record there -- skills, value, position, face -- as
the club has him). A player the world's clubs do not hold gets a level from his club when the
world has it, else from his nation's FIFA ranking points (Wikipedia's Module:SportsRankings data,
fitted against the nations whose players are matched), and his sub-position from his own
Wikipedia article's infobox (the squad lists only GK/DF/MF/FW: the infobox says right-back, left
winger ...). The 16 are picked and placed as the clubs' (model.pick_sixteen). The coach is the
article infobox's.

The 143 national teams of 96/97 keep their place (file 80..85 and ordinal: competitions and saves
name them so) and their kits and tactics; the 68 nations 96/97 does not have go after them in their
confederation's file (Australia stays in Oceania's, Mauritius and Papua New Guinea in Asia's, as
96/97 has them). Writes, into the world's folder (gitignored: real people's data):
  teams/EUROPE.json AFRICA.json SOUTH_AMERICA.json NORTH_AMERICA.json ASIA.json OCEANIA.json
  national_teams.json   the national teams' records (include/national_teams.h), all 211
The Amiga's TEAM.080-085 are not written (its global team numbers are baked: §15).
"""
import argparse
import collections
import json
import math
import os
import re
import statistics
import sys
import uuid
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import model                                            # noqa: E402
import modport                                          # noqa: E402
import wiki                                             # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))
BASE = os.path.join(REPO, 'bin', 'data_json')
NAMESPACE = uuid.UUID('af19cc5a-6529-537b-935b-407bd73b75fb')   # assign_team_uuids.py's, world_import's
TAG = '2627'

FILES = {80: 'EUROPE', 81: 'AFRICA', 82: 'SOUTH_AMERICA', 83: 'NORTH_AMERICA', 84: 'ASIA', 85: 'OCEANIA'}
CONF_FILE = {'UEFA': 80, 'CAF': 81, 'CONMEBOL': 82, 'CONCACAF': 83, 'AFC': 84, 'OFC': 85}

# FIFA code: (confederation, Wikipedia's name, the game's team name, nationality number,
#             96/97's team name of its slot or None). Nationalities: the game's codes
# (_shortCountryNames) as the world's clubs use them; 214..222 are new (NEW_NATIONALITIES).
NATIONS = [
    # UEFA (55)
    ('ALB', 'UEFA', 'Albania', 'ALBANIA', 0, 'ALBANIA'), ('AND', 'UEFA', 'Andorra', 'ANDORRA', 158, None),
    ('ARM', 'UEFA', 'Armenia', 'ARMENIA', 41, 'ARMENIA'), ('AUT', 'UEFA', 'Austria', 'AUSTRIA', 1, 'AUSTRIA'),
    ('AZE', 'UEFA', 'Azerbaijan', 'AZERBAIJAN', 43, 'AZERBAIJAN'), ('BLR', 'UEFA', 'Belarus', 'BELARUS', 38, 'BELARUS'),
    ('BEL', 'UEFA', 'Belgium', 'BELGIUM', 2, 'BELGIUM'),
    ('BIH', 'UEFA', 'Bosnia and Herzegovina', 'BOSNIA-HERZ.', 42, None),
    ('BUL', 'UEFA', 'Bulgaria', 'BULGARIA', 3, 'BULGARIA'), ('CRO', 'UEFA', 'Croatia', 'CROATIA', 4, 'CROATIA'),
    ('CYP', 'UEFA', 'Cyprus', 'CYPRUS', 5, 'CYPRUS'), ('CZE', 'UEFA', 'Czech Republic', 'CZECHIA', 6, 'CZECH REPUBLIC'),
    ('DEN', 'UEFA', 'Denmark', 'DENMARK', 7, 'DENMARK'), ('ENG', 'UEFA', 'England', 'ENGLAND', 8, 'ENGLAND'),
    ('EST', 'UEFA', 'Estonia', 'ESTONIA', 9, 'ESTONIA'), ('FRO', 'UEFA', 'Faroe Islands', 'FAROE ISLES', 10, 'FAROE ISLES'),
    ('FIN', 'UEFA', 'Finland', 'FINLAND', 11, 'FINLAND'), ('FRA', 'UEFA', 'France', 'FRANCE', 12, 'FRANCE'),
    ('GEO', 'UEFA', 'Georgia', 'GEORGIA', 44, 'GEORGIA'), ('GER', 'UEFA', 'Germany', 'GERMANY', 13, 'GERMANY'),
    ('GIB', 'UEFA', 'Gibraltar', 'GIBRALTAR', 159, None), ('GRE', 'UEFA', 'Greece', 'GREECE', 14, 'GREECE'),
    ('HUN', 'UEFA', 'Hungary', 'HUNGARY', 15, 'HUNGARY'), ('ISL', 'UEFA', 'Iceland', 'ICELAND', 16, 'ICELAND'),
    ('ISR', 'UEFA', 'Israel', 'ISRAEL', 17, 'ISRAEL'), ('ITA', 'UEFA', 'Italy', 'ITALY', 18, 'ITALY'),
    ('KAZ', 'UEFA', 'Kazakhstan', 'KAZAKHSTAN', 160, None), ('KOS', 'UEFA', 'Kosovo', 'KOSOVO', 157, None),
    ('LVA', 'UEFA', 'Latvia', 'LATVIA', 19, 'LATVIA'), ('LIE', 'UEFA', 'Liechtenstein', 'LIECHTENSTEIN', 49, 'LIECHTENSTEIN'),
    ('LTU', 'UEFA', 'Lithuania', 'LITHUANIA', 20, 'LITHUANIA'), ('LUX', 'UEFA', 'Luxembourg', 'LUXEMBOURG', 21, 'LUXEMBOURG'),
    ('MLT', 'UEFA', 'Malta', 'MALTA', 22, 'MALTA'), ('MDA', 'UEFA', 'Moldova', 'MOLDOVA', 50, 'MOLDOVA'),
    ('MNE', 'UEFA', 'Montenegro', 'MONTENEGRO', 156, None), ('NED', 'UEFA', 'Netherlands', 'HOLLAND', 23, 'HOLLAND'),
    ('NIR', 'UEFA', 'Northern Ireland', 'NORTHERN IRELAND', 24, 'NORTHERN IRELAND'),
    ('MKD', 'UEFA', 'North Macedonia', 'NORTH MACEDONIA', 47, 'MACEDONIA'),
    ('NOR', 'UEFA', 'Norway', 'NORWAY', 25, 'NORWAY'), ('POL', 'UEFA', 'Poland', 'POLAND', 26, 'POLAND'),
    ('POR', 'UEFA', 'Portugal', 'PORTUGAL', 27, 'PORTUGAL'),
    ('IRL', 'UEFA', 'Republic of Ireland', 'REPUBLIC IRELAND', 46, 'REPUBLIC IRELAND'),
    ('ROU', 'UEFA', 'Romania', 'ROMANIA', 28, 'ROMANIA'), ('RUS', 'UEFA', 'Russia', 'RUSSIA', 29, 'RUSSIA'),
    ('SMR', 'UEFA', 'San Marino', 'SAN MARINO', 30, 'SAN MARINO'), ('SCO', 'UEFA', 'Scotland', 'SCOTLAND', 31, 'SCOTLAND'),
    ('SRB', 'UEFA', 'Serbia', 'SERBIA', 37, 'YUGOSLAVIA'), ('SVK', 'UEFA', 'Slovakia', 'SLOVAKIA', 39, 'SLOVAKIA'),
    ('SVN', 'UEFA', 'Slovenia', 'SLOVENIA', 32, 'SLOVENIA'), ('ESP', 'UEFA', 'Spain', 'SPAIN', 40, 'SPAIN'),
    ('SWE', 'UEFA', 'Sweden', 'SWEDEN', 33, 'SWEDEN'), ('SUI', 'UEFA', 'Switzerland', 'SWITZERLAND', 45, 'SWITZERLAND'),
    ('TUR', 'UEFA', 'Türkiye', 'TURKEY', 34, 'TURKEY'), ('UKR', 'UEFA', 'Ukraine', 'UKRAINE', 35, 'UKRAINE'),
    ('WAL', 'UEFA', 'Wales', 'WALES', 36, 'WALES'),
    # CAF (54: Mauritius in Asia's file, as 96/97)
    ('ALG', 'CAF', 'Algeria', 'ALGERIA', 80, 'ALGERIA'), ('ANG', 'CAF', 'Angola', 'ANGOLA', 102, 'ANGOLA'),
    ('BEN', 'CAF', 'Benin', 'BENIN', 111, 'BENIN'), ('BOT', 'CAF', 'Botswana', 'BOTSWANA', 82, 'BOTSWANA'),
    ('BFA', 'CAF', 'Burkina Faso', 'BURKINA FASO', 83, 'BURKINA FASO'), ('BDI', 'CAF', 'Burundi', 'BURUNDI', 84, 'BURUNDI'),
    ('CPV', 'CAF', 'Cape Verde', 'CAPE VERDE', 178, None), ('CMR', 'CAF', 'Cameroon', 'CAMEROON', 94, 'CAMEROON'),
    ('CTA', 'CAF', 'Central African Republic', 'CENTRAL AFRICA', 185, None), ('CHA', 'CAF', 'Chad', 'CHAD', 95, None),
    ('COM', 'CAF', 'Comoros', 'COMOROS', 184, None), ('CGO', 'CAF', 'Congo', 'CONGO', 112, 'CONGO'),
    ('COD', 'CAF', 'DR Congo', 'DR CONGO', 86, None), ('CIV', 'CAF', 'Ivory Coast', 'IVORY COAST', 90, 'IVORY COAST'),
    ('DJI', 'CAF', 'Djibouti', 'DJIBOUTI', 191, None), ('EGY', 'CAF', 'Egypt', 'EGYPT', 105, 'EGYPT'),
    ('EQG', 'CAF', 'Equatorial Guinea', 'EQUAT. GUINEA', 180, None), ('ERI', 'CAF', 'Eritrea', 'ERITREA', 187, None),
    ('SWZ', 'CAF', 'Eswatini', 'ESWATINI', 101, 'SWAZILAND'), ('ETH', 'CAF', 'Ethiopia', 'ETHIOPIA', 108, 'ETHIOPIA'),
    ('GAB', 'CAF', 'Gabon', 'GABON', 109, 'GABON'), ('GAM', 'CAF', 'Gambia', 'GAMBIA', 116, None),
    ('GHA', 'CAF', 'Ghana', 'GHANA', 88, 'GHANA'), ('GUI', 'CAF', 'Guinea', 'GUINEA', 113, 'GUINEA'),
    ('GNB', 'CAF', 'Guinea-Bissau', 'GUINEA-BISSAU', 179, None), ('KEN', 'CAF', 'Kenya', 'KENYA', 99, 'KENYA'),
    ('LES', 'CAF', 'Lesotho', 'LESOTHO', 85, 'LESOTHO'), ('LBR', 'CAF', 'Liberia', 'LIBERIA', 97, 'LIBERIA'),
    ('LBY', 'CAF', 'Libya', 'LIBYA', 190, None), ('MAD', 'CAF', 'Madagascar', 'MADAGASCAR', 93, None),
    ('MWI', 'CAF', 'Malawi', 'MALAWI', 117, 'MALAWI'), ('MLI', 'CAF', 'Mali', 'MALI', 92, 'MALI'),
    ('MTN', 'CAF', 'Mauritania', 'MAURITANIA', 181, None), ('MRI', 'CAF', 'Mauritius', 'MAURITIUS', 141, 'MAURITIUS'),
    ('MAR', 'CAF', 'Morocco', 'MOROCCO', 115, 'MOROCCO'), ('MOZ', 'CAF', 'Mozambique', 'MOZAMBIQUE', 98, 'MOZAMBIQUE'),
    ('NAM', 'CAF', 'Namibia', 'NAMIBIA', 189, None), ('NIG', 'CAF', 'Niger', 'NIGER', 182, 'NIGER'),
    ('NGA', 'CAF', 'Nigeria', 'NIGERIA', 107, 'NIGERIA'), ('RWA', 'CAF', 'Rwanda', 'RWANDA', 183, None),
    ('STP', 'CAF', 'São Tomé and Príncipe', 'SAO TOME', 192, None), ('SEN', 'CAF', 'Senegal', 'SENEGAL', 89, 'SENEGAL'),
    ('SEY', 'CAF', 'Seychelles', 'SEYCHELLES', 215, None), ('SLE', 'CAF', 'Sierra Leone', 'SIERRA LEONE', 110, 'SIERRA LEONE'),
    ('SOM', 'CAF', 'Somalia', 'SOMALIA', 188, None), ('RSA', 'CAF', 'South Africa', 'SOUTH AFRICA', 81, 'SOUTH AFRICA'),
    ('SSD', 'CAF', 'South Sudan', 'SOUTH SUDAN', 186, None), ('SDN', 'CAF', 'Sudan', 'SUDAN', 100, 'SUDAN'),
    ('TAN', 'CAF', 'Tanzania', 'TANZANIA', 106, 'TANZANIA'), ('TOG', 'CAF', 'Togo', 'TOGO', 103, 'TOGO'),
    ('TUN', 'CAF', 'Tunisia', 'TUNISIA', 91, 'TUNISIA'), ('UGA', 'CAF', 'Uganda', 'UGANDA', 96, 'UGANDA'),
    ('ZAM', 'CAF', 'Zambia', 'ZAMBIA', 87, 'ZAMBIA'), ('ZIM', 'CAF', 'Zimbabwe', 'ZIMBABWE', 104, 'ZIMBABWE'),
    # AFC (46: Australia in Oceania's file, as 96/97)
    ('AFG', 'AFC', 'Afghanistan', 'AFGHANISTAN', 202, None), ('AUS', 'AFC', 'Australia', 'AUSTRALIA', 148, 'AUSTRALIA'),
    ('BHR', 'AFC', 'Bahrain', 'BAHRAIN', 197, 'BAHRAIN'), ('BAN', 'AFC', 'Bangladesh', 'BANGLADESH', 121, 'BANGLADESH'),
    ('BHU', 'AFC', 'Bhutan', 'BHUTAN', 214, None), ('BRU', 'AFC', 'Brunei', 'BRUNEI', 122, 'BRUNEI'),
    ('CAM', 'AFC', 'Cambodia', 'CAMBODIA', 201, None), ('CHN', 'AFC', 'China', 'CHINA', 139, 'CHINA'),
    ('TPE', 'AFC', 'Chinese Taipei', 'TAIWAN', 119, 'TAIWAN'), ('GUM', 'AFC', 'Guam', 'GUAM', 208, None),
    ('HKG', 'AFC', 'Hong Kong, China', 'HONG KONG', 198, 'HONG KONG'), ('IND', 'AFC', 'India', 'INDIA', 120, 'INDIA'),
    ('IDN', 'AFC', 'Indonesia', 'INDONESIA', 200, 'INDONESIA'), ('IRQ', 'AFC', 'Iraq', 'IRAQ', 123, 'IRAQ'),
    ('IRN', 'AFC', 'Iran', 'IRAN', 128, 'IRAN'), ('JPN', 'AFC', 'Japan', 'JAPAN', 118, 'JAPAN'),
    ('JOR', 'AFC', 'Jordan', 'JORDAN', 124, 'JORDAN'), ('PRK', 'AFC', 'North Korea', 'NORTH KOREA', 135, 'NORTH KOREA'),
    ('KOR', 'AFC', 'South Korea', 'SOUTH KOREA', 127, 'SOUTH KOREA'), ('KUW', 'AFC', 'Kuwait', 'KUWAIT', 133, 'KUWAIT'),
    ('KGZ', 'AFC', 'Kyrgyzstan', 'KYRGYZSTAN', 161, None), ('LAO', 'AFC', 'Laos', 'LAOS', 134, 'LAOS'),
    ('LBN', 'AFC', 'Lebanon', 'LEBANON', 195, None), ('MAC', 'AFC', 'Macau', 'MACAU', 204, None),
    ('MAS', 'AFC', 'Malaysia', 'MALAYSIA', 130, 'MALAYSIA'), ('MDV', 'AFC', 'Maldives', 'MALDIVES', 206, None),
    ('MNG', 'AFC', 'Mongolia', 'MONGOLIA', 205, None), ('MYA', 'AFC', 'Myanmar', 'MYANMAR', 142, 'MYANMAR'),
    ('NEP', 'AFC', 'Nepal', 'NEPAL', 203, None), ('OMA', 'AFC', 'Oman', 'OMAN', 136, 'OMAN'),
    ('PAK', 'AFC', 'Pakistan', 'PAKISTAN', 137, 'PAKISTAN'), ('PLE', 'AFC', 'Palestine', 'PALESTINE', 196, None),
    ('PHI', 'AFC', 'Philippines', 'PHILIPPINES', 138, 'PHILIPPINES'), ('QAT', 'AFC', 'Qatar', 'QATAR', 146, 'QATAR'),
    ('KSA', 'AFC', 'Saudi Arabia', 'SAUDI ARABIA', 131, 'SAUDI ARABIA'), ('SGP', 'AFC', 'Singapore', 'SINGAPORE', 140, 'SINGAPORE'),
    ('SRI', 'AFC', 'Sri Lanka', 'SRI LANKA', 125, 'SRI LANKA'), ('SYR', 'AFC', 'Syria', 'SYRIA', 126, 'SYRIA'),
    ('TJK', 'AFC', 'Tajikistan', 'TAJIKISTAN', 144, None), ('THA', 'AFC', 'Thailand', 'THAILAND', 199, None),
    ('TLS', 'AFC', 'TLS', 'TIMOR-LESTE', 207, None), ('TKM', 'AFC', 'Turkmenistan', 'TURKMENISTAN', 48, None),
    ('UAE', 'AFC', 'United Arab Emirates', 'UNITED ARAB EMIS', 147, 'UNITED ARAB EMIS'),
    ('UZB', 'AFC', 'Uzbekistan', 'UZBEKISTAN', 145, None), ('VIE', 'AFC', 'Vietnam', 'VIETNAM', 129, 'VIETNAM'),
    ('YEM', 'AFC', 'Yemen', 'YEMEN', 132, 'YEMEN'),
    # CONCACAF (35)
    ('AIA', 'CONCACAF', 'Anguilla', 'ANGUILLA', 216, None), ('ATG', 'CONCACAF', 'Antigua and Barbuda', 'ANTIGUA-BARBUDA', 173, None),
    ('ARU', 'CONCACAF', 'Aruba', 'ARUBA', 165, None), ('BAH', 'CONCACAF', 'Bahamas', 'BAHAMAS', 55, None),
    ('BRB', 'CONCACAF', 'Barbados', 'BARBADOS', 65, None), ('BLZ', 'CONCACAF', 'Belize', 'BELIZE', 162, None),
    ('BER', 'CONCACAF', 'Bermuda', 'BERMUDA', 61, 'BERMUDA'),
    ('VGB', 'CONCACAF', 'British Virgin Islands', 'BRIT. VIRGIN IS.', 217, None),
    ('CAN', 'CONCACAF', 'Canada', 'CANADA', 64, 'CANADA'), ('CAY', 'CONCACAF', 'Cayman Islands', 'CAYMAN ISLANDS', 177, None),
    ('CRC', 'CONCACAF', 'Costa Rica', 'COSTA RICA', 51, 'COSTA RICA'), ('CUB', 'CONCACAF', 'Cuba', 'CUBA', 167, None),
    ('CUW', 'CONCACAF', 'Curaçao', 'CURACAO', 164, None), ('DMA', 'CONCACAF', 'Dominica', 'DOMINICA', 218, None),
    ('DOM', 'CONCACAF', 'Dominican Republic', 'DOMINICAN REP.', 166, None),
    ('SLV', 'CONCACAF', 'El Salvador', 'EL SALVADOR', 52, 'EL SALVADOR'), ('GRN', 'CONCACAF', 'Grenada', 'GRENADA', 169, None),
    ('GUA', 'CONCACAF', 'Guatemala', 'GUATEMALA', 53, 'GUATEMALA'), ('GUY', 'CONCACAF', 'Guyana', 'GUYANA', 78, 'GUYANA'),
    ('HAI', 'CONCACAF', 'Haiti', 'HAITI', 163, None), ('HON', 'CONCACAF', 'Honduras', 'HONDURAS', 54, 'HONDURAS'),
    ('JAM', 'CONCACAF', 'Jamaica', 'JAMAICA', 62, 'JAMAICA'), ('MEX', 'CONCACAF', 'Mexico', 'MEXICO', 56, 'MEXICO'),
    ('MSR', 'CONCACAF', 'Montserrat', 'MONTSERRAT', 176, None), ('NCA', 'CONCACAF', 'Nicaragua', 'NICARAGUA', 60, 'NICARAGUA'),
    ('PAN', 'CONCACAF', 'Panama', 'PANAMA', 57, 'PANAMA'), ('PUR', 'CONCACAF', 'Puerto Rico', 'PUERTO RICO', 168, None),
    ('SKN', 'CONCACAF', 'Saint Kitts and Nevis', 'ST. KITTS-NEVIS', 174, None),
    ('LCA', 'CONCACAF', 'Saint Lucia', 'ST. LUCIA', 175, None),
    ('VIN', 'CONCACAF', 'Saint Vincent and the Grenadines', 'ST. VINCENT', 67, 'ST. VINCENT'),
    ('SUR', 'CONCACAF', 'Suriname', 'SURINAM', 75, 'SURINAM'),
    ('TRI', 'CONCACAF', 'Trinidad and Tobago', 'TRINIDAD-TOBAGO', 63, 'TRINIDAD-TOBAGO'),
    ('TCA', 'CONCACAF', 'Turks and Caicos Islands', 'TURKS-CAICOS', 219, None),
    ('USA', 'CONCACAF', 'United States', 'UNITED STATES', 58, 'UNITED STATES'),
    ('VIR', 'CONCACAF', 'United States Virgin Islands', 'US VIRGIN IS.', 220, None),
    # CONMEBOL (10)
    ('ARG', 'CONMEBOL', 'Argentina', 'ARGENTINA', 68, 'ARGENTINA'), ('BOL', 'CONMEBOL', 'Bolivia', 'BOLIVIA', 69, 'BOLIVIA'),
    ('BRA', 'CONMEBOL', 'Brazil', 'BRAZIL', 70, 'BRAZIL'), ('CHI', 'CONMEBOL', 'Chile', 'CHILE', 71, 'CHILE'),
    ('COL', 'CONMEBOL', 'Colombia', 'COLOMBIA', 72, 'COLOMBIA'), ('ECU', 'CONMEBOL', 'Ecuador', 'ECUADOR', 73, 'ECUADOR'),
    ('PAR', 'CONMEBOL', 'Paraguay', 'PARAGUAY', 74, 'PARAGUAY'), ('PER', 'CONMEBOL', 'Peru', 'PERU', 79, 'PERU'),
    ('URU', 'CONMEBOL', 'Uruguay', 'URUGUAY', 76, 'URUGUAY'), ('VEN', 'CONMEBOL', 'Venezuela', 'VENEZUELA', 77, 'VENEZUELA'),
    # OFC (11: Papua New Guinea in Asia's file, as 96/97)
    ('ASA', 'OFC', 'American Samoa', 'AMERICAN SAMOA', 221, None), ('COK', 'OFC', 'Cook Islands', 'COOK ISLANDS', 222, None),
    ('FIJ', 'OFC', 'Fiji', 'FIJI', 150, 'FIJI'), ('NCL', 'OFC', 'New Caledonia', 'NEW CALEDONIA', 209, None),
    ('NZL', 'OFC', 'New Zealand', 'NEW ZEALAND', 149, 'NEW ZEALAND'),
    ('PNG', 'OFC', 'Papua New Guinea', 'PAPUA NEW GUINEA', 143, 'PAPUA NEW GUINEA'),
    ('SAM', 'OFC', 'Samoa', 'SAMOA', 212, None), ('SOL', 'OFC', 'Solomon Islands', 'SOLOMON ISLANDS', 151, 'SOLOMON ISLANDS'),
    ('TAH', 'OFC', 'Tahiti', 'TAHITI', 210, None), ('TGA', 'OFC', 'Tonga', 'TONGA', 213, None),
    ('VAN', 'OFC', 'Vanuatu', 'VANUATU', 211, None),
]
# Transfermarkt's team id of each nation (Wikidata P7223): its page, saved from the owner's browser,
# gives the last match's line-up -- formation and who played where (tmlineup, §15)
TM_IDS = {
    'ALB': 3561, 'AND': 10533, 'ARM': 6219, 'AUT': 3383, 'AZE': 8605, 'BLR': 3450, 'BEL': 3382, 'BIH': 3446, 'BUL': 3394, 'CRO': 3556,
    'CYP': 3668, 'CZE': 3445, 'DEN': 3436, 'ENG': 3299, 'EST': 6133, 'FRO': 9173, 'FIN': 3443, 'FRA': 3377, 'GEO': 3669, 'GER': 3262,
    'GIB': 37574, 'GRE': 3378, 'HUN': 3468, 'ISL': 3574, 'ISR': 5547, 'ITA': 3376, 'KAZ': 9110, 'KOS': 53982, 'LVA': 3555, 'LIE': 5673,
    'LTU': 3851, 'LUX': 3580, 'MLT': 3587, 'MDA': 6090, 'MNE': 11953, 'NED': 3379, 'NIR': 5674, 'MKD': 5148, 'NOR': 3440, 'POL': 3442,
    'POR': 3300, 'IRL': 3509, 'ROU': 3447, 'RUS': 3448, 'SMR': 10521, 'SCO': 3380, 'SRB': 3438, 'SVK': 3503, 'SVN': 3588, 'ESP': 3375,
    'SWE': 3557, 'SUI': 3384, 'TUR': 3381, 'UKR': 3699, 'WAL': 3864, 'ALG': 3614, 'ANG': 3585, 'BEN': 3955, 'BOT': 15229, 'BFA': 5872,
    'BDI': 13943, 'CPV': 4311, 'CMR': 3434, 'CTA': 12933, 'CHA': 13978, 'COM': 16430, 'CGO': 3702, 'COD': 3854, 'CIV': 3591, 'DJI': 16427,
    'EGY': 3672, 'EQG': 13485, 'ERI': 13953, 'SWZ': 13675, 'ETH': 13941, 'GAB': 5704, 'GAM': 6186, 'GHA': 3441, 'GUI': 3856, 'GNB': 3701,
    'KEN': 8987, 'LES': 13962, 'LBR': 9172, 'LBY': 6602, 'MAD': 14635, 'MWI': 8988, 'MLI': 3674, 'MTN': 14238, 'MRI': 14239, 'MAR': 3575,
    'MOZ': 5129, 'NAM': 3573, 'NIG': 14163, 'NGA': 3444, 'RWA': 3855, 'STP': 15236, 'SEN': 3499, 'SEY': 3562, 'SLE': 6187, 'SOM': 13974,
    'RSA': 3806, 'SSD': 33192, 'SDN': 13313, 'TAN': 14666, 'TOG': 3815, 'TUN': 3670, 'UGA': 13497, 'ZAM': 3703, 'ZIM': 3583, 'AFG': 3576,
    'AUS': 3433, 'BHR': 7214, 'BAN': 15033, 'BHU': 15920, 'BRU': 14233, 'CAM': 16038, 'CHN': 5598, 'TPE': 15363, 'GUM': 17753, 'HKG': 15977,
    'IND': 13957, 'IDN': 13958, 'IRQ': 3560, 'IRN': 3582, 'JPN': 3435, 'JOR': 15737, 'PRK': 15457, 'KOR': 3589, 'KUW': 3432, 'KGZ': 3956,
    'LAO': 16431, 'LBN': 3586, 'MAC': 16432, 'MAS': 15738, 'MDV': 15456, 'MNG': 15739, 'MYA': 16423, 'NEP': 15350, 'OMA': 14165, 'PAK': 15352,
    'PLE': 17758, 'PHI': 15234, 'QAT': 14162, 'KSA': 3807, 'SGP': 15357, 'SRI': 15358, 'SYR': 13674, 'TJK': 13975, 'THA': 5676, 'TLS': 17757,
    'TKM': 14248, 'UAE': 5147, 'UZB': 3563, 'VIE': 8778, 'YEM': 15922, 'AIA': 17748, 'ATG': 16028, 'ARU': 17749, 'BAH': 15918, 'BRB': 16030,
    'BLZ': 15919, 'BER': 15735, 'VGB': 17750, 'CAN': 3510, 'CAY': 17751, 'CRC': 8497, 'CUB': 3808, 'CUW': 32364, 'DMA': 16033, 'DOM': 15232,
    'SLV': 13951, 'GRN': 14175, 'GUA': 13342, 'GUY': 15736, 'HAI': 14161, 'HON': 3590, 'JAM': 3671, 'MEX': 6303, 'MSR': 17754, 'NCA': 15351,
    'PAN': 3577, 'PUR': 17759, 'SKN': 17760, 'LCA': 17761, 'VIN': 17762, 'SUR': 15359, 'TRI': 7149, 'TCA': 17764, 'USA': 3505, 'VIR': 17747,
    'ARG': 3437, 'BOL': 5233, 'BRA': 3439, 'CHI': 3700, 'COL': 3816, 'ECU': 5750, 'PAR': 3581, 'PER': 3584, 'URU': 3449, 'VEN': 3504,
    'ASA': 17746, 'COK': 17752, 'FIJ': 13955, 'NCL': 17755, 'NZL': 9171, 'PNG': 15233, 'SAM': 15235, 'SOL': 15740, 'TAH': 17763, 'TGA': 16435,
    'VAN': 15238,
}

# the nationalities the game had no code for (src/swos_data_bulk_imports.c _shortCountryNames, from 214)
NEW_NATIONALITIES = ['BHU', 'SEY', 'AIA', 'VGB', 'DMA', 'TCA', 'VIR', 'ASA', 'COK']

ARTICLE = {'United States': "United States men's national soccer team",
           'Canada': "Canada men's national soccer team",
           'Australia': "Australia men's national soccer team",
           'New Zealand': "New Zealand men's national football team",
           'Sweden': "Sweden men's national football team",
           'Hong Kong, China': 'Hong Kong national football team',
           'TLS': 'Timor-Leste national football team',
           'Türkiye': 'Turkey national football team',
           'United States Virgin Islands': 'United States Virgin Islands national soccer team'}
RANK_NAME = {'Türkiye': ['Turkey', 'Türkiye'], 'Hong Kong, China': ['Hong Kong, China', 'Hong Kong'], 'TLS': ['Timor-Leste', 'East Timor'],
             'Czech Republic': ['Czech Republic', 'Czechia'], 'Ivory Coast': ['Ivory Coast', "Côte d'Ivoire"],
             'Cape Verde': ['Cape Verde', 'Cabo Verde'], 'Eswatini': ['Eswatini', 'Swaziland'],
             'Republic of Ireland': ['Republic of Ireland', 'Ireland'], 'São Tomé and Príncipe': ['São Tomé and Príncipe', 'Sao Tome and Principe'],
             'Curaçao': ['Curaçao', 'Curacao'], 'Chinese Taipei': ['Chinese Taipei', 'Taiwan'],
             'Macau': ['Macau', 'Macao'], 'Gambia': ['Gambia', 'The Gambia'], 'DR Congo': ['DR Congo', 'Congo DR'],
             'United States': ['USA'], 'Iran': ['IR Iran'], 'South Korea': ['Korea Republic'], 'China': ['China PR'],
             'Kyrgyzstan': ['Kyrgyz Republic'], 'North Korea': ['Korea DPR'], 'Brunei': ['Brunei Darussalam'],
             'Saint Kitts and Nevis': ['St Kitts and Nevis'], 'Saint Lucia': ['St Lucia'],
             'Saint Vincent and the Grenadines': ['St Vincent and the Grenadines'],
             'United States Virgin Islands': ['US Virgin Islands']}

# the world's club files' country (their file name) -> FIFA code, for a player's club (clubnat)
FILE_FIFA = {'ALBANIA': 'ALB', 'ALGERIA': 'ALG', 'ANDORRA': 'AND', 'ARGENTINA': 'ARG', 'ARMENIA': 'ARM',
             'AUSTRALIA': 'AUS', 'AUSTRIA': 'AUT', 'AZERBAIJAN': 'AZE', 'BELARUS': 'BLR', 'BELGIUM': 'BEL',
             'BOLIVIA': 'BOL', 'BOSNIA_HERZ': 'BIH', 'BRAZIL': 'BRA', 'BRAZIL_POOL': 'BRA', 'BULGARIA': 'BUL',
             'CANADA': 'CAN', 'CHILE': 'CHI', 'CHINA': 'CHN', 'COLOMBIA': 'COL', 'COSTA_RICA': 'CRC',
             'CROATIA': 'CRO', 'CYPRUS': 'CYP', 'CZECH_REPUBLIC': 'CZE', 'DENMARK': 'DEN', 'ECUADOR': 'ECU',
             'EGYPT': 'EGY', 'EL_SALVADOR': 'SLV', 'ENGLAND': 'ENG', 'ESTONIA': 'EST', 'FAROE_ISLANDS': 'FRO',
             'FINLAND': 'FIN', 'FRANCE': 'FRA', 'GEORGIA': 'GEO', 'GERMANY': 'GER', 'GERMANY_POOL': 'GER',
             'GHANA': 'GHA', 'GIBRALTAR': 'GIB', 'GREECE': 'GRE', 'GUATEMALA': 'GUA', 'HOLLAND': 'NED',
             'HOLLAND_POOL': 'NED', 'HONDURAS': 'HON', 'HUNGARY': 'HUN', 'ICELAND': 'ISL', 'INDIA': 'IND',
             'IRAN': 'IRN', 'ISRAEL': 'ISR', 'ITALY': 'ITA', 'ITALY_2': 'ITA', 'ITALY_POOL': 'ITA', 'JAPAN': 'JPN',
             'KAZAKHSTAN': 'KAZ', 'KOSOVO': 'KOS', 'LATVIA': 'LVA', 'LIECHTENSTEIN': 'LIE', 'LITHUANIA': 'LTU',
             'LUXEMBOURG': 'LUX', 'MALTA': 'MLT', 'MEXICO': 'MEX', 'MOLDOVA': 'MDA', 'MONTENEGRO': 'MNE',
             'MOROCCO': 'MAR', 'NEW_ZEALAND': 'NZL', 'NORTHERN_IRELAND': 'NIR', 'NORWAY': 'NOR',
             'N_MACEDONIA': 'MKD', 'PANAMA': 'PAN', 'PARAGUAY': 'PAR', 'PERU': 'PER', 'POLAND': 'POL',
             'PORTUGAL': 'POR', 'PORTUGAL_POOL': 'POR', 'QATAR': 'QAT', 'REPUBLIC_IRELAND': 'IRL',
             'ROMANIA': 'ROU', 'RUSSIA': 'RUS', 'SAN_MARINO': 'SMR', 'SAUDI_ARABIA': 'KSA', 'SCOTLAND': 'SCO',
             'SLOVAKIA': 'SVK', 'SLOVENIA': 'SVN', 'SOUTH_AFRICA': 'RSA', 'SOUTH_KOREA': 'KOR', 'SPAIN': 'ESP',
             'SPAIN_POOL': 'ESP', 'SURINAM': 'SUR', 'SWEDEN': 'SWE', 'SWITZERLAND': 'SUI', 'TAIWAN': 'TPE',
             'TUNISIA': 'TUN', 'TURKEY': 'TUR', 'UKRAINE': 'UKR', 'URUGUAY': 'URU', 'USA': 'USA', 'U_A_E': 'UAE',
             'VENEZUELA': 'VEN', 'WALES': 'WAL', 'YUGOSLAVIA': 'SRB'}
# Wikipedia's clubnat codes that are not FIFA's
CLUBNAT = {'NIR': 'NIR', 'WAL': 'WAL', 'SCO': 'SCO', 'ENG': 'ENG', 'GDR': 'GER', 'TUR': 'TUR', 'SER': 'SRB'}

POS_CODE = {'G': 0, 'RB': 1, 'LB': 2, 'D': 3, 'RW': 4, 'LW': 5, 'M': 6, 'A': 7}
CODE_POS = {v: k for k, v in POS_CODE.items()}
SKILL_FIELDS = ['passing', 'velocity', 'heading', 'tackling', 'control', 'speed', 'finishing']
STOP = {'FC', 'F', 'C', 'CF', 'AFC', 'SC', 'AC', 'CD', 'CA', 'CLUB', 'DE', 'FK', 'SK', 'NK', 'KS', 'SV', 'VFB',
        'VFL', 'TSG', 'BK', 'IF', 'FF', 'IK', 'AIK', 'SPORTING', 'UNITED', 'CITY', 'ATHLETIC', 'CALCIO', 'THE',
        'OF', 'AND', 'E', 'Y', 'SPORT', 'CLUBE', 'ESPORTE', 'FUTEBOL', 'FUTBOL', 'FOOTBALL', 'SOCCER', 'U', 'S'}


def fold(s):
    return modport.swos_name(s, 99)


def tokens(s):
    return frozenset(t for t in re.split(r'[^A-Z0-9]+', fold(s)) if t)


def club_tokens(s):
    return frozenset(t for t in tokens(s) if t not in STOP and len(t) > 1)


# ---- the world's clubs -------------------------------------------------------------------------

class World:
    def __init__(self, folder):
        self.by_name = collections.defaultdict(list)      # frozenset(tokens) -> [player]
        self.by_last = collections.defaultdict(list)      # surname token -> [player]
        self.clubs = collections.defaultdict(list)        # FIFA code of the league -> [club]
        self.home_file = {}                               # FIFA code -> its club team file (country number)
        tdir = os.path.join(folder, 'teams')
        for fn in sorted(os.listdir(tdir)):
            stem = fn[:-5]
            if not fn.endswith('.json') or stem not in FILE_FIFA:
                continue
            d = json.load(open(os.path.join(tdir, fn), encoding='utf-8'))
            fifa = FILE_FIFA[stem]
            if not stem.endswith(('_POOL', '_2')):
                self.home_file.setdefault(fifa, d['country'])
            for t in d['teams']:
                club = {'name': t['team']['team_name'], 'fifa': fifa, 'tok': club_tokens(t['team']['team_name']),
                        'players': []}
                for k, p in enumerate(t['players'] + t.get('extra_players', [])):
                    rec = {'p': p, 'club': club, 'extra': k >= 16, 'tok': tokens(p['player_name'])}
                    club['players'].append(rec)
                    self.by_name[rec['tok']].append(rec)
                    for w in rec['tok']:
                        if len(w) > 2:
                            self.by_last[w].append(rec)
                self.clubs[fifa].append(club)

    def find_club(self, name, link, clubnat):
        """The world's club of a Wikipedia club (its display name and link), in its league's country."""
        fifa = CLUBNAT.get(clubnat, clubnat)
        want = club_tokens(name) | club_tokens(re.sub(r'\(.*?\)', '', link or ''))
        if not want:
            return None
        best, score = None, 0.0
        for c in self.clubs.get(fifa, []):
            if not c['tok']:
                continue
            common = len(want & c['tok'])
            if not common:
                continue
            s = common / len(c['tok'] | (want & c['tok'])) + (0.5 if c['tok'] <= want else 0)
            if s > score:
                best, score = c, s
        return best if score >= 0.5 else None

    def match(self, name, nat, club):
        """The world's record of a squad player: his name's tokens, his nationality or his club."""
        want = tokens(name)
        if not want:
            return None
        exact = self.by_name.get(want, [])
        for pool in ([r for r in exact if club is not None and r['club'] is club],
                     [r for r in exact if (r['p']['nationality'] & 255) == nat]):
            if pool:
                return max(pool, key=lambda r: (not r['extra'], r['p']['price']))
        if club is None:
            return None
        # a shorter or longer form of the name at the same club (Wikipedia's 'Gabriel Magalhães', the
        # world's 'GABRIEL'), the surname shared
        last = fold(name).split()[-1] if fold(name).split() else ''
        cands = [r for r in club['players'] if (r['tok'] <= want or want <= r['tok']) and r['tok']
                 and (last in r['tok'] or len(r['tok']) == 1)]
        if len(cands) == 1:
            return cands[0]
        return None


# ---- Wikipedia -----------------------------------------------------------------------------------

def plain(v):
    v = re.sub(r'<ref[^>]*/>|<ref.*?</ref>|<!--.*?-->', '', v, flags=re.S)
    v = re.sub(r'\{\{(?:ill|interlanguage link|illm)\|([^|}]*)[^}]*\}\}', r'\1', v, flags=re.I)
    v = re.sub(r'\{\{(?:sortname|sname)\|([^|}]*)\|([^|}]*)[^}]*\}\}', r'\1 \2', v, flags=re.I)
    v = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]', r'\1', v)
    v = re.sub(r'\{\{[^}]*\}\}|<[^>]*>', '', v)
    return v.replace("'''", '').replace("''", '').strip()


def params(body):
    """A template's |key=value parameters (nested [[..]] and {{..}} kept whole)."""
    out, depth, cur = {}, 0, ''
    parts = []
    i = 0
    while i < len(body):
        two = body[i:i + 2]
        if two in ('[[', '{{'):
            depth += 1
            cur += two
            i += 2
            continue
        if two in (']]', '}}'):
            depth -= 1
            cur += two
            i += 2
            continue
        if body[i] == '|' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += body[i]
        i += 1
    parts.append(cur)
    for p in parts:
        if '=' in p:
            k, v = p.split('=', 1)
            out[k.strip().lower()] = v.strip()
    return out


def templates(text, name_re):
    """Every {{name ...}} whose name matches, as its body (balanced braces)."""
    out = []
    for m in re.finditer(r'\{\{\s*(' + name_re + r')\s*\|', text, flags=re.I):
        i, depth = m.start(), 0
        while i < len(text):
            if text.startswith('{{', i):
                depth += 1
                i += 2
            elif text.startswith('}}', i):
                depth -= 1
                i += 2
                if depth == 0:
                    break
            else:
                i += 1
        out.append(text[m.end():i - 2])
    return out


def squad(text):
    """The current squad: [{no, pos, name, link, caps, born, club, clublink, clubnat}] -- the first
    {{nat fs g start}} ... {{nat fs end}} block after the 'Current squad' heading (the recent
    call-ups that follow are left out)."""
    m = (re.search(r'\n=+\s*(?:Current|Latest|Recent) squad\s*=+', text, flags=re.I) or
         re.search(r'\n=+\s*(?:Squad|Players)\s*=+', text, flags=re.I))
    seg = text[m.end():] if m else text
    st = re.search(r'\{\{\s*nat fs (?:g )?start', seg, flags=re.I)
    if st:
        seg = seg[st.start():]
    end = re.search(r'\{\{\s*nat fs (?:g )?end', seg, flags=re.I)
    if end:
        seg = seg[:end.start()]
    nxt = re.search(r'\n==[^=]', seg)          # (no end template: up to the next section)
    if nxt:
        seg = seg[:nxt.start()]
    out = []
    for body in templates(seg, r'nat fs (?:g )?player'):
        p = params(body)
        raw = p.get('name', '')
        lm = re.search(r'\[\[([^|\]]+)(?:\|([^\]]+))?\]\]', raw)
        name = plain(raw)
        other = plain(p.get('other', '')).lower()
        captain = other == 'captain' or bool(re.search(r'\((?:captain|c)\)', name, flags=re.I))
        name = re.sub(r'\s*\((?:captain|vice-captain|c)\)\s*', ' ', name, flags=re.I).strip()
        link = lm.group(1).strip() if lm else None
        cm = re.search(r'\[\[([^|\]]+)(?:\|([^\]]+))?\]\]', p.get('club', ''))
        born = re.search(r'(\d{4})\|(\d{1,2})\|(\d{1,2})', p.get('age', ''))
        caps = re.match(r'\d+', plain(p.get('caps', '')) or '')
        no = re.match(r'\d+', plain(p.get('no', '')) or '')
        out.append(dict(no=int(no.group()) if no else None, pos=plain(p.get('pos', '')).upper()[:2], name=name,
                        link=link, caps=int(caps.group()) if caps else 0,
                        born='%04d-%02d-%02d' % tuple(int(x) for x in born.groups()) if born else None,
                        club=plain(p.get('club', '')), clublink=cm.group(1) if cm else None,
                        clubnat=plain(p.get('clubnat', '')).upper(), captain=captain))
    return out


def coach(text):
    for f in ('coach', 'head coach', 'head_coach', 'manager'):
        m = re.search(r'\|\s*' + f + r'\s*=\s*([^\n]*)', text, flags=re.I)
        if m:
            v = re.sub(r'\(.*?\)', '', plain(m.group(1))).strip(" ,'|")
            if v and v.lower() not in ('vacant', 'tba', 'tbd', 'none'):
                return v
    return ''


def rankings(offline):
    """{name: FIFA points} from Wikipedia's ranking module (the FIFA's monthly ranking)."""
    t = wiki.wikitext(['Module:SportsRankings/data/FIFA World Rankings'], offline=offline)
    v = next(iter(t.values()), '')
    return {m.group(1): float(m.group(3)) for m in
            re.finditer(r'\{\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*-?\d+\s*,\s*([\d.]+)', v)}


# a player infobox's position -> ours; 'FB' and 'W' are a side to be chosen
INFOBOX_POS = [('goalkeeper', 'G'), ('right-back', 'RB'), ('right back', 'RB'), ('right wing-back', 'RB'),
               ('right wingback', 'RB'), ('left-back', 'LB'), ('left back', 'LB'), ('left wing-back', 'LB'),
               ('left wingback', 'LB'), ('wing-back', 'FB'), ('wingback', 'FB'), ('full-back', 'FB'),
               ('fullback', 'FB'), ('full back', 'FB'), ('centre-back', 'D'), ('center-back', 'D'),
               ('centre back', 'D'), ('center back', 'D'), ('sweeper', 'D'), ('defender', 'D'),
               ('right winger', 'RW'), ('right midfielder', 'RW'), ('right wing', 'RW'),
               ('left winger', 'LW'), ('left midfielder', 'LW'), ('left wing', 'LW'),
               ('winger', 'W'), ('wide midfielder', 'W'), ('midfielder', 'M'), ('playmaker', 'M'),
               ('striker', 'A'), ('forward', 'A')]
GROUP = {'G': 'GK', 'RB': 'DF', 'LB': 'DF', 'FB': 'DF', 'D': 'DF', 'M': 'MF', 'RW': 'MF', 'LW': 'MF', 'W': 'MF',
         'A': 'FW'}
FITS = {'GK': {'G'}, 'DF': {'RB', 'LB', 'FB', 'D'}, 'MF': {'M', 'RW', 'LW', 'W'}, 'FW': {'A', 'RW', 'LW', 'W'}}


def infobox_midfield(text):
    """'D' for a defensive midfielder, 'A' for an attacking one, by his infobox's first word of the
    two; None when it says neither."""
    m = re.search(r'\|\s*positions?\s*=\s*([^\n]*)', text, flags=re.I)
    if not m:
        return None
    raw = m.group(1).lower()
    d, a = raw.find('defensive midfield'), raw.find('attacking midfield')
    if d < 0 and a < 0:
        return None
    return 'D' if a < 0 or (0 <= d < a) else 'A'


def infobox_positions(text):
    m = re.search(r'\|\s*positions?\s*=\s*([^\n]*)', text, flags=re.I)
    if not m:
        return []
    raw = m.group(1).lower()
    raw = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]', r'\1', raw)
    raw = re.sub(r'<ref.*?(</ref>|/>)|\{\{[^}]*\}\}|<[^>]*>', ' ', raw)
    out = []
    for part in re.split(r'[,/;]| or | and ', raw):
        part = part.strip()
        for word, code in INFOBOX_POS:
            if word in part:
                out.append(code)
                break
    return out


# ---- building a team -----------------------------------------------------------------------------

# The line-up (team.player_numbers: the record's player in each place on the pitch) of a record in
# model.pick_sixteen's order -- G RB LB D D RW LW M M A A, then the bench -- in 4-4-2 (tactic 0), the
# place order of SWOS's teams: GK, RB, D, D, LB, RW, M (defending), M (attacking), LW, A, A; the
# World 2026-27 clubs' (the Mod's mkseason) and 96/97's own, whose records are sorted by position.
LINEUP_442 = [0, 1, 3, 4, 2, 5, 7, 8, 6, 9, 10, 11, 12, 13, 14, 15]
TACTIC_442 = 0
BENCH_GROUP = {'G': 0, 'RB': 1, 'LB': 1, 'D': 1, 'RW': 2, 'LW': 2, 'M': 2, 'A': 3}


def defending(player, mtype):
    """How much a central midfielder is the defending one of the two: his article's word (defensive /
    attacking midfielder), else his skills -- tackling and heading against finishing and shooting."""
    k = lambda f: player[f] & 7                         # noqa: E731
    return (10 if mtype == 'D' else -10 if mtype == 'A' else 0) + \
        k('tackling') + k('heading') - k('finishing') - k('velocity')


def order_lineup(players, sixteen):
    """The record in model.pick_sixteen's order made SWOS's: of its two central midfielders (records 7
    and 8) the defending one first (LINEUP_442 puts record 7 in the defending place), and the bench
    (records 11..15) goalkeeper, defenders, midfielders, attackers -- each group as picked."""
    if len(players) < 16:
        return players, sixteen
    rows = list(zip(players, sixteen))
    if sixteen[7]['position'] == 'M' and sixteen[8]['position'] == 'M' and \
            defending(players[8], sixteen[8].get('mtype')) > defending(players[7], sixteen[7].get('mtype')):
        rows[7], rows[8] = rows[8], rows[7]
    rows[11:16] = sorted(rows[11:16], key=lambda r: BENCH_GROUP.get(r[1]['position'], 2))
    return [r[0] for r in rows], [r[1] for r in rows]

def team_record(head, players, name, nat_code, coach_name, fifa, file_no, ordinal, global_number,
                lineup=None, tactic=TACTIC_442):
    tid = str(uuid.uuid5(NAMESPACE, '%s/team/nation:%s' % (TAG, fifa)))
    out_players = []
    for i, p in enumerate(players):
        rec = {'uuid': str(uuid.uuid5(NAMESPACE, '%s/team/nation:%s/player/%d' % (TAG, fifa, i)))}
        rec.update(p)
        out_players.append(rec)
    h = dict(head)
    h.update(country_number=file_no, team_ordinal=ordinal, global_team_number=global_number, team_name=name,
             coach_name=coach_name, player_numbers=list(lineup or LINEUP_442), tactics=tactic, player_values_dirty=0)
    return {'uuid': tid, 'checksum': checksum(out_players), 'team': h, 'players': out_players}


def checksum(players):
    """world_import.py's (assign_team_uuids.py's): the positions, skills and values of the 16."""
    sys.path.insert(0, os.path.join(REPO, 'scripts'))
    import world_import                                 # noqa: E402
    return world_import.checksum(players)


def new_head():
    return {'country_number': 0, 'team_ordinal': 0, 'global_team_number': 0, 'team_controls': 0,
            'team_name': '', 'writes_zero_here': 0, 'and_with_0xfe': 0, 'tactics': 0, 'league': 0,
            'primary_shirt_type': 0, 'primary_stripes_color': 1, 'primary_basic_color': 1,
            'primary_shorts_color': 1, 'primary_socks_color': 1, 'secondary_shirt_type': 0,
            'secondary_stripes_color': 2, 'secondary_basic_color': 2, 'secondary_shorts_color': 2,
            'secondary_socks_color': 2, 'coach_name': '', 'player_values_dirty': 0, 'player_numbers': list(LINEUP_442)}


# ---- real line-ups: the formation and who played where (tmlineup.py, §15) ---------------------------

import tmlineup                                         # noqa: E402

TACTIC_FILES = {0: '442', 1: '541', 2: '451', 3: '532', 4: '352', 5: '433', 6: '424', 7: '343', 9: '523'}
TACTIC_NAMES = {0: '4-4-2', 1: '5-4-1', 2: '4-5-1', 3: '5-3-2', 4: '3-5-2', 5: '4-3-3', 6: '4-2-4', 7: '3-4-3',
                9: '5-2-3'}
# a real formation -> the game's tactic (the owner's choices, 2026-11-17: 3-4-2-1 -> 3-4-3, 4-3-2-1 ->
# 4-5-1, 4-2-3-1 -> 4-2-4 with wingers, else 4-5-1 (tactic_of); 4-4-1-1 -> 4-4-2; a playmaker behind two
# strikers (3-4-1-2, 4-3-1-2) a midfielder; Wikipedia's three centre-backs, two wing-backs, RM CM CM LM
# and a striker -- '3-6-1' as its lines count -- 5-4-1)
FORMATION_TACTIC = {'4-4-2': 0, '4-1-3-2': 0, '4-3-1-2': 0, '4-4-1-1': 0, '4-1-2-1-2': 0, '5-4-1': 1, '4-5-1': 2,
                    '4-1-4-1': 2, '4-3-2-1': 2, '5-3-2': 3, '3-5-2': 4, '3-1-4-2': 4, '3-4-1-2': 4, '4-3-3': 5,
                    '4-2-4': 6, '3-4-3': 7, '3-4-2-1': 7, '5-2-3': 9, '3-6-1': 1}
DMF_TACTIC = {(4, 4, 2): 0, (5, 4, 1): 1, (4, 5, 1): 2, (5, 3, 2): 3, (3, 5, 2): 4, (4, 3, 3): 5, (4, 2, 4): 6,
              (3, 4, 3): 7, (5, 2, 3): 9}
# a Wikipedia line-up's position -> ours, and where it stands (x 0 the right touchline .. 1 the left,
# y 0 the own goal .. 1 the opponents')
WIKI_SWOS = {'GK': 'G', 'RB': 'RB', 'RWB': 'RB', 'LB': 'LB', 'LWB': 'LB', 'CB': 'D', 'RCB': 'D', 'LCB': 'D', 'SW': 'D',
             'DF': 'D', 'DM': 'M', 'RDM': 'M', 'LDM': 'M', 'CM': 'M', 'RCM': 'M', 'LCM': 'M', 'AM': 'M', 'MF': 'M',
             'RM': 'RW', 'RW': 'RW', 'RF': 'RW', 'LM': 'LW', 'LW': 'LW', 'LF': 'LW', 'CF': 'A', 'ST': 'A', 'SS': 'A',
             'FW': 'A'}
WIKI_XY = {'GK': (.5, 0), 'SW': (.5, .05), 'RCB': (.38, .12), 'CB': (.5, .12), 'LCB': (.62, .12), 'DF': (.5, .12),
           'RB': (.08, .22), 'LB': (.92, .22), 'RWB': (.05, .38), 'LWB': (.95, .38), 'RDM': (.4, .36),
           'DM': (.5, .36), 'LDM': (.6, .36), 'RCM': (.38, .5), 'CM': (.5, .5), 'LCM': (.62, .5), 'MF': (.5, .5),
           'RM': (.08, .55), 'LM': (.92, .55), 'AM': (.5, .68), 'RW': (.1, .78), 'LW': (.9, .78), 'SS': (.5, .8),
           'RF': (.25, .9), 'LF': (.75, .9), 'CF': (.5, .92), 'ST': (.5, .92), 'FW': (.5, .9)}
_SPOTS = {}
# each tactic's places 1..10 as SWOS's own teams fill them -- the most common position in each place of
# the 96/97 teams playing it (962 in 4-4-2 ... ; 4-2-4's five are mixed: its wide places taken as the
# wingers, as 4-2-3-1's -> 4-2-4 has them). A starter's position is his place's (the owner: 'to match
# SWOS defaults for a 433 tactic'). 5-3-2 and 3-5-2 share their places' spots in the game's data.
SWOS_ROLES = {0: 'RB D D LB RW M M LW A A', 1: 'RB D D LB RW D M LW M A', 2: 'RB D D LB RW M M LW M A',
              3: 'RB D D LB RW D M LW A A', 4: 'RB D M LB RW M M LW A A', 5: 'RB D D LB RW M A LW A A',
              6: 'RB D D LB RW M M LW A A', 7: 'RB D M LB RW M A LW A A', 9: 'RB D D LB A D M A M A'}
# each tactic's midfield places (its 'M's in SWOS_ROLES, in order) as defending / central / attacking
# midfielders (the owner, 2026-11-17): their skills' priorities (order_skills)
MID_KINDS = {0: 'DA', 1: 'CA', 2: 'DAA', 3: 'A', 4: 'DDA', 5: 'C', 6: 'CC', 7: 'DC', 9: 'CC'}


def tactic_spots(tactic):
    """The tactic's ten outfield places (its record's players 1..10), each its mean spot over the ball's
    35 zones (bin/data_json/tactics: a byte a zone, x the high nibble from the right touchline, y the low
    from the own goal), as (x, y) in 0..1."""
    if tactic not in _SPOTS:
        d = json.load(open(os.path.join(BASE, 'tactics', TACTIC_FILES[tactic] + '.json'), encoding='utf-8'))
        spots = []
        for pl in d['players']:
            v = [int(b, 16) for row in pl for b in row]
            spots.append(((sum(b >> 4 for b in v) / len(v) - 1) / 12, (sum(b & 15 for b in v) / len(v) - 2) / 10))
        _SPOTS[tactic] = spots
    return _SPOTS[tactic]


def slot_role(x, y):
    """Our position of a place on the pitch (x, y in 0..1, as tactic_spots): a wide place up to y 0.45
    a full-back (4-4-2's are at 0.32), a central one up to 0.3 a centre-back (the deep midfielder of
    4-4-2 / 3-4-3 / 3-5-2 is at 0.33-0.39), forwards from 0.75."""
    wide = x < 0.14 or x > 0.86
    if wide and y < 0.45:
        return 'RB' if x < 0.5 else 'LB'
    if not wide and y < 0.3:
        return 'D'
    if y < 0.75:
        return ('RW' if x < 0.5 else 'LW') if wide else 'M'
    return 'A'


def assign(cost):
    """The assignment of n rows to n columns with the least total cost (cost[i][j]): DP over subsets."""
    n = len(cost)
    best = {0: (0.0, [])}
    for i in range(n):
        nxt = {}
        for mask, (c, picks) in best.items():
            for j in range(n):
                if not mask >> j & 1:
                    m2, c2 = mask | 1 << j, c + cost[i][j]
                    if m2 not in nxt or c2 < nxt[m2][0]:
                        nxt[m2] = (c2, picks + [j])
        best = nxt
    return best[(1 << n) - 1][1]


def tactic_of(lineup, wide_wingers):
    """The game's tactic of a line-up: a Transfermarkt formation's by FORMATION_TACTIC, a Wikipedia one's
    by its positions' lines (defenders - midfielders - attacking midfielders - forwards; a winger is a
    forward unless an attacking midfielder plays); a 4-2-3-1 is 4-2-4 when its wide men are wingers, else
    4-5-1. (tactic, the real formation's name, known)."""
    if lineup['source'] == 'tm':
        f = lineup['formation'].split()[0]
        if f == '4-2-3-1':
            return (6 if wide_wingers else 2), lineup['formation'], True
        if f in FORMATION_TACTIC:
            return FORMATION_TACTIC[f], lineup['formation'], True
        digits = [int(c) for c in f.split('-') if c.isdigit()]
        d, fw = (digits[0], digits[-1]) if digits else (4, 2)
        return DMF_TACTIC.get((d, 10 - d - fw, fw), 0), lineup['formation'], False
    pos = [p['pos'] for p in lineup['xi'] if p['pos'] != 'GK']
    cb = sum(q in ('CB', 'SW', 'RCB', 'LCB', 'DF') for q in pos)
    wb = sum(q in ('RWB', 'LWB') for q in pos)
    D = cb + sum(q in ('RB', 'LB') for q in pos) + (wb if cb < 3 else 0)
    am = sum(q == 'AM' for q in pos)
    wing = sum(q in ('RW', 'LW') for q in pos)
    F = sum(q in ('CF', 'ST', 'SS', 'RF', 'LF', 'FW') for q in pos) + (0 if am else wing)
    AM = am + (wing if am else 0)
    M = 10 - D - AM - F
    name = '-'.join(str(k) for k in (D, M, AM, F) if k)
    if name in FORMATION_TACTIC:
        return FORMATION_TACTIC[name], name, True
    if name == '4-2-3-1':
        return (6 if wing else 2), name, True
    if (D, M + AM, F) in DMF_TACTIC:
        return DMF_TACTIC[(D, M + AM, F)], name, True
    return DMF_TACTIC.get((D, 10 - D - F, F), 0), name, False


def lineup_xy(lineup):
    """Each XI player's spot (x, y in 0..1, x 0 the right touchline, y 0 the own goal)."""
    out = []
    if lineup['source'] == 'tm':
        for p in lineup['xi']:                  # (the drawing's boxes: left 5..75, the middle at 40)
            out.append((min(1.0, max(0.0, 0.5 - (p['left'] - 40) / 70)), max(0.0, (80 - p['top']) / 80)))
        return out
    count = collections.Counter(p['pos'] for p in lineup['xi'])
    seen = collections.Counter()
    for p in lineup['xi']:
        x, y = WIKI_XY.get(p['pos'], (.5, .5))
        n = count[p['pos']]
        if n > 1:                               # (the same position twice: listed from the right)
            x += (seen[p['pos']] - (n - 1) / 2) * 0.18
            seen[p['pos']] += 1
        out.append((x, y))
    return out


def defending_row(row):
    rec = row.get('rec')
    mt = row.get('mtype')
    if rec:
        return defending(rec, mt)
    return 10 if mt == 'D' else -10 if mt == 'A' else 0


def place_lineup(lineup, tactic, xi_rows):
    """The XI in the tactic's record order: the goalkeeper, then the outfield ten each in the place
    nearest where he played (the least total squared distance, depth counting double; of two central
    midfielders level with each other the defending one deeper)."""
    xy = lineup_xy(lineup)
    gk = max(range(11), key=lambda k: (xi_rows[k]['pos'] == 'G', -xy[k][1]))
    out = [k for k in range(11) if k != gk]
    pts = []
    for k in out:
        x, y = xy[k]
        if xi_rows[k]['pos'] == 'M':
            y -= 0.003 * defending_row(xi_rows[k])
        pts.append((x, y))
    spots = tactic_spots(tactic)

    def norm(v):
        lo, hi = min(v), max(v)
        return [(a - lo) / (hi - lo) if hi > lo else 0.5 for a in v]
    py, sy = norm([p[1] for p in pts]), norm([s[1] for s in spots])
    cost = [[(pts[i][0] - spots[j][0]) ** 2 + 2 * (py[i] - sy[j]) ** 2 for j in range(10)] for i in range(10)]
    pick = assign(cost)
    order = [None] * 10
    for i, j in enumerate(pick):
        order[j] = out[i]
    return [gk] + order, SWOS_ROLES[tactic].split()


# ---- kits ------------------------------------------------------------------------------------------

def kit_colour(r, g, b):
    """The game's kit colour (model.KIT_RGB: 0 grey 1 white 2 black 3 orange 4 red 5 blue 6 maroon
    7 sky blue 8 green 9 yellow 10 purple) by hue, as a player names it -- the nearest by RGB made
    navy black and mint sky blue (Germany's 2026 away: 'blue with green pants and socks')."""
    import colorsys
    h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    h *= 360
    if v < 0.06 or (v < 0.12 and sat < 0.5) or (sat < 0.25 and v < 0.3):
        return 2                            # (a saturated dark keeps its hue: Scotland's navy is blue)
    if sat < 0.18:
        return 1 if v > 0.8 else 0
    if h < 8 or h >= 335:
        return 6 if v < 0.55 else 4         # (#ff5533, hue 10, is the Dutch orange)
    if h < 40:
        return 6 if v < 0.45 else 3
    if h < 70:
        return 9
    if h < 182:
        return 8
    if h < 215:
        return 7 if v > 0.7 and sat < 0.75 else 5
    if h < 262:
        return 7 if sat < 0.35 and v > 0.7 else 5
    if h < 300:
        return 10
    return 10 if v < 0.6 else 4           # (magenta, pink: the game has neither)


def hex_colour(hx):
    return kit_colour(*(int(hx[i:i + 2], 16) for i in (1, 3, 5)))


# where each part of a Wikipedia kit sits in its pattern image (rows, columns): the images are
# opaque, white outside the garment's outline (the kit template draws the outline over them)
PART_BOX = {'body': (16, 58, 4, 34), 'leftarm': (18, 31, 22, 31), 'shorts': (2, 30, 34, 66),
            'socks': (12, 38, 32, 68)}


def part_grid(px, base, part):
    """The kit colours of a part's pattern composed over its base colour, in the part's box (rows)."""
    br, bg, bb = (int(base[i:i + 2], 16) for i in (1, 3, 5))
    y0, y1, x0, x1 = PART_BOX[part]
    grid = []
    for row in px[y0:y1]:
        line = []
        for r, g, b, al in row[x0:x1]:
            f = al / 255
            line.append(kit_colour(r * f + br * (1 - f), g * f + bg * (1 - f), b * f + bb * (1 - f)))
        grid.append(line)
    return grid


def part_counts(px, base, part):
    """{kit colour: pixels} of a part (part_grid)."""
    return collections.Counter(c for line in part_grid(px, base, part) for c in line)


# two shades of one colour on the game's few: a tonal pattern, never stripes (Scotland's navy)
NEAR_SHADES = {frozenset(p) for p in ((2, 5), (5, 7), (4, 6), (0, 1), (0, 2), (6, 2), (5, 10), (6, 10))}


def stripes(grid, two):
    """2 (vertical stripes) or 3 (hoops) for a shirt of two main colours: where the changes between
    them run (model.torso's rule, on our colours)."""
    across = sum(1 for line in grid for x in range(1, len(line))
                 if line[x] != line[x - 1] and line[x] in two and line[x - 1] in two)
    down = sum(1 for y in range(1, len(grid)) for x in range(len(grid[0]))
               if grid[y][x] != grid[y - 1][x] and grid[y][x] in two and grid[y - 1][x] in two)
    return 2 if across >= down else 3


def kit_of(k, offline):
    """A Wikipedia infobox kit -> 'type stripes basic shorts socks' (model.kit's form: types 0 plain,
    1 coloured sleeves, 2 vertical stripes, 3 hoops), each part read from its pattern image over its
    colour when it has one -- the image draws the kit, the infobox colour is only what shows through
    (Germany's 2026 away: pink under a navy shirt) -- else from the colour."""
    if not k.get('body') and not k.get('pattern_body'):
        return None
    k = dict(k)
    k.setdefault('body', '#ffffff')         # (a kit of patterns alone: Argentina's 2026 away)

    class Uncached(Exception):
        pass

    def part(name, base):
        pat = k.get('pattern_' + name)
        try:
            px = wiki.pattern_image(name, pat, offline) if pat else None
        except SystemExit:                  # (offline and not in the cache: the kit stays as it was)
            raise Uncached()
        if px and len(px) >= PART_BOX[name][1] and len(px[0]) >= PART_BOX[name][3]:
            c = part_counts(px, base, name)
            if c:
                return c
        return collections.Counter({hex_colour(base): 1})
    try:
        body = part('body', k['body'])
        arm = part('leftarm', k.get('leftarm') or k['body']).most_common(1)[0][0]
        shorts = part('shorts', k.get('shorts') or k['body']).most_common(1)[0][0]
        socks = part('socks', k.get('socks') or k.get('shorts') or k['body']).most_common(1)[0][0]
    except Uncached:
        return None
    order = body.most_common()
    main = order[0][0]
    total = sum(body.values())
    if len(order) > 1 and order[1][1] >= 0.2 * total and k.get('pattern_body') and \
            frozenset((main, order[1][0])) not in NEAR_SHADES:
        px = wiki.pattern_image('body', k['pattern_body'], offline)
        kind = stripes(part_grid(px, k['body'], 'body'), {main, order[1][0]})
        return f'{kind} {order[1][0]} {main} {shorts} {socks}'
    if arm != main:
        return f'1 {main} {arm} {shorts} {socks}'
    return f'0 {main} {main} {shorts} {socks}'


def set_kits(head, text, offline=False, colours_only=False):
    """The team's two kits from its article's infobox (kit 1 the home, 2 the away); a kit the
    infobox lacks stays as it was, and so does one whose pattern images are not in the cache offline
    -- unless colours_only (a new nation: no kit of its own), then the infobox colours (model.kit)."""
    names = ('shirt_type', 'stripes_color', 'basic_color', 'shorts_color', 'socks_color')
    for n, side in ((1, 'primary_'), (2, 'secondary_')):
        k = kit_of(wiki.infobox_kit(text, n), offline)
        if k is None and colours_only:
            k = model.kit(wiki.infobox_kit(text, n))
        if k:
            for nm, v in zip(names, k.split()):
                head[side + nm] = int(v)


def group(pos):
    return {'G': 'G', 'RB': 'D', 'LB': 'D', 'D': 'D', 'RW': 'M', 'LW': 'M', 'M': 'M', 'A': 'A'}.get(pos, 'M')


def captain_in(sixteen, rows):
    """The squad's captain (his article's 'captain') in the first eleven: in place of the weakest
    starter of his line (a defender for a defender ...), who goes to the bench in his place."""
    cap = next((r for r in rows if r['s'].get('captain')), None)
    if not cap or len(sixteen) < 16 or any(x['s'] is cap['s'] for x in sixteen[:11]):
        return
    grp = group(cap['pos'])
    same = [k for k in range(11) if group(sixteen[k]['position']) == grp] or list(range(1, 11))
    k = min(same, key=lambda k: sixteen[k]['value'])
    j = next((j for j in range(11, 16) if sixteen[j]['s'] is cap['s']), None)
    out = sixteen[k]
    sixteen[k] = dict(cap, position=sixteen[k]['position'])
    if j is not None:
        sixteen[j] = dict(out, position=out['pos'])


# An estimated player's skills by his position's priorities, highest first (AG_SWSEdt's
# GenerateAttrbyPosVal, uagswos.pas, GPL -- its orders only; the owner's changes 2026-11-17: a
# full-back's control before his heading, a sweeper a centre-back's, a forward F H S V C). Skills: P V H T C S F (passing,
# shooting power, heading, tackling, control, speed, finishing). The first two are his key skills.
SKILL_ORDER = {'RB': 'TSPVHCF', 'LB': 'TSPVHCF', 'D': 'THPSCVF', 'M': 'PTSCHVF', 'DM': 'TPCSHVF',
               'AM': 'VPCFSTH', 'RW': 'CSPTHFV', 'LW': 'CSPTHFV', 'A': 'FHSVCPT'}
# full-backs and wingers in the game's own orders (FormTopSkillsString's _aTspvhcfstpvhcf: each
# position's letters in two variants, the first two swapped, picked by a player's odd / even hash;
# the owner, 2026-11-17): the first two swapped for a player whose name's hash is odd
SWAP_FIRST_TWO = {'RB', 'LB', 'RW', 'LW'}
ORDER_WEIGHTS = [4.0, 3.7, 1.4, 1.3, 1.2, 1.1, 1.0]   # (the generator's fitted profiles' shape: two
                                                      #  leading skills near 4, the rest near 1)


def order_skills(S, position, mtype, mplace=None, ident=''):
    """(7 skills, key indices) of level S for a position by SKILL_ORDER. A midfielder is one of three
    (the owner): defending (DM: a centre-back's kind of skills), central (M), attacking (AM) -- a
    starter by his place's depth in the tactic (mplace), a bench player by his article's word."""
    key = {'D': 'DM', 'A': 'AM'}.get(mplace or mtype, 'M') if position == 'M' else position
    order = SKILL_ORDER.get(key, SKILL_ORDER['M'])
    if key in SWAP_FIRST_TWO and zlib.crc32(str(ident).encode()) & 1:
        order = order[1] + order[0] + order[2:]
    letters = 'PVHTCSF'
    w = [0.0] * 7
    for rank, ch in enumerate(order):
        w[letters.index(ch)] = ORDER_WEIGHTS[rank]
    return model.split(S, w), sorted(letters.index(ch) for ch in order[:2])


def surname_is(row, who):
    """A row is the player a hand override names ('KIMMICH', 'TER STEGEN': his name's last words)."""
    want = fold(who).split()
    have = fold(row['s']['name']).split()
    return bool(want) and have[-len(want):] == want


FACES = {'white': 0, 'blond': 1, 'black': 2, 'black-blond': 3}
OVERRIDES = os.path.join(BASE, 'worldgen', TAG, 'nations_overrides.json')


def shirt_numbers_of(sixteen, tmpage):
    """Each player's own shirt number (it stays with him; never his place's): Transfermarkt's squad
    list's when his nation's page was saved, else the number he wore in the line-up's match (the saved
    page's, the latest window), else the Wikipedia call-up's, else an older Wikipedia tournament line-up's;
    a number another player holds by a better source is not his; none left: 0 (a free one later)."""
    tm = (tmpage or {}).get('numbers') or []
    by_id = {x['tm_id']: x['number'] for x in tm if x['number']}
    by_tok = [(tokens(x['slug'].replace('-', ' ')) | tokens(x['name']), x['number']) for x in tm if x['number']]

    def from_tm(r):
        if r.get('tm_id') in by_id:
            return by_id[r['tm_id']]
        mine = tokens(r['s']['name']) | tokens(re.sub(r'\(.*?\)', '', r['s'].get('link') or ''))
        last = fold(r['s']['name']).split()[-1:] or ['']
        hits = [num for toks, num in by_tok if toks == mine or (last[0] in toks and (mine <= toks or toks <= mine))]
        return hits[0] if len(hits) == 1 else None
    cands = []
    for k, r in enumerate(sixteen):
        ln, src = r.get('lineup_no') or (None, None)
        opts = [(0, from_tm(r)), (1, ln if src == 'tm' else None), (2, r['s'].get('no')), (3, ln if src == 'wiki' else None)]
        cands.append([(pr, num) for pr, num in opts if num and 1 <= num <= 99])
    out, used = [0] * len(sixteen), set()
    for k in sorted(range(len(sixteen)), key=lambda k: (cands[k][0][0] if cands[k] else 9, k)):
        for pr, num in cands[k]:
            if num not in used:
                out[k] = num
                used.add(num)
                break
    return out


def strongest_bench(rest):
    """Places 12..16: the strongest of the squad not starting (the owner) -- the best spare goalkeeper
    and the four best outfield players (rest: best first) -- goalkeeper, defenders, midfielders,
    forwards."""
    keepers = [r for r in rest if r['pos'] == 'G']
    bench = keepers[:1] + [r for r in rest if r['pos'] != 'G'][:5 - min(1, len(keepers))]
    bench += [r for r in rest if r not in bench][:5 - len(bench)]
    return sorted(bench, key=lambda r: BENCH_GROUP.get(r['pos'], 2))


def lineup_sixteen(lu, rows, n, nation_level, world, unmapped, ov=None):
    """The 16 of a nation with a real line-up: its XI as they played (tactic_of, place_lineup), each the
    squad's player when he is in the call-up, else the world's or an estimated one; the bench the
    call-up's best goalkeeper, defender, midfielder, forward and the best left, in that order."""
    def find(lp):
        full = tokens(lp['slug'].replace('-', ' ')) if lp.get('slug') else tokens(lp['name'])
        short = tokens(lp['name'])
        last = fold(lp['name']).split()[-1] if fold(lp['name']).split() else ''
        for r in rows:
            if lp.get('link') and r['s'].get('link') == lp['link']:
                return r
        best, score = None, 0
        for r in rows:                          # (his name, or his article's: Gjimshiti is [[Berat Djimsiti]])
            have = tokens(r['s']['name']) | tokens(re.sub(r'\(.*?\)', '', r['s'].get('link') or ''))
            if have == full or have == short or full <= have:
                return r
            common = len(have & (full | short))
            if common > score and last in have and (have <= full | short or (full | short) <= have or common >= 2):
                best, score = r, common
        return best

    xi_rows = []
    for lp in lu['xi']:
        r = find(lp)
        if r is None:                           # (not in the call-up: the world's man, else estimated)
            full = ' '.join(w.capitalize() for w in lp['slug'].split('-')) if lp.get('slug') else lp['name']
            rec = world.match(full, n['nat'], None) or world.match(lp['name'], n['nat'], None)
            pos = WIKI_SWOS.get(lp.get('pos')) or (CODE_POS.get(rec['p']['position']) if rec else None) or 'M'
            S = rec['p']['price'] if rec else int(max(2, min(45, round(nation_level - 1))))
            r = dict(src='world' if rec else 'nation', rec=rec['p'] if rec else None, pos=pos, S=S, value=S,
                     order=99, mtype=None, rated=True,
                     s=dict(name=rec['p']['player_name'] if rec else full, no=lp.get('number'), caps=0,
                            link=lp.get('link'), captain=lp.get('captain')))
        else:
            r = dict(r, orig=id(r))
            if lp.get('pos') in WIKI_SWOS:      # (where he played in that match)
                r['pos'] = WIKI_SWOS[lp['pos']]
        r['lineup_no'] = (lp.get('number'), lu['source'])
        if lp.get('tm_id'):
            r['tm_id'] = lp['tm_id']
        xi_rows.append(r)
    # the squad's captain starts (the owner's rule): in the place of the weakest of his line
    cap = next((r for r in rows if r['s'].get('captain')), None)
    if cap and not any(x.get('orig') == id(cap) for x in xi_rows):
        same = [k for k in range(11) if group(xi_rows[k]['pos']) == group(cap['pos']) and
                (xi_rows[k]['pos'] == 'G') == (cap['pos'] == 'G')]
        if same:
            k = min(same, key=lambda k: xi_rows[k]['value'])
            xi_rows[k] = dict(cap, orig=id(cap))
    if lu['source'] == 'tm':
        xy = lineup_xy(lu)
        fwd = sorted((k for k in range(11) if xi_rows[k]['pos'] != 'G'), key=lambda k: -xy[k][1])
        three = fwd[1:4]
        wide = [min(three, key=lambda k: xy[k][0]), max(three, key=lambda k: xy[k][0])] if three else []
        wingers = any(xi_rows[k]['pos'] in ('RW', 'LW') for k in wide)
    else:
        wingers = False
    tactic, fname, known = tactic_of(lu, wingers)
    if not known:
        unmapped.append('%s %s -> %s' % (n['fifa'], fname, TACTIC_NAMES[tactic]))
    ov = ov or {}
    if ov.get('tactic'):                        # (the owner's hand: NATION_OVERRIDES)
        tactic = {v: k for k, v in TACTIC_NAMES.items()}[ov['tactic']]
        fname = '%s, %s by hand' % (fname, ov['tactic'])
    order, roles = place_lineup(lu, tactic, xi_rows)
    for who, want in (ov.get('places') or {}).items():  # (a starter put in a place of that role)
        k = next((j for j in range(1, 11) if surname_is(xi_rows[order[j]], who)), None)
        if k is None or roles[k - 1] == want:
            continue
        j = next((j for j in range(1, 11) if roles[j - 1] == want and not any(
            surname_is(xi_rows[order[j]], w) and (ov['places'][w] == roles[j - 1]) for w in ov['places'])), None)
        if j is not None:
            order[k], order[j] = order[j], order[k]
    sixteen = []
    for slot, k in enumerate(order):
        r = xi_rows[k]
        pos = 'G' if slot == 0 else roles[slot - 1]     # (his place's role in the tactic: SWOS's own --
        row = dict(r, position=pos)                     #  4-3-3's RB D D LB RW M A LW A A; the owner)
        if pos == 'M':                                  # (a midfield place's kind: MID_KINDS)
            nth = sum(1 for k in range(slot - 1) if roles[k] == 'M')
            row['mplace'] = MID_KINDS[tactic][nth]
        sixteen.append(row)
    used = {r.get('orig') for r in sixteen}     # (the squad's rows the XI took)
    names = {tokens(r['s']['name']) for r in sixteen}
    rest = sorted((r for r in rows if id(r) not in used and tokens(r['s']['name']) not in names),
                  key=lambda r: (-r['value'], r['order']))
    bench = strongest_bench(rest)
    sixteen += [dict(r, position=r['pos']) for r in bench]
    return sixteen, dict(tactic=tactic, formation=fname, where=lu['where'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--world', default=os.path.join(BASE, 'worlds', TAG), help="the world's folder")
    ap.add_argument('--out', help="where the files go (default: the world's folder)")
    ap.add_argument('--offline', action='store_true', help='read the cache only')
    ap.add_argument('--no-kit-images', action='store_true', help="kits from the cache's pattern images only "
                    '(no fetching; a kit without its images from its colours)')
    ap.add_argument('--lineups', action='append', default=[], help="a folder of saved Transfermarkt national "
                    'team pages (tmlineup.py; repeatable; default bin/data_json/worldgen-cache/tm-pages)')
    ap.add_argument('--lineup-confs', default='all', help="the confederations whose nations play their real "
                    "line-ups (comma-separated, or 'all'; the others 4-4-2)")
    ap.add_argument('--overrides', default=OVERRIDES, help='the hand overrides (a nation\'s tactic, a starter\'s '
                    'place, a face): JSON, gitignored data')
    ap.add_argument('--report', default=os.path.join(BASE, 'worldgen', TAG, 'nations_report.txt'))
    a = ap.parse_args()
    curves = model.load_curves()
    dark = set(curves.get('dark', ()))
    world = World(a.world)
    titles = {n[2]: ARTICLE.get(n[2], n[2] + ' national football team') for n in NATIONS}
    texts = wiki.wikitext(list(titles.values()), offline=a.offline)
    points = rankings(a.offline)

    # 1. the squads and their matches
    nations = []
    for fifa, conf, wname, name, nat, slot in NATIONS:
        text = texts.get(titles[wname], '')
        sq = squad(text)
        for k, s in enumerate(sq):
            s['order'] = k
            s['club_rec'] = world.find_club(s['club'], s['clublink'], s['clubnat'])
            s['rec'] = world.match(s['name'], nat, s['club_rec'])
        pts = next((points[n] for n in RANK_NAME.get(wname, [wname]) if n in points), None)
        nations.append(dict(fifa=fifa, conf=conf, wname=wname, name=name, nat=nat, slot=slot, text=text,
                            squad=sq, points=pts, coach=coach(text)))

    # 2. the level of a player the world does not have: his club's, else his nation's from its
    #    FIFA points (a line through the nations with at least 8 matched players' median level)
    fit = [(n['points'], statistics.median(s['rec']['p']['price'] for s in n['squad'] if s['rec']))
           for n in nations if n['points'] and sum(1 for s in n['squad'] if s['rec']) >= 8]
    mx = statistics.mean(x for x, _ in fit)
    my = statistics.mean(y for _, y in fit)
    slope = sum((x - mx) * (y - my) for x, y in fit) / sum((x - mx) ** 2 for x, _ in fit)

    def by_points(p):
        return my + slope * ((p if p else min(x for x, _ in fit) - 100) - mx)

    # 3. the unmatched players' sub-positions, from their own articles
    want = sorted({s['link'] for n in nations for s in n['squad'] if not s['rec'] and s['link']})
    ptexts = wiki.wikitext(want, offline=a.offline) if want else {}

    # 3b. the real line-ups: a saved Transfermarkt page's, else the newest Wikipedia tournament's (its
    #     last match there)
    confs = None if a.lineup_confs == 'all' else set(a.lineup_confs.split(','))
    by_tm = {str(v): k for k, v in TM_IDS.items()}
    lineups, tm_pages = {}, {}
    for tid, r in tmlineup.pages(a.lineups or [os.path.join(BASE, 'worldgen-cache', 'tm-pages')]).items():
        if tid in by_tm:
            tm_pages[by_tm[tid]] = r
        if tid in by_tm and len(r['xi']) == 11:
            lineups[by_tm[tid]] = dict(source='tm', formation=r['formation'], xi=r['xi'], subs=r['subs'],
                                       where='Transfermarkt: ' + os.path.basename(r['path']))
    by_name = {}
    for nt in NATIONS:
        for nm in {nt[2], nt[3].title()} | set(RANK_NAME.get(nt[2], [])):
            by_name[nm.lower()] = nt[0]
    by_name.update({'united states': 'USA', 'usa': 'USA', 'turkey': 'TUR', 'türkiye': 'TUR', 'ivory coast': 'CIV',
                    'dr congo': 'COD', 'cape verde': 'CPV', 'south korea': 'KOR', 'north korea': 'PRK', 'iran': 'IRN',
                    'china pr': 'CHN', 'curaçao': 'CUW', 'curacao': 'CUW', 'czech republic': 'CZE', 'czechia': 'CZE',
                    'bosnia and herzegovina': 'BIH', 'republic of ireland': 'IRL'})
    arts = [x for tour in tmlineup.WIKI_TOURNAMENTS for x in tour]
    wtexts = wiki.wikitext(arts, offline=a.offline)
    newest = {}
    for rank, tour in enumerate(tmlineup.WIKI_TOURNAMENTS):
        for art in tour:
            for team, xi, subs in tmlineup.wiki_lineups(wtexts.get(art, '')):
                c = by_name.get(team.lower())
                if c and (c not in newest or newest[c][0] == rank):
                    newest[c] = (rank, art, xi, subs)
    for c, (rank, art, xi, subs) in newest.items():
        lineups.setdefault(c, dict(source='wiki', formation=None, xi=xi, subs=subs, where='Wikipedia: ' + art))
    unmapped = []
    overrides = json.load(open(a.overrides, encoding='utf-8')) if os.path.exists(a.overrides) else {}
    overrides = {k: v for k, v in overrides.items() if not k.startswith('_')}

    report = []
    files = {c: [] for c in FILES}
    base = {c: json.load(open(os.path.join(BASE, 'teams', FILES[c] + '.json'), encoding='utf-8')) for c in FILES}
    slots = {}
    for c, d in base.items():
        for i, t in enumerate(d['teams']):
            slots[t['team']['team_name']] = (c, i, t)
    counts = collections.Counter()
    built = {}
    for n in nations:
        rows = []
        local = []
        for s in n['squad']:
            if s['rec']:
                local.append(s['rec']['p']['price'])
        nation_level = by_points(n['points'])
        if len(local) >= 3:                     # (a nation of the world's: its home-based men nearer its own)
            nation_level = min(nation_level, statistics.median(local))
        for s in n['squad']:
            r = s['rec']
            if r:
                p = r['p']
                pos = CODE_POS.get(p['position'], 'M')
                if GROUP.get(pos) != (s['pos'] or GROUP.get(pos)) and not (s['pos'] == 'FW' and pos in ('RW', 'LW')):
                    pos = {'GK': 'G', 'DF': 'D', 'MF': 'M', 'FW': 'A'}.get(s['pos'], pos)
                rows.append(dict(src='world', s=s, rec=p, pos=pos, S=p['price'], value=p['price'] + s['caps'] / 1000,
                                 order=s['order'], rated=True))
                counts['matched'] += 1
                continue
            grp = s['pos'] if s['pos'] in FITS else 'MF'
            pos = {'GK': 'G', 'DF': 'D', 'MF': 'M', 'FW': 'A'}[grp]
            mtype = None
            if s['link'] and s['link'] in ptexts:
                for code in infobox_positions(ptexts[s['link']]):
                    if code in FITS[grp]:
                        pos = code
                        break
                mtype = infobox_midfield(ptexts[s['link']])
            c = s['club_rec']
            if c:
                same = sorted(q['p']['price'] for q in c['players'][:16]
                              if (q['p']['position'] == 0) == (grp == 'GK'))
                S = statistics.median(same) - 1 if same else nation_level
                counts['level from the club'] += 1
            else:
                S = nation_level - 1
                counts['level from the nation'] += 1
            S += min(3, s['caps'] // 30)
            S = int(max(2, min(45, round(S))))
            rows.append(dict(src='club' if c else 'nation', s=s, rec=None, pos=pos, S=S, mtype=mtype,
                             value=S + s['caps'] / 1000, order=s['order'], rated=True))
            counts['unmatched'] += 1
        # sides for the full-backs and wingers whose article does not say
        for side, (r, l) in (('FB', ('RB', 'LB')), ('W', ('RW', 'LW'))):
            for p in rows:
                if p['pos'] == side:
                    nr = sum(1 for q in rows if q['pos'] == r)
                    nl = sum(1 for q in rows if q['pos'] == l)
                    p['pos'] = r if nr <= nl else l
        lu = lineups.get(n['fifa']) if confs is None or n['conf'] in confs else None
        lineup_info = None
        if lu and rows:
            sixteen, lineup_info = lineup_sixteen(lu, rows, n, nation_level, world, unmapped,
                                                  overrides.get(n['fifa']))
        else:
            sixteen, _ = model.pick_sixteen(rows) if rows else ([], [])
            captain_in(sixteen, rows)
            if len(sixteen) >= 16:              # (the bench the strongest left: strongest_bench)
                taken = {id(r['s']) for r in sixteen[:11]}
                rest = sorted((r for r in rows if id(r['s']) not in taken), key=lambda r: (-r['value'], r['order']))
                sixteen[11:16] = [dict(r, position=r['pos']) for r in strongest_bench(rest)]
        players = []
        numbers = shirt_numbers_of(sixteen, tm_pages.get(n['fifa']))
        for p, number in zip(sixteen, numbers):
            s, rec = p['s'], p['rec']
            out = {'nationality': n['nat'], 'cached_match_value': 0, 'shirt_number': number,
                   'player_name': modport.swos_name(s['name'])}
            if rec:
                out.update({k: rec[k] for k in ('face',) if k in rec})
                if rec.get('looks'):
                    out['looks'] = rec['looks']
            else:
                out['face'] = 2 if n['nat'] in dark else 0
            out.update(position=POS_CODE[p['position']], flags=0, cards=0, injury=0, status_bucket=0)
            if rec and (rec['position'] == 0) == (p['position'] == 'G') and \
                    (p['position'] == 'G' or CODE_POS.get(rec['position']) == p['position']):
                for f in SKILL_FIELDS:                  # (his club's position: his club's skills)
                    out[f] = rec[f]
                out['price'] = rec['price']
            elif rec and rec['position'] != 0 and p['position'] != 'G':
                # another position than at his club (the place's role): his skills spread again by
                # the new position's priorities, the total and the price his club's (the owner)
                total = sum(rec[f] & 7 for f in SKILL_FIELDS)
                v, keys = order_skills(total, p['position'], p.get('mtype'), p.get('mplace'), s['name'])
                for i, f in enumerate(SKILL_FIELDS):
                    out[f] = v[i] | (8 if i in keys else 0)
                out['price'] = rec['price']
                counts['club players re-skilled for another position'] += 1
            elif p['position'] == 'G':
                for f in SKILL_FIELDS:
                    out[f] = 0
                out['price'] = p['S']
            else:
                v, keys = order_skills(p['S'], p['position'], p.get('mtype'), p.get('mplace'), s['name'])
                for i, f in enumerate(SKILL_FIELDS):
                    out[f] = v[i] | (8 if i in keys else 0)
                out['price'] = sum(v)
            out.update(goal_stats=[0, 0, 0, 0], form=0)
            for who, face in ((overrides.get(n['fifa']) or {}).get('faces') or {}).items():
                if surname_is(p, who):
                    out['face'] = FACES[face]
            players.append(out)
        if not lineup_info:
            players, sixteen = order_lineup(players, sixteen)
        # shirt numbers: each once, 1..99
        used = set()
        for p in players:
            if not 1 <= p['shirt_number'] <= 99 or p['shirt_number'] in used:
                p['shirt_number'] = 0
            used.add(p['shirt_number'])
        free = [k for k in range(1, 100) if k not in used]
        for p in players:
            if not p['shirt_number']:
                p['shirt_number'] = free.pop(0)
        built[n['fifa']] = (n, players, lineup_info)
        m = sum(1 for p in sixteen if p['src'] == 'world')
        report.append('%s %-17s squad %2d  of the 16: %2d from the world, %2d estimated  points %s  coach %s  %s  %s'
                      % (n['fifa'], n['name'], len(n['squad']), m, len(sixteen) - m,
                         '%.0f' % n['points'] if n['points'] else '-', n['coach'] or '-',
                         '' if len(players) == 16 else '(SHORT: %d players)' % len(players),
                         ('%s as %s (%s)' % (lineup_info['formation'], TACTIC_NAMES[lineup_info['tactic']],
                                             lineup_info['where'])) if lineup_info else '4-4-2 (no line-up)'))

    # 4. the files: 96/97's slots in place, the new nations after them
    out_teams = {c: list(base[c]['teams']) for c in FILES}
    placed = {}
    for n in nations:
        if n['slot']:
            c, i, _ = slots[n['slot']]
            placed[n['fifa']] = (c, i)
    for n in nations:
        if not n['slot']:
            c = CONF_FILE[n['conf']]
            placed[n['fifa']] = (c, len(out_teams[c]))
            out_teams[c].append(None)
    for n in nations:
        c, i = placed[n['fifa']]
        old = base[c]['teams'][i]['team'] if n['slot'] else None
        head = dict(old) if old else new_head()
        set_kits(head, n['text'], a.offline or a.no_kit_images, colours_only=not old)
        nn, players, lineup_info = built[n['fifa']]
        if len(players) < 16:
            print('%s: %d players in its squad -- kept 96/97\'s team' % (n['fifa'], len(players)) if old else
                  '%s: %d players -- no team' % (n['fifa'], len(players)), file=sys.stderr)
            if old:
                out_teams[c][i] = base[c]['teams'][i]
                continue
            sys.exit('a new nation without a squad: %s' % n['fifa'])
        first = base[c]['teams'][0]['team']['global_team_number']
        coach_name = modport.swos_name(n['coach'], 24)     # ('' when the post is vacant: not 96/97's man)
        out_teams[c][i] = team_record(head, players, n['name'], n['nat'], coach_name, n['fifa'], c, i, first + i,
                                      list(range(16)) if lineup_info else None,
                                      lineup_info['tactic'] if lineup_info else TACTIC_442)
    out_dir = a.out or a.world
    os.makedirs(os.path.join(out_dir, 'teams'), exist_ok=True)
    for c, teams in out_teams.items():
        d = dict(base[c])
        d['teams'], d['count'] = teams, len(teams)
        path = os.path.join(out_dir, 'teams', FILES[c] + '.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(d, f, indent=1, ensure_ascii=False)
            f.write('\n')
        print('%s: %d national teams' % (os.path.relpath(path, REPO), len(teams)))

    # 5. the records (include/national_teams.h): the nations with a league of the world first
    #    (a national job is offered from them), then the rest; skill as 96/97's (the 11's level)
    def strength(players):
        return statistics.mean(p['price'] for p in players[:11])
    recs = []
    for n in nations:
        c, i = placed[n['fifa']]
        home = world.home_file.get(n['fifa'], 255)
        players = out_teams[c][i]['players']
        skill = int(max(1, min(40, round(0.85 * strength(players) - 2))))
        recs.append(dict(country=c, ordinal=i, nationality=n['nat'], team_file=home, skill=skill,
                         _job=home != 255, _fifa=n['fifa']))
    recs.sort(key=lambda r: (not r['_job'], r['country'], r['ordinal']))
    jobs = sum(1 for r in recs if r['_job'])
    nt = {'about': 'World 2026-27: the 211 FIFA members (scripts/worldgen/nations.py)',
          'records': [{k: v for k, v in r.items() if not k.startswith('_')} for r in recs],
          'job_offer_pick': jobs}
    with open(os.path.join(out_dir, 'national_teams.json'), 'w', encoding='utf-8') as f:
        json.dump(nt, f, indent=1)
        f.write('\n')
    print('national_teams.json: %d records, %d for a job' % (len(recs), jobs))

    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    with open(a.report, 'w', encoding='utf-8') as f:
        f.write('\n'.join(report) + '\n')
    print('players: %s; the nation line: level = %.1f + %.4f x (points - %.0f), %d nations' %
          (dict(counts), my, slope, mx, len(fit)))
    print('report:', os.path.relpath(a.report, REPO))
    for u in unmapped:
        print('formation not in the table (nearest tactic taken):', u)


if __name__ == '__main__':
    main()
