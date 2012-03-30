#include "cps3.h"

#define CPS3_VOICES		16

#define CPS3_SND_INT_RATE		(nBurnFPS / 100)
#define CPS3_SND_RATE			(42954500 / 3 / 384)
#define CPS3_SND_BUFFER_SIZE	(CPS3_SND_RATE / CPS3_SND_INT_RATE)
#define CPS3_SND_LINEAR_SHIFT	12

typedef struct {
	UINT16 regs[16];
	UINT32 pos;
	UINT16 frac;
} cps3_voice;

typedef struct {
	cps3_voice voice[CPS3_VOICES];
	UINT16 key;

	UINT8 * rombase;
	UINT32 delta;

} cps3snd_chip;

static cps3snd_chip * chip;

UINT8 __fastcall cps3SndReadByte(UINT32 addr)
{
	return 0;
}

UINT16 __fastcall cps3SndReadWord(UINT32 addr)
{
	addr &= 0x000003ff;

	if (addr < 0x200)
		return chip->voice[addr >> 5].regs[(addr>>1) & 0xf];
	else if (addr == 0x200)
		return chip->key;

	return 0;
}

UINT32 __fastcall cps3SndReadLong(UINT32 addr)
{
	return 0;
}

void __fastcall cps3SndWriteByte(UINT32 addr, UINT8 data)
{
}

void __fastcall cps3SndWriteWord(UINT32 addr, UINT16 data)
{
	addr &= 0x000003ff;
	
	if (addr < 0x200)
		chip->voice[addr >> 5].regs[(addr>>1) & 0xf] = data;
	else if (addr == 0x200)
	{
		UINT16 key = data;
		for (INT32 i = 0; i < CPS3_VOICES; i++)
		{
			// Key off -> Key on
			if ((key & (1 << i)) && !(chip->key & (1 << i)))
			{
				chip->voice[i].frac = 0;
				chip->voice[i].pos = 0;
			}
		}
		chip->key = key;
	}
}

void __fastcall cps3SndWriteLong(UINT32 addr, UINT32 data)
{
}

INT32 cps3SndInit(UINT8 * sndrom)
{
	chip = (cps3snd_chip *)BurnMalloc( sizeof(cps3snd_chip) );
	if ( chip )
	{
		memset( chip, 0, sizeof(cps3snd_chip) );
		chip->rombase = sndrom;
		
		/* 
		 * CPS-3 Sound chip clock: 42954500 / 3 / 384 = 37286.89
		 * Sound interupt 80Hz 
		 */
		
		if (nBurnSoundRate)
			chip->delta = (CPS3_SND_BUFFER_SIZE << CPS3_SND_LINEAR_SHIFT) / nBurnSoundLen;
		
		return 0;
	}
	return 1;
}

void cps3SndReset()
{
}

void cps3SndExit()
{
	BurnFree( chip );
}

void cps3SndUpdate()
{
	memset(pBurnSoundOut, 0, nBurnSoundLen * 2 * sizeof(INT16));
	INT8 * base = (INT8 *)chip->rombase;
	cps3_voice *vptr = &chip->voice[0];

	for(INT32 i=0; i<CPS3_VOICES; i++, vptr++)
	{
		if (chip->key & (1 << i))
		{
			UINT32 start = ((vptr->regs[ 3] << 16) | vptr->regs[ 2]) - 0x400000;
			UINT32 end   = ((vptr->regs[11] << 16) | vptr->regs[10]) - 0x400000;
			UINT32 loop  = ((vptr->regs[ 9] << 16) | vptr->regs[ 7]) - 0x400000;
			UINT32 step  = ( vptr->regs[ 6] * chip->delta ) >> CPS3_SND_LINEAR_SHIFT;

			INT32 vol_l = (INT16)vptr->regs[15];
			INT32 vol_r = (INT16)vptr->regs[14];

			UINT32 pos = vptr->pos;
			UINT32 frac = vptr->frac;
			
			/* Go through the buffer and add voice contributions */
			INT16 * buffer = (INT16 *)pBurnSoundOut;

			for (INT32 j=0; j<nBurnSoundLen; j++)
			{
				INT32 sample;

				pos += (frac >> 12);
				frac &= 0xfff;

				if (start + pos >= end)
				{
					if (vptr->regs[5])
						pos = loop - start;
					else
					{
						chip->key &= ~(1 << i);
						break;
					}
				}

				// 8bit sample store with 16bit bigend ???
				sample = base[(start + pos) ^ 1];
				frac += step;

#ifdef __ALTIVEC__
/* Experimental Altivec */
				vector signed short vec0 = { buffer[0], buffer[1] };
				vector signed short vec1 = { vol_l, vol_r };
				vector signed short vec2 = { sample << 7, sample << 7 };
				vector signed short vec3 = vec_mradds(vec1, vec2, vec0);
				buffer[0] = vec3[0];
				buffer[1] = vec3[1];
#else
#define CLAMP16(io) if((int16_t) io != io) io = (io >> 31) ^ 0x7FFF;
/* Blargg-style clamping - less branching */
				INT32 sample_l;
				sample_l = ((sample * vol_r) >> 8) + buffer[0];
				CLAMP16(sample_l);
				buffer[0] = sample_l;
				
				sample_l = ((sample * vol_l) >> 8) + buffer[1];
				CLAMP16(sample_l);
				buffer[1] = sample_l;
#endif
#if 0
/* Original unoptimised code */
				INT32 sample_l;
				sample_l = ((sample * vol_r) >> 8) + buffer[0];
				if (sample_l > 32767)		buffer[0] = 32767;
				else if (sample_l < -32768)	buffer[0] = -32768;
				else 						buffer[0] = sample_l;
				
				sample_l = ((sample * vol_l) >> 8) + buffer[1];
				if (sample_l > 32767)		buffer[1] = 32767;
				else if (sample_l < -32768)	buffer[1] = -32768;
				else 						buffer[1] = sample_l;
#endif

				buffer += 2;
			}


			vptr->pos = pos;
			vptr->frac = frac;
		}
	}
	
}

INT32 cps3SndScan(INT32 nAction)
{
	if (nAction & ACB_DRIVER_DATA) {
		
		SCAN_VAR( chip->voice );
		SCAN_VAR( chip->key );
		
	}
	return 0;
}

