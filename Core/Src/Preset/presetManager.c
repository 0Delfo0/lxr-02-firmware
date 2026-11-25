/*
 * presetManager.c
 *
 * Created: 09.05.2012 16:06:19
 *  Author: Julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */ 
//#include "../config.h"
#include <frontPanelParser_ui.h>
#include "Preset/PresetManager.h"
#include "Preset/KitEngineFormat.h"
//#include "../Hardware/SD/ff.h"
#include "ff.h"
#include <stdio.h>
#include "menu/CcNr2Text.h"
#include "menu/menu.h"
//#include <util/delay.h>
#include "lcd.h"
//#include <avr/pgmspace.h>
#include <stdlib.h>
//#include <util/atomic.h>
#include "IO/uart.h"
#include "IO/DIO.h"

#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Soundengine/Soundengine.h"

_Static_assert(PAR_ENGINE_1 == KIT_ENGINE_FIRST, "kit engine layout changed");
_Static_assert(PAR_TIMBRE_DECAY_1 == KIT_ENGINE_DECAY_FIRST, "kit engine layout changed");

static FRESULT preset_readEngineTag(uint8_t *params)
{
	uint8_t tag[KIT_ENGINE_TAG_SIZE];
	unsigned int count;
	const FRESULT res = f_read((FIL*)&SDFile, tag, sizeof(tag), &count);
	if(res == FR_OK) kitEngine_migrate(params, END_OF_SOUND_PARAMETERS, tag, count);
	return res;
}

static FRESULT preset_writeEngineTag(void)
{
	unsigned int count;
	const FRESULT res = f_write((FIL*)&SDFile, KIT_ENGINE_TAG, KIT_ENGINE_TAG_SIZE, &count);
	return res != FR_OK ? res : (count == KIT_ENGINE_TAG_SIZE ? FR_OK : FR_DISK_ERR);
}




//#include "../Hardware/timebase.h"

//FATFS SDFatFS;		/* File system object for the logical drive */
//FIL SDFile;		/* place to hold 1 file*/
static uint8_t morph_state = 0; //flag for non blocking morph
static uint8_t morph_index = 0;
char savefileNames[NUM_KIT_SLOTS][NUM_SAVEFILE_NAMES][8];

char currentFolder[13];

/* Working buffer for empty_directory(), which builds the full path of every file it
   deletes into the buffer it is given. currentFolder is 13 bytes and cannot hold
   "/projNN" plus '/' plus an 8.3 name. Kept at file scope and not on the stack because
   the project save is the deepest call path in the firmware and runs with the audio
   interrupt active. */
static char emptyDirPath[256];

//char presetNameBuffer[8];
//char currentName[8];
//uint8_t activePresetNr = 0;

#define FILE_VERSION 2

/*
#define NUM_TRACKS 7
#define NUM_PATTERN 8*/
#define STEPS_PER_PATTERN NUM_STEPS //64
#define PATTERN_BODY_V3_LEN (NUM_TRACKS * (NUM_STEPS * sizeof(Step) \
		+ NUM_STEPS * sizeof(uint8_t)) + NUM_TRACKS * NUM_BARS * sizeof(uint16_t) \
		+ 1 + NUM_TRACKS + 1)


#define FEXT_SOUND 	0
#define FEXT_PAT 	1
#define FEXT_ALL 	2
#define FEXT_PROJ 	3
#define FEXT_SONG 	4



// fill buffer (length 9 total) with a filename. type is one of above
// eg p001.snd
static void preset_makeFileNameOld(char *buf, uint8_t num, uint8_t type);

//load uses wildcard to find a name of the pattern 00-*.ext
static FRESULT preset_makeFileNameLoad(char *buf, uint8_t num, uint8_t type);
//save uses the kit name to create a filename eg 00-NAME.ext
static void preset_makeFileNameSave(char *buf, uint8_t num, uint8_t type, char *name);
static const char *preset_fileExtension(uint8_t type);
static void preset_makeFileNamePattern(char *buf, uint8_t num, uint8_t type);
static void preset_removeStaleSlotFiles(uint8_t num, uint8_t type, const char *keep);
static FRESULT preset_fopenWrite(FIL *file, const char *path);
static FRESULT preset_fwrite(FIL *file, const void *data, UINT size);
static FRESULT preset_fcloseWrite(FIL *file);
static FRESULT preset_abortSdOperation(FRESULT res);
static FRESULT preset_removeFolder(const char *path);
static void preset_recoverInterruptedSave(uint8_t presetNr);

//static void preset_makeFileNameTemp(char *buf, uint8_t num, uint8_t type);


#define PRESET_SAVE_TIME 2000

uint32_t preset_kitTimer = 0;
uint32_t preset_patTimer = 0;

uint8_t preset_kitDirty = 0;
uint8_t preset_patDirty = 0;

/* Track length as it comes off the card. The sequencer reads 0 as 16
   (sequencer.c:1413,1636), so 0 is kept; a length above NUM_STEPS is not a
   length at all. Unchecked, 200 reaches seq_subStepPattern[...][length-1]
   (sequencer.c:1146) and quantizedStep % length (sequencer.c:2533), which
   writes a note past the end of the 64-step array. */
static uint8_t preset_validLength(uint8_t length)
{
	return length > NUM_STEPS ? NUM_STEPS : length;
}

/* A pattern's kit number indexes mainParameters[NUM_KIT_SLOTS] by way of
   preset_loadDrumsetRam() (main.c:866, sequencer.c:665). The SysEx path
   already clamps it (SysexHandler.c:425); the card path did not. */
static void preset_validateKitNr(uint8_t patternNr)
{
	if(seq_patternSet.kitNr[patternNr] >= NUM_KIT_SLOTS)
		seq_patternSet.kitNr[patternNr] = 0;
}

/* One pattern as it arrives from the card. The readers used to write straight
   into seq_patternSet, so a file that stopped part way through left a pattern
   half new and half old, with the sequencer playing the result (P10).
   3,817 bytes: it lives in the RAM region rather than on the stack, because
   both readers are reached from the menu and from preset_loadProject(), and it
   is its own buffer rather than sx_patternBuf or seq_tmpPattern, which would
   couple a card read to the SysEx path or to the pattern edit buffer. */
typedef struct
{
	Step		steps[NUM_TRACKS][STEPS_PER_PATTERN];
	StepTiming	timing[NUM_TRACKS][STEPS_PER_PATTERN];
	uint16_t	mainSteps[NUM_BARS][NUM_TRACKS];
	uint8_t		shuffle;
	uint8_t		length[NUM_TRACKS];
	uint8_t		kitNr;
	uint8_t		conditions[SEQ_CONDITION_BYTES];
} PresetPatternBuf;
static PresetPatternBuf preset_patternBuf;

/* Read one pattern body from the open SDFile into the buffer. hasTiming and
   hasConditions say whether this generation of the file carries those blocks;
   the buffer is cleared first, so what a file does not carry reads back as
   zero instead of as the previous pattern's - which is the second half of P10,
   patterns from older firmware inheriting flams and shifts never saved with
   them. Returns 1 when the whole body was there. */
static uint8_t preset_stagePatternBody(uint8_t hasTiming, uint8_t hasConditions)
{
	UINT bytesRead;

	memset(&preset_patternBuf, 0, sizeof(preset_patternBuf));

	for(uint8_t trk=0; trk<NUM_TRACKS; trk++)
	{
		f_read((FIL*)&SDFile, preset_patternBuf.steps[trk],
				sizeof(Step)*STEPS_PER_PATTERN, &bytesRead);
		if(bytesRead != sizeof(Step)*STEPS_PER_PATTERN)
			return 0;	// file error, or we ran out of file (eof)

		if(hasTiming)
		{
			f_read((FIL*)&SDFile, preset_patternBuf.timing[trk],
					sizeof(StepTiming)*STEPS_PER_PATTERN, &bytesRead);
			if(bytesRead != sizeof(StepTiming)*STEPS_PER_PATTERN)
				return 0;
		}
	}

	/* main step data, in the order it is written: track outer, bar inner */
	for(uint8_t trk=0; trk<NUM_TRACKS; trk++)
	{
		for(uint8_t bar=0; bar<NUM_BARS; bar++)
		{
			f_read((FIL*)&SDFile, &preset_patternBuf.mainSteps[bar][trk],
					sizeof(uint16_t), &bytesRead);
			if(bytesRead != sizeof(uint16_t))
				return 0;
		}
	}

	f_read((FIL*)&SDFile, &preset_patternBuf.shuffle, 1, &bytesRead);
	if(bytesRead != 1)
		return 0;

	/* The track lengths may legitimately be absent - older files do not have
	   them - and that is not a failure. Absent means 0, which the sequencer
	   reads as 16. */
	f_read((FIL*)&SDFile, preset_patternBuf.length, NUM_TRACKS, &bytesRead);

	f_read((FIL*)&SDFile, &preset_patternBuf.kitNr, 1, &bytesRead);
	if(bytesRead == 0)
		return 0;

	if(hasConditions)
	{
		f_read((FIL*)&SDFile, preset_patternBuf.conditions,
				SEQ_CONDITION_BYTES, &bytesRead);
		if(bytesRead != SEQ_CONDITION_BYTES)
			return 0;
	}
	return 1;
}

/* Replace one pattern slot with what was staged, and only then. */
static void preset_commitPatternBody(uint8_t patternNr)
{
	for(uint8_t trk=0; trk<NUM_TRACKS; trk++)
	{
		memcpy(seq_patternSet.seq_subStepPattern[patternNr][trk],
				preset_patternBuf.steps[trk], sizeof(Step)*STEPS_PER_PATTERN);
		memcpy(seq_patternSet.seq_stepTiming[patternNr][trk],
				preset_patternBuf.timing[trk],
				sizeof(StepTiming)*STEPS_PER_PATTERN);
		seq_patternSet.length[patternNr][trk] =
				preset_validLength(preset_patternBuf.length[trk]);
	}
	for(uint8_t bar=0; bar<NUM_BARS; bar++)
	{
		for(uint8_t trk=0; trk<NUM_TRACKS; trk++)
			seq_patternSet.seq_mainSteps[bar][patternNr][trk] =
					preset_patternBuf.mainSteps[bar][trk];
	}
	seq_patternSet.shuffle[patternNr] = preset_patternBuf.shuffle;
	seq_patternSet.kitNr[patternNr] = preset_patternBuf.kitNr;
	preset_validateKitNr(patternNr);
	memcpy(seq_patternSet.seq_conditions[patternNr],
			preset_patternBuf.conditions, SEQ_CONDITION_BYTES);
}

void preset_initTmpFolder()
{
	char folderName[13];
	sprintf(folderName, "/tmp");

	FRESULT res;
	FILINFO fno;

	res = f_stat(folderName, &fno);
	if(res == FR_NO_FILE)
	{
		res = f_mkdir(folderName);
		if(res == FR_OK)
		{
			f_chdir(folderName);
		}
	}
	else
	{
		f_chdir(folderName);
	}
}

void preset_resetAllSaveTimers()
{
	preset_kitDirty = 0;
	preset_patDirty = 0;
}

FRESULT loadTmpFolderFiles()
{
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	FRESULT res;
	//check tmp folder for *.snd and *.pat files and load them to RAM
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{

		//FR_NO_FILE just means an incomplete project and is no reason to abort!
		res = preset_loadDrumsetSD(i, 0);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			lcd_process_display_interrupt = 1;
			preset_initTmpFolder();
			f_close((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
			return res;
		}
		res = preset_loadPatternSD(i);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			f_close((FIL*)&SDFile);
			return preset_abortSdOperation(res);
		}
		res = preset_loadSongSD(i);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			f_close((FIL*)&SDFile);
			return preset_abortSdOperation(res);
		}
	}

	return FR_OK;
}

void preset_touchTimer(PresetTimer type)
{
	if(type==PRESET_KIT_TIMER)
	{
		preset_kitTimer = HAL_GetTick();
		preset_kitDirty = 1;
	}
	else if(type==PRESET_PAT_TIMER)
	{
		preset_patTimer = HAL_GetTick();
		preset_patDirty = 1;
	}
	else if(type==PRESET_STOP_PAT_TIMER)
	{
		//abort pattern save timer, since seq is running again...
		preset_patDirty = 0;
	}

};


FRESULT preset_saveTmpKit()
{
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
	FRESULT res;

	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, "tmp.snd");
	if(res != FR_OK)
	{
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		return res;
	}

	//write current preset nr to file (the kit in the edit buffer, not the load/save menu cursor)
	res = preset_fwrite((FIL*)&SDFile, (void*)&menu_loadedPresetNr[CURRENT_KIT],1);
	if(res != FR_OK)
	{
		f_close((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		return res;
	}
	//write the preset data
	res = preset_fwrite((FIL*)&SDFile, activeParameters,END_OF_SOUND_PARAMETERS);
	if(res == FR_OK) res = preset_writeEngineTag();

	//close the file
	res = preset_fcloseWrite((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
	return res;
}

FRESULT preset_LoadTmpKit()
{
	FRESULT res;
	unsigned int bytesRead;
	uint8_t kitNr = 0;

	//open the file
	res = f_open((FIL*)&SDFile,"tmp.snd",FA_OPEN_EXISTING | FA_READ);
	if(res != FR_OK) return res;

	//first the preset nr
	res=f_read((FIL*)&SDFile,(void*)&kitNr,1,&bytesRead);
	if(bytesRead != 1) {
		res=FR_DISK_ERR;
		goto closeFile;
	}

	menu_selectedPresetNr[SAVE_TYPE_KIT] = kitNr;


	//read the file content
		bytesRead=1;
		int16_t i;
		uint8_t *para;
		bytesRead = 0;

		para=activeParameters;

		res=f_read((FIL*)&SDFile, para,END_OF_SOUND_PARAMETERS, &bytesRead);
		if(res==FR_OK) {
			// set to 0 for any that were not read from the file
			if(END_OF_SOUND_PARAMETERS-bytesRead)
				memset(para+bytesRead,0,END_OF_SOUND_PARAMETERS-bytesRead);
			res = preset_readEngineTag(para);

			//special case mod targets. normalize.
			const uint8_t nmt=getNumModTargets();
			for(i=0;i<6;i++) {
				// since I've changed the meaning of these, I'll ensure that it's valid for kits saved prior to the
				// change
				if(para[PAR_VEL_DEST_1+i] >= nmt )
					para[PAR_VEL_DEST_1+i] = 0;
				if(para[PAR_TARGET_LFO1+i] >= nmt )
					para[PAR_TARGET_LFO1+i] = 0;
			}
		}


	closeFile:
		//close the file handle
		f_close((FIL*)&SDFile);

		// update the soundengine with the new parameters
		if(res==FR_OK) {
			// the edit buffer now holds this kit
			if(kitNr < NUM_KIT_SLOTS)
				menu_loadedPresetNr[CURRENT_KIT] = kitNr;
			preset_sendDrumsetParameters();
			return FR_OK;
		}

		return res;
}

FRESULT preset_LoadTmpPat()
{

	uint8_t success=1; // start off succeeding
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
	//filename in 8.3  format
		//uint8_t fileVersion = 1;
		UINT bytesRead;
		FRESULT res;



		res = f_open((FIL*)&SDFile,"tmp.pat",FA_OPEN_EXISTING | FA_READ);
		if(res != FR_OK)
		{
		#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
					HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
		#else

		#endif
				return res;
		}

		//read current preset nr
		res = f_read((FIL*)&SDFile,(void*)&menu_playedPattern,1,&bytesRead);
		if(bytesRead != 1)
		{
			success = 0;
			goto closeFile;
		}

		if(menu_playedPattern >= NUM_PATTERN)
		{
			menu_playedPattern = 0;
			success = 0;
			goto closeFile;
		}

		menu_shownPattern = menu_playedPattern;
		seq_activePattern = menu_playedPattern;
		seq_pendingPattern = menu_playedPattern;
		menu_selectedPresetNr[SAVE_TYPE_PATTERN] = menu_playedPattern;

		frontPanel_sendData(SEQ_CC,SEQ_SET_SHOWN_PATTERN,menu_playedPattern);

		seq_copyPatternToBackupBuffer(seq_activePattern);
		//frontParser_shownPattern = menu_playedPattern;
		uint8_t patternNr = menu_playedPattern;

	//---------------- read pattern data -----------------------------

		// note that the pattern length data is no longer stored in the same way.
		// This means that loading old patterns with non-standard length, the length will be
		// ignored and a standard length used. I think it's acceptable as long as users are aware of this.

		/* tmp.pat always carries the timing block; the condition block only if
		   the file is long enough to hold one. Staged, then committed, so a
		   half-written autosave leaves the pattern as it was. */
		const uint8_t hasConditions = f_size((FIL*)&SDFile)
				>= 1u + PATTERN_BODY_V3_LEN + SEQ_CONDITION_BYTES;
		success = preset_stagePatternBody(1, hasConditions);
		if(success)
			preset_commitPatternBody(patternNr);

		if( success == 0)
		{


			f_close((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
			return FR_INT_ERR;
		}
		seq_copyPatternToBackupBuffer(patternNr);

		success=1;
		//close the file handle



	closeFile:
		f_close((FIL*)&SDFile);

		#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
					HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
		#else

		#endif


		// return success or failure
		if(success) return FR_OK;
		else return FR_INT_ERR;


}

/* Every write on the save paths goes through these three.
 *
 * preset_writeResult is sticky for the life of one file: once a write has
 * failed, the ones after it do nothing and preset_fcloseWrite() reports the
 * first failure. That turns "every call site must remember to check" into "one
 * check at the end", which is what the save paths needed - this file had 22
 * f_write() calls and three of them checked nothing, so a failure in the middle
 * of a project save was swallowed and the save carried on (#73).
 *
 * A short count is FR_DENIED, not FR_DISK_ERR. FatFs reports a full card as
 * FR_OK with fewer bytes written than asked, and "card full" is what that is;
 * calling it a disk error told the user their card was failing when it was
 * merely full (#70). menu_showSdError() names FR_DENIED, so the message
 * follows. */
static FRESULT preset_writeResult = FR_OK;

static FRESULT preset_fopenWrite(FIL *file, const char *path)
{
	preset_writeResult = f_open(file, path, FA_CREATE_ALWAYS | FA_WRITE);
	return preset_writeResult;
}

static FRESULT preset_fwrite(FIL *file, const void *data, UINT size)
{
	UINT written = 0;
	if(preset_writeResult != FR_OK) return preset_writeResult;
	preset_writeResult = f_write(file, data, size, &written);
	if(preset_writeResult == FR_OK && written != size)
		preset_writeResult = FR_DENIED;		/* the card is full */
	return preset_writeResult;
}

static FRESULT preset_fcloseWrite(FIL *file)
{
	const FRESULT closeRes = f_close(file);
	return preset_writeResult != FR_OK ? preset_writeResult : closeRes;
}

FRESULT preset_saveTmpPat()
{

	//don't save invalid pattern
	if(menu_playedPattern >= NUM_PATTERN) return FR_INVALID_PARAMETER;
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	FRESULT res;
	uint8_t presetNr = menu_playedPattern;
	//lcd_clear();
	//lcd_home();
	//lcd_string(PSTR("Saving pattern"));

	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, "tmp.pat");
	if(res != FR_OK)
	{
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		return res;
	}

	//write current preset nr to file
	res = preset_fwrite((FIL*)&SDFile, &menu_playedPattern, 1);
	if(res != FR_OK)
	{
		goto writeFailed;
	}

	//write pattern data
	//preset_writePatternData(presetNr);
#if 0
#else

		uint8_t length;

		//---------------  write the step data (note, vol, prob, autom.) ---------------
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
			res = preset_fwrite((FIL*)&SDFile,
					&seq_patternSet.seq_subStepPattern[presetNr][trk][0],
					sizeof(Step) * STEPS_PER_PATTERN);
			if(res != FR_OK) goto writeFailed;

			res = preset_fwrite((FIL*)&SDFile,
					&seq_patternSet.seq_stepTiming[presetNr][trk][0],
					sizeof(uint8_t) * STEPS_PER_PATTERN);
			if(res != FR_OK) goto writeFailed;
		}

		//--------------- now the main step data ---------------
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
			for(uint8_t bar=0;bar<NUM_BARS; bar++)
			{
				const uint16_t mainStepData = seq_patternSet.seq_mainSteps[bar][presetNr][trk];
				res = preset_fwrite((FIL*)&SDFile, &mainStepData, sizeof(mainStepData));
				if(res != FR_OK) goto writeFailed;
			}
		}

		//----- shuffle setting ------


		//f_write((FIL*)&SDFile,(const void*)&activeParameters[PAR_SHUFFLE],sizeof(uint8_t),&bytesWritten);
		res = preset_fwrite((FIL*)&SDFile, &seq_patternSet.shuffle[presetNr],
				sizeof(uint8_t));
		if(res != FR_OK) goto writeFailed;

		//----- pattern/track lengths ------
		// we reuse the same call from above (when saving main step data)
		// but we only use the length info retrieved. We want to store it at the end
		// to avoid breaking compatibility with save file


		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
				length = seq_patternSet.length[presetNr][trk] ;//seq_patternSet.seq_mainSteps[bar][pat][trk];
				res = preset_fwrite((FIL*)&SDFile, &length, sizeof(length));
				if(res != FR_OK) goto writeFailed;
		}

		//write associated kit nr
		res = preset_fwrite((FIL*)&SDFile, &seq_patternSet.kitNr[presetNr],
				sizeof(uint8_t));
		if(res != FR_OK) goto writeFailed;
		res = preset_fwrite((FIL*)&SDFile,
				seq_patternSet.seq_conditions[presetNr], SEQ_CONDITION_BYTES);
		if(res != FR_OK) goto writeFailed;

#endif

	//close the file
	res = f_close((FIL*)&SDFile);

	//reset the lcd
//	menu_repaintAll();

#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	return res;

writeFailed:
	f_close((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Stop_IT(&htim2);
#endif
	return res;
}

/*------------------------------------------------------------/
/ Delete a sub-directory even if it contains any file
/-------------------------------------------------------------/
/ The delete_node() function is for R0.12+.
/ It works regardless of FF_FS_RPATH.
*/


FRESULT delete_node (
    TCHAR* path,    /* Path name buffer with the sub-directory to delete */
    UINT sz_buff,   /* Size of path name buffer (items) */
    FILINFO* fno    /* Name read buffer */
)
{
    UINT i, j;
    FRESULT fr;
    DIR dir;


    fr = f_opendir(&dir, path); /* Open the sub-directory to make it empty */
    if (fr != FR_OK) return fr;

    for (i = 0; path[i]; i++) ; /* Get current path length */
    path[i++] = _T('/');

    for (;;) {
        fr = f_readdir(&dir, fno);  /* Get a directory item */
        if (fr != FR_OK || !fno->fname[0]) break;   /* End of directory? */
        j = 0;
        do {    /* Make a path name */
            if (i + j >= sz_buff) { /* Buffer over flow? */
                fr = 100; break;    /* Fails with 100 when buffer overflow */
            }
            path[i + j] = fno->fname[j];
        } while (fno->fname[j++]);
        if (fno->fattrib & AM_DIR) {    /* Item is a sub-directory */
            fr = delete_node(path, sz_buff, fno);
        } else {                        /* Item is a file */
            fr = f_unlink(path);
        }
        if (fr != FR_OK) break;
    }

    path[--i] = 0;  /* Restore the path name */
    f_closedir(&dir);

    if (fr == FR_OK) fr = f_unlink(path);  /* Delete the empty sub-directory */
    return fr;
}

FRESULT preset_clearTmpFolder()
{
	FILINFO fno;
	FRESULT fr;
	TCHAR buff[256];

	f_chdir("/");

	/* Directory to be deleted */
	sprintf(buff, "/tmp");

	fr = delete_node(buff, sizeof buff / sizeof buff[0], &fno);

	preset_initTmpFolder();

	return fr;
}


void preset_processTimer()
{
	if(preset_patDirty == 1)
	{
		if(HAL_GetTick()-preset_patTimer >= PRESET_SAVE_TIME )
		{
			preset_patDirty = 0;
			preset_saveTmpPat();
		}
	}

	if(preset_kitDirty == 1)
	{
		if(HAL_GetTick()-preset_kitTimer >= PRESET_SAVE_TIME )
		{
			preset_kitDirty = 0;
			preset_saveTmpKit();
		}
	}
}


/*------------------------------------------------------------/
/ Remove all contents of a directory
/ This function works regardless of FF_FS_RPATH.
/------------------------------------------------------------*/




FRESULT empty_directory (
    char* path,     /* Working buffer filled with start directory */
    UINT sz_buff    /* Size of the working buffer (items) */
)
{
	FILINFO fno;
    UINT i, j;
    FRESULT fr;
    DIR dir;

    fr = f_opendir(&dir, path);
    if (fr == FR_OK) {
        for (i = 0; path[i]; i++) ;
        if (i + 1 >= sz_buff) {     /* No room for the separator */
            f_closedir(&dir);
            return FR_NOT_ENOUGH_CORE;
        }
        path[i++] = '/';
        for (;;) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || !fno.fname[0]) break;
            if (_FS_RPATH && fno.fname[0] == '.') continue;
            j = 0;
            do {
                if (i + j >= sz_buff) {     /* Path does not fit the working buffer */
                    fr = FR_NOT_ENOUGH_CORE;
                    break;
                }
                path[i+j] = fno.fname[j];
            } while (fno.fname[j++]);
            if (fr != FR_OK) break;
            if (fno.fattrib & AM_DIR) {
                fr = empty_directory(path, sz_buff);
                if (fr != FR_OK) break;
            }
            fr = f_unlink(path);
            if (fr != FR_OK) break;
        }
        path[--i] = '\0';
        f_closedir(&dir);
    }

    return fr;
}
//----------------------------------

uint8_t preset_isNameEmpty(char* name)
{
	uint8_t isEmpty = 1;

	if( (name[0] != 'E') && (name[0] != 0) ) isEmpty = 0;
	if( (name[1] != 'm') && (name[1] != 0) ) isEmpty = 0;
	if( (name[2] != 'p') && (name[2] != 0) ) isEmpty = 0;
	if( (name[3] != 't') && (name[3] != 0) ) isEmpty = 0;
	if( (name[4] != 'y') && (name[4] != 0) ) isEmpty = 0;
	if( (name[5] != 0) ) isEmpty = 0;
	if( (name[6] != 0) ) isEmpty = 0;
	if( (name[7] != 0) ) isEmpty = 0;


	return isEmpty;
}
//----------------------------------------------------
//load init kit
void preset_init(uint8_t presetNr)
{

	//const uint8_t presetNr = menu_selectedPresetNr[SAVE_TYPE_KIT];

	//first the preset name
	memcpy(savefileNames[presetNr][KIT_NAMES],initKit,8*sizeof(uint8_t));


	if(preset_isNameEmpty(savefileNames[presetNr][KIT_NAMES]))
	{
		sprintf((void*)savefileNames[presetNr][KIT_NAMES], "Empty");
	}


	//then the data
	//read the file content

	int16_t i;
	uint8_t *para;


	para=mainParameters[presetNr];

	/* initKit is 8 name bytes followed by the parameters, so parameter n is
	   initKit[8+n]. The loop used to stop at END_OF_SOUND_PARAMETERS while
	   reading from i and writing to i-8, which filled parameters 0-238 and
	   left 239-246 - PAR_FX_P2 to PAR_MORPH_KIT_DEST - holding whatever the
	   previous kit had. INIT_KIT_SIZE is 254, so the array reaches parameter
	   245 and PAR_MORPH_KIT_DEST (246) has no initial value of its own: it
	   is zeroed rather than inherited. */
	for(i=0;i<END_OF_SOUND_PARAMETERS; i++)
	{
		para[i] = i < INIT_KIT_SIZE - 8 ? initKit[8 + i] : 0;
	}
	kitEngine_migrate(para, END_OF_SOUND_PARAMETERS, NULL, 0);

	//special case mod targets. normalize.
	const uint8_t nmt=getNumModTargets();
	for(i=0;i<6;i++) {
		// since I've changed the meaning of these, I'll ensure that it's valid for kits saved prior to the
		// change
		if(para[PAR_VEL_DEST_1+i] >= nmt )
			para[PAR_VEL_DEST_1+i] = 0;
		if(para[PAR_TARGET_LFO1+i] >= nmt )
			para[PAR_TARGET_LFO1+i] = 0;
	}

	//copy parameters to edit buffer
	memcpy(activeParameters, mainParameters[presetNr], sizeof(uint8_t) * END_OF_SOUND_PARAMETERS);
	preset_sendDrumsetParameters();
	//frontPanel_sendData(PRESET,PRESET_LOAD,presetNr);

}

//----------------------------------------------------
static FRESULT preset_writeDrumsetData(uint8_t presetNr, uint8_t isMorph)
{
	uint16_t i;
	FRESULT res;

	if(isMorph)
	{
		for(i=0;i<END_OF_SOUND_PARAMETERS;i++)
		{
			uint8_t value;
			//Mod targets are not morphed!!!
			if( (i >= PAR_VEL_DEST_1) && (i <= PAR_VEL_DEST_6) )
			{
				value = activeParameters[i];
			} else if( (i >= PAR_TARGET_LFO1) && (i <= PAR_TARGET_LFO6) )
			{
				value = activeParameters[i];
			} else if( (i >= PAR_VOICE_LFO1) && (i <= PAR_VOICE_LFO6) )
			{
				value = activeParameters[i];
			} 
			else 
			{
				value = preset_getMorphValue(i,activeParameters[PAR_MORPH]);
			}					
			res = preset_fwrite((FIL*)&SDFile, &value,1);
			/* the sticky flag makes the rest no-ops, but there are 247 of them */
			if(res != FR_OK) return res;
		}
	} else {
		for(i=0;i<END_OF_SOUND_PARAMETERS;i++)
		{
			res = preset_fwrite((FIL*)&SDFile, &mainParameters[presetNr][i],1);
			/* the sticky flag makes the rest no-ops, but there are 247 of them */
			if(res != FR_OK) return res;
		}
	}
	return FR_OK;
}

//----------------------------------------------------
void preset_loadDrumsetRam(uint8_t presetNr, uint8_t isMorph, uint8_t sendImmediate)
{
	//mainParameters has NUM_KIT_SLOTS rows. The bound is checked here and not at
	//the call sites because presetNr arrives from three places that cannot all be
	//trusted: a MIDI bank select carries 0-127 (frontPanelParser.c), a pattern's
	//kit number is read from the card, and only the menu clamps its own value.
	if(presetNr >= NUM_KIT_SLOTS) return;
	soundengine_process();
	if(isMorph)
	{
		morphParameters = mainParameters[presetNr];
	}
	else
	{
		//copy parameters to edit buffer
		memcpy(activeParameters, mainParameters[presetNr], sizeof(uint8_t) * END_OF_SOUND_PARAMETERS);
		//if(activeParameters[PAR_PATTERN_MODE] = 1)
		//load asociated morph kit as well

		// morphParameters = mainParameters[activeParameters[PAR_MORPH_KIT_DEST]];


	}
	soundengine_process();

	if(sendImmediate)
	{
		preset_sendDrumsetParametersImmediate();
	}
	else
	{
		//activePresetNr = menu_currentPresetNr[menu_saveOptions.category];
		preset_sendDrumsetParameters();
	}
}
//----------------------------------------------------
void preset_saveDrumsetRam(uint8_t presetNr, uint8_t isMorph)
{
	//Same array, same reason, and this one writes: see preset_loadDrumsetRam().
	if(presetNr >= NUM_KIT_SLOTS) return;
	if(isMorph)
	{
		for(uint16_t i=0;i<END_OF_SOUND_PARAMETERS;i++)
		{
			uint8_t value;
			//Mod targets are not morphed!!!
			if( (i >= PAR_VEL_DEST_1) && (i <= PAR_VEL_DEST_6) )
			{
				value = activeParameters[i];
			} else if( (i >= PAR_TARGET_LFO1) && (i <= PAR_TARGET_LFO6) )
			{
				value = activeParameters[i];
			} else if( (i >= PAR_VOICE_LFO1) && (i <= PAR_VOICE_LFO6) )
			{
				value = activeParameters[i];
			}
			else
			{
				value = preset_getMorphValue(i,activeParameters[PAR_MORPH]);
			}
			//f_write((FIL*)&SDFile,&value,1,&bytesWritten);
			mainParameters[presetNr][i] = value;
		}
	}
	else
	{
		//copy edit buffer to parameter array
		memcpy(mainParameters[presetNr], activeParameters, sizeof(uint8_t) * END_OF_SOUND_PARAMETERS);
	}
}
//----------------------------------------------------
FRESULT preset_saveDrumsetSD(uint8_t presetNr, uint8_t isMorph)
{
#if USE_SD_CARD
	FRESULT res;
	//filename in 8.3  format
	char filename[13];
	preset_makeFileNameSave(filename,presetNr,FEXT_SOUND, savefileNames[presetNr][KIT_NAMES]);
	//sprintf(filename,"p%03d.snd",presetNr);



	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, filename);
	if(res != FR_OK) return res;
	
	//write preset name to file

	res = preset_fwrite((FIL*)&SDFile, (void*)savefileNames[presetNr][KIT_NAMES],8);
	//write the preset data
	if(res == FR_OK) res = preset_writeDrumsetData(presetNr, isMorph);
	if(res == FR_OK) res = preset_writeEngineTag();
	
	//close the file
	res = preset_fcloseWrite((FIL*)&SDFile);
	if(res == FR_OK)
		preset_removeStaleSlotFiles(presetNr, FEXT_SOUND, filename);

	return res;
#else
#endif
};

//----------------------------------------------------
static FRESULT preset_writeGlobalData()
{
	int i;
	for(i=PAR_BEGINNING_OF_GLOBALS;(i<NUM_PARAMS);i++)
	{
		const FRESULT res = preset_fwrite((FIL*)&SDFile,&activeParameters[i],1);
		if(res != FR_OK) return res;
	}
	return FR_OK;
}

//----------------------------------------------------
FRESULT preset_saveGlobals()
{
#if USE_SD_CARD
	FRESULT res;
/*
	res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
	if( res != FR_OK)
	{
		return res;
	}
	*/
	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, "glo.cfg");
	if(res != FR_OK) return res;
	res = preset_writeGlobalData();

	//close the file
	res = preset_fcloseWrite((FIL*)&SDFile);

	//f_unmount((TCHAR const*)SDPath);
	return res;
#else
#endif	
}
//----------------------------------------------------
// Older glo.cfg files are shorter, and older .all files have 0xFF padding where
// the SysEx globals now live, so fall back to defaults for anything out of range.
static void preset_sanitizeSysexGlobals(uint8_t forceDefaults)
{
	if(forceDefaults || activeParameters[PAR_SYSEX_ENABLE] > 1)
		activeParameters[PAR_SYSEX_ENABLE] = 1;
	if(forceDefaults || activeParameters[PAR_SYSEX_DEVICE_ID] < 1 || activeParameters[PAR_SYSEX_DEVICE_ID] > 16)
		activeParameters[PAR_SYSEX_DEVICE_ID] = 1;
	if(forceDefaults || activeParameters[PAR_SYSEX_ECHO] > 1)
		activeParameters[PAR_SYSEX_ECHO] = 0;
}
//----------------------------------------------------
// returns 1 on success
static uint8_t preset_readGlobalData()
{
#if USE_SD_CARD

	UINT bytesRead;
#if 0
	int i;
	for(i=PAR_BEGINNING_OF_GLOBALS;(i<NUM_PARAMS) &&( bytesRead!=0);i++) {
		f_read((FIL*)&SDFile,&activeParameters[i],1,&bytesRead);
		if(!bytesRead)
			return 0;
	}
	if(!bytesRead)
		return 0;
	return 1;
#else
	const uint8_t numGlobals = NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS;
	// stock v1.7 files only contain the globals up to PAR_PATTERN_MODE
	const uint8_t numLegacyGlobals = PAR_SYSEX_ENABLE - PAR_BEGINNING_OF_GLOBALS;
	uint8_t staged[NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS];

	/* Staged, not read straight into activeParameters: a file that stops part
	   way through leaves the settings part from the card and part from the
	   project before it, and the caller cannot tell that happened. Nothing is
	   applied unless the whole file is there.

	   The values are not range checked, on purpose. Erica's own glo.cfg files
	   carry PAR_MIDI_CHAN_GLOBAL = 0 and PAR_MIDI_NOTEn = 0 in all six factory
	   projects, both outside the ranges docs/params.json documents, and
	   buttonHandler.c:1178 stores SKIP_NEXT_KIT_AUTOLOAD (2) in
	   PAR_PATTERN_MODE, which that file calls a 0-1 menu. A clamp built from
	   those ranges would silently rewrite valid files. */
	bytesRead = 0;
	if(f_read((FIL*)&SDFile,staged,numGlobals,&bytesRead) != FR_OK) return 0;
	if(bytesRead < numLegacyGlobals) return 0;
	memcpy(&activeParameters[PAR_BEGINNING_OF_GLOBALS],staged,bytesRead);
	preset_sanitizeSysexGlobals(bytesRead < numGlobals);
	return 1;

#endif
#endif
}
//----------------------------------------------------
uint8_t preset_loadGlobals()
{


	FRESULT res;
/*
	res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
	if( res != FR_OK)
	{
		return res;
	}
	*/

	//open the file
	res = f_open((FIL*)&SDFile,"glo.cfg",FA_OPEN_EXISTING | FA_READ);
	
	if(res!=FR_OK)
		return 0; //file open error... maybe the file does not exist?

	if(!preset_readGlobalData())
	{
		f_close((FIL*)&SDFile);
		return 0;
	}
	f_close((FIL*)&SDFile);
	menu_sendAllGlobals();
	
	//f_unmount((TCHAR const*)SDPath);

	return 1;

}




//----------------------------------------------------
void preset_loadDefaultGlobals()
{
	activeParameters[PAR_BPM] = 120;

	activeParameters[PAR_MIDI_CHAN_1] = 1;
	activeParameters[PAR_MIDI_CHAN_2] = 1;
	activeParameters[PAR_MIDI_CHAN_3] = 1;
	activeParameters[PAR_MIDI_CHAN_4] = 1;
	activeParameters[PAR_MIDI_CHAN_5] = 1;
	activeParameters[PAR_MIDI_CHAN_6] = 1;
	activeParameters[PAR_MIDI_CHAN_7] = 1;

	activeParameters[PAR_MIDI_NOTE1] = 48;
	activeParameters[PAR_MIDI_NOTE2] = 48;
	activeParameters[PAR_MIDI_NOTE3] = 48;
	activeParameters[PAR_MIDI_NOTE4] = 48;
	activeParameters[PAR_MIDI_NOTE5] = 48;
	activeParameters[PAR_MIDI_NOTE6] = 48;
	activeParameters[PAR_MIDI_NOTE7] = 48;


	activeParameters[PAR_FETCH] = 0;
	activeParameters[PAR_FOLLOW] = 0;

	activeParameters[PAR_QUANTISATION] = 0;

	activeParameters[PAR_SCREENSAVER_ON_OFF] = 1;

	activeParameters[PAR_MIDI_ROUTING] = 0;
	activeParameters[PAR_MIDI_FILT_TX] = 15;	//all
	activeParameters[PAR_MIDI_FILT_RX] = 15; //all

	activeParameters[PAR_PRESCALER_CLOCK_IN] = 0; //all
	activeParameters[PAR_PRESCALER_CLOCK_OUT1] = 0; //all
	activeParameters[PAR_PRESCALER_CLOCK_OUT2] = 0; //all
	activeParameters[PAR_TRIG_GATE_MODE] = 0; //all

	activeParameters[PAR_BAR_RESET_MODE] = 1; //all
	activeParameters[PAR_MIDI_CHAN_GLOBAL] = 0; //all

	activeParameters[PAR_PATTERN_MODE] = 1; //auto

	activeParameters[PAR_SYSEX_ENABLE] = 1;
	activeParameters[PAR_SYSEX_DEVICE_ID] = 1;
	activeParameters[PAR_SYSEX_ECHO] = 0;

}
//----------------------------------------------------
void preset_createGlobals()
{
	preset_loadDefaultGlobals();
	preset_saveGlobals();
}
//----------------------------------------------------
/* A glo.cfg that cannot be used - missing, short, unreadable - leaves the
   settings undefined: without this the running state keeps whatever the
   previous project put there while the card says something else. Fall back to
   the defaults, push them out like a successful load does, and say so, because
   the user's settings for this project have just been ignored. */
void preset_useDefaultGlobals()
{
	preset_loadDefaultGlobals();
	menu_sendAllGlobals();
	lcd_clear();
	lcd_home();
	lcd_string(PSTR("glo.cfg error"));
	lcd_setcursor(0,2);
	lcd_string("Using defaults.");
	HAL_Delay(1000);
}
//----------------------------------------------------
FRESULT preset_loadLastUsedProjectNrFromSD(uint8_t* projNr)
{
    FRESULT res;

    *projNr = 0;
    unsigned int bytesRead;

    res = f_open((FIL*)&SDFile,"/proj.idx",FA_OPEN_EXISTING | FA_READ);
    if(res != FR_OK)
    {
    	//close the file handle
    	f_close((FIL*)&SDFile);
    	return res;
    }

    res=f_read((FIL*)&SDFile, projNr,1, &bytesRead);
	/* The number indexes savefileNames[NUM_KIT_SLOTS], which menu.c:1988
	   writes through, by way of menu_selectedPresetNr[SAVE_TYPE_PROJECT]
	   (main.c:854), so a damaged proj.idx must not get that far. */
	if(*projNr >= NUM_KIT_SLOTS)
		*projNr = 0;
	//close the file handle
	f_close((FIL*)&SDFile);


	return res;
}
//----------------------------------------------------
FRESULT preset_saveLastUsedProjectNrToSD(uint8_t projNr)
{
#if USE_SD_CARD
    FRESULT res;

	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, "/proj.idx");
	if(res != FR_OK)
	{
		//close the file handle
		f_close((FIL*)&SDFile);
		return res;
	}

	res = preset_fwrite((FIL*)&SDFile, &projNr,1);
	if(res != FR_OK)
	{
		//close the file handle
		f_close((FIL*)&SDFile);
		return res;
	}

	//close the file
	f_close((FIL*)&SDFile);

	//f_unmount((TCHAR const*)SDPath);
	return res;
#else
#endif
}
//----------------------------------------------------
// read the data into either the main parameters or the morph parameters.
// fix some values, and set values to 0 that were not read in
static FRESULT preset_readDrumsetData(uint8_t presetNr, uint8_t isMorph)
{
	
#if USE_SD_CARD		
	//read the file content
	unsigned int bytesRead=1;
	int16_t i;
	uint8_t *para;
	bytesRead = 0;
	
	// TODO check: using this function for morph may be wrong
	if(isMorph)
		para=morphParameters;
	else
		para=mainParameters[presetNr];

	FRESULT res=f_read((FIL*)&SDFile, para,END_OF_SOUND_PARAMETERS, &bytesRead);
	if(res==FR_OK) {
		// set to 0 for any that were not read from the file
		if(END_OF_SOUND_PARAMETERS-bytesRead)
			memset(para+bytesRead,0,END_OF_SOUND_PARAMETERS-bytesRead);
		res = preset_readEngineTag(para);
		
		//special case mod targets. normalize.
		const uint8_t nmt=getNumModTargets();
		for(i=0;i<6;i++) {
			// since I've changed the meaning of these, I'll ensure that it's valid for kits saved prior to the
			// change
			if(para[PAR_VEL_DEST_1+i] >= nmt )
				para[PAR_VEL_DEST_1+i] = 0;
			if(para[PAR_TARGET_LFO1+i] >= nmt )
				para[PAR_TARGET_LFO1+i] = 0;
		}
	}

	return res;

#endif
}

//----------------------------------------------------
FRESULT preset_loadDrumsetSD(uint8_t presetNr, uint8_t isMorph)
{
	//activePresetNr = presetNr;
#if USE_SD_CARD
	//filename in 8.3  format
	char filename[13];
	unsigned int bytesRead;

#if !CALC_SAMPLES_IN_INTERRUPT
	while(!soundengine_calcNextSampleBlock())
	{
		//wait till new block is calculated
	}
#endif

	//sprintf(filename,"p%03d.snd",presetNr);
	/*
	preset_makeFileNameLoad(filename,presetNr,FEXT_SOUND);

	//open the file
	FRESULT res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
	*/
	FRESULT res = preset_makeFileNameLoad(filename,presetNr,FEXT_SOUND);
	if(res!=FR_OK)
	{
		//try old filename
		preset_makeFileNameOld(filename,presetNr,FEXT_SOUND);
		res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
		if(res!=FR_OK)
			goto error; //file open error... maybe the file does not exist?
	}

#if !CALC_SAMPLES_IN_INTERRUPT
	//to prevent audio dropouts
	soundengine_calcNextSampleBlock();
#endif
	//first the preset name
	res=f_read((FIL*)&SDFile,(void*)savefileNames[presetNr][KIT_NAMES],8,&bytesRead);
	if(bytesRead != 8) {
		res=FR_DISK_ERR;
		goto closeFile;
	}
	if(preset_isNameEmpty(savefileNames[presetNr][KIT_NAMES]))
	{
		sprintf((void*)savefileNames[presetNr][KIT_NAMES], "Empty");
	}



#if !CALC_SAMPLES_IN_INTERRUPT
	//to prevent audio dropouts
	soundengine_calcNextSampleBlock();
#endif
	//then the data
	res=preset_readDrumsetData(presetNr, isMorph);
#if !CALC_SAMPLES_IN_INTERRUPT
	//to prevent audio dropouts
	soundengine_calcNextSampleBlock();
#endif
closeFile:
	//close the file handle
	f_close((FIL*)&SDFile);

	// update the soundengine with the new parameters
	if(res==FR_OK) {
		preset_sendDrumsetParameters();
		return FR_OK;
	}
error:
	return res;
#else
	frontPanel_sendData(PRESET,PRESET_LOAD,presetNr);
#endif
}
//----------------------------------------------------
//val is interpolation value between 0 and 255
//uses bankers rounding
static uint8_t interpolate(uint8_t a, uint8_t b, uint8_t x)
{
	uint16_t fixedPointValue = (uint16_t)(((a*256) + (b-a)*x));
	uint8_t result = (uint8_t)(fixedPointValue/256);

	return (uint8_t)((fixedPointValue&0xff) < 0x7f ? result : result+1);
	//return ((a*255) + (b-a)*x)/255;
}

//----------------------------------------------------
// These slots store MIDI transport state, not sound parameters.
static uint8_t preset_isTransportParameter(uint8_t paramNr)
{
	return paramNr == PAR_NRPN_DATA_ENTRY_COARSE ||
		paramNr == PAR_NRPN_FINE || paramNr == PAR_NRPN_COARSE ||
		paramNr == PAR_BANK_CHANGE;
}

//----------------------------------------------------
IN_ITCM_RAM void preset_sendDrumsetParametersImmediate()
{
		//special case mod targets
		for(uint8_t i=0;i<6;i++)
		{
			//**VELO load drumkit. translate to param value before sending
			// parameter_values[PAR_VEL_DEST_1+i] is an index into modTargets, we need to send
			// a parameter number
			uint8_t value = (uint8_t)pgm_read_word(&modTargets[activeParameters[PAR_VEL_DEST_1+i]].param);
			uint8_t upper,lower;
			upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
			lower = value&0x7f;
			frontPanel_sendData(CC_VELO_TARGET,upper,lower);

			// ensure target voice # is valid
			if(activeParameters[PAR_VOICE_LFO1+i] < 1 || activeParameters[PAR_VOICE_LFO1+i] > 6 )
				activeParameters[PAR_VOICE_LFO1+i]=1;

			// **LFO par_target_lfo will be an index into modTargets, but we need a parameter number to send
			value = (uint8_t)pgm_read_word(&modTargets[activeParameters[PAR_TARGET_LFO1+i]].param);

			upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
			lower = value&0x7f;
			frontPanel_sendData(CC_LFO_TARGET,upper,lower);
		}

		static MidiMsg msg = {0};
		//pre-send fx type to prevent clicks
		/*
		msg.status = CC_2;
		msg.data1 = PAR_FX_TYPE-128; //stupid offset only in CC not CC2
		msg.data2 = activeParameters[PAR_FX_TYPE];

		//	msg.data1 += 1;
		msg.data1 &= 0x7f;
		midiParser_ccHandler(msg,1, 0, 0.f);
		*/

		//try to prevent click by sending BD params first

		//stuff from the morph function
		for(uint32_t i=0;i<END_OF_SOUND_PARAMETERS; i++)
		{

			uint8_t paramNumber = parameterOrder[i];
			if(preset_isTransportParameter(paramNumber))
			{
				soundengine_process();
				continue;
			}

			//const uint8_t morph = activeParameters[PAR_MORPH];

			const uint8_t val = activeParameters[paramNumber];//interpolate(activeParameters[i],morphParameters[i],morph);

			if(paramNumber < 128)
			{
				/* comes from cortex midi handler
				 * stupid offset!
				 * TODO fix this stupid offset			 *
				 * 		frontParser_midiMsg.data1 += 1;

						//because hh slope on front is 127 and on cortex is 0 wrap data1 at 127
						frontParser_midiMsg.data1 &= 0x7f;
				 *
				 */
				msg.status = MIDI_CC;
				msg.data1 = paramNumber;
				msg.data2 = val;

				msg.data1 += 1;
				msg.data1 &= 0x7f;
				midiParser_ccHandler(msg,1, 0, 0.f);


			}
			else
			{
				msg.status = CC_2;
				msg.data1 = paramNumber-128; //stupid offset only in CC not CC2
				msg.data2 = val;

			//	msg.data1 += 1;
				msg.data1 &= 0x7f;
				midiParser_ccHandler(msg,1, 0, 0.f);

			}
			soundengine_process();
		}
}
//----------------------------------------------------
// send loaded parameters to back
void preset_sendDrumsetParameters()
{
	uint8_t i;
	//special case mod targets
	for(i=0;i<6;i++)
	{
		//**VELO load drumkit. translate to param value before sending
		// parameter_values[PAR_VEL_DEST_1+i] is an index into modTargets, we need to send
		// a parameter number
		uint8_t value = (uint8_t)pgm_read_word(&modTargets[activeParameters[PAR_VEL_DEST_1+i]].param);
		uint8_t upper,lower;
		upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
		lower = value&0x7f;
		frontPanel_sendData(CC_VELO_TARGET,upper,lower);

		// ensure target voice # is valid
		if(activeParameters[PAR_VOICE_LFO1+i] < 1 || activeParameters[PAR_VOICE_LFO1+i] > 6 )
			activeParameters[PAR_VOICE_LFO1+i]=1;

		// **LFO par_target_lfo will be an index into modTargets, but we need a parameter number to send
		value = (uint8_t)pgm_read_word(&modTargets[activeParameters[PAR_TARGET_LFO1+i]].param);

		upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
		lower = value&0x7f;
		frontPanel_sendData(CC_LFO_TARGET,upper,lower);
	}

	// todo will this morph (and fuck up) our modulation targets?
	// send parameters (possibly combined with morph parameters) to back
	preset_morph(activeParameters[PAR_MORPH]);



}
//----------------------------------------------------
/*
char* preset_loadName(uint8_t presetNr, uint8_t nameBufferIdx, uint8_t what)
{
	uint8_t type;

	switch(what) {
	case SAVE_TYPE_PATTERN:
		type=FEXT_PAT;
		break;
	case SAVE_TYPE_KIT:
	case SAVE_TYPE_MORPH:
		type=FEXT_SOUND;
		break;
	case SAVE_TYPE_ALL:
		type=FEXT_ALL;
		break;
	case SAVE_TYPE_PERFORMANCE:
		type=FEXT_PERF;
		break;
	default:
		return NULL; // for glo and sample we don't load a name
	}
	

#if USE_SD_CARD		
	//filename in 8.3  format
	char filename[9];
	preset_makeFileName(filename,presetNr,type);

	//try to open the file
	FRESULT res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
	


	if(res!=FR_OK)
	{
		//error opening the file
		memcpy((void*)savefileNames[nameBufferIdx][presetNr],PSTR("Empty   "),8);
		return NULL;	
	}
	
	//file opened correctly -> extract name (first 8 bytes)
	unsigned int bytesRead;
	f_read((FIL*)&SDFile,(void*)savefileNames[nameBufferIdx][presetNr],8,&bytesRead);
	


	//close the file handle
	f_close((FIL*)&SDFile);
	
	return (char*)savefileNames[nameBufferIdx][presetNr];
#else

return "ToDo";
#endif
}
*/

//----------------------------------------------------
/** request step data from the cortex via uart and save it in the provided step struct*/
/*
void preset_queryStepDataFromSeq(uint16_t stepNr)
{
	frontParser_newSeqDataAvailable = 0;

	//request step data
	//the max number for 14 bit data is 16383!!!
	//current max step nr is 128*7*8 = 7168
	frontPanel_sendByte((stepNr>>7)&0x7f);		//upper nibble 7 bit
	frontPanel_sendByte(stepNr&0x7f);			//lower nibble 7 bit

	//wait until data arrives
	uint8_t newSeqDataLocal = 0;
	uint16_t now = systick_ticks;
	while((newSeqDataLocal==0))
	{
		//we have to call the uart parser to handle incoming messages from the sequencer
		uart_checkAndParse();
		
		newSeqDataLocal = frontParser_newSeqDataAvailable;
		
		if(systick_ticks-now >= 31)
		{
			//timeout
			now = systick_ticks;
			//request step again
			frontPanel_sendByte((stepNr>>7)&0x7f);		//upper nibble 7 bit
			frontPanel_sendByte(stepNr&0x7f);
		}
	}
}
*/
//----------------------------------------------------
/*
static void preset_sendMainStepDataToSeq( uint16_t mainStepData)
{
	frontPanel_sendByte(mainStepData	& 0x7f);
	frontPanel_sendByte((mainStepData>>7)	& 0x7f);
	frontPanel_sendByte((uint8_t)((mainStepData>>14)	& 0x7f));
}
*/
//----------------------------------------------------
/** send step data from SD card to the sequencer*/
/*
static void preset_sendStepDataToSeq()
{
	frontPanel_sendByte(frontParser_stepData.volume	& 0x7f);
	frontPanel_sendByte(frontParser_stepData.prob	& 0x7f);
	frontPanel_sendByte(frontParser_stepData.note	& 0x7f);

	frontPanel_sendByte(frontParser_stepData.param1Nr	& 0x7f);
	frontPanel_sendByte(frontParser_stepData.param1Val	& 0x7f);

	frontPanel_sendByte(frontParser_stepData.param2Nr	& 0x7f);
	frontPanel_sendByte(frontParser_stepData.param2Val	& 0x7f);

	//now the MSBs from all 7 values
	frontPanel_sendByte((uint8_t) 	(((frontParser_stepData.volume 	& 0x80)>>7) |
							((frontParser_stepData.prob	 	& 0x80)>>6) |
							((frontParser_stepData.note	 	& 0x80)>>5) |
							((frontParser_stepData.param1Nr	& 0x80)>>4) |
							((frontParser_stepData.param1Val	& 0x80)>>3) |
							((frontParser_stepData.param2Nr	& 0x80)>>2) |
							((frontParser_stepData.param2Val	& 0x80)>>1))
							);
}	
*/
//----------------------------------------------------
void preset_queryPatternInfoFromSeq(uint8_t patternNr, uint8_t* next, uint8_t* repeat)
{
	frontParser_newSeqDataAvailable = 0;
	//request pattern info
	frontPanel_sendByte(patternNr);	

	//wait until data arrives
	uint8_t newSeqDataLocal = 0;
	uint16_t now = systick_ticks;
	while((newSeqDataLocal==0))
	{
		//we have to call the uart parser to handle incoming messages from the sequencer
		uart_checkAndParse();
		
		newSeqDataLocal = frontParser_newSeqDataAvailable;
		
		if(systick_ticks-now >= 31)
		{
			//timeout
			now = systick_ticks;
			//request step again
			frontPanel_sendByte(patternNr);	
		}
	}
	
	//the stepdata struct is used as buffer for the data
	*next = frontParser_stepData.volume; 
	*repeat =  frontParser_stepData.prob;
}
//----------------------------------------------------
void preset_queryMainStepDataFromSeq(uint16_t stepNr, uint16_t *mainStepData, uint8_t *length)
{
	frontParser_newSeqDataAvailable = 0;

	//request step data
	//the max number for 14 bit data is 16383!!!
	//current max step nr is 7*8 = 56
	frontPanel_sendByte((stepNr>>7)&0x7f);		//upper nibble 7 bit
	frontPanel_sendByte(stepNr&0x7f);	//lower nibble 7 bit

	//wait until data arrives
	uint8_t newSeqDataLocal = 0;
	uint16_t now = systick_ticks;
	while((newSeqDataLocal==0))
	{
		//we have to call the uart parser to handle incoming messages from the sequencer
		uart_checkAndParse();
		
		newSeqDataLocal = frontParser_newSeqDataAvailable;
		
		if(systick_ticks-now >= 31)
		{
			//timeout
			now = systick_ticks;
			//request step again
			frontPanel_sendByte((stepNr>>7)&0x7f);		//upper nibble 7 bit
			frontPanel_sendByte(stepNr&0x7f);
		}
	}
	
	// we are reusing these members for purposes other than those that were originally intended
	*mainStepData =(uint16_t) ((frontParser_stepData.volume<<8) | frontParser_stepData.prob);
	*length=frontParser_stepData.note;
};

//----------------------------------------------------
static FRESULT preset_writePatternData(uint8_t patternNr)
{
#if 0
#else
		FRESULT res;
		uint8_t length;

		//---------------  write the step data (note, vol, prob, autom.) ---------------
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
			res = preset_fwrite((FIL*)&SDFile,(const void*)&seq_patternSet.seq_subStepPattern[patternNr][trk][0],sizeof(Step)*STEPS_PER_PATTERN);
			if(res != FR_OK) return res;

			res = preset_fwrite((FIL*)&SDFile,(const void*)&seq_patternSet.seq_stepTiming[patternNr][trk][0],sizeof(uint8_t)*STEPS_PER_PATTERN);
			if(res != FR_OK) return res;
		}

		//--------------- now the main step data ---------------
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
			for(uint8_t bar=0;bar<NUM_BARS; bar++)
			{
				const uint16_t mainStepData = seq_patternSet.seq_mainSteps[bar][patternNr][trk];
				res = preset_fwrite((FIL*)&SDFile,(const void*)&mainStepData,sizeof(uint16_t));
				if(res != FR_OK) return res;
			}
		}

		//----- shuffle setting ------


		//f_write((FIL*)&SDFile,(const void*)&activeParameters[PAR_SHUFFLE],sizeof(uint8_t),&bytesWritten);
		res = preset_fwrite((FIL*)&SDFile,(const void*)&seq_patternSet.shuffle[patternNr],sizeof(uint8_t));
		if(res != FR_OK) return res;

		//----- pattern/track lengths ------
		// we reuse the same call from above (when saving main step data)
		// but we only use the length info retrieved. We want to store it at the end
		// to avoid breaking compatibility with save file


		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
				length = seq_patternSet.length[patternNr][trk] ;//seq_patternSet.seq_mainSteps[bar][pat][trk];
				res = preset_fwrite((FIL*)&SDFile,(const void*)&length,sizeof(uint8_t));
				if(res != FR_OK) return res;
		}

		//write associated kit nr
		res = preset_fwrite((FIL*)&SDFile,&seq_patternSet.kitNr[patternNr],sizeof(uint8_t));
		if(res != FR_OK) return res;

		return FR_OK;
#endif
}

//----------------------------------------------------
/*
void preset_savePatternTemp(uint8_t presetNr)
{

	UINT bytesWritten;
	//lcd_clear();
	//lcd_home();
	//lcd_string(PSTR("Saving pattern"));


	//filename in 8.3  format
	char filename[9];
	//sprintf(filename,"p%03d.pat",presetNr);
	preset_makeFileNameTemp(filename,presetNr,FEXT_PAT);
	//open the file
	preset_fopenWrite((FIL*)&SDFile, filename);

	//write preset name to file
	preset_fwrite((FIL*)&SDFile, (void*)preset_currentName,8);

	preset_writePatternData();

	//close the file
	f_close((FIL*)&SDFile);

	//reset the lcd
	menu_repaintAll();


}
*/
//----------------------------------------------------
FRESULT preset_savePatternSD(uint8_t presetNr)
{

	FRESULT res;
	//lcd_clear();
	//lcd_home();
	//lcd_string(PSTR("Saving pattern"));
	
	
	//filename in 8.3  format
	char filename[13];
	//sprintf(filename,"p%03d.pat",presetNr);
	preset_makeFileNameSave(filename,presetNr,FEXT_PAT, savefileNames[presetNr][PATTERN_NAMES]);
	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, filename);
	if(res != FR_OK) return res;

	//write preset name to file
	res = preset_fwrite((FIL*)&SDFile,(void*)savefileNames[presetNr][PATTERN_NAMES],8);
	//write version tag
	if(res == FR_OK) res = preset_fwrite((FIL*)&SDFile,"PatV04",6);

	//write pattern data
	if(res == FR_OK) res = preset_writePatternData(presetNr);
	if(res == FR_OK) res = preset_fwrite((FIL*)&SDFile, seq_patternSet.seq_conditions[presetNr],
			SEQ_CONDITION_BYTES);

	//close the file
	res = preset_fcloseWrite((FIL*)&SDFile);
	if(res != FR_OK) return res;
	preset_removeStaleSlotFiles(presetNr, FEXT_PAT, filename);

	//reset the lcd
	menu_repaintAll();
	
	return FR_OK;
}

//----------------------------------------------------
// returns 1 on success
static uint8_t preset_readPatternData(uint8_t patternNr, uint8_t dataVersion)
{
	/* the conditions no longer need clearing here: the staging buffer is
	   cleared before every read and the commit always copies them */

#if 0


	// note that the pattern length data is no longer stored in the same way.
	// This means that loading old patterns with non-standard length, the length will be
	// ignored and a standard length used. I think it's acceptable as long as users are aware of this.

	UINT bytesRead;
	uint8_t success=1; // start off succeeding
	//uint16_t i;
	//uint8_t repeat=0;
	uint8_t next=0;

	//lcd_clear();
	//lcd_home();
	//lcd_string(PSTR("Loading pattern"));

	// ---------- Load step data first



	// load step data


	for(uint8_t pat=0; pat<NUM_PATTERN;pat++)
	{
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{

#if 1

			if( (pat == seq_activePattern) && seq_isRunning() )
			{
				f_read((FIL*)&SDFile,(void*)&seq_tmpPattern.seq_subStepPattern[trk][0],sizeof(Step)*STEPS_PER_PATTERN,&bytesRead);
			} else {
				f_read((FIL*)&SDFile,(void*)&seq_patternSet.seq_subStepPattern[pat][trk][0],sizeof(Step)*STEPS_PER_PATTERN,&bytesRead);
			}



			if(bytesRead!=sizeof(Step)*(STEPS_PER_PATTERN))
			{
				success=0;
				break; // file error, or we ran out of file (eof)
			}
#else
			for(uint8_t stp=0;stp<STEPS_PER_PATTERN; stp++)
			{
				f_read((FIL*)&SDFile,(void*)&frontParser_stepData,sizeof(Step),&bytesRead);
				if(bytesRead==sizeof(Step))
				{
					seq_patternSet.seq_subStepPattern[pat][trk][stp] = frontParser_stepData;
				} else {
					success=0;
					break; // file error, or we ran out of file (eof)
				}
			}
#endif
		}
	}

	// ---------- load the main step data next
	uint16_t mainStepData;

	for(uint8_t pat=0; pat<NUM_PATTERN;pat++)
	{
		for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
		{
			for(uint8_t bar=0;bar<NUM_BARS; bar++)
			{
				f_read((FIL*)&SDFile,(void*)&mainStepData,sizeof(uint16_t),&bytesRead);

				if( bytesRead==sizeof(uint16_t)) {

					if( (pat == seq_activePattern) && seq_isRunning() )
					{
						seq_tmpPattern.seq_mainSteps[bar][trk] = mainStepData;
					} else {
						seq_patternSet.seq_mainSteps[bar][pat][trk] = mainStepData;
					}
				}
				 else {
						success=0;
						break;
					}
			}
		}
	}

	//---------- pattern info (next/repeat)
	/*
	if(success) {
		for(i=0;i<(NUM_PATTERN);i++)
		{
			f_read((FIL*)&preset_File,(void*)&next,sizeof(uint8_t),&bytesRead);
			if(!bytesRead) {
				success=0;
				break;
			}
			f_read((FIL*)&preset_File,(void*)&repeat,sizeof(uint8_t),&bytesRead);
			if(!bytesRead) {
				success=0;
				break;
			}

			if( (i == seq_activePattern) && seq_isRunning() )
			{
				seq_tmpPattern.seq_patternSettings.nextPattern = next;
				seq_tmpPattern.seq_patternSettings.changeBar = repeat;
			} else {
				seq_patternSet.seq_patternSettings[i].nextPattern = next;
				seq_patternSet.seq_patternSettings[i].changeBar = repeat;
			}

		}
	}
	*/

	// ------------ shuffle settings
	if(success) {
		//load the shuffle settings
		f_read((FIL*)&SDFile,(void*)&activeParameters[PAR_SHUFFLE],sizeof(uint8_t),&bytesRead);
		//and update on cortex
		if(bytesRead)
			//frontPanel_sendData(SEQ_CC,SEQ_SHUFFLE,parameter_values[PAR_SHUFFLE]);
			seq_setShuffle(activeParameters[PAR_SHUFFLE]/127.f);
		else {
			success=0;
		}
	}

	// ----------- Pattern/track lengths
	// -- AS this might not exist in the saved data file, we still want success
	if(success) {


		for(uint8_t pat=0; pat<NUM_PATTERN;pat++)
		{
			for(uint8_t trk=0; trk<NUM_TRACKS;trk++)
			{
				f_read((FIL*)&SDFile,(void*)&next,sizeof(uint8_t),&bytesRead);


				if( (pat == seq_activePattern) && seq_isRunning() )
				{
					seq_tmpPattern.length[trk] = next;
				} else {
					seq_patternSet.length[pat][trk] = next;
				}
			}
		}

		// signal new pattern after receiving all the data
		/*
		if( seq_isRunning() ) {
			seq_newPatternAvailable = 1;
		}
		*/

		success=1; // just to document that we don't consider this a failure, we just didn't read the bytes for this
	}

	return success;
#else

	// note that the pattern length data is no longer stored in the same way.
	// This means that loading old patterns with non-standard length, the length will be
	// ignored and a standard length used. I think it's acceptable as long as users are aware of this.

	/* V3 added the timing block, V4 the trig conditions. Staged and then
	   committed, so a truncated file leaves the pattern as it was, and a file
	   from older firmware gets zeroed timing instead of the previous
	   pattern's flams and shifts. */
	const uint8_t success = preset_stagePatternBody(dataVersion > 2,
			dataVersion >= 4);
	if(success)
		preset_commitPatternBody(patternNr);

	return success;
#endif

}
//----------------------------------------------------
/*
uint8_t preset_loadPatternTemp(uint8_t presetNr)
{
#if USE_SD_CARD
	//filename in 8.3  format
	char filename[9];
	UINT bytesRead;
	uint8_t success=0;
	//sprintf(filename,"p%03d.pat",presetNr);
	preset_makeFileNameTemp(filename,presetNr,FEXT_PAT);

	//open the file
	FRESULT res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);

	if(res!=FR_OK)
	{

		return 0; //file open error... maybe the file does not exist?
	}


	//read the preset name
	f_read((FIL*)&SDFile,(void*)preset_currentName,8,&bytesRead);
	if(bytesRead != 8)
		goto closeFile;

	if(!preset_readPatternData())
		goto closeFile;

	success=1;
	//close the file handle
closeFile:
	f_close((FIL*)&SDFile);

	//force complete repaint
	menu_repaintAll();
	// return success or failure
	return success;

#else
frontPanel_sendData(PRESET,PATTERN_LOAD,presetNr);

#endif
}
*/
//----------------------------------------------------
FRESULT preset_loadPatternSD(uint8_t presetNr)
{
#if USE_SD_CARD		
	//filename in 8.3  format
	uint8_t fileVersion = 1;
	char filename[13];
	UINT bytesRead;
	uint8_t success=0;
	//sprintf(filename,"p%03d.pat",presetNr);

	/*
	preset_makeFileNameLoad(filename,presetNr,FEXT_PAT);

	//open the file
	FRESULT res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
	*/
	FRESULT res = preset_makeFileNameLoad(filename,presetNr,FEXT_PAT);
	if(res!=FR_OK)
	{
		//try old filename
		preset_makeFileNameOld(filename,presetNr,FEXT_PAT);
		res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
		if(res!=FR_OK)
			return res; //file open error... maybe the file does not exist?
	}
	
	//read the preset name
	res = f_read((FIL*)&SDFile,(void*)savefileNames[presetNr][PATTERN_NAMES],8,&bytesRead);
	if(bytesRead != 8)
		goto closeFile;
	
	char buf[7];
	res = f_read((FIL*)&SDFile,buf,6,&bytesRead);
	//zero termination
	buf[6]=0;
	fileVersion = atoi(&buf[4]);
	buf[4] = 0; // PatV\0

	if(strcmp(buf, "PatV") == 0)
	{
		//we have a new version pattern preset >V0 (V0 has no version tag)


	}
	else
	{
		//V1 preset data without version tag -> rewind read pointer
		fileVersion = 1; //re-assign since atoi() broke the 1
		f_lseek((FIL*)&SDFile, f_tell((FIL*)&SDFile) - 6);
	}
	//f_write((FIL*)&SDFile,"PatV02",6,&bytesWritten);

	if(!preset_readPatternData(presetNr, fileVersion))
	{
		goto closeFile;
	}
	
	success=1;
	//close the file handle



closeFile:
	f_close((FIL*)&SDFile);
	
	//force complete repaint
	menu_repaintAll();
	// return success or failure
	if(success) return FR_OK;
	else return FR_INT_ERR;
	
#else
frontPanel_sendData(PRESET,PATTERN_LOAD,presetNr);

#endif
}

//----------------------------------------------------
FRESULT preset_savePatternRAM(uint8_t presetNr)
{

	//check if current slot or new slot
	if(presetNr == seq_activePattern)
	{
		//### Current Slot ###
		//write current (changed) pattern to backup buffer, to make changes permanent
		seq_copyPatternToBackupBuffer(presetNr);
	}
	else
	{
		//### NEW SLOT ###
		//- write current (changed) pattern to new slot
		seq_copyPattern(seq_activePattern, presetNr);
		//- change active pattern to new slot (writes backup buffer back to current slot, reverting changes being made in current slot)
		seq_setNextPattern(presetNr);
	}

	return FR_OK;
}
//----------------------------------------------------
FRESULT preset_loadPatternRAM(uint8_t presetNr)
{

	//I'm unused since there is no Pattern Load page, it just uses the pattern change/select page instead
	return FR_OK;
}
//----------------------------------------------------
//called periodically to process non blocking morph
void preset_morph_tick()
{
	//morph_state = 0;
	//return;
	if(morph_state)
	{

		// Consume the reserved slot's tick without executing a MIDI command.
		// All transport slots precede the final sound parameter.
		if(preset_isTransportParameter(morph_index))
		{
			morph_index++;
			return;
		}
		
		uint8_t morph;

		//Do not morph audio routing
		if((morph_index >= PAR_AUDIO_OUT1) && (morph_index <= PAR_AUDIO_OUT6)) //morph_index++;
		{
			//always use source kit (morph = 0) for audio routing
			morph = 0;
		}
		else
		{
			morph = activeParameters[PAR_MORPH];
		}

		static MidiMsg msg = {0};


		const uint8_t val = preset_getMorphValue(morph_index,morph);


		if(morph_index < 128)
		{
			/* comes from cortex midi handler
			 * stupid offset!
			 * TODO fix this stupid offset			 *
			 * 		frontParser_midiMsg.data1 += 1;

					//because hh slope on front is 127 and on cortex is 0 wrap data1 at 127
					frontParser_midiMsg.data1 &= 0x7f;
			 *
			 */
			msg.status = MIDI_CC;
			msg.data1 = morph_index;
			msg.data2 = val;

			msg.data1 += 1;
			msg.data1 &= 0x7f;
			midiParser_ccHandler(msg,1, 0, 0.f);


		}
		else
		{
			msg.status = CC_2;
			msg.data1 = morph_index-128; //stupid offset only in CC not CC2
			msg.data2 = val;

		//	msg.data1 += 1;
			msg.data1 &= 0x7f;
			midiParser_ccHandler(msg,1, 0, 0.f);

		}


		morph_index++;
		if(morph_index >= END_OF_SOUND_PARAMETERS)
		{
			//finished morphing
			morph_state = 0;
		}
	}
}
//----------------------------------------------------
// This will determine an interpolated value between parameters
// and morph parameters and send the data to the back
// if the morph value is 0 it will just send the parameter values
// to the back
void preset_morph(uint8_t morph)
{
#if 1
	//initialize new morph
	morph_state = 1; //start morph
	morph_index = 0; //reset index
#else
	int i;
	//TODO so far only sound parameters are interpolated, no LFOs etc

	for(i=0;i<END_OF_SOUND_PARAMETERS;i++)
	{


		uint8_t val;
		
		val = interpolate(mainParameters[i],morphParameters[i],morph);

		if(i<128) {
			frontPanel_sendData(MIDI_CC,(uint8_t)i,val);

		} else {
			frontPanel_sendData(CC_2,(uint8_t)(i-128),val);

		}		
		
			
		//to omit front panel button/LED lag we have to process din dout and uart here
		//read next button

		//update LEDs
		dio_process();
		//read uart messages from sequencer
		uart_checkAndParse();
	}		
#endif
}

//----------------------------------------------------
uint8_t preset_getMorphValue(uint16_t index, uint8_t morph)
{
	if(index >= PAR_ENGINE_1 && index <= PAR_ENGINE_3)
		return morph < 128 ? activeParameters[index] : morphParameters[index];
	return interpolate(activeParameters[index],morphParameters[index],morph);
};
//----------------------------------------------------

FRESULT preset_createProject(uint8_t presetNr)
{


	//create default globals

	//clear all patterns

	//clear all kits

	//clear all songs

	//load init kit


	int i;

	preset_loadDefaultGlobals();
	//give active song pointer a proper value
	seq_activeSong = seq_songData[0];

	memset(seq_songData, SONG_END, sizeof(uint8_t) * MAX_SONG_LENGTH * NUM_KIT_SLOTS);
	seq_songData[0][0] = 0;


	//memset(seq_stepIndex,0,NUM_TRACKS);
//	memset(seq_lastMasterStep,0,NUM_TRACKS);


	for(i=0;i<NUM_PATTERN;i++)
	{
		//seq_patternSet.seq_patternSettings[i].changeBar 	= 0;	//default setting: zero repeats (play once then change)
		//seq_patternSet.seq_patternSettings[i].nextPattern 	= i;	//default setting: repeat same pattern
		seq_clearPattern(i); // will clear all tracks in the pattern
		preset_init(i); //set all kits to init sound
	}

	sprintf(savefileNames[presetNr][PROJECT_NAMES], "Init");

	preset_clearTmpFolder();

	return FR_OK;
}
//----------------------------------------------------
// **SAVEALL
// save all settings (global, pattern, kit)
// or save performance settings (pattern, kit)

/* Every failure path of a project load or save ends the same way: give the
   display back, put the /tmp working copy back and, when the audio interrupt was
   turned on for the operation, turn it off again. There were eight textually
   identical copies of this - which is why the globals save had no check at all,
   since adding one meant a ninth. The name says "SD operation" rather than
   "save" because the load paths use the same clean-up. */
/* Remove a folder and everything in it. Missing counts as success: the callers
   use it to clear leftovers that are normally not there. */
/* Set while a project save is writing into /projNNn, so that any of the save's
   many failure exits leaves nothing behind: preset_abortSdOperation() removes
   it. Without this a failed save left a part-written /projNNn on the card,
   holding the space it had used until the next save cleared it. */
static char preset_pendingNewFolder[13] = "";

static FRESULT preset_removeFolder(const char *path)
{
	FILINFO fno;
	if(f_stat(path, &fno) != FR_OK)
		return FR_OK;
	sprintf(emptyDirPath, "%s", path);
	return delete_node(emptyDirPath, sizeof emptyDirPath / sizeof emptyDirPath[0], &fno);
}

/* A project save swaps two folders, and between the two renames /projNN does
   not exist. If the power goes at that moment the data is in /projNNo, so put
   it back the next time anything looks for the project. Costs one f_stat on a
   path that is normally absent. */
static void preset_recoverInterruptedSave(uint8_t presetNr)
{
	FILINFO fno;
	char projFolder[13], oldFolder[13];
	sprintf(projFolder, "/proj%02d", presetNr);
	sprintf(oldFolder, "/proj%02do", presetNr);
	if(f_stat(projFolder, &fno) == FR_NO_FILE && f_stat(oldFolder, &fno) == FR_OK)
		f_rename(oldFolder, projFolder);
}

static FRESULT preset_abortSdOperation(FRESULT res)
{
	if(preset_pendingNewFolder[0])
	{
		f_chdir("/");
		preset_removeFolder(preset_pendingNewFolder);
		preset_pendingNewFolder[0] = 0;
	}
	lcd_process_display_interrupt = 1;
	preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#endif
	return res;
}

FRESULT preset_saveProject(uint8_t presetNr)
{
	FRESULT res;
	FILINFO fno;
	char newFolder[13], oldFolder[13];

	/* The kit being edited, not the slot the encoder was last turned to in a
	   load or save menu. Scrolling that menu without pressing anything used to
	   decide where the edit buffer was written. */
	preset_saveDrumsetRam(menu_loadedPresetNr[CURRENT_KIT], 0);
	preset_savePatternRAM(seq_activePattern);

#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	lcd_process_display_interrupt = 0;
	while(sendDisplayBuffer() != 0);

	lcd_clear();
	lcd_home();
	lcd_string(PSTR("Saving Project"));
	lcd_setcursor(0,2);
	lcd_string(PSTR("0%"));
	/*
	MX_FATFS_ReInit();
	f_mount(0, (TCHAR const*)SDPath, 0); //un-mount
	memset((FATFS*)&SDFatFS,0,sizeof(FATFS)); //clear FS object to force reinit
	res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
	if( res != FR_OK)
	{
		return res;
	}
*/
	//change project folder
	/* The project is written into /projNNn and only becomes /projNN once every
	   file is on the card, so a save that fails - a full card, a card pulled -
	   leaves the project that was there untouched. It used to empty /projNN
	   first and write into it, which destroyed the destination before the
	   replacement existed (#65).

	   Three names are in play:
	     /projNN   the project, as everything else knows it
	     /projNNn  the one being written
	     /projNNo  the previous one, between the two renames at the end
	   All of them are 8.3 names, which FatFs needs here (_USE_LFN 0). */
	sprintf(currentFolder, "/proj%02d", presetNr);
	sprintf(newFolder, "/proj%02dn", presetNr);
	sprintf(oldFolder, "/proj%02do", presetNr);

	/* Leftovers from a save that did not finish. */
	res = preset_removeFolder(newFolder);
	if(res == FR_OK)
		res = preset_removeFolder(oldFolder);

	/* A project that does not exist yet gets its folder now, so the rename at
	   the end always has something to move aside. */
	if(res == FR_OK && f_stat(currentFolder, &fno) == FR_NO_FILE)
		res = f_mkdir(currentFolder);

	if(res == FR_OK)
		res = f_mkdir(newFolder);
	if(res == FR_OK)
		res = f_chdir(newFolder);
	if(res != FR_OK)
		return preset_abortSdOperation(res);
	/* from here on a failure must take /projNNn with it */
	sprintf(preset_pendingNewFolder, "%s", newFolder);
	//save data

#if 1



	char text[17];

	//Save Project name
	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, "prj.nfo");
	if(res != FR_OK) {
		lcd_process_display_interrupt = 1;
		preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

		return res;
	}
	//write preset name to file
	res = preset_fwrite((FIL*)&SDFile, (void*)savefileNames[presetNr][PROJECT_NAMES],8);
	if(res != FR_OK) {
		lcd_process_display_interrupt = 1;
		preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

		return res;
	}
	//close the file
	f_close((FIL*)&SDFile);

//	soundengine_process();
	//save globals
	/* The result used to be thrown away, so a globals write that failed - a full
	   card, most likely - left the project with a glo.cfg that is short or
	   missing and reported success (#69). */
	res = preset_saveGlobals();
	if(res != FR_OK)
		return preset_abortSdOperation(res);
	//save all drumkits, patterns and songs
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{
		sprintf(text, "%d%%", (uint8_t)(i/(float)NUM_KIT_SLOTS * 100.f));
		lcd_setcursor(0,2);
		lcd_string(text);
	//	soundengine_process();
		res = preset_saveDrumsetSD(i, 0);
		if(res != FR_OK) {
			lcd_process_display_interrupt = 1;
			preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

			return res;
		}
	//	soundengine_process();
		res = preset_savePatternSD(i);
		if(res != FR_OK) {
			return preset_abortSdOperation(res);
		}
	//	soundengine_process();
		res = preset_saveSongSD(i);
		if(res != FR_OK) {
			return preset_abortSdOperation(res);
		}
	}

	/* Everything is written. Swap the folders: the project that was there stays
	   until the new one has taken its place, and if the second rename fails the
	   first is undone so /projNN still exists. Between the two renames /projNN
	   is absent - preset_recoverInterruptedSave() puts /projNNo back if the
	   power goes exactly there. */
	res = f_chdir("/");
	if(res == FR_OK)
		res = f_rename(currentFolder, oldFolder);
	if(res != FR_OK)
		return preset_abortSdOperation(res);

	res = f_rename(newFolder, currentFolder);
	if(res != FR_OK)
	{
		f_rename(oldFolder, currentFolder);	//put the old one back
		return preset_abortSdOperation(res);
	}

	preset_pendingNewFolder[0] = 0;	//it is /projNN now, not a leftover

	/* The save has succeeded by now; failing to remove the previous copy costs
	   space and nothing else, so it does not fail the save. */
	preset_removeFolder(oldFolder);

	lcd_process_display_interrupt = 1;
	preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

			preset_clearTmpFolder();

	return FR_OK;

#else
#if USE_SD_CARD
	uint16_t remain;
	uint8_t siz;
	//filename in 8.3  format
	char filename[GEN_BUF_LEN];
	if(isAll)
		preset_makeFileName(filename,presetNr,FEXT_ALL);
	else
		preset_makeFileName(filename,presetNr,FEXT_PROJ);
	//open the file
	preset_fopenWrite((FIL*)&SDFile, filename);


	unsigned int bytesWritten;
	//write preset name to file
	preset_fwrite((FIL*)&SDFile, (void*)savefileNames[KIT_NAMES][presetNr],8);

	//write the version
	filename[0]=FILE_VERSION;
	preset_fwrite((FIL*)&SDFile, filename, 1);

	memset(filename,0xFF,GEN_BUF_LEN); // reuse this as our buffer for padding

	if(isAll) {
		// write global settings
		preset_writeGlobalData();

		// write some padding (we'll allocate 64 bytes for globals, right now we have +/- 18) so that
		// we don't need to change the file format later
		remain=	64- (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);

	} else {
		// write the bpm
		preset_fwrite((FIL*)&SDFile, &activeParameters[PAR_BPM], 1);
		// bar reset mode
		preset_fwrite((FIL*)&SDFile, &activeParameters[PAR_BAR_RESET_MODE], 1);

		remain=	64 - (1+1);
	}

	// write padding after globals/perf data
	while(remain){
		if(remain < GEN_BUF_LEN)
			siz=(uint8_t)remain;
		else
			siz=GEN_BUF_LEN;
		preset_fwrite((FIL*)&SDFile, filename, siz);
		remain -=siz;
	}

	// save kit
	preset_writeDrumsetData(0);

	// write some more padding (we'll allocate 512 bytes for kit data. This should be more than
	// we'll ever need. Right now we use about 228 bytes).
	remain = 512 - (END_OF_SOUND_PARAMETERS);
	while(remain){
		if(remain < GEN_BUF_LEN)
			siz=(uint8_t)remain;
		else
			siz=GEN_BUF_LEN;
		preset_fwrite((FIL*)&SDFile, filename, siz);
		remain -=siz;
	}

	// write pattern data
	preset_writePatternData(0);

	//close the file
	f_close((FIL*)&SDFile);
#else
#endif
#endif
}

//----------------------------------------------------
FRESULT preset_loadSongSD(uint8_t songNr)
{
	FRESULT res;
	char filename[13];
	UINT bytesRead;

	/*
	preset_makeFileNameLoad(filename,songNr,FEXT_SONG);
	//open the file
	res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
	*/
	res = preset_makeFileNameLoad(filename,songNr,FEXT_SONG);

	if(res!=FR_OK)
	{
		//try old filename
		preset_makeFileNameOld(filename,songNr,FEXT_SONG);
		res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);
		if(res!=FR_OK)
			return res; //file open error... maybe the file does not exist?
	}

	//first the song name
	res = f_read((FIL*)&SDFile,(void*)savefileNames[songNr][SONG_NAMES],8,&bytesRead);

	if((!bytesRead) || (res != FR_OK))
		goto closeFile;

	if(preset_isNameEmpty(savefileNames[songNr][SONG_NAMES]))
	{
		sprintf((void*)savefileNames[songNr][SONG_NAMES], "Empty");
	}

	//read song data
	res = f_read((FIL*)&SDFile, seq_songData[songNr], MAX_SONG_LENGTH, &bytesRead);

	closeFile:
		//close the file handle
		f_close((FIL*)&SDFile);
		return res;
}
//----------------------------------------------------
FRESULT preset_saveSongSD(uint8_t songNr)
{
	FRESULT res;
	char filename[13];
	preset_makeFileNameSave(filename,songNr,FEXT_SONG, savefileNames[songNr][SONG_NAMES]);
	//open the file
	res = preset_fopenWrite((FIL*)&SDFile, filename);
	if(res!=FR_OK)
		return res; //file open error... maybe the file does not exist?
	//write name
	res = preset_fwrite((FIL*)&SDFile, (void*)savefileNames[songNr][SONG_NAMES],8);
	if (res != FR_OK)
	{
		f_close((FIL*)&SDFile);
		return res;
	}

	//write song data
	res = preset_fwrite((FIL*)&SDFile, seq_songData[songNr],MAX_SONG_LENGTH);
	if (res != FR_OK)
	{
		f_close((FIL*)&SDFile);
		return res;
	}

	//close the file handle
	res = f_close((FIL*)&SDFile);
	if(res == FR_OK)
		preset_removeStaleSlotFiles(songNr, FEXT_SONG, filename);
	return res;
}


//----------------------------------------------------
void preset_loadProjectNames(uint8_t projectNr)
{
	FRESULT res;
	FILINFO fno;

	//change project folder
	/* If a save was interrupted between its two renames, /projNN is missing
	   and the data is in /projNNo. Put it back before looking for it. */
	preset_recoverInterruptedSave(projectNr);
	sprintf(currentFolder, "/proj%02d", projectNr);

	res = f_stat(currentFolder, &fno);
	if(res == FR_NO_FILE)
	{
		sprintf((void*)savefileNames[projectNr][PROJECT_NAMES], "Empty");
		return;
	}
	else
	{
		f_chdir(currentFolder);
	}

	//Load Project name

		UINT bytesRead;

		//open the file
		res = f_open((FIL*)&SDFile,"prj.nfo",FA_OPEN_EXISTING | FA_READ);

		//read preset name
		//first the preset name
		res=f_read((FIL*)&SDFile,(void*)savefileNames[projectNr][PROJECT_NAMES],8,&bytesRead);
		if(bytesRead != 8) {
			res=FR_DISK_ERR;
		}
		if(preset_isNameEmpty(savefileNames[projectNr][PROJECT_NAMES]))
		{
			sprintf((void*)savefileNames[projectNr][PROJECT_NAMES], "Empty");
		}

		f_close((FIL*)&SDFile);
		preset_initTmpFolder();

}
//----------------------------------------------------
FRESULT preset_loadProject(uint8_t presetNr, uint8_t clearTmp)
{
	FRESULT res;
	FILINFO fno;
	char text[17];

	lcd_process_display_interrupt = 0;
	while(sendDisplayBuffer() != 0);

#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
	HAL_TIM_Base_Start_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	if(clearTmp)
	{
		preset_clearTmpFolder();
	}
	/*
	MX_FATFS_ReInit();
	f_mount(0, (TCHAR const*)SDPath, 0); //un-mount
	memset((FATFS*)&SDFatFS,0,sizeof(FATFS)); //clear FS object to force reinit

	res = f_mount((FATFS*)&SDFatFS, (TCHAR const*)SDPath,1);
	if( res != FR_OK)
	{
		return res;
	}
*/

	lcd_clear();
	lcd_home();
	lcd_string(PSTR("Loading Project"));
	lcd_setcursor(0,2);
	lcd_string(PSTR("0%"));

	//change project folder
	/* Same recovery as above, for this load path. */
	preset_recoverInterruptedSave(presetNr);
	sprintf(currentFolder, "/proj%02d", presetNr);

	res = f_stat(currentFolder, &fno);
	if(res == FR_NO_FILE)
	{
		res = f_mkdir(currentFolder);
		if(res == FR_OK)
		{
			f_chdir(currentFolder);
		}
		else
		{
			return preset_abortSdOperation(res);
		}
	}
	else
	{
		res = f_chdir(currentFolder);
		if (res != FR_OK)
		{
			return preset_abortSdOperation(res);
		}
	}
	//load data

#if 1

	//Load Project name

	UINT bytesRead;

	//open the file
	res = f_open((FIL*)&SDFile,"prj.nfo",FA_OPEN_EXISTING | FA_READ);
	if (res != FR_OK)
	{
		lcd_process_display_interrupt = 1;
		preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		return res;
	}
	//read preset name
	//first the preset name
	res = f_read((FIL*)&SDFile,(void*)savefileNames[presetNr][PROJECT_NAMES],8,&bytesRead);
	if (res != FR_OK)
	{
		lcd_process_display_interrupt = 1;
		preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		f_close((FIL*)&SDFile);
		return res;
	}

	if(bytesRead != 8) {
		sprintf((void*)savefileNames[presetNr][PROJECT_NAMES], "Empty");
		/*
		//res=FR_DISK_ERR;
		lcd_process_display_interrupt = 1;
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
		f_close((FIL*)&SDFile);
		return res;
		*/
	}

	else if(preset_isNameEmpty(savefileNames[presetNr][PROJECT_NAMES]))
	{
		sprintf((void*)savefileNames[presetNr][PROJECT_NAMES], "Empty");
	}
	//close the file
	f_close((FIL*)&SDFile);

	//load globals
	if(!preset_loadGlobals())
		preset_useDefaultGlobals();
	//load all drumkits, patterns and songs
	for(uint8_t i=0;i<NUM_KIT_SLOTS;i++)
	{

		sprintf(text, "%d%%", (uint8_t)(i/(float)NUM_KIT_SLOTS * 100.f));
		lcd_setcursor(0,2);
		lcd_string(text);

		//FR_NO_FILE just means an incomplete project and is no reason to abort!
		res = preset_loadDrumsetSD(i, 0);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			lcd_process_display_interrupt = 1;
			preset_initTmpFolder();
			f_close((FIL*)&SDFile);
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif
			return res;
		}
		res = preset_loadPatternSD(i);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			f_close((FIL*)&SDFile);
			return preset_abortSdOperation(res);
		}
		res = preset_loadSongSD(i);
		if ((res != FR_OK) && (res != FR_NO_FILE))
		{
			f_close((FIL*)&SDFile);
			return preset_abortSdOperation(res);
		}
	}

	res = FR_OK;

	//init morph target to peset 0
	morphParameters = mainParameters[0];


	//if pattern change mode is 'auto' load corresponding kit
	if(activeParameters[PAR_PATTERN_MODE] >= 1)
	{
		activeParameters[PAR_PATTERN_MODE] = 1; //prevent a value of 2 (skip next autoload) being saved with the project
		preset_loadDrumsetRam(seq_patternSet.kitNr[seq_activePattern], 0, 1);
		menu_selectedPresetNr[SAVE_TYPE_KIT] = seq_patternSet.kitNr[seq_activePattern];
		menu_loadedPresetNr[CURRENT_KIT] = seq_patternSet.kitNr[seq_activePattern];
		//refresh backup buffer with new kit, to prevent corruption when changin patterns with PCM=auto
		seq_copyPatternToBackupBuffer(seq_activePattern);
	}
	else
	{
		//init to preset 1
		//TODO maybe init to current active preset nr???
		preset_loadDrumsetRam(0,0, 1);
		menu_selectedPresetNr[SAVE_TYPE_KIT] = 0;
		menu_loadedPresetNr[CURRENT_KIT] = 0;
		seq_copyPatternToBackupBuffer(seq_activePattern);

	}

	preset_saveLastUsedProjectNrToSD(presetNr);

	//change pattern to pat 1
	seq_setNextPattern(0);

	lcd_process_display_interrupt = 1;
	preset_initTmpFolder();
#if USE_INTERRUPT_ONLY_DURING_SAVE_PROJ
			HAL_TIM_Base_Stop_IT(&htim2);	//calc sample interrupt - prio 1
#else

#endif

	return res;
#else
#if USE_SD_CARD
	//filename in 8.3  format
	char filename[GEN_BUF_LEN];
	UINT bytesRead;
	uint16_t remain;
	uint8_t siz;
	uint8_t version=0;
	//sprintf(filename,"p%03d.snd",presetNr);
	if(isAll)
		preset_makeFileName(filename,presetNr,FEXT_ALL);
	else
		preset_makeFileName(filename,presetNr,FEXT_PROJ);

	//open the file
	FRESULT res = f_open((FIL*)&SDFile,filename,FA_OPEN_EXISTING | FA_READ);

	if(res!=FR_OK)
		return; //file open error... maybe the file does not exist?

	//first the preset name
	f_read((FIL*)&SDFile,(void*)savefileNames[KIT_NAMES][presetNr],8,&bytesRead);
	if(!bytesRead)
		goto closeFile;

	// read version number and make sure its valid
	f_read((FIL*)&SDFile,&version,1,&bytesRead);
	if(!bytesRead || version > FILE_VERSION)
		goto closeFile;

	if(isAll){
		// read global data
		if(!preset_readGlobalData())
			goto closeFile;
		// size of padding after globals
		remain=	64- (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);

	} else {
		// read bpm
		f_read((FIL*)&SDFile,&activeParameters[PAR_BPM],1,&bytesRead);
		if(!bytesRead)
			goto closeFile;

		// size of padding after these
		if(version>1) {
			// read pat reset bit
			f_read((FIL*)&SDFile,&activeParameters[PAR_BAR_RESET_MODE],1,&bytesRead);
			if(!bytesRead)
				goto closeFile;
			remain=	64- (1+1);
		} else
			remain=0; // version 1 didn't have padding for performances
	}

	while(remain){
		if(remain < GEN_BUF_LEN)
			siz=(uint8_t)remain;
		else
			siz=GEN_BUF_LEN;
		f_read((FIL*)&SDFile, filename, siz, &bytesRead);
		if(bytesRead !=siz)
			goto closeFile;
		remain -=siz;
	}



	// read kit data
	res=preset_readDrumsetData(presetNr, 0);
	if(res!=FR_OK)
		goto closeFile;

	// read padding
	remain = 512 - (END_OF_SOUND_PARAMETERS);
	while(remain){
		if(remain < GEN_BUF_LEN)
			siz=(uint8_t)remain;
		else
			siz=GEN_BUF_LEN;
		f_read((FIL*)&SDFile, filename, siz, &bytesRead);
		if(bytesRead !=siz)
			goto closeFile;
		remain -=siz;
	}


	// read pattern data
	/*//temporary removed
	if(!preset_readPatternData())
		goto closeFile;
		*/



closeFile:
	//close the file handle
	f_close((FIL*)&SDFile);

	// send drumset parameters to back
	preset_sendDrumsetParameters();

	// send global params
	menu_sendAllGlobals();

	//force complete repaint
	menu_repaintAll();

#else
	frontPanel_sendData(PRESET,PRESET_LOAD,presetNr);
#endif
#endif
}
//----------------------------------------------------

static void preset_makeFileNameOld(char *buf, uint8_t num, uint8_t type)
{

	const char *ext;
	buf[0]='p';
	numtostrpu(&buf[1],num,'0');
	switch(type) {
	case FEXT_SOUND:
		ext=PSTR(".snd");
		break;
	case FEXT_PAT:
		ext=PSTR(".pat");
		break;
	case FEXT_ALL:
		ext=PSTR(".all");
		break;
	default:
	case FEXT_PROJ:
		ext=PSTR(".prj");
		break;

	case FEXT_SONG:
		ext=PSTR(".sng");
		break;
	}
	strcpy(&buf[4],ext);

};


//load uses wildcard to find a name of the pattern 00-*.ext
static const char *preset_fileExtension(uint8_t type)
{
	switch(type) {
	case FEXT_SOUND:
		return PSTR(".snd");
	case FEXT_PAT:
		return PSTR(".pat");
	case FEXT_ALL:
		return PSTR(".all");
	case FEXT_SONG:
		return PSTR(".sng");
	default:
	case FEXT_PROJ:
		return PSTR(".prj");
	}
}

/* "NN-*.ext": every file that belongs to a slot, which is what the loader
   matches on and what the stale file clean-up has to look for. */
static void preset_makeFileNamePattern(char *buf, uint8_t num, uint8_t type)
{
	numtostrpu_2Bytes(&buf[0],num,'0');
	buf[2] = '-';
	buf[3] = '*';
	strcpy(&buf[4],preset_fileExtension(type));
	buf[8] = 0;
}

/* 8.3 names come back from FatFs in upper case while the names we build are
   lower case, so the one file to keep has to be recognised without regard to
   case. */
static uint8_t preset_sameFileName(const char *a, const char *b)
{
	/* 8.3 names are ASCII, so this does not need ctype */
	for( ; *a && *b; a++, b++)
	{
		const char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 'a' + 'A') : *a;
		const char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 'a' + 'A') : *b;
		if(ca != cb)
			return 0;
	}
	return *a == *b;
}

/* A slot's file name carries the first five characters of the slot name, so
   saving a renamed slot writes NN-NEWNAME.ext and leaves NN-OLDNAME.ext behind.
   The loader takes the first NN-*.ext that f_findfirst() returns, so which of
   the two answers is directory order, and a slot can come back with its
   previous contents (P4). Called after the new file is written, never before:
   the slot must never be without a file, which is what #65 was about. */
static void preset_removeStaleSlotFiles(uint8_t num, uint8_t type, const char *keep)
{
	char pattern[9];
	preset_makeFileNamePattern(pattern,num,type);

	/* One file per pass: FatFs does not define what happens to an open
	   directory when the directory is modified, so the search is closed before
	   anything is unlinked. The guard is the number of files a slot could
	   plausibly have accumulated. */
	for(uint8_t pass=0; pass<16; pass++)
	{
		DIR dj;
		FILINFO fno;
		char victim[13];

		victim[0] = 0;
		if(f_findfirst(&dj, &fno, "", pattern) != FR_OK)
		{
			f_closedir(&dj);
			return;
		}
		while(fno.fname[0])
		{
			if(!preset_sameFileName(fno.fname, keep))
			{
				strcpy(victim, fno.fname);
				break;
			}
			if(f_findnext(&dj, &fno) != FR_OK)
				break;
		}
		f_closedir(&dj);

		if(victim[0] == 0)
			return;
		if(f_unlink(victim) != FR_OK)
			return;
	}
}

//----------------------------------------------------
static FRESULT preset_makeFileNameLoad(char *buf, uint8_t num, uint8_t type)
{

	FRESULT fr;     /* Return value */
    DIR dj;         /* Directory object */
    FILINFO fno;    /* File information */

	preset_makeFileNamePattern(buf,num,type);


    fr = f_findfirst(&dj, &fno, "", buf);  /* Start to search for preset files */
    f_closedir(&dj);
    if(fr == FR_OK && fno.fname[0])
    {
    	fr = f_open((FIL*)&SDFile,fno.fname,FA_OPEN_EXISTING | FA_READ);
    	return fr;
    }

    return FR_NO_FILE;
}

void clearIllegalCharacters(char *input, char* output, uint8_t sz)
{
	for(uint8_t i=0; i<sz; i++)
	{
		if((input[i] >= 'a') && (input[i] <= 'z')) output[i] = input[i];
		else if((input[i] >= 'A') && (input[i] <= 'Z')) output[i] = input[i];
		else if((input[i] >= '0') && (input[i] <= '9')) output[i] = input[i];
		//! # $ % & ' ( ) - @ ^ _ ` { } ~
		else if(input[i] == ' ')  output[i] = '_';
		else if(input[i] == '!')  output[i] = input[i];
		else if(input[i] == '#')  output[i] = input[i];
		else if(input[i] == '$')  output[i] = input[i];
		else if(input[i] == '%')  output[i] = input[i];
		else if(input[i] == '&')  output[i] = input[i];
		else if(input[i] == '(')  output[i] = input[i];
		else if(input[i] == ')')  output[i] = input[i];
		else if(input[i] == '-')  output[i] = input[i];
		else if(input[i] == '@')  output[i] = input[i];
		else if(input[i] == '^')  output[i] = input[i];
		else if(input[i] == '_')  output[i] = input[i];
		else if(input[i] == '`')  output[i] = input[i];
		else if(input[i] == '{')  output[i] = input[i];
		else if(input[i] == '}')  output[i] = input[i];
		else if(input[i] == '~')  output[i] = input[i];
		else if(input[i] == 0)  output[i] = input[i];
		else output[i] = '_';
	}

}
//save uses the kit name to create a filename eg 00-NAME.ext
static void preset_makeFileNameSave(char *buf, uint8_t num, uint8_t type, char *name)
{

	char clearedName[9];
	clearedName[8] = 0;
	clearIllegalCharacters(name , clearedName, 8);
	//name scheme: 01-NAME.ext
	numtostrpu_2Bytes(&buf[0],num,'0');
	buf[2] = '-';

	//copy the first 5 characters of the name to the filename if not empty
	uint8_t i=0;
	for( ; i<5; i++)
	{
		if(clearedName[i] != 0)
		{
			if(clearedName[i] == ' ')
				buf[3+i] = '_';
			else
				buf[3+i] = clearedName[i];
		}
		else
		{
			break;
		}
	}

	strcpy(&buf[3+i],preset_fileExtension(type));
}

//----------------------------------------------------
/*
static void preset_makeFileNameTemp(char *buf, uint8_t num, uint8_t type)
{
	const char *ext;
	buf[0]='t';
	numtostrpu(&buf[1],num,'0');
	switch(type) {
	case 0:
		ext=PSTR(".snd");
		break;
	case 1:
		ext=PSTR(".pat");
		break;
	case 2:
		ext=PSTR(".all");
		break;
	default:
	case 3:
		ext=PSTR(".prf");
		break;
	}
	strcpy(&buf[4],ext);
}
*/
