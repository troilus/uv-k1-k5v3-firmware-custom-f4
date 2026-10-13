/* Generated from App/apps/lbj/gen_assets.py (TYPES + abbr()).
 * Locomotive model code -> ASCII name, sorted by code for binary search. */
#ifndef LBJ_TYPES_H
#define LBJ_TYPES_H
#include <stdint.h>

typedef struct { uint16_t code; const char *name; } lbj_type_t;

static const lbj_type_t LBJ_TYPES[] = {
    {   1,"JF"}, {   3,"QJ"}, {   5,"JS"}, {   6,"KD7"}, {  55,"LJK"}, {  81,"DF21"}, { 101,"DF"},
    { 102,"DF2"}, { 103,"DF3"}, { 104,"DF4"}, { 105,"DF4K"}, { 106,"DF4C"}, { 107,"DF5"},
    { 108,"DF5W"}, { 109,"DF6"}, { 110,"DF7"}, { 111,"DF8"}, { 112,"DF9"}, { 113,"DF10"},
    { 114,"DFH1"}, { 115,"DFH2"}, { 116,"DFH3"}, { 117,"DFH5"}, { 118,"BJ"}, { 119,"BJW"},
    { 120,"ND2"}, { 121,"ND3"}, { 122,"ND4"}, { 123,"ND5"}, { 124,"NY5"}, { 125,"NY6"},
    { 126,"NY7"}, { 127,"QY"}, { 128,"DFH21"}, { 129,"DF7B"}, { 130,"DF5S"}, { 131,"DF7C"},
    { 132,"DF7S"}, { 133,"GK1"}, { 134,"GK1F"}, { 135,"DF4E"}, { 136,"DF7D"}, { 137,"GK1A"},
    { 138,"DF11"}, { 139,"TA"}, { 140,"DF10F"}, { 141,"DF4D"}, { 142,"DF8B"}, { 143,"DF12"},
    { 144,"DF7E"}, { 145,"NYJ1"}, { 146,"NZJ1"}, { 147,"NZJ2"}, { 148,"DF4DJ"}, { 149,"XSG"},
    { 150,"SZ"}, { 151,"NJ2"}, { 152,"DF7G"}, { 153,"NDJ3"}, { 156,"DF11Z"}, { 157,"FXN3D"},
    { 158,"DF11G"}, { 160,"HXN3"}, { 161,"HXN5"}, { 162,"HXN3B"}, { 163,"HXN5B"}, { 167,"FXN3B"},
    { 169,"FXN3C"}, { 170,"FXN5C"}, { 171,"FXN3-J"}, { 201,"8G"}, { 202,"8K"}, { 203,"6G"},
    { 204,"6K"}, { 205,"SS1"}, { 206,"SS3"}, { 207,"SS4"}, { 208,"SS5"}, { 209,"SS6"},
    { 210,"SS3B"}, { 211,"SS7"}, { 212,"SS8"}, { 213,"SS7B"}, { 214,"SS7C"}, { 215,"SS6B"},
    { 216,"SS9"}, { 217,"SS7D"}, { 218,"DJXM"}, { 219,"DJ1"}, { 220,"DJ2"}, { 221,"DJF"},
    { 222,"LJD"}, { 223,"XFH"}, { 224,"SS7E"}, { 225,"SS4G"}, { 226,"SS3C"}, { 228,"TS"},
    { 229,"DJ4HX"}, { 230,"KTT"}, { 231,"HXD1"}, { 232,"HXD2"}, { 233,"HXD3"}, { 234,"HXD1B"},
    { 235,"HXD2B"}, { 236,"HXD3B"}, { 237,"HXD1C"}, { 238,"HXD2C"}, { 239,"HXD3C"}, { 240,"HXD1D"},
    { 241,"HXD2D"}, { 242,"HXD3D"}, { 243,"FXD1B"}, { 244,"FXD2B"}, { 245,"FXD1"}, { 246,"FXD3"},
    { 247,"FXD1-J"}, { 248,"FXD3-J"}, { 249,"KZ25TA"}, { 251,"KZ25TB"}, { 252,"HXD1D-J"},
    { 254,"FXD1H"}, { 300,"XYSZ"}, { 301,"CRH1"}, { 302,"CRH2"}, { 303,"CRH3"}, { 305,"CRH5"},
    { 306,"CRH380A"}, { 307,"CRH380B"}, { 308,"CRH380C"}, { 309,"CRH380D"}, { 310,"CRH6A"},
    { 311,"CR400AF"}, { 312,"CR400BF"}, { 313,"CR300AF"}, { 314,"CR300BF"}, { 315,"CRH2E"},
    { 316,"CRH6F"}, { 330,"CJ1"}, { 331,"CJ2"}, { 332,"CJ3"}, { 333,"CJ4"}, { 334,"CJ5"},
    { 335,"CJ6"}, { 400,"GCD-1000J"}, { 403,"GX-160"}, {3073,"CRH380BL"}, {3075,"CRH380BG"},
    {3111,"CR400AF-Z"}, {3112,"CR400AF-S"}, {3125,"CR400BF-G"},
};

#define LBJ_TYPE_COUNT ((uint16_t)(sizeof(LBJ_TYPES) / sizeof(LBJ_TYPES[0])))

#endif /* LBJ_TYPES_H */
