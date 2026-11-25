#pragma once

#include "lyric_data.h"

// True if the given lyrics appear to already contain a translation.
// Storage format: each line is saved twice with the same timestamp -- the
// original text first, then the translated text. Loading such a file collapses
// the two lines into one (parsers::lrc does that for identical timestamps), so
// a translated line is simply one whose text contains a newline.
bool lyrics_are_translated(const LyricData& lyrics);

// Drops the translated half of every line, leaving only the original text.
LyricData lyrics_without_translation(const LyricData& lyrics);

// Translates every line using the configured OpenAI-compatible chat API and
// returns lyrics where each line is followed by its translation (same timestamp).
// Throws std::runtime_error with a user-facing message on any failure.
// The configuration lives in Preferences -> Tools -> OpenLyrics -> Translation.
LyricData translate_lyrics(const LyricData& lyrics, abort_callback& abort);

// Entry point for the UI: asks for confirmation if the lyrics are already translated,
// then translates them on a background thread and saves the result.
// Must be called on the main thread.
void SpawnLyricTranslation(LyricData lyrics, metadb_handle_ptr track, metadb_v2_rec_t track_info);
