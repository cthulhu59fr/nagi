/* FUNCTION list 	---	---	---	---	---	---	---

*/

/* BASE headers	---	---	---	---	---	---	--- */
#include "../agi.h"

/* LIBRARY headers	---	---	---	---	---	---	--- */
#include <stdlib.h>
#include <stdio.h>

#include <assert.h>

/* OTHER headers	---	---	---	---	---	---	--- */
#include "sound_base.h"
#include "sound_gen.h"
#include "tone.h"

#include "../flags.h"
#include "../sys/endian.h"

/* PROTOTYPES	---	---	---	---	---	---	--- */


/* VARIABLES	---	---	---	---	---	---	--- */
#define CHAN_MAX 4

// "fade out" or possibly "dissolve"
u8 dissolve_data[0x44] =
{
	0xFE, 0xFD, 0xFE, 0xFF, 0x00, 0x00, 0x01, 0x01, 0x01,
	0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07,
	0x07, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09,
	0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B,
	0x0B, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0D, 0x80
}; 

u8 channels_left = 0;
SNDGEN_CHAN channel[CHAN_MAX];

		
/* CODE	---	---	---	---	---	---	---	--- */

void sndgen_init(void)
{
	if (!c_snd_disable)
	{
		// init tone_gen
		if (tone_init())
		{
			c_snd_disable = 1;
		}
		else
		{
			tone_state_set(0);
		}
	}
}

void sndgen_shutdown(void)
{
	// shutdown tone_gen
	tone_shutdown();
}

int tone_type = 0;

void sndgen_play(SOUND *snd)
{
	int i;
	
	assert(snd);
	
	if (!c_snd_disable)
	{
		for (i=0; i<CHAN_MAX ; i++)
		{
			channel[i].data = snd->channel[i];
			channel[i].duration = 0;
			channel[i].dissolve_count = 0xFFFF;
			channel[i].toggle = 0xFFFF;
			channel[i].freq_count = 0;
			channel[i].tone_handle = tone_open(i);
		}
		
		// we're assuming 4 channel tandy/pcjr here anyways
		channels_left = CHAN_MAX;	// channels
		
		sound_state = SS_OPEN_PLAYING;
		
		//sound_vector();
		
		tone_state_set(1);
	}
	else
	{
		flag_set(sound_flag);
	}}

void sndgen_stop(void)
{
	int i;
	
	if (!c_snd_disable)
	{
		sound_state = SS_CLOSED;
		tone_state_set(0);
		for (i=0; i<CHAN_MAX ; i++)
			tone_close(channel[i].tone_handle);
		flag_set(sound_flag);
	}}

int volume_calc(SNDGEN_CHAN *chan)
{
	// blah
	assert(chan);
	return chan->attenuation;
}

// read the next channel data.. fill it in *tone
// if tone isn't touched.. it should be inited so it just plays silence
void sndgen_callback(int ch, TONE *tone)
{
	SNDGEN_CHAN *chan;
	void *data;

	assert(tone);
	assert(ch < CHAN_MAX);
	
	if ( (!flag_test(F09_SOUND)) || (sound_state!=SS_OPEN_PLAYING) )
		sound_state=SS_OPEN_STOPPED;
	else
	{
		chan = &channel[ch];
		if (chan->toggle)
		{
			while ( (chan->duration == 0) && (chan->duration != 0xFFFF) )
			{
				data = chan->data;
				
				// read the duration of the note
				chan->duration = load_le_16(data);	// duration
				
				// if it's 0 then it's not going to be played
				// if it's 0xFFFF then the channel data has finished.
				if ( (chan->duration != 0) && (chan->duration != 0xFFFF))
				{
					// only tone channels dissolve
					if (ch != 3)	// != noise??
						chan->dissolve_count = 0;
					
										// attenuation (volume)
					chan->attenuation = ((u8*)data)[4] & 0xF;	
					
					
					// frequency
					if (ch < (CHAN_MAX-1))
					{
						chan->freq_count = (u16)((u8*)data)[2] & 0x3F;
						chan->freq_count <<= 4;
						chan->freq_count |= ((u8*)data)[3] & 0x0F;
						
						chan->gen_type = GEN_TONE;
						
						#warning if chan 2.. change noise chan if appropriate
					}
					else
					{
						int noise_freq;
						
						// check for white noise (1) or periodic (0)
						chan->gen_type = (((u8*)data)[3] & 0x04) ? 
								GEN_WHITE : GEN_PERIOD;
						
						noise_freq = ((u8*)data)[3] & 0x03;
						
						switch (noise_freq)
						{
							case 0:
								chan->freq_count = 32;
								break;
							case 1:
								chan->freq_count = 64;
								break;
							case 2:
								chan->freq_count = 128;
								break;
							case 3:
								chan->freq_count = channel[2].freq_count*2;
								break;
						}
					}
				}
				// data now points to the next data seg-a-ment
				chan->data += 5;
			}
			
			if (chan->duration != 0xFFFF)
			{
				tone->freq_count = chan->freq_count;
				tone->atten = volume_calc(chan);	// calc volume, sent vol is different from saved vol
				tone->type = chan->gen_type;
				chan->duration --;
			}
			else
			{
				// kill channel
				channels_left --;
				chan->toggle = 0;
				chan->attenuation = 0x0F;	// silent
				chan->attenuation_copy = 0;	// dunno really
			}
		}
		
		if (channels_left == 0)
			sound_state=SS_OPEN_STOPPED;
	}
	
}



void sndgen_poll()
{
	if (sound_state==SS_OPEN_STOPPED)
	{
		sndgen_stop();
	}
	
}