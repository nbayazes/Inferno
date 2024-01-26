#include "pch.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include "dr_mp3.h"

//#define DR_WAV_IMPLEMENTATION
//#define DR_WAV_NO_STDIO
//#include "dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_CRC
#include "dr_flac.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.h"