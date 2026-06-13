// ============================================================================
//  gm_names.h  --  General MIDI program + drum names for the display.
//  On the RP2040 const arrays live in flash automatically (no PROGMEM needed).
// ============================================================================
#pragma once
#include <Arduino.h>

static const char* const GM_NAMES[128] = {
  "Ac Grand Pno","Br Acou Pno","El Grand Pno","Honkytonk","E.Piano 1","E.Piano 2","Harpsichord","Clavinet",
  "Celesta","Glockenspl","Music Box","Vibraphone","Marimba","Xylophone","Tub Bells","Dulcimer",
  "Drawbar Org","Perc Organ","Rock Organ","Church Org","Reed Organ","Accordion","Harmonica","Tango Acc",
  "Nylon Gtr","Steel Gtr","Jazz Gtr","Clean Gtr","Muted Gtr","Ovrdrv Gtr","Dist Gtr","Gtr Harm",
  "Acou Bass","Fngr Bass","Pick Bass","Fretless","Slap Bass1","Slap Bass2","Syn Bass1","Syn Bass2",
  "Violin","Viola","Cello","Contrabass","Trem Strs","Pizz Strs","Harp","Timpani",
  "Str Ens 1","Str Ens 2","SynStrs 1","SynStrs 2","Choir Aahs","Voice Oohs","Synth Voice","Orch Hit",
  "Trumpet","Trombone","Tuba","Mute Trpt","Frnch Horn","Brass Sec","SynBrass1","SynBrass2",
  "Soprano Sx","Alto Sax","Tenor Sax","Bari Sax","Oboe","Eng Horn","Bassoon","Clarinet",
  "Piccolo","Flute","Recorder","Pan Flute","Blown Bot","Shakuhachi","Whistle","Ocarina",
  "Sq Lead","Saw Lead","Calliope","Chiff Lead","Charang","Voice Lead","Fifths","Bass+Lead",
  "Pad NewAge","Pad Warm","Polysynth","Pad Choir","Pad Bowed","Pad Metal","Pad Halo","Pad Sweep",
  "FX Rain","FX Sndtrk","FX Crystal","FX Atmos","FX Bright","FX Goblins","FX Echoes","FX SciFi",
  "Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
  "Tnkl Bell","Agogo","Steel Drum","Woodblock","Taiko","Melo Tom","Synth Drum","Rev Cymbal",
  "Gtr Fret","Breath","Seashore","Bird","Telephone","Helicopter","Applause","Gunshot"
};

static inline const char* gmName(uint8_t prog) { return GM_NAMES[prog & 0x7F]; }
