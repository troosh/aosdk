/*

	Audio Overload SDK

	Copyright (c) 2007-2008, R. Belmont and Richard Bannister.

	All rights reserved.

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
	* Neither the names of R. Belmont and Richard Bannister nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
	CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
	EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// corlett.c

// Decodes file format designed by Neill Corlett (PSF, QSF, ...)

/*
 - First 3 bytes: ASCII signature: "PSF" (case sensitive)

- Next 1 byte: Version byte
  The version byte is used to determine the type of PSF file.  It does NOT
  affect the basic structure of the file in any way.

  Currently accepted version bytes are:
    0x01: Playstation (PSF1)
    0x02: Playstation 2 (PSF2)
    0x11: Saturn (SSF) [TENTATIVE]
    0x12: Dreamcast (DSF) [TENTATIVE]
    0x21: Nintendo 64 (USF) [RESERVED]
    0x41: Capcom QSound (QSF)

- Next 4 bytes: Size of reserved area (R), little-endian unsigned long

- Next 4 bytes: Compressed program length (N), little-endian unsigned long
  This is the length of the program data _after_ compression.

- Next 4 bytes: Compressed program CRC-32, little-endian unsigned long
  This is the CRC-32 of the program data _after_ compression.  Filling in
  this value is mandatory, as a PSF file may be regarded as corrupt if it
  does not match.

- Next R bytes: Reserved area.
  May be empty if R is 0 bytes.

- Next N bytes: Compressed program, in zlib compress() format.
  May be empty if N is 0 bytes.

The following data is optional and may be omitted:

- Next 5 bytes: ASCII signature: "[TAG]" (case sensitive)
  If these 5 bytes do not match, then the remainder of the file may be
  regarded as invalid and discarded.

- Remainder of file: Uncompressed ASCII tag data.
*/

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "ao.h"
#include "corlett.h"

#include <zlib.h>
#include <stdlib.h>

#define DECOMP_MAX_SIZE		((32 * 1024 * 1024) + 12)

uint32 total_samples;
uint32 decaybegin;
uint32 decayend;

int corlett_decode(uint8 *input, uint32 input_len, uint8 **output, uint64 *size, corlett_t **c)
{
	uint32 *buf;
	uint32 res_area, comp_crc,  actual_crc;
	uint8 *decomp_dat, *tag_dec;
	uLongf decomp_length, comp_length;

	// 32-bit pointer to data
	buf = (uint32 *)input;

	// Check we have a PSF format file.
	if ((input[0] != 'P') || (input[1] != 'S') || (input[2] != 'F'))
	{
		return AO_FAIL;
	}

	// Get our values
	res_area = LE32(buf[1]);
	comp_length = LE32(buf[2]);
	comp_crc = LE32(buf[3]);

	if (comp_length > 0)
	{
		// Check length
		if (input_len < comp_length + 16)
			return AO_FAIL;

		// Check CRC is correct
		actual_crc = crc32(0, (unsigned char *)&buf[4+(res_area/4)], comp_length);
		if (actual_crc != comp_crc)
			return AO_FAIL;

		// Decompress data if any
		decomp_dat = malloc(DECOMP_MAX_SIZE);
		decomp_length = DECOMP_MAX_SIZE;
		if (uncompress(decomp_dat, &decomp_length, (unsigned char *)&buf[4+(res_area/4)], comp_length) != Z_OK)
		{
			free(decomp_dat);
			return AO_FAIL;
		}

		// Resize memory buffer to what we actually need
		decomp_dat = realloc(decomp_dat, (size_t)decomp_length + 1);
	}
	else
	{
		decomp_dat = NULL;
		decomp_length =  0;
	}

	// Make structure
	*c = malloc(sizeof(corlett_t));
	if (!(*c))
	{
		free(decomp_dat);
		return AO_FAIL;
	}
	memset(*c, 0, sizeof(corlett_t));
	strcpy((*c)->inf_title, "n/a");
	strcpy((*c)->inf_copy, "n/a");
	strcpy((*c)->inf_artist, "n/a");
	strcpy((*c)->inf_game, "n/a");
	strcpy((*c)->inf_year, "n/a");
	strcpy((*c)->inf_length, "n/a");
	strcpy((*c)->inf_fade, "n/a");

	// set reserved section pointer
	(*c)->res_section = &buf[4];
	(*c)->res_size = res_area;

	// Return it
	*output = decomp_dat;
	*size = decomp_length;

	// Next check for tags
	input_len -= (comp_length + 16 + res_area);
	if (input_len < 5)
		return AO_SUCCESS;

//	printf("\n\nNew corlett: input len %d\n", input_len);

	tag_dec = input + (comp_length + res_area + 16);
	if ((tag_dec[0] == '[') && (tag_dec[1] == 'T') && (tag_dec[2] == 'A') && (tag_dec[3] == 'G') && (tag_dec[4] == ']'))
	{
		int tag, l, num_tags, data;

		// Tags found!
		tag_dec += 5;
		input_len -= 5;

		tag = 0;
		data = false;
		num_tags = 0;
		l = 0;
		while (input_len && (num_tags < MAX_UNKNOWN_TAGS))
		{
			if (data)
			{
				if ((*tag_dec == 0xA) || (*tag_dec == 0x00))
				{
					(*c)->tag_data[num_tags][l] = 0;
					data = false;
					num_tags++;
					l = 0;
				}
				else
				{
					(*c)->tag_data[num_tags][l++] = *tag_dec;
				}
			}
			else
			{
				if (*tag_dec == '=')
				{
					(*c)->tag_name[num_tags][l] = 0;
					l = 0;
					data = true;
				}
				else
				{
					(*c)->tag_name[num_tags][l++] = *tag_dec;
				}
			}

			tag_dec++;
			input_len--;
		}


		// Now, process that tag array into what we expect
		for (num_tags = 0; num_tags < MAX_UNKNOWN_TAGS; num_tags++)
		{
			// See if tag belongs in one of the special fields we have
			if (corlett_tag_recognize(*c, (*c)->lib, num_tags, "_lib")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[0], num_tags, "_lib2")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[1], num_tags, "_lib3")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[2], num_tags, "_lib4")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[3], num_tags, "_lib5")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[4], num_tags, "_lib6")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[5], num_tags, "_lib7")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[6], num_tags, "_lib8")) {
			} else if (corlett_tag_recognize(*c, (*c)->libaux[7], num_tags, "_lib9")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_refresh, num_tags, "_refresh")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_title, num_tags, "title")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_copy, num_tags, "copyright")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_artist, num_tags, "artist")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_game, num_tags, "game")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_year, num_tags, "year")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_length, num_tags, "length")) {
			} else if (corlett_tag_recognize(*c, (*c)->inf_fade, num_tags, "fade")) {
			}
		}
	}

	// Bingo
	return AO_SUCCESS;
}

int corlett_tag_recognize(corlett_t *c, char *target_value, int tag_num, const char *key)
{
	if (!strcasecmp(c->tag_name[tag_num], key))
	{
		strcpy(target_value, c->tag_data[tag_num]);
		c->tag_data[tag_num][0] = 0;
		c->tag_name[tag_num][0] = 0;
		return 1;
	}
	return 0;
}

void corlett_length_set(uint32 length_ms, int32 fade_ms)
{
	total_samples = 0;
	if (length_ms == 0)
	{
		decaybegin = ~0;
	}
	else
	{
		length_ms = (length_ms * 441) / 10;
		fade_ms = (fade_ms * 441) / 10;

		decaybegin = length_ms;
		decayend = length_ms + fade_ms;
	}
}

uint32 corlett_sample_count(void)
{
	return total_samples;
}

void corlett_sample_fade(int16 *l, int16 *r)
{
	if(total_samples >= decaybegin)
	{
		int32 fader;
		if(total_samples >= decayend)
		{
			ao_song_done = 1;
			*l = 0;
			*r = 0;
		}
		else
		{
			fader = 256 - (256 * (total_samples - decaybegin) / (decayend - decaybegin));
			*l = (*l * fader) >> 8;
			*r = (*r * fader) >> 8;
		}
	}
	total_samples++;
}

uint32 psfTimeToMS(char *str)
{
	int x, c=0;
	uint32 acc=0;
	char s[100];

	strncpy(s,str,100);
	s[99]=0;

	for (x=strlen(s); x>=0; x--)
	{
		if (s[x]=='.' || s[x]==',')
		{
			acc=atoi(s+x+1);
			s[x]=0;
		}
		else if (s[x]==':')
		{
			if(c==0)
			{
				acc+=atoi(s+x+1)*10;
			}
			else if(c==1)
			{
				acc+=atoi(s+x+(x?1:0))*10*60;
			}

			c++;
			s[x]=0;
		}
		else if (x==0)
		{
			if(c==0)
			{
				acc+=atoi(s+x)*10;
			}
			else if(c==1)
			{
				acc+=atoi(s+x)*10*60;
			}
			else if(c==2)
			{
				acc+=atoi(s+x)*10*60*60;
			}
		}
	}

	acc*=100;
	return(acc);
}

