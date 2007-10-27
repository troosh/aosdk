/*
	Audio Overload SDK - SSF engine

	Copyright (c) 2007, R. Belmont and Richard Bannister.

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

//
// eng_ssf.c
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ao.h"
#include "corlett.h"
#include "sat_hw.h"
#include "scsp.h"

#define DEBUG_LOADER	(0)

static corlett_t	*c = NULL;
static char 		psfby[256];

int32 ssf_start(uint8 *buffer, uint32 length)
{
	uint8 *file, *lib_decoded, *lib_raw_file;
	uint32 offset, plength;
	uint64 file_len, lib_len, lib_raw_length;
	corlett_t *lib;
	int i;

	// clear Saturn work RAM before we start scribbling in it
	memset(sat_ram, 0, 512*1024);

	// Decode the current SSF
	if (corlett_decode(buffer, length, &file, &file_len, &c) != AO_SUCCESS)
	{
		return AO_FAIL;
	}

	#if DEBUG_LOADER
	printf("%d bytes decoded\n", file_len);
	#endif

	// Get the library file, if any
	if (c->lib[0] != 0)
	{
		uint64 tmp_length;
	
		#if DEBUG_LOADER	
		printf("Loading library: %s\n", c->lib);
		#endif
		if (ao_get_lib(c->lib, &lib_raw_file, &tmp_length) != AO_SUCCESS)
		{
			return AO_FAIL;
		}
		lib_raw_length = tmp_length;
		
		if (corlett_decode(lib_raw_file, lib_raw_length, &lib_decoded, &lib_len, &lib) != AO_SUCCESS)
		{
			free(lib_raw_file);
			return AO_FAIL;
		}
				
		// Free up raw file
		free(lib_raw_file);

		// patch the file into ram
		offset = lib_decoded[0] | lib_decoded[1]<<8 | lib_decoded[2]<<16 | lib_decoded[3]<<24;
		memcpy(&sat_ram[offset], lib_decoded+4, lib_len-4);

		// Dispose the corlett structure for the lib - we don't use it
		free(lib);
	}

	// now patch the file into RAM over the libraries
	offset = file[3]<<24 | file[2]<<16 | file[1]<<8 | file[0];
	memcpy(&sat_ram[offset], file+4, file_len-4);

	free(file);
	
	// Finally, set psfby tag
	strcpy(psfby, "n/a");
	if (c)
	{
		int i;
		for (i = 0; i < MAX_UNKNOWN_TAGS; i++)
		{
			if (!strcasecmp(c->tag_name[i], "psfby"))
				strcpy(psfby, c->tag_data[i]);
		}
	}

	#if DEBUG_LOADER && 1
	{
		FILE *f;

		f = fopen("satram.bin", "wb");
		fwrite(sat_ram, 512*1024, 1, f);
		fclose(f);
	}
	#endif

	// now flip everything (this makes sense because he's using starscream)
	for (i = 0; i < 512*1024; i+=2)
	{
		uint8 temp;

		temp = sat_ram[i];
		sat_ram[i] = sat_ram[i+1];
		sat_ram[i+1] = temp;
	}

	sat_hw_init();

	return AO_SUCCESS;
}

int32 ssf_gen(int16 *buffer, uint32 samples)
{	
	int i;
	int16 output[44100/30], output2[44100/30];
	int16 *stereo[2];
	int16 *outp = buffer;
	int opos;

	opos = 0;
	for (i = 0; i < samples; i++)
	{
		m68k_execute((11300000/60)/735);
		stereo[0] = &output[opos];
		stereo[1] = &output2[opos];
		SCSP_Update(0, NULL, stereo, 1);
		opos++;		
	}

	for (i = 0; i < samples; i++)
	{
		*outp++ = output[i];
		*outp++ = output2[i];
	}

	return AO_SUCCESS;
}

int32 ssf_stop(void)
{
	return AO_SUCCESS;
}

int32 ssf_command(int32 command, int32 parameter)
{
	switch (command)
	{
		case COMMAND_RESTART:
			return AO_SUCCESS;
		
	}
	return AO_FAIL;
}

int32 ssf_fill_info(ao_display_info *info)
{
	if (c == NULL)
		return AO_FAIL;
		
	strcpy(info->title[1], "Name: ");
	sprintf(info->info[1], "%s", c->inf_title);

	strcpy(info->title[2], "Game: ");
	sprintf(info->info[2], "%s", c->inf_game);
	
	strcpy(info->title[3], "Artist: ");
	sprintf(info->info[3], "%s", c->inf_artist);

	strcpy(info->title[4], "Copyright: ");
	sprintf(info->info[4], "%s", c->inf_copy);

	strcpy(info->title[5], "Year: ");
	sprintf(info->info[5], "%s", c->inf_year);

	strcpy(info->title[6], "Length: ");
	sprintf(info->info[6], "%s", c->inf_length);

	strcpy(info->title[7], "Fade: ");
	sprintf(info->info[7], "%s", c->inf_fade);

	strcpy(info->title[8], "Ripper: ");
	sprintf(info->info[8], "%s", psfby);

	return AO_SUCCESS;
}
