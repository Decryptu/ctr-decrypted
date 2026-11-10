# Native presentation

## Mask music

`Audio_SetMaskSong` selects from live human mask-weapon child threads with
positive duration. Dead threads, mask hints, bots, rescue and engine-revving
states cannot claim the song. Keep the current mask song while that kind of
mask remains active; otherwise choose Aku, then Uka, then the level song.
Every call applies `Music_Adjust` and updates `AKU_SONG`/`UKA_SONG` together.
The action flag alone is insufficient during birth, replacement and expiry.

## Checking

`tools/test-mask-song.py` compiles production song selection and checks live
thread selection, duration, bots, rescue/revving, competing masks and song flags.
