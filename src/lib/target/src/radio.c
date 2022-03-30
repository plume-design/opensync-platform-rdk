/*
Copyright (c) 2017, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <ev.h>

#include "log.h"
#include "const.h"
#include "target.h"
#include "target_internal.h"
#include "os_nif.h"

#ifndef __WIFI_HAL_H__
#include <ccsp/wifi_hal.h>
#endif

#define MODULE_ID LOG_MODULE_ID_RADIO
#define HAL_CB_QUEUE_MAX    20

#define CSA_TBTT                        25
#define RESYNC_UPDATE_DELAY_SECONDS     5

/*****************************************************************************/

static c_item_t map_band_str[] =
{
    C_ITEM_STR(WIFI_FREQUENCY_2_4_BAND,         "2.4G"),
    C_ITEM_STR(WIFI_FREQUENCY_5_BAND,           "5G"),
    C_ITEM_STR(WIFI_FREQUENCY_5L_BAND,          "5GL"),
    C_ITEM_STR(WIFI_FREQUENCY_5H_BAND,          "5GH"),
    C_ITEM_STR(WIFI_FREQUENCY_6_BAND,           "6G")
};

static c_item_t map_country_str[] =
{
    C_ITEM_STR(wifi_countrycode_AC,         "AC"), /**< ASCENSION ISLAND */
    C_ITEM_STR(wifi_countrycode_AD,         "AD"), /**< ANDORRA */
    C_ITEM_STR(wifi_countrycode_AE,         "AE"), /**< UNITED ARAB EMIRATES */
    C_ITEM_STR(wifi_countrycode_AF,         "AF"), /**< AFGHANISTAN */
    C_ITEM_STR(wifi_countrycode_AG,         "AG"), /**< ANTIGUA AND BARBUDA */
    C_ITEM_STR(wifi_countrycode_AI,         "AI"), /**< ANGUILLA */
    C_ITEM_STR(wifi_countrycode_AL,         "AL"), /**< ALBANIA */
    C_ITEM_STR(wifi_countrycode_AM,         "AM"), /**< ARMENIA */
    C_ITEM_STR(wifi_countrycode_AN,         "AN"), /**< NETHERLANDS ANTILLES */
    C_ITEM_STR(wifi_countrycode_AO,         "AO"), /**< ANGOLA */
    C_ITEM_STR(wifi_countrycode_AQ,         "AQ"), /**< ANTARCTICA */
    C_ITEM_STR(wifi_countrycode_AR,         "AR"), /**< ARGENTINA */
    C_ITEM_STR(wifi_countrycode_AS,         "AS"), /**< AMERICAN SAMOA */
    C_ITEM_STR(wifi_countrycode_AT,         "AT"), /**< AUSTRIA */
    C_ITEM_STR(wifi_countrycode_AU,         "AU"), /**< AUSTRALIA */
    C_ITEM_STR(wifi_countrycode_AW,         "AW"), /**< ARUBA */
    C_ITEM_STR(wifi_countrycode_AZ,         "AZ"), /**< AZERBAIJAN */
    C_ITEM_STR(wifi_countrycode_BA,         "BA"), /**< BOSNIA AND HERZEGOVINA */
    C_ITEM_STR(wifi_countrycode_BB,         "BB"), /**< BARBADOS */
    C_ITEM_STR(wifi_countrycode_BD,         "BD"), /**< BANGLADESH */
    C_ITEM_STR(wifi_countrycode_BE,         "BE"), /**< BELGIUM */
    C_ITEM_STR(wifi_countrycode_BF,         "BF"), /**< BURKINA FASO */
    C_ITEM_STR(wifi_countrycode_BG,         "BG"), /**< BULGARIA */
    C_ITEM_STR(wifi_countrycode_BH,         "BH"), /**< BAHRAIN */
    C_ITEM_STR(wifi_countrycode_BI,         "BI"), /**< BURUNDI */
    C_ITEM_STR(wifi_countrycode_BJ,         "BJ"), /**< BENIN */
    C_ITEM_STR(wifi_countrycode_BM,         "BM"), /**< BERMUDA */
    C_ITEM_STR(wifi_countrycode_BN,         "BN"), /**< BRUNEI DARUSSALAM */
    C_ITEM_STR(wifi_countrycode_BO,         "BO"), /**< BOLIVIA */
    C_ITEM_STR(wifi_countrycode_BR,         "BR"), /**< BRAZIL */
    C_ITEM_STR(wifi_countrycode_BS,         "BS"), /**< BAHAMAS */
    C_ITEM_STR(wifi_countrycode_BT,         "BT"), /**< BHUTAN */
    C_ITEM_STR(wifi_countrycode_BV,         "BV"), /**< BOUVET ISLAND */
    C_ITEM_STR(wifi_countrycode_BW,         "BW"), /**< BOTSWANA */
    C_ITEM_STR(wifi_countrycode_BY,         "BY"), /**< BELARUS */
    C_ITEM_STR(wifi_countrycode_BZ,         "BZ"), /**< BELIZE */
    C_ITEM_STR(wifi_countrycode_CA,         "CA"), /**< CANADA */
    C_ITEM_STR(wifi_countrycode_CC,         "CC"), /**< COCOS (KEELING) ISLANDS */
    C_ITEM_STR(wifi_countrycode_CD,         "CD"), /**< CONGO, THE DEMOCRATIC REPUBLIC OF THE */
    C_ITEM_STR(wifi_countrycode_CF,         "CF"), /**< CENTRAL AFRICAN REPUBLIC */
    C_ITEM_STR(wifi_countrycode_CG,         "CG"), /**< CONGO */
    C_ITEM_STR(wifi_countrycode_CH,         "CH"), /**< SWITZERLAND */
    C_ITEM_STR(wifi_countrycode_CI,         "CI"), /**< COTE D'IVOIRE */
    C_ITEM_STR(wifi_countrycode_CK,         "CK"), /**< COOK ISLANDS */
    C_ITEM_STR(wifi_countrycode_CL,         "CL"), /**< CHILE */
    C_ITEM_STR(wifi_countrycode_CM,         "CM"), /**< CAMEROON */
    C_ITEM_STR(wifi_countrycode_CN,         "CN"), /**< CHINA */
    C_ITEM_STR(wifi_countrycode_CO,         "CO"), /**< COLOMBIA */
    C_ITEM_STR(wifi_countrycode_CP,         "CP"), /**< CLIPPERTON ISLAND */
    C_ITEM_STR(wifi_countrycode_CR,         "CR"), /**< COSTA RICA */
    C_ITEM_STR(wifi_countrycode_CU,         "CU"), /**< CUBA */
    C_ITEM_STR(wifi_countrycode_CV,         "CV"), /**< CAPE VERDE */
    C_ITEM_STR(wifi_countrycode_CY,         "CY"), /**< CYPRUS */
    C_ITEM_STR(wifi_countrycode_CX,         "CX"), /**< CHRISTMAS ISLAND */
    C_ITEM_STR(wifi_countrycode_CZ,         "CZ"), /**< CZECH REPUBLIC */
    C_ITEM_STR(wifi_countrycode_DE,         "DE"), /**< GERMANY */
    C_ITEM_STR(wifi_countrycode_DJ,         "DJ"), /**< DJIBOUTI */
    C_ITEM_STR(wifi_countrycode_DK,         "DK"), /**< DENMARK */
    C_ITEM_STR(wifi_countrycode_DM,         "DM"), /**< DOMINICA */
    C_ITEM_STR(wifi_countrycode_DO,         "DO"), /**< DOMINICAN REPUBLIC */
    C_ITEM_STR(wifi_countrycode_DZ,         "DZ"), /**< ALGERIA */
    C_ITEM_STR(wifi_countrycode_EC,         "EC"), /**< ECUADOR */
    C_ITEM_STR(wifi_countrycode_EE,         "EE"), /**< ESTONIA */
    C_ITEM_STR(wifi_countrycode_EG,         "EG"), /**< EGYPT */
    C_ITEM_STR(wifi_countrycode_EH,         "EH"), /**< WESTERN SAHARA */
    C_ITEM_STR(wifi_countrycode_ER,         "ER"), /**< ERITREA */
    C_ITEM_STR(wifi_countrycode_ES,         "ES"), /**< SPAIN */
    C_ITEM_STR(wifi_countrycode_ET,         "ET"), /**< ETHIOPIA */
    C_ITEM_STR(wifi_countrycode_FI,         "FI"), /**< FINLAND */
    C_ITEM_STR(wifi_countrycode_FJ,         "FJ"), /**< FIJI */
    C_ITEM_STR(wifi_countrycode_FK,         "FK"), /**< FALKLAND ISLANDS (MALVINAS) */
    C_ITEM_STR(wifi_countrycode_FM,         "FM"), /**< MICRONESIA, FEDERATED STATES OF */
    C_ITEM_STR(wifi_countrycode_FO,         "FO"), /**< FAROE ISLANDS */
    C_ITEM_STR(wifi_countrycode_FR,         "FR"), /**< FRANCE */
    C_ITEM_STR(wifi_countrycode_GA,         "GA"), /**< GABON */
    C_ITEM_STR(wifi_countrycode_GB,         "GB"), /**< UNITED KINGDOM */
    C_ITEM_STR(wifi_countrycode_GD,         "GD"), /**< GRENADA */
    C_ITEM_STR(wifi_countrycode_GE,         "GE"), /**< GEORGIA */
    C_ITEM_STR(wifi_countrycode_GF,         "GF"), /**< FRENCH GUIANA */
    C_ITEM_STR(wifi_countrycode_GG,         "GG"), /**< GUERNSEY */
    C_ITEM_STR(wifi_countrycode_GH,         "GH"), /**< GHANA */
    C_ITEM_STR(wifi_countrycode_GI,         "GI"), /**< GIBRALTAR */
    C_ITEM_STR(wifi_countrycode_GL,         "GL"), /**< GREENLAND */
    C_ITEM_STR(wifi_countrycode_GM,         "GM"), /**< GAMBIA */
    C_ITEM_STR(wifi_countrycode_GN,         "GN"), /**< GUINEA */
    C_ITEM_STR(wifi_countrycode_GP,         "GP"), /**< GUADELOUPE */
    C_ITEM_STR(wifi_countrycode_GQ,         "GQ"), /**< EQUATORIAL GUINEA */
    C_ITEM_STR(wifi_countrycode_GR,         "GR"), /**< GREECE */
    C_ITEM_STR(wifi_countrycode_GS,         "GS"), /**< SOUTH GEORGIA AND THE SOUTH SANDWICH ISLANDS */
    C_ITEM_STR(wifi_countrycode_GT,         "GT"), /**< GUATEMALA */
    C_ITEM_STR(wifi_countrycode_GU,         "GU"), /**< GUAM */
    C_ITEM_STR(wifi_countrycode_GW,         "GW"), /**< GUINEA-BISSAU */
    C_ITEM_STR(wifi_countrycode_GY,         "GY"), /**< GUYANA */
    C_ITEM_STR(wifi_countrycode_HR,         "HR"), /**< CROATIA */
    C_ITEM_STR(wifi_countrycode_HT,         "HT"), /**< HAITI */
    C_ITEM_STR(wifi_countrycode_HM,         "HM"), /**< HEARD ISLAND AND MCDONALD ISLANDS */
    C_ITEM_STR(wifi_countrycode_HN,         "HN"), /**< HONDURAS */
    C_ITEM_STR(wifi_countrycode_HK,         "HK"), /**< HONG KONG */
    C_ITEM_STR(wifi_countrycode_HU,         "HU"), /**< HUNGARY */
    C_ITEM_STR(wifi_countrycode_IS,         "IS"), /**< ICELAND */
    C_ITEM_STR(wifi_countrycode_IN,         "IN"), /**< INDIA */
    C_ITEM_STR(wifi_countrycode_ID,         "ID"), /**< INDONESIA */
    C_ITEM_STR(wifi_countrycode_IR,         "IR"), /**< IRAN, ISLAMIC REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_IQ,         "IQ"), /**< IRAQ */
    C_ITEM_STR(wifi_countrycode_IE,         "IE"), /**< IRELAND */
    C_ITEM_STR(wifi_countrycode_IL,         "IL"), /**< ISRAEL */
    C_ITEM_STR(wifi_countrycode_IM,         "IM"), /**< MAN, ISLE OF */
    C_ITEM_STR(wifi_countrycode_IT,         "IT"), /**< ITALY */
    C_ITEM_STR(wifi_countrycode_IO,         "IO"), /**< BRITISH INDIAN OCEAN TERRITORY */
    C_ITEM_STR(wifi_countrycode_JM,         "JM"), /**< JAMAICA */
    C_ITEM_STR(wifi_countrycode_JP,         "JP"), /**< JAPAN */
    C_ITEM_STR(wifi_countrycode_JE,         "JE"), /**< JERSEY */
    C_ITEM_STR(wifi_countrycode_JO,         "JO"), /**< JORDAN */
    C_ITEM_STR(wifi_countrycode_KE,         "KE"), /**< KENYA */
    C_ITEM_STR(wifi_countrycode_KG,         "KG"), /**< KYRGYZSTAN */
    C_ITEM_STR(wifi_countrycode_KH,         "KH"), /**< CAMBODIA */
    C_ITEM_STR(wifi_countrycode_KI,         "KI"), /**< KIRIBATI */
    C_ITEM_STR(wifi_countrycode_KM,         "KM"), /**< COMOROS */
    C_ITEM_STR(wifi_countrycode_KN,         "KN"), /**< SAINT KITTS AND NEVIS */
    C_ITEM_STR(wifi_countrycode_KP,         "KP"), /**< KOREA, DEMOCRATIC PEOPLE'S REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_KR,         "KR"), /**< KOREA, REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_KW,         "KW"), /**< KUWAIT */
    C_ITEM_STR(wifi_countrycode_KY,         "KY"), /**< CAYMAN ISLANDS */
    C_ITEM_STR(wifi_countrycode_KZ,         "KZ"), /**< KAZAKHSTAN */
    C_ITEM_STR(wifi_countrycode_LA,         "LA"), /**< LAO PEOPLE'S DEMOCRATIC REPUBLIC */
    C_ITEM_STR(wifi_countrycode_LB,         "LB"), /**< LEBANON */
    C_ITEM_STR(wifi_countrycode_LC,         "LC"), /**< SAINT LUCIA */
    C_ITEM_STR(wifi_countrycode_LI,         "LI"), /**< LIECHTENSTEIN */
    C_ITEM_STR(wifi_countrycode_LK,         "LK"), /**< SRI LANKA */
    C_ITEM_STR(wifi_countrycode_LR,         "LR"), /**< LIBERIA */
    C_ITEM_STR(wifi_countrycode_LS,         "LS"), /**< LESOTHO */
    C_ITEM_STR(wifi_countrycode_LT,         "LT"), /**< LITHUANIA */
    C_ITEM_STR(wifi_countrycode_LU,         "LU"), /**< LUXEMBOURG */
    C_ITEM_STR(wifi_countrycode_LV,         "LV"), /**< LATVIA */
    C_ITEM_STR(wifi_countrycode_LY,         "LY"), /**< LIBYAN ARAB JAMAHIRIYA */
    C_ITEM_STR(wifi_countrycode_MA,         "MA"), /**< MOROCCO */
    C_ITEM_STR(wifi_countrycode_MC,         "MC"), /**< MONACO */
    C_ITEM_STR(wifi_countrycode_MD,         "MD"), /**< MOLDOVA, REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_ME,         "ME"), /**< MONTENEGRO */
    C_ITEM_STR(wifi_countrycode_MG,         "MG"), /**< MADAGASCAR */
    C_ITEM_STR(wifi_countrycode_MH,         "MH"), /**< MARSHALL ISLANDS */
    C_ITEM_STR(wifi_countrycode_MK,         "MK"), /**< MACEDONIA, THE FORMER YUGOSLAV REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_ML,         "ML"), /**< MALI */
    C_ITEM_STR(wifi_countrycode_MM,         "MM"), /**< MYANMAR */
    C_ITEM_STR(wifi_countrycode_MN,         "MN"), /**< MONGOLIA */
    C_ITEM_STR(wifi_countrycode_MO,         "MO"), /**< MACAO */
    C_ITEM_STR(wifi_countrycode_MQ,         "MQ"), /**< MARTINIQUE */
    C_ITEM_STR(wifi_countrycode_MR,         "MR"), /**< MAURITANIA */
    C_ITEM_STR(wifi_countrycode_MS,         "MS"), /**< MONTSERRAT */
    C_ITEM_STR(wifi_countrycode_MT,         "MT"), /**< MALTA */
    C_ITEM_STR(wifi_countrycode_MU,         "MU"), /**< MAURITIUS */
    C_ITEM_STR(wifi_countrycode_MV,         "MV"), /**< MALDIVES */
    C_ITEM_STR(wifi_countrycode_MW,         "MW"), /**< MALAWI */
    C_ITEM_STR(wifi_countrycode_MX,         "MX"), /**< MEXICO */
    C_ITEM_STR(wifi_countrycode_MY,         "MY"), /**< MALAYSIA */
    C_ITEM_STR(wifi_countrycode_MZ,         "MZ"), /**< MOZAMBIQUE */
    C_ITEM_STR(wifi_countrycode_NA,         "NA"), /**< NAMIBIA */
    C_ITEM_STR(wifi_countrycode_NC,         "NC"), /**< NEW CALEDONIA */
    C_ITEM_STR(wifi_countrycode_NE,         "NE"), /**< NIGER */
    C_ITEM_STR(wifi_countrycode_NF,         "NF"), /**< NORFOLK ISLAND */
    C_ITEM_STR(wifi_countrycode_NG,         "NG"), /**< NIGERIA */
    C_ITEM_STR(wifi_countrycode_NI,         "NI"), /**< NICARAGUA */
    C_ITEM_STR(wifi_countrycode_NL,         "NL"), /**< NETHERLANDS */
    C_ITEM_STR(wifi_countrycode_NO,         "NO"), /**< NORWAY */
    C_ITEM_STR(wifi_countrycode_NP,         "NP"), /**< NEPAL */
    C_ITEM_STR(wifi_countrycode_NR,         "NR"), /**< NAURU */
    C_ITEM_STR(wifi_countrycode_NU,         "NU"), /**< NIUE */
    C_ITEM_STR(wifi_countrycode_NZ,         "NZ"), /**< NEW ZEALAND */
    C_ITEM_STR(wifi_countrycode_MP,         "MP"), /**< NORTHERN MARIANA ISLANDS */
    C_ITEM_STR(wifi_countrycode_OM,         "OM"), /**< OMAN */
    C_ITEM_STR(wifi_countrycode_PA,         "PA"), /**< PANAMA */
    C_ITEM_STR(wifi_countrycode_PE,         "PE"), /**< PERU */
    C_ITEM_STR(wifi_countrycode_PF,         "PF"), /**< FRENCH POLYNESIA */
    C_ITEM_STR(wifi_countrycode_PG,         "PG"), /**< PAPUA NEW GUINEA */
    C_ITEM_STR(wifi_countrycode_PH,         "PH"), /**< PHILIPPINES */
    C_ITEM_STR(wifi_countrycode_PK,         "PK"), /**< PAKISTAN */
    C_ITEM_STR(wifi_countrycode_PL,         "PL"), /**< POLAND */
    C_ITEM_STR(wifi_countrycode_PM,         "PM"), /**< SAINT PIERRE AND MIQUELON */
    C_ITEM_STR(wifi_countrycode_PN,         "PN"), /**< PITCAIRN */
    C_ITEM_STR(wifi_countrycode_PR,         "PR"), /**< PUERTO RICO */
    C_ITEM_STR(wifi_countrycode_PS,         "PS"), /**< PALESTINIAN TERRITORY, OCCUPIED */
    C_ITEM_STR(wifi_countrycode_PT,         "PT"), /**< PORTUGAL */
    C_ITEM_STR(wifi_countrycode_PW,         "PW"), /**< PALAU */
    C_ITEM_STR(wifi_countrycode_PY,         "PY"), /**< PARAGUAY */
    C_ITEM_STR(wifi_countrycode_QA,         "QA"), /**< QATAR */
    C_ITEM_STR(wifi_countrycode_RE,         "RE"), /**< REUNION */
    C_ITEM_STR(wifi_countrycode_RO,         "RO"), /**< ROMANIA */
    C_ITEM_STR(wifi_countrycode_RS,         "RS"), /**< SERBIA */
    C_ITEM_STR(wifi_countrycode_RU,         "RU"), /**< RUSSIAN FEDERATION */
    C_ITEM_STR(wifi_countrycode_RW,         "RW"), /**< RWANDA */
    C_ITEM_STR(wifi_countrycode_SA,         "SA"), /**< SAUDI ARABIA */
    C_ITEM_STR(wifi_countrycode_SB,         "SB"), /**< SOLOMON ISLANDS */
    C_ITEM_STR(wifi_countrycode_SD,         "SD"), /**< SUDAN */
    C_ITEM_STR(wifi_countrycode_SE,         "SE"), /**< SWEDEN */
    C_ITEM_STR(wifi_countrycode_SC,         "SC"), /**< SEYCHELLES */
    C_ITEM_STR(wifi_countrycode_SG,         "SG"), /**< SINGAPORE */
    C_ITEM_STR(wifi_countrycode_SH,         "SH"), /**< SAINT HELENA */
    C_ITEM_STR(wifi_countrycode_SI,         "SI"), /**< SLOVENIA */
    C_ITEM_STR(wifi_countrycode_SJ,         "SJ"), /**< SVALBARD AND JAN MAYEN */
    C_ITEM_STR(wifi_countrycode_SK,         "SK"), /**< SLOVAKIA */
    C_ITEM_STR(wifi_countrycode_SL,         "SL"), /**< SIERRA LEONE */
    C_ITEM_STR(wifi_countrycode_SM,         "SM"), /**< SAN MARINO */
    C_ITEM_STR(wifi_countrycode_SN,         "SN"), /**< SENEGAL */
    C_ITEM_STR(wifi_countrycode_SO,         "SO"), /**< SOMALIA */
    C_ITEM_STR(wifi_countrycode_SR,         "SR"), /**< SURINAME */
    C_ITEM_STR(wifi_countrycode_ST,         "ST"), /**< SAO TOME AND PRINCIPE */
    C_ITEM_STR(wifi_countrycode_SV,         "SV"), /**< EL SALVADOR */
    C_ITEM_STR(wifi_countrycode_SY,         "SY"), /**< SYRIAN ARAB REPUBLIC */
    C_ITEM_STR(wifi_countrycode_SZ,         "SZ"), /**< SWAZILAND */
    C_ITEM_STR(wifi_countrycode_TA,         "TA"), /**< TRISTAN DA CUNHA */
    C_ITEM_STR(wifi_countrycode_TC,         "TC"), /**< TURKS AND CAICOS ISLANDS */
    C_ITEM_STR(wifi_countrycode_TD,         "TD"), /**< CHAD */
    C_ITEM_STR(wifi_countrycode_TF,         "TF"), /**< FRENCH SOUTHERN TERRITORIES */
    C_ITEM_STR(wifi_countrycode_TG,         "TG"), /**< TOGO */
    C_ITEM_STR(wifi_countrycode_TH,         "TH"), /**< THAILAND */
    C_ITEM_STR(wifi_countrycode_TJ,         "TJ"), /**< TAJIKISTAN */
    C_ITEM_STR(wifi_countrycode_TK,         "TK"), /**< TOKELAU */
    C_ITEM_STR(wifi_countrycode_TL,         "TL"), /**< TIMOR-LESTE (EAST TIMOR) */
    C_ITEM_STR(wifi_countrycode_TM,         "TM"), /**< TURKMENISTAN */
    C_ITEM_STR(wifi_countrycode_TN,         "TN"), /**< TUNISIA */
    C_ITEM_STR(wifi_countrycode_TO,         "TO"), /**< TONGA */
    C_ITEM_STR(wifi_countrycode_TR,         "TR"), /**< TURKEY */
    C_ITEM_STR(wifi_countrycode_TT,         "TT"), /**< TRINIDAD AND TOBAGO */
    C_ITEM_STR(wifi_countrycode_TV,         "TV"), /**< TUVALU */
    C_ITEM_STR(wifi_countrycode_TW,         "TW"), /**< TAIWAN, PROVINCE OF CHINA */
    C_ITEM_STR(wifi_countrycode_TZ,         "TZ"), /**< TANZANIA, UNITED REPUBLIC OF */
    C_ITEM_STR(wifi_countrycode_UA,         "UA"), /**< UKRAINE */
    C_ITEM_STR(wifi_countrycode_UG,         "UG"), /**< UGANDA */
    C_ITEM_STR(wifi_countrycode_UM,         "UM"), /**< UNITED STATES MINOR OUTLYING ISLANDS */
    C_ITEM_STR(wifi_countrycode_US,         "US"), /**< UNITED STATES */
    C_ITEM_STR(wifi_countrycode_UY,         "UY"), /**< URUGUAY */
    C_ITEM_STR(wifi_countrycode_UZ,         "UZ"), /**< UZBEKISTAN */
    C_ITEM_STR(wifi_countrycode_VA,         "VA"), /**< HOLY SEE (VATICAN CITY STATE) */
    C_ITEM_STR(wifi_countrycode_VC,         "VC"), /**< SAINT VINCENT AND THE GRENADINES */
    C_ITEM_STR(wifi_countrycode_VE,         "VE"), /**< VENEZUELA */
    C_ITEM_STR(wifi_countrycode_VG,         "VG"), /**< VIRGIN ISLANDS, BRITISH */
    C_ITEM_STR(wifi_countrycode_VI,         "VI"), /**< VIRGIN ISLANDS, U.S. */
    C_ITEM_STR(wifi_countrycode_VN,         "VN"), /**< VIET NAM */
    C_ITEM_STR(wifi_countrycode_VU,         "VU"), /**< VANUATU */
    C_ITEM_STR(wifi_countrycode_WF,         "WF"), /**< WALLIS AND FUTUNA */
    C_ITEM_STR(wifi_countrycode_WS,         "WS"), /**< SAMOA */
    C_ITEM_STR(wifi_countrycode_YE,         "YE"), /**< YEMEN */
    C_ITEM_STR(wifi_countrycode_YT,         "YT"), /**< MAYOTTE */
    C_ITEM_STR(wifi_countrycode_YU,         "YU"), /**< YUGOSLAVIA */
    C_ITEM_STR(wifi_countrycode_ZA,         "ZA"), /**< SOUTH AFRICA */
    C_ITEM_STR(wifi_countrycode_ZM,         "ZM"), /**< ZAMBIA */
    C_ITEM_STR(wifi_countrycode_ZW,         "ZW"), /**< ZIMBABWE */
};

static c_item_t map_htmode_str[] =
{
    C_ITEM_STR(WIFI_CHANNELBANDWIDTH_20MHZ,    "HT20"),
    C_ITEM_STR(WIFI_CHANNELBANDWIDTH_40MHZ,    "HT40"),
    C_ITEM_STR(WIFI_CHANNELBANDWIDTH_80MHZ,    "HT80"),
    C_ITEM_STR(WIFI_CHANNELBANDWIDTH_80_80MHZ, "HT80+80"),
    C_ITEM_STR(WIFI_CHANNELBANDWIDTH_160MHZ,   "HT160")
};

static c_item_t map_variant_str[] =
{
    C_ITEM_STR(WIFI_80211_VARIANT_A,    "11a"),
    C_ITEM_STR(WIFI_80211_VARIANT_B,    "11b"),
    C_ITEM_STR(WIFI_80211_VARIANT_G,    "11g"),
    C_ITEM_STR(WIFI_80211_VARIANT_N,    "11n"),
    C_ITEM_STR(WIFI_80211_VARIANT_H,    "11h"),
    C_ITEM_STR(WIFI_80211_VARIANT_AC,   "11ac"),
    C_ITEM_STR(WIFI_80211_VARIANT_AD,   "11ad"),
    C_ITEM_STR(WIFI_80211_VARIANT_AX,   "11ax")
};

typedef enum
{
    TARGET_RADIO_CHAN_MODE_MANUAL    = 0,
    TARGET_RADIO_CHAN_MODE_AUTO      = 1,
    TARGET_RADIO_CHAN_MODE_CLOUD     = 2,
} target_radio_chan_mode_t;

static c_item_t map_csa_chanwidth[] =
{
    C_ITEM_STR(20,                              "HT20"),
    C_ITEM_STR(40,                              "HT40"),
    C_ITEM_STR(80,                              "HT80"),
    C_ITEM_STR(80,                              "HT80+80"),
    C_ITEM_STR(160,                             "HT160")
};

typedef struct
{
    UINT                  radioIndex;
    wifi_chan_eventType_t event;
    UCHAR                 channel;
    struct timeval        tv;

    ds_dlist_node_t       node;
} hal_cb_entry_t;

static radio_cloud_mode_t radio_cloud_mode = RADIO_CLOUD_MODE_UNKNOWN;

static bool dfs_event_cb_registered = false;
static char dfs_last_channel[32];             // last channel reported by driver in radar event
static char dfs_num_detected[32];             // number of DFS events detected
static unsigned int dfs_radar_timestamp = 0;  // saved timestamp from radar detection event
static struct ev_loop      *hal_cb_loop = NULL;
static pthread_mutex_t      hal_cb_lock;
static ev_async             hal_cb_async;
static ds_dlist_t           hal_cb_queue;
static int                  hal_cb_queue_len = 0;

static ev_timer healthcheck_timer;
static ev_timer radio_resync_all_task_timer;
static struct target_radio_ops g_rops;
static bool g_resync_ongoing = false;

bool target_radio_config_need_reset()
{
    return true;
}

static void healthcheck_task(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    LOGI("Healthcheck re-sync");
    radio_trigger_resync();
    ev_timer_stop(wifihal_evloop, &healthcheck_timer);
    ev_timer_set(&healthcheck_timer, CONFIG_RDK_HEALTHCHECK_INTERVAL, 0.);
    ev_timer_start(wifihal_evloop, &healthcheck_timer);
}

static bool radio_change_channel(
        INT radioIndex,
        int channel,
        const char *ht_mode)
{
    c_item_t            *citem;
    INT                 ret;
    int                 ch_width = 0;
    char                radio_ifname[128];

    LOGD("Switch to channel %d triggered", channel);

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radioIndex, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio ifname for idx %d", __func__,
                radioIndex);
        return false;
    }

    if (ht_mode == NULL)
    {
        LOGE("%s: No ht_mode configured, cannot change channel!", radio_ifname);
        return false;
    }

    if ((citem = c_get_item_by_str(map_csa_chanwidth, ht_mode)) == NULL)
    {
        LOGE("%s: Failed to change channel -- HT Mode '%s' unsupported",
             radio_ifname, ht_mode);
        return false;
    }
    ch_width = citem->key;

    ret = wifi_pushRadioChannel2(radioIndex, channel, ch_width, CSA_TBTT);
    LOGD("[WIFI_HAL SET] wifi_pushRadioChannel2(%d, %d, %d, %d) = %d",
         radioIndex, channel, ch_width, CSA_TBTT, ret);
    if (ret != RETURN_OK)
    {
        LOGE("%s: Failed to push new channel %d", radio_ifname, channel);
        return false;
    }

#ifndef CONFIG_RDK_DISABLE_SYNC
    if (!sync_send_channel_change(radioIndex, channel))
    {
        LOGW("%d: Failed to sync channel change to %u", radioIndex, channel);
    }

    if (!sync_send_channel_bw_change(radioIndex, ch_width))
    {
        LOGW("%d: Failed to sync channel bandwidth change to %u MHz", radioIndex, ch_width);
    }
#endif

    LOGI("%s: Started CSA to channel %d, width %d, tbtt %d",
         radio_ifname, channel, ch_width, CSA_TBTT);

    return true;
}

static bool radio_change_zero_wait_dfs(INT radioIndex, const struct schema_Wifi_Radio_Config *rconf)
{
    INT     ret;
    BOOL    zero_dfs_supported = false;
    BOOL    enable = false;
    BOOL    precac = false;

    ret = wifi_isZeroDFSSupported(radioIndex, &zero_dfs_supported);
    if (ret != RETURN_OK)
    {
        LOGW("%s: cannot get isZeroDFSSupported for idx %d", __func__, radioIndex);
        return false;
    }
    if (!zero_dfs_supported)
    {
        LOGE("%s, zero dfs is not supported for idx %d", __func__, radioIndex);
        return false;
    }

    if (!strncmp(rconf->zero_wait_dfs, "enable", sizeof(rconf->zero_wait_dfs)))
    {
        enable = true;
    }
    else if (!strncmp(rconf->zero_wait_dfs, "precac", sizeof(rconf->zero_wait_dfs)))
    {
        enable = true;
        precac = true;
    }

    ret = wifi_setZeroDFSState(radioIndex, enable, precac);
    if (ret != RETURN_OK)
    {
        LOGE("%s, cannot setZeroDFSState, enable: %d, precac: %d for idx %d", __func__,
            enable, precac, radioIndex);
        return false;
    }

    return true;
}

static const char* channel_state_to_state_str(wifi_channelState_t state)
{
    switch (state)
    {
        case CHAN_STATE_AVAILABLE:
            return "{\"state\":\"allowed\"}";
        case CHAN_STATE_DFS_NOP_FINISHED:
            return "{\"state\": \"nop_finished\"}";
        case CHAN_STATE_DFS_NOP_START:
            return "{\"state\": \"nop_started\"}";
        case CHAN_STATE_DFS_CAC_START:
            return "{\"state\": \"cac_started\"}";
        case CHAN_STATE_DFS_CAC_COMPLETED:
            return "{\"state\": \"cac_completed\"}";
        default:
            LOGE("Unknown channel state: %d\n", state);
    }

    return NULL;
}

static bool update_channels_map(
        struct schema_Wifi_Radio_State *rstate,
        INT radioIndex)
{
    int i;
    int j = 0;
    int ret;

    /* The number of allowed 5GHz channels are around 55. Hence
     * setting MAP_SIZE to 64 to accommodate all.
     */
    const int MAP_SIZE = 64;
    wifi_channelMap_t channel_map[MAP_SIZE];

    memset(channel_map, 0, sizeof(channel_map));

    ret = wifi_getRadioChannels(radioIndex, channel_map, MAP_SIZE);
    if (ret != RETURN_OK)
    {
        LOGE("Cannot get channel map for %d\n", radioIndex);
        return false;
    }

    for (i = 0; i < MAP_SIZE; i++)
    {
        if (channel_map[i].ch_number == 0)
        {
            // If channel number is not set it means we don't have data for it
            continue;
        }

        LOGT("Adding channel %d state %d to channel map\n", channel_map[i].ch_number,
             channel_map[i].ch_state);
        snprintf(rstate->channels_keys[j], sizeof(rstate->channels_keys[j]),
                 "%d", channel_map[i].ch_number);
        STRSCPY_WARN(rstate->channels[j], channel_state_to_state_str(channel_map[i].ch_state));
        j++;
    }

    rstate->channels_len = j;

    return true;
}

static void update_radar_info(struct schema_Wifi_Radio_State *rstate)
{
    STRSCPY_WARN(rstate->radar_keys[0], "last_channel");
    STRSCPY_WARN(rstate->radar[0], strlen(dfs_last_channel) > 0 ? dfs_last_channel : "0");

    STRSCPY_WARN(rstate->radar_keys[1], "num_detected");
    if (strlen(dfs_num_detected) > 0) STRSCPY_WARN(rstate->radar[1], dfs_num_detected);

    STRSCPY_WARN(rstate->radar_keys[2], "time");
    snprintf(rstate->radar[2], sizeof(rstate->radar[2]), "%u", dfs_radar_timestamp);

    rstate->radar_len = 3;
}

static void update_zero_wait_dfs(INT radioIndex, struct schema_Wifi_Radio_State *rstate)
{
    INT     ret;
    BOOL    zero_dfs_supported = false;
    BOOL    enable;
    BOOL    precac;

    ret = wifi_isZeroDFSSupported(radioIndex, &zero_dfs_supported);
    if (ret != RETURN_OK)
    {
        LOGW("%s: cannot get isZeroDFSSupported for idx %d", __func__, radioIndex);
        return;
    }
    if (!zero_dfs_supported)
    {
        LOGI("%s, zero dfs is not supported for idx %d", __func__, radioIndex);
        return;
    }

    ret = wifi_getZeroDFSState(radioIndex, &enable, &precac);
    if (ret != RETURN_OK)
    {
        LOGE("%s, cannot getZeroDFSState for idx %d", __func__, radioIndex);
        return;
    }
    if (enable && precac)
    {
        STRSCPY(rstate->zero_wait_dfs, "precac");
    }
    else if (enable)
    {
        STRSCPY(rstate->zero_wait_dfs, "enable");
    }
    else
    {
        STRSCPY(rstate->zero_wait_dfs, "disable");
    }
    rstate->zero_wait_dfs_exists = true;
}

static char *radio_get_hw_type(const char *band)
{
    if (!strcmp(band, "2.4G")) return CONFIG_RDK_CHIPSET_NAME_2G;
    if (!strcmp(band, "5G")) return CONFIG_RDK_CHIPSET_NAME_5G;
    if (!strcmp(band, "6G")) return CONFIG_RDK_CHIPSET_NAME_6G;

    return NULL;
}

static bool radio_state_get(
        INT radioIndex,
        struct schema_Wifi_Radio_State *rstate)
{
    os_macaddr_t                        macaddr;
    CHAR                                pchannels[256];
    char                                *p;
    int                                 chan;
    int                                 ret;
    int                                 i;
    char                                radio_ifname[128];
    char                                *str = NULL;
    ULONG                               lval;
    wifi_radio_operationParam_t         radio_params;
    wifi_ieee80211Variant_t             max_variant = WIFI_80211_VARIANT_A;

    memset(rstate, 0, sizeof(*rstate));
    schema_Wifi_Radio_State_mark_all_present(rstate);
    rstate->_partial_update = true;
    rstate->channel_sync_present = false;
    rstate->channel_mode_present = false;
    rstate->radio_config_present = false;
    rstate->vif_states_present = false;

    memset(radio_ifname, 0, sizeof(radio_ifname));
    ret = wifi_getRadioIfName(radioIndex, radio_ifname);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio ifname for idx %d", __func__, radioIndex);
        return false;
    }

    // TODO: use SCHEMA_SET_STR etc. (?)
    // if_name (w/ exists)
    STRSCPY(rstate->if_name, target_unmap_ifname((char *)radio_ifname));
    rstate->if_name_exists = true;

    memset(&radio_params, 0, sizeof(radio_params));
    ret = wifi_getRadioOperatingParameters(radioIndex, &radio_params);
    if (ret != RETURN_OK)
    {
        LOGE("%s: cannot get radio operating parameters for %s\n", __func__, radio_ifname);
        return false;
    }

    // freq_band
    str = c_get_str_by_key(map_band_str, radio_params.band);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to decode band string: code=%d", radio_ifname, (int)radio_params.band);
    }
    else
    {
        STRSCPY(rstate->freq_band, str);
    }

    // hw_type (w/ exists)
    str = radio_get_hw_type(rstate->freq_band);
    if (!str)
    {
        LOGW("%s: Failed to get wifi chipset type", radio_ifname);
    }
    else
    {
        STRSCPY(rstate->hw_type, str);
        rstate->hw_type_exists = true;
    }

    // enabled (w/ exists)
    rstate->enabled = radio_params.enable;
    rstate->enabled_exists = true;

    rstate->channel = radio_params.channel;
    rstate->channel_exists = true;

    // tx_power (w/ exists)
    // HAL 3.0: we need to use old API because 'operating params'
    // provides tx power only in percentage
    ret = wifi_getRadioTransmitPower(radioIndex, &lval);
    if (ret == RETURN_OK)
    {
        // WAR: in schema the max txpower is between 1 and 32
        if (lval > 32) {
            lval = 32;
        } else if (lval < 1) {
            lval = 1;
        }
        rstate->tx_power = lval;
        rstate->tx_power_exists = true;
    } else
    {
        LOGW("Cannot read tx power for radio index %d", radioIndex);
    }

    // country (w/ exists)
    str = c_get_str_by_key(map_country_str, radio_params.countryCode);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to decode country: code=%d", radio_ifname, (int)radio_params.countryCode);
    }
    else
    {
        STRSCPY(rstate->country, str);
        rstate->country_exists = true;
    }

    str = c_get_str_by_key(map_htmode_str, radio_params.channelWidth);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to decode ht mode: code=%d", radio_ifname, (int)radio_params.channelWidth);
    }
    else
    {
        // ht_mode (w/ exists)
        STRSCPY(rstate->ht_mode, str);
        rstate->ht_mode_exists = true;
    }

    if (radio_params.variant & WIFI_80211_VARIANT_AX) max_variant = WIFI_80211_VARIANT_AX;
    else if (radio_params.variant & WIFI_80211_VARIANT_AD) max_variant = WIFI_80211_VARIANT_AD;
    else if (radio_params.variant & WIFI_80211_VARIANT_AC) max_variant = WIFI_80211_VARIANT_AC;
    else if (radio_params.variant & WIFI_80211_VARIANT_H) max_variant = WIFI_80211_VARIANT_H;
    else if (radio_params.variant & WIFI_80211_VARIANT_N) max_variant = WIFI_80211_VARIANT_N;
    else if (radio_params.variant & WIFI_80211_VARIANT_G) max_variant = WIFI_80211_VARIANT_G;
    else if (radio_params.variant & WIFI_80211_VARIANT_B) max_variant = WIFI_80211_VARIANT_B;
    else if (radio_params.variant & WIFI_80211_VARIANT_A) max_variant = WIFI_80211_VARIANT_A;

    str = c_get_str_by_key(map_variant_str, max_variant);
    if (strlen(str) == 0)
    {
        LOGW("%s: Failed to decode hw mode: code=%d", radio_ifname, (int)radio_params.variant);
    }
    else
    {
        // hw_mode (w/ exists)
        STRSCPY(rstate->hw_mode, str);
        rstate->hw_mode_exists = true;
    }

    // Update DFS data
    if (!update_channels_map(rstate, radioIndex))
    {
        LOGW("Cannot update channels map");
    }

    update_radar_info(rstate);

    if (os_nif_macaddr((char *)radio_ifname, &macaddr))
    {
        dpp_mac_to_str(macaddr.addr, rstate->mac);
        rstate->mac_exists = true;
    }

    // Possible Channels
    ret = wifi_getRadioPossibleChannels(radioIndex, pchannels);
    if (ret == RETURN_OK)
    {
        strcat(pchannels, ",");
        i = 0;
        p = strtok(pchannels, ",");
        while (p)
        {
            chan = atoi(p);
            if (chan >= 1)
            {
                rstate->allowed_channels[i++] = chan;
            }

            p = strtok(NULL, ",");
        }
        rstate->allowed_channels_len = i;
    }
    else
    {
        LOGW("%s: Failed to get possible channels", radio_ifname);
    }

    update_zero_wait_dfs(radioIndex, rstate);

    LOGN("Get radio state completed for radio index %d", radioIndex);
    return true;
}

static bool radio_state_update(UINT radioIndex)
{
    struct schema_Wifi_Radio_State  rstate;

    if (!radio_state_get(radioIndex, &rstate))
    {
        LOGE("%s: Radio state update failed -- unable to get state for idx %d",
             __func__, radioIndex);
        return false;
    }
    LOGN("Updating state for radio index %d...", radioIndex);
    g_rops.op_rstate(&rstate);

    return true;
}

static void chan_event_async_cb(EV_P_ ev_async *w, int revents)
{
    ds_dlist_iter_t     qiter;
    hal_cb_entry_t      *ptr;
    hal_cb_entry_t      cbe[HAL_CB_QUEUE_MAX];
    bool state_already_synchronized = false;
    int i;
    int number_of_events = 0;

    memset(&cbe, 0, sizeof(cbe));

    pthread_mutex_lock(&hal_cb_lock);
    ptr = ds_dlist_ifirst(&qiter, &hal_cb_queue);
    while (ptr)
    {
        ds_dlist_iremove(&qiter);
        hal_cb_queue_len--;

        // Only copy data to stack
        cbe[number_of_events] = *ptr;
        number_of_events += 1;

        LOGT("Free DFS chan cb event");
        free(ptr);
        ptr = ds_dlist_inext(&qiter);
    }
    pthread_mutex_unlock(&hal_cb_lock);

    for (i = 0; i < number_of_events; i++)
    {
        switch (cbe[i].event)
        {
            case WIFI_EVENT_CHANNELS_CHANGED:
                LOGD("CHANNELS CHANGED, last_channel = %d\n", cbe[i].channel);
                if (!state_already_synchronized)
                {
                    // Only need to read channel map once
                    // (after the last event).
                    radio_state_update(cbe[i].radioIndex);
                    state_already_synchronized = true;
                }
                break;
            case WIFI_EVENT_DFS_RADAR_DETECTED:
                LOGD("DFS RADAR DETECTED, last_channel = %d\n", cbe[i].channel);

                // Save last channel
                snprintf(dfs_last_channel, sizeof(dfs_last_channel), "%d", cbe[i].channel);

                // dfs_num_detected is always set to "1" currently
                STRSCPY_WARN(dfs_num_detected, "1");

                // Save timestamp
                dfs_radar_timestamp = (unsigned int)cbe->tv.tv_sec;

                radio_state_update(cbe[i].radioIndex);
                break;
            default:
                LOGE("Unknown channel event: %d\n", cbe[i].event);
                break;
        }
    }
}

static void chan_event_cb(
        UINT radioIndex,
        wifi_chan_eventType_t event,
        UCHAR channel)
{
    hal_cb_entry_t      *cbe;
    INT                 ret = RETURN_ERR;
    struct timeval tv;

    // Save timestamp immediately
    gettimeofday(&tv, NULL);

    pthread_mutex_lock(&hal_cb_lock);

    if (hal_cb_queue_len == HAL_CB_QUEUE_MAX)
    {
        LOGW("chan_event_cb: Queue is full! Ignoring event...");
        goto exit;
    }

    LOGT("Allocate DFS cb event");
    if ((cbe = calloc(1, sizeof(*cbe))) == NULL)
    {
        LOGE("chan_event_cb: Failed to allocate memory!");
        goto exit;
    }

    cbe->radioIndex = radioIndex;
    cbe->event = event;
    cbe->channel = channel;
    cbe->tv = tv;

    ds_dlist_insert_tail(&hal_cb_queue, cbe);
    hal_cb_queue_len++;
    ret = RETURN_OK;
exit:
    pthread_mutex_unlock(&hal_cb_lock);
    if (ret == RETURN_OK && hal_cb_loop)
    {
        if (!ev_async_pending(&hal_cb_async))
        {
            ev_async_send(hal_cb_loop, &hal_cb_async);
        }
    }
}

static bool radio_copy_config_from_state(
        INT radioIndex,
        struct schema_Wifi_Radio_State *rstate,
        struct schema_Wifi_Radio_Config *rconf)
{
    memset(rconf, 0, sizeof(*rconf));
    schema_Wifi_Radio_Config_mark_all_present(rconf);
    rconf->_partial_update = true;
    rconf->vif_configs_present = false;

    SCHEMA_SET_STR(rconf->if_name, rstate->if_name);
    LOGT("rconf->ifname = %s", rconf->if_name);
    SCHEMA_SET_STR(rconf->freq_band, rstate->freq_band);
    LOGT("rconf->freq_band = %s", rconf->freq_band);
    SCHEMA_SET_STR(rconf->hw_type, rstate->hw_type);
    LOGT("rconf->hw_type = %s", rconf->hw_type);
    SCHEMA_SET_INT(rconf->enabled, rstate->enabled);
    LOGT("rconf->enabled = %d", rconf->enabled);
    SCHEMA_SET_INT(rconf->channel, rstate->channel);
    LOGT("rconf->channel = %d", rconf->channel);
    SCHEMA_SET_INT(rconf->tx_power, rstate->tx_power);
    LOGT("rconf->tx_power = %d", rconf->tx_power);
    SCHEMA_SET_STR(rconf->country, rstate->country);
    LOGT("rconf->country = %s", rconf->country);
    SCHEMA_SET_STR(rconf->ht_mode, rstate->ht_mode);
    LOGT("rconf->ht_mode = %s", rconf->ht_mode);
    SCHEMA_SET_STR(rconf->hw_mode, rstate->hw_mode);
    LOGT("rconf->hw_mode = %s", rconf->hw_mode);
    if (rstate->zero_wait_dfs_exists)
    {
        SCHEMA_SET_STR(rconf->zero_wait_dfs, rstate->zero_wait_dfs);
        LOGT("rconf->zero_wait_dfs = %s", rconf->zero_wait_dfs);
    }

    return true;
}

bool vap_controlled(const char *ifname)
{
#ifdef CONFIG_RDK_CONTROL_ALL_VAPS
    return true;
#endif

    if (!strcmp(ifname, CONFIG_RDK_HOME_AP_24_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_HOME_AP_50_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_HOME_AP_60_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_BHAUL_AP_24_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_BHAUL_AP_50_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_BHAUL_AP_60_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_ONBOARD_AP_24_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_ONBOARD_AP_50_IFNAME) ||
        !strcmp(ifname, CONFIG_RDK_ONBOARD_AP_60_IFNAME))
        {
            return true;
        }

    return false;
}

bool target_radio_config_init2()
{
    INT ret;
    ULONG r;
    ULONG rnum;
    ULONG s;
    ULONG snum;
    INT ssid_radio_idx;
    char ssid_ifname[128];

    struct schema_Wifi_VIF_Config   vconfig;
    struct schema_Wifi_VIF_State    vstate;
    struct schema_Wifi_Radio_Config rconfig;
    struct schema_Wifi_Radio_State  rstate;

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        return false;
    }

    for (r = 0; r < rnum; r++)
    {
        radio_state_get(r, &rstate);
        radio_copy_config_from_state(r, &rstate, &rconfig);
        g_rops.op_rconf(&rconfig);
        g_rops.op_rstate(&rstate);

        ret = wifi_getSSIDNumberOfEntries(&snum);
        if (ret != RETURN_OK)
        {
            LOGE("%s: failed to get SSID count", __func__);
            return false;
        }

        if (snum == 0)
        {
            LOGE("%s: no SSIDs detected", __func__);
            continue;
        }

        for (s = 0; s < snum; s++)
        {
            memset(ssid_ifname, 0, sizeof(ssid_ifname));
            ret = wifi_getApName(s, ssid_ifname);
            if (ret != RETURN_OK)
            {
                LOGW("%s: failed to get AP name for index %lu. Skipping.\n", __func__, s);
                continue;
            }

            ret = wifi_getSSIDRadioIndex(s, &ssid_radio_idx);
            if (ret != RETURN_OK)
            {
                LOGW("Cannot get radio index for SSID %lu", s);
                continue;
            }

            if ((ULONG)ssid_radio_idx != r)
            {
                continue;
            }

            // Silentely skip VAPs that are not controlled by OpenSync
            if (!vap_controlled(ssid_ifname)) continue;

            LOGI("Found SSID index %lu: %s", s, ssid_ifname);
            if (!vif_state_get(s, &vstate))
            {
                LOGE("%s: cannot get vif state for SSID index %lu", __func__, s);
                continue;
            }
            if (!vif_copy_to_config(s, &vstate, &vconfig))
            {
                LOGE("%s: cannot copy VIF state to config for SSID index %lu", __func__, s);
                continue;
            }
            g_rops.op_vconf(&vconfig, rconfig.if_name);
            g_rops.op_vstate(&vstate, rstate.if_name);
        }

    }

    if (!dfs_event_cb_registered)
    {
        hal_cb_loop = wifihal_evloop;
        ds_dlist_init(&hal_cb_queue, hal_cb_entry_t, node);
        hal_cb_queue_len = 0;
        pthread_mutex_init(&hal_cb_lock, NULL);
        ev_async_init(&hal_cb_async, chan_event_async_cb);
        ev_async_start(hal_cb_loop, &hal_cb_async);

        if (wifi_chan_eventRegister(chan_event_cb) != RETURN_OK)
        {
            LOGE("Failed to register chan event callback\n");
        }

        dfs_event_cb_registered = true;
    }

#ifdef CONFIG_RDK_WPS_SUPPORT
    wps_hal_init();
#endif

#ifdef CONFIG_RDK_MULTI_AP_SUPPORT
    multi_ap_hal_init();
#endif

    return true;
}

bool radio_ifname_to_idx(const char *ifname, INT *outRadioIndex)
{
    INT ret;
    ULONG r, rnum;
    INT radio_index = -1;
    char radio_ifname[128];

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        return false;
    }

    for (r = 0; r < rnum; r++)
    {
        memset(radio_ifname, 0, sizeof(radio_ifname));
        ret = wifi_getRadioIfName(r, radio_ifname);
        if (ret != RETURN_OK)
        {
            LOGE("%s: failed to get radio ifname for idx %ld", __func__,
                    r);
            return false;
        }
        if (!strncmp(radio_ifname, ifname, sizeof(radio_ifname)))
        {
                radio_index = r;
                break;
        }
    }

    if (radio_index == -1)
    {
        LOGE("Cannot get radio index for %s", ifname);
        return false;
    }

    *outRadioIndex = radio_index;
    return true;
}

bool target_radio_config_set2(
        const struct schema_Wifi_Radio_Config *rconf,
        const struct schema_Wifi_Radio_Config_flags *changed)
{
    int radioIndex;

    if (!radio_ifname_to_idx(rconf->if_name, &radioIndex))
    {
        LOGE("%s: cannot get radio index for %s", __func__, rconf->if_name);
        return false;
    }

    if (changed->channel || changed->ht_mode)
    {
        if (!radio_change_channel(radioIndex, rconf->channel, rconf->ht_mode))
        {
            LOGE("%s: cannot change radio channel for %s", __func__, rconf->if_name);
            return false;
        }
    }

    if (changed->zero_wait_dfs)
    {
        if (!radio_change_zero_wait_dfs(radioIndex, rconf))
        {
            LOGE("%s: cannot change radio zero wait dfs for %s", __func__, rconf->if_name);
            return false;
        }
    }

    radio_trigger_resync();
    return true;
}

static void radio_resync_all_task(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    INT ret;
    ULONG r, rnum;
    ULONG s, snum;
    char ssid_ifname[128];
    BOOL enabled = false;

    LOGT("Re-sync started");

    ret = wifi_getRadioNumberOfEntries(&rnum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get radio count", __func__);
        goto out;
    }

    for (r = 0; r < rnum; r++)
    {
        if (!radio_state_update(r))
        {
            LOGW("Cannot update radio state for radio index %lu", r);
            continue;
        }
    }

    ret = wifi_getSSIDNumberOfEntries(&snum);
    if (ret != RETURN_OK)
    {
        LOGE("%s: failed to get SSID count", __func__);
        goto out;
    }

    if (snum == 0)
    {
        LOGE("%s: no SSIDs detected", __func__);
        goto out;
    }

    for (s = 0; s < snum; s++)
    {
        ret = wifi_getSSIDEnable(s, &enabled);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get SSID enabled state for index %lu. Skipping", __func__, s);
            continue;
        }

        // Silently skip ifaces that are not enabled
        if (enabled == false) continue;

        memset(ssid_ifname, 0, sizeof(ssid_ifname));
        ret = wifi_getApName(s, ssid_ifname);
        if (ret != RETURN_OK)
        {
            LOGW("%s: failed to get AP name for index %lu. Skipping.\n", __func__, s);
            continue;
        }

        // Silentely skip VAPs that are not controlled by OpenSync
        if (!vap_controlled(ssid_ifname)) continue;

        // Fetch existing clients
        if (!clients_hal_fetch_existing(s))
        {
            LOGW("Fetching existing clients for %s failed", ssid_ifname);
        }

        if (!vif_state_update(s))
        {
            LOGW("Cannot update VIF state for SSID index %lu", s);
            continue;
        }
    }
out:
    LOGT("Re-sync completed");
    g_resync_ongoing = false;
}

bool target_radio_init(const struct target_radio_ops *ops)
{
    /* Register callbacks */
    g_rops = *ops;

    if (!clients_hal_init(ops))
    {
        LOGW("Cannot initialize clients");
    }

    ev_timer_init(&healthcheck_timer, healthcheck_task, 2, 0);
    ev_timer_init(&radio_resync_all_task_timer, radio_resync_all_task, RESYNC_UPDATE_DELAY_SECONDS, 0);
    ev_timer_start(wifihal_evloop, &healthcheck_timer);
    return true;
}

void radio_trigger_resync()
{
    if (!g_resync_ongoing)
    {
        g_resync_ongoing = true;
        LOGI("Radio re-sync scheduled");
        ev_timer_stop(wifihal_evloop, &radio_resync_all_task_timer);
        ev_timer_start(wifihal_evloop, &radio_resync_all_task_timer);
    } else
    {
        LOGT("Radio re-sync already ongoing!");
    }
}

bool radio_rops_vstate(
        struct schema_Wifi_VIF_State *vstate,
        const char *radio_ifname)
{
    if (!g_rops.op_vstate)
    {
        LOGE("%s: op_vstate not set", __func__);
        return false;
    }

    g_rops.op_vstate(vstate, radio_ifname);
    return true;
}

bool radio_rops_vconfig(
        struct schema_Wifi_VIF_Config *vconf,
        const char *radio_ifname)
{
    if (!g_rops.op_vconf)
    {
        LOGE("%s: op_vconf not set", __func__);
        return false;
    }

    g_rops.op_vconf(vconf, radio_ifname);
    return true;
}

radio_cloud_mode_t radio_cloud_mode_get()
{
    return radio_cloud_mode;
}

bool
radio_cloud_mode_set(radio_cloud_mode_t mode)
{
    radio_cloud_mode = mode;

#ifdef CONFIG_RDK_DISABLE_SYNC
    return true;
#else
    return sync_send_status(radio_cloud_mode);
#endif
}
