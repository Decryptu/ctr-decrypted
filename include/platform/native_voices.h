#ifndef NATIVE_VOICES_H
#define NATIVE_VOICES_H

// Voice language (OPTIONS > Extras > Voices, g_config.voices). The game always
// asks for clips by their NTSC-U number; a PAL or Japanese language plays the
// matching clip from a pack imported from the player's own disc image
// (assets/XA/VOICES/<dir>). See docs/EXTRAS.md#voices.
enum NativeVoiceLang
{
	NATIVE_VOICES_US,
	NATIVE_VOICES_UK,
	NATIVE_VOICES_FRENCH,
	NATIVE_VOICES_GERMAN,
	NATIVE_VOICES_SPANISH,
	NATIVE_VOICES_ITALIAN,
	NATIVE_VOICES_DUTCH,
	NATIVE_VOICES_JAPANESE,
	NATIVE_VOICES_COUNT
};

enum NativeVoicesImport
{
	NATIVE_VOICES_IMPORT_OK,
	NATIVE_VOICES_IMPORT_NO_DISC, // no disc image of the right region in assets/
	NATIVE_VOICES_IMPORT_FAILED,  // a disc was found but the copy failed (see the log)
};

int NativeVoices_Clamp(int lang);
const char *NativeVoices_Label(int lang);
// 1 for the Japanese disc, 0 for PAL, -1 for NTSC-U (nothing to import).
int NativeVoices_NeedsJapaneseDisc(int lang);
int NativeVoices_Installed(int lang);
// 1 when a disc image the language can be imported from is in assets/.
int NativeVoices_HasSourceDisc(int lang);
// Imports the language's pack from a disc image in assets/ when it is missing.
enum NativeVoicesImport NativeVoices_Ensure(int lang);
// Result of the last NativeVoices_Ensure for this language (OK when never tried).
enum NativeVoicesImport NativeVoices_LastImport(int lang);
// Startup: import the chosen language, and PAL English for Penta when a PAL disc is present.
void NativeVoices_Init(void);
// The language a character's lines come from right now (falls back to NTSC-U).
int NativeVoices_ForCharacter(int characterID);
// Maps an NTSC-U clip (category 1 extra, 2 game) to the active pack. Returns
// the pack's directory name and sets *idOut, or NULL to play the NTSC-U clip.
const char *NativeVoices_Map(int categoryID, int usID, int *idOut);

#endif
