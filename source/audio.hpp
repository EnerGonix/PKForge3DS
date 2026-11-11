// Sound: background music, effects and Pokémon cries, through the DSP (ndsp).
//  - Music: the user's own tracks (MP3, OGG, WAV) in sdmc:/3ds/PKForge/music/{menu,park-day,park-night}, as
//    the Android PKForge plays the user's library; without any, three original chiptune tracks written for
//    this port. Streamed on a thread.
//  - Effects: original synthesized sounds (menus, the park's ball, hops, sparkles, wins).
//  - Cries: the user's own files in sdmc:/3ds/PKForge/cries/<national number>.(wav|ogg|mp3) when present,
//    else a voice made up for each species (pitch, length and warble from its number) - not the games' cries.
// Without the DSP firmware (sdmc:/3ds/dspfirm.cdc, dumped by Luma's Rosalina menu) PKForge stays silent.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace audio
{
    // appPriority: the app thread's priority; music streams just above it (init may run on another thread).
    void init(int appPriority);
    void exit();
    bool available();

    enum class Place
    {
        Title, // the title screen
        Menu,
        ParkDay,
        ParkNight
    };
    void place(Place p);

    enum class Sfx
    {
        Blip,
        Open,
        Back,
        Error,
        Throw,
        Land,
        Hop,
        Sparkle,
        Found,
        Win,
        Cursor // the soft tick of a cursor move
    };
    void sfx(Sfx s);
    void cry(int species, float volume = 1);

    // Four loud notes, whatever the settings: to check the speakers. Logged in sdmc:/3ds/PKForge/audio.log.
    void test();
    bool musicOn();
    void musicOn(bool on);
    bool effectsOn();
    void effectsOn(bool on);
    // What is playing now, or why there is no sound.
    std::string status();

    // The music menu (the original's MusicMenu): play/pause, skip, order, autostart, what each place plays
    // ("" every track, "same" as the menus, "silence", or a track's path) and the library of every track.
    bool paused();
    void pause(bool on);
    void skip();
    bool shuffle();
    void shuffle(bool on);
    bool autostart();
    void autostart(bool on);
    std::string choice(Place p);
    void choice(Place p, const std::string& value);
    std::vector<std::pair<std::string, std::string>> library(); // path, title
    std::string libraryFolder();
}
