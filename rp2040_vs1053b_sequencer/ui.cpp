// ============================================================================
//  ui.cpp  --  OLED UI, menus and live control mapping (CORE 0)
//
//  Single-encoder editing model (standard for compact groove gear):
//    * turn  -> move a linear cursor over the editable items on screen
//    * press -> "enter/toggle": booleans flip; numeric fields enter an
//               edit mode where turning changes the value, press confirms
//    * SHIFT + press on a step (MAIN) -> open the per-step editor
//  Buttons:
//    PLAY  : Play/Stop            (SHIFT: Pattern<->Song mode)
//    FUNC  : Mute sel. track      (SHIFT: Solo) (long: clear track)
//    SHIFT : modifier
//    MENU  : Menu / Back          (SHIFT: File) (long: PANIC all-notes-off)
//  8 pots:
//    MAIN  : live performance - pot i sends the selected CC on track i
//    STEP  : pot i edits a parameter of the selected step (param-lock feel)
// ============================================================================
#include "ui.h"
#include "model.h"
#include "input.h"
#include "engine.h"
#include "storage.h"
#include "ipc.h"
#include "scales.h"
#include "gm_names.h"
#include <Wire.h>
#include <U8g2lib.h>

// ---- display -----------------------------------------------------------------
#if OLED_IS_SH1106
U8G2_SH1106_128X64_NONAME_F_2ND_HW_I2C  g_oled(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_2ND_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE);
#endif

// ---- performance CC targets for the 8 pots ----------------------------------
struct PerfTgt { const char* name; uint8_t cc; };
static const PerfTgt PERF[] = {
  {"Volume",7},{"Pan",10},{"Reverb",91},{"Chorus",93},
  {"ModWhl",1},{"Expr",11},{"Cutoff",74},{"Reson",71},
};
static const uint8_t NUM_PERF = sizeof(PERF)/sizeof(PERF[0]);
static const char* NOTE_NAMES[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

// ---- screens -----------------------------------------------------------------
enum Screen : uint8_t { SC_MAIN, SC_STEP, SC_MENU, SC_TRACK, SC_PATOPS, SC_SONG, SC_FILE, SC_GLOBAL };

static Screen   scr        = SC_MAIN;
static uint8_t  selTrack   = 0;
static uint8_t  selStep    = 0;
static int      cursor     = 0;
static bool     editing    = false;
static uint8_t  perfTarget = 0;
static int      euclidPulses = 4;
static int      copySrcPat   = 0;
static uint8_t  fileSlot     = 0;
static bool     fileLoadMode = false;
static char     toast[20]    = "";
static uint32_t toastUntil   = 0;
static uint32_t lastDraw     = 0;

// ---- helpers -----------------------------------------------------------------
#define LOCK()   mutex_enter_blocking(&g_lock)
#define UNLOCK() mutex_exit(&g_lock)

static void setToast(const char* s){ strncpy(toast,s,sizeof(toast)-1); toast[sizeof(toast)-1]=0; toastUntil=millis()+1200; }
static int  clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int  wrapi (int v,int n){ v%=n; if(v<0)v+=n; return v; }
static void pushMidi(uint8_t st,uint8_t a,uint8_t b){ ipc_push(ipc_midi(st,a,b)); }
static void sendProgram(uint8_t tr){ Track&t=curPattern().track[tr]; if(t.channel!=9) pushMidi(0xC0|(t.channel&0x0F),t.program,0); }

static void requestPattern(uint8_t p){
  g_reqPattern = p;
  if (g_transport==TRANSPORT_STOP){ g_curPattern=p; for(uint8_t t=0;t<NUM_TRACKS;t++) sendProgram(t); }
}

// ---- generic numeric list ----------------------------------------------------
struct Item { const char* label; int* val; int lo, hi; bool isBool; };

static void listInput(Item* items,int n,int d,bool encPress,bool shift,void(*onChange)(int)){
  if (editing){
    if (d){ int step=shift?5:1; *items[cursor].val=clampi(*items[cursor].val+d*step,items[cursor].lo,items[cursor].hi); if(onChange)onChange(cursor); }
    if (encPress) editing=false;
    return;
  }
  if (d) cursor=clampi(cursor+d,0,n-1);
  if (encPress){ if(items[cursor].isBool){*items[cursor].val^=1; if(onChange)onChange(cursor);} else editing=true; }
}
static void listDraw(const char* title,Item* items,int n){
  g_oled.setFont(u8g2_font_5x7_tf);
  g_oled.drawStr(0,7,title); g_oled.drawHLine(0,9,128);
  int top=(cursor>4)?(cursor-4):0;
  for(int row=0;row<5&&(top+row)<n;row++){
    int i=top+row; int y=18+row*9; char buf[26];
    if(items[i].isBool) snprintf(buf,sizeof(buf),"%-10s %s",items[i].label,*items[i].val?"ON":"off");
    else                snprintf(buf,sizeof(buf),"%-10s %d",items[i].label,*items[i].val);
    if(i==cursor){ g_oled.drawBox(0,y-7,128,9); g_oled.setDrawColor(0); g_oled.drawStr(2,y,buf);
                   if(editing)g_oled.drawStr(122,y,"<"); g_oled.setDrawColor(1); }
    else g_oled.drawStr(2,y,buf);
  }
}

// ============================================================================
//  Shared buttons
// ============================================================================
static void handleButtons(){
  bool shift=input_shift();
  if (input_btn_pressed(BTN_PLAY)){
    if (shift){ g_playMode=(g_playMode==MODE_PATTERN)?MODE_SONG:MODE_PATTERN; setToast(g_playMode==MODE_SONG?"Song mode":"Pattern mode"); }
    else {
      if (g_transport==TRANSPORT_STOP){
        if (g_playMode==MODE_SONG && g_proj.song.length>0){ g_songPos=0; requestPattern(g_proj.song.slot[0].pattern); }
        g_transport=TRANSPORT_PLAY;
      } else g_transport=TRANSPORT_STOP;
    }
  }
  // FUNC: in SONG screen it inserts/deletes a slot; elsewhere mute/solo/clear
  if (scr==SC_SONG){
    Song& sg=g_proj.song;
    if (input_btn_pressed(BTN_REC)){
      LOCK();
      if (shift){ if(sg.length>0){ uint8_t s=clampi(cursor/2,0,sg.length-1); for(uint8_t k=s;k+1<sg.length;k++)sg.slot[k]=sg.slot[k+1]; sg.length--; } }
      else      { if(sg.length<MAX_SONG_SLOTS){ uint8_t s=sg.length?clampi(cursor/2+1,0,sg.length):0; for(int k=sg.length;k>s;k--)sg.slot[k]=sg.slot[k-1]; sg.slot[s].pattern=g_curPattern; sg.slot[s].repeats=1; sg.length++; cursor=s*2; } }
      if (sg.length) cursor=clampi(cursor,0,sg.length*2-1); else cursor=0;
      UNLOCK();
      setToast(shift?"Slot deleted":"Slot inserted");
    }
    input_btn_long(BTN_REC);   // discard: FUNC long-press has no song action
  } else {
    if (input_btn_pressed(BTN_REC)){ LOCK(); Track&tr=curPattern().track[selTrack]; if(shift)tr.solo^=1; else tr.mute^=1; UNLOCK(); setToast(shift?"Solo":"Mute"); }
    if (input_btn_long(BTN_REC)){ LOCK(); Track&tr=curPattern().track[selTrack]; for(uint8_t s=0;s<MAX_STEPS;s++)tr.steps[s].active=0; UNLOCK(); setToast("Track cleared"); }
  }
  if (input_btn_pressed(BTN_MENU)){ editing=false; if(shift){scr=SC_FILE;cursor=0;} else {scr=(scr==SC_MAIN)?SC_MENU:SC_MAIN;cursor=0;} }
  if (input_btn_long(BTN_MENU)){ pushMidi(0xB0,123,0); setToast("PANIC"); }
}

// ============================================================================
//  Live pots
// ============================================================================
static void handlePots(){
  if (scr==SC_MAIN){
    for(uint8_t i=0;i<NUM_POTS;i++) if(input_pot_moved(i)){
      uint8_t v=clampi(input_pot(i)>>5,0,127);
      pushMidi(0xB0|(curPattern().track[i].channel&0x0F),PERF[perfTarget].cc,v);
    }
  } else if (scr==SC_STEP){
    for(uint8_t i=0;i<NUM_POTS;i++) if(input_pot_moved(i)){
      uint16_t p=input_pot(i);
      LOCK();
      Step& st=curPattern().track[selTrack].steps[selStep];
      Track& tr=curPattern().track[selTrack];
      switch(i){
        case 0: st.note=clampi(p>>5,0,127); break;
        case 1: st.velocity=clampi(p>>5,1,127); break;
        case 2: st.gate=clampi(p*100/4095,1,100); break;
        case 3: st.prob=clampi(p*100/4095,0,100); break;
        case 4: st.ratchet=clampi(p*4/4095+1,1,4); break;
        case 5: st.nudge=clampi(p*24/4095-12,-12,12); break;
        case 6: tr.octave=clampi(p*8/4095,0,8); break;
        case 7: tr.length=clampi(p*MAX_STEPS/4095+1,1,MAX_STEPS); break;
      }
      UNLOCK();
    }
  }
}

// ============================================================================
//  MAIN
// ============================================================================
static void mainInput(int d,bool encPress,bool shift){
  const int N=3+MAX_STEPS;
  if (editing){
    if (d){
      int step=shift?5:1;
      if (cursor==0){ LOCK(); g_proj.bpm=clampi(g_proj.bpm+d*step,20,300); UNLOCK(); }
      else if (cursor==1) requestPattern(wrapi(g_reqPattern+d,NUM_PATTERNS));
      else if (cursor==2) selTrack=wrapi(selTrack+d,NUM_TRACKS);
    }
    if (encPress) editing=false;
    return;
  }
  if (d) cursor=clampi(cursor+d,0,N-1);
  if (encPress){
    if (cursor<3) editing=true;
    else if (shift){ selStep=cursor-3; scr=SC_STEP; cursor=0; editing=false; }  // SHIFT+press: deep-edit step
    else { LOCK(); Track&tr=curPattern().track[selTrack]; Step&s=tr.steps[cursor-3];
           if(!s.active){s.active=1; if(!s.note)s.note=tr.defNote;} else s.active=0; UNLOCK(); }
  }
}
static void mainDraw(){
  Track& tr=curPattern().track[selTrack]; char buf[26];
  g_oled.setFont(u8g2_font_5x7_tf);
  snprintf(buf,sizeof(buf),"P%02d T%d %3dBPM %c%c",g_curPattern+1,selTrack+1,g_proj.bpm,
           g_transport==TRANSPORT_PLAY?'>':'.',g_playMode==MODE_SONG?'S':'P');
  g_oled.drawStr(0,7,buf);
  if (tr.mute) g_oled.drawStr(116,7,"M");
  if (tr.solo) g_oled.drawStr(122,7,"S");
  if (tr.channel==9) snprintf(buf,sizeof(buf),"DRUMS  note %d",tr.steps[selStep].note);
  else               snprintf(buf,sizeof(buf),"%s",gmName(tr.program));
  g_oled.drawStr(0,16,buf);
  for(uint8_t i=0;i<MAX_STEPS;i++){
    int x=i*8,top=22,w=7,h=14; bool on=tr.steps[i].active;
    if(on) g_oled.drawBox(x+1,top+2,w-1,h-3); else g_oled.drawFrame(x+1,top+2,w-1,h-3);
    if(i>=tr.length) g_oled.drawHLine(x+1,top+h,w-1);
    if(tr.steps[i].accent) g_oled.drawPixel(x+w/2,top+h/2);
    if(g_transport==TRANSPORT_PLAY && g_playStep==i) g_oled.drawHLine(x+1,top,w-1);
    if(cursor>=3 && (cursor-3)==i) g_oled.drawFrame(x,top,w+1,h+2);
  }
  if(cursor<3){ const int fx[3]={0,18,36}; g_oled.drawHLine(fx[cursor],9,16); if(editing)g_oled.drawHLine(fx[cursor],10,16); }
  g_oled.setFont(u8g2_font_4x6_tf);
  if(cursor>=3){ Step&s=tr.steps[cursor-3];
    snprintf(buf,sizeof(buf),"S%02d n%d v%d g%d%% p%d r%d",cursor-2,s.note,s.velocity,s.gate,s.prob,s.ratchet); }
  else snprintf(buf,sizeof(buf),"Pot:%s  Swg%d Hum%d",PERF[perfTarget].name,g_proj.swing,g_proj.humanize);
  g_oled.drawStr(0,63,buf);
}

// ============================================================================
//  STEP editor
// ============================================================================
static int sNote,sVel,sGate,sProb,sRat,sNudge,sAcc,sTie;
static void stepPull(){ Step&s=curPattern().track[selTrack].steps[selStep];
  sNote=s.note;sVel=s.velocity;sGate=s.gate;sProb=s.prob;sRat=s.ratchet;sNudge=s.nudge;sAcc=s.accent;sTie=s.tie; }
static void stepPush(int){ LOCK(); Step&s=curPattern().track[selTrack].steps[selStep];
  s.note=sNote;s.velocity=sVel;s.gate=sGate;s.prob=sProb;s.ratchet=sRat;s.nudge=sNudge;s.accent=sAcc;s.tie=sTie; UNLOCK(); }
static void stepItems(Item*it){
  it[0]={"Note",&sNote,0,127,false}; it[1]={"Velocity",&sVel,1,127,false}; it[2]={"Gate %",&sGate,1,100,false};
  it[3]={"Prob %",&sProb,0,100,false}; it[4]={"Ratchet",&sRat,1,4,false}; it[5]={"Nudge",&sNudge,-12,12,false};
  it[6]={"Accent",&sAcc,0,1,true}; it[7]={"Tie",&sTie,0,1,true};
}
static void stepIn(int d,bool p,bool sh){ stepPull(); Item it[8]; stepItems(it); listInput(it,8,d,p,sh,stepPush); }
static void stepDr(){ stepPull(); Item it[8]; stepItems(it); char t[20]; snprintf(t,sizeof(t),"STEP %d  T%d",selStep+1,selTrack+1); listDraw(t,it,8);
  g_oled.setFont(u8g2_font_4x6_tf);
  if(curPattern().track[selTrack].channel!=9){ char b[20]; snprintf(b,sizeof(b),"%s %s",NOTE_NAMES[sNote%12],gmName(curPattern().track[selTrack].program)); g_oled.drawStr(0,63,b);} }

// ============================================================================
//  TRACK editor
// ============================================================================
static int tChan,tProg,tLen,tOct,tDefN,tDefV,tMute,tSolo;
static void trackPull(){ Track&t=curPattern().track[selTrack];
  tChan=t.channel;tProg=t.program;tLen=t.length;tOct=t.octave;tDefN=t.defNote;tDefV=t.defVel;tMute=t.mute;tSolo=t.solo; }
static void trackPush(int idx){ LOCK(); Track&t=curPattern().track[selTrack];
  t.channel=tChan;t.program=tProg;t.length=tLen;t.octave=tOct;t.defNote=tDefN;t.defVel=tDefV;t.mute=tMute;t.solo=tSolo; UNLOCK();
  if(idx==0||idx==1) sendProgram(selTrack); }
static void trackItems(Item*it){
  it[0]={"Channel",&tChan,0,15,false}; it[1]={"Program",&tProg,0,127,false}; it[2]={"Length",&tLen,1,MAX_STEPS,false};
  it[3]={"Octave",&tOct,0,8,false}; it[4]={"DefNote",&tDefN,0,127,false}; it[5]={"DefVel",&tDefV,1,127,false};
  it[6]={"Mute",&tMute,0,1,true}; it[7]={"Solo",&tSolo,0,1,true};
}
static void trackIn(int d,bool p,bool sh){ trackPull(); Item it[8]; trackItems(it); listInput(it,8,d,p,sh,trackPush); }
static void trackDr(){ trackPull(); Item it[8]; trackItems(it); char t[16]; snprintf(t,sizeof(t),"TRACK %d",selTrack+1); listDraw(t,it,8);
  g_oled.setFont(u8g2_font_4x6_tf); g_oled.drawStr(0,63, tChan==9?"GM Drum channel":gmName(tProg)); }

// ============================================================================
//  GLOBAL
// ============================================================================
static int gBpm,gSwing,gHum,gScale,gRoot,gMVol,gPerf;
static void globalPull(){ gBpm=g_proj.bpm;gSwing=g_proj.swing;gHum=g_proj.humanize;gScale=g_proj.scaleId;gRoot=g_proj.scaleRoot;gMVol=g_proj.masterVol;gPerf=perfTarget; }
static void globalPush(int idx){ LOCK(); g_proj.bpm=gBpm;g_proj.swing=gSwing;g_proj.humanize=gHum;g_proj.scaleId=gScale;g_proj.scaleRoot=gRoot;g_proj.masterVol=gMVol; UNLOCK();
  perfTarget=gPerf; if(idx==5) ipc_push(ipc_pack(IPC_MVOL,0,0,gMVol)); }
static void globalItems(Item*it){
  it[0]={"BPM",&gBpm,20,300,false}; it[1]={"Swing %",&gSwing,50,75,false}; it[2]={"Humanize",&gHum,0,50,false};
  it[3]={"Scale",&gScale,0,NUM_SCALES-1,false}; it[4]={"Root",&gRoot,0,11,false}; it[5]={"Master",&gMVol,0,100,false};
  it[6]={"PotCC",&gPerf,0,NUM_PERF-1,false};
}
static void globalIn(int d,bool p,bool sh){ globalPull(); Item it[7]; globalItems(it); listInput(it,7,d,p,sh,globalPush); }
static void globalDr(){ globalPull(); Item it[7]; globalItems(it); listDraw("GLOBAL",it,7);
  g_oled.setFont(u8g2_font_4x6_tf); char b[26]; snprintf(b,sizeof(b),"%s %s pot:%s",NOTE_NAMES[gRoot%12],kScales[gScale].name,PERF[gPerf].name); g_oled.drawStr(0,63,b); }

// ============================================================================
//  MENU
// ============================================================================
static const char* MENU_ITEMS[]={"Track Edit","Pattern Ops","Song","Global","File (Save/Load)"};
static void menuIn(int d,bool p){
  const int n=5; if(d)cursor=clampi(cursor+d,0,n-1);
  if(p){ switch(cursor){case 0:scr=SC_TRACK;break;case 1:scr=SC_PATOPS;break;case 2:scr=SC_SONG;break;case 3:scr=SC_GLOBAL;break;case 4:scr=SC_FILE;fileLoadMode=false;break;} cursor=0; editing=false; }
}
static void menuDr(){
  g_oled.setFont(u8g2_font_6x10_tf); g_oled.drawStr(0,9,"MENU"); g_oled.drawHLine(0,11,128);
  g_oled.setFont(u8g2_font_5x7_tf);
  for(int i=0;i<5;i++){ int y=22+i*9;
    if(i==cursor){g_oled.drawBox(0,y-7,128,9);g_oled.setDrawColor(0);g_oled.drawStr(4,y,MENU_ITEMS[i]);g_oled.setDrawColor(1);}
    else g_oled.drawStr(4,y,MENU_ITEMS[i]); }
}

// ============================================================================
//  PATTERN OPS
// ============================================================================
static void patopsIn(int d,bool p,bool sh){
  const int n=7;
  if(editing){ int step=sh?2:1;
    if(cursor==3) euclidPulses=clampi(euclidPulses+d*step,0,MAX_STEPS);
    else if(cursor==5) copySrcPat=wrapi(copySrcPat+d,NUM_PATTERNS);
    if(p)editing=false; return; }
  if(d)cursor=clampi(cursor+d,0,n-1);
  if(p)switch(cursor){
    case 0:{LOCK();Track&t=curPattern().track[selTrack];for(uint8_t s=0;s<MAX_STEPS;s++)t.steps[s].active=0;UNLOCK();setToast("Track cleared");}break;
    case 1:{LOCK();for(uint8_t tr=0;tr<NUM_TRACKS;tr++)for(uint8_t s=0;s<MAX_STEPS;s++)curPattern().track[tr].steps[s].active=0;UNLOCK();setToast("Pattern cleared");}break;
    case 2:{LOCK();Track&t=curPattern().track[selTrack];for(uint8_t s=0;s<MAX_STEPS;s++){t.steps[s].active=(random(100)<45);if(t.steps[s].active){int nn=t.defNote+random(-5,8);t.steps[s].note=quantizeNote(clampi(nn,0,127),g_proj.scaleId,g_proj.scaleRoot);t.steps[s].velocity=70+random(50);}}UNLOCK();setToast("Randomized");}break;
    case 3: editing=true; break;
    case 4:{uint8_t pat[MAX_STEPS];euclid(euclidPulses,curPattern().track[selTrack].length,0,pat);LOCK();Track&t=curPattern().track[selTrack];for(uint8_t s=0;s<t.length;s++){t.steps[s].active=pat[s];if(pat[s]&&!t.steps[s].note)t.steps[s].note=t.defNote;}UNLOCK();setToast("Euclid applied");}break;
    case 5: editing=true; break;
    case 6:{LOCK();g_proj.pattern[g_curPattern]=g_proj.pattern[copySrcPat];UNLOCK();for(uint8_t t=0;t<NUM_TRACKS;t++)sendProgram(t);setToast("Pasted");}break;
  }
}
static void patopsDr(){
  g_oled.setFont(u8g2_font_5x7_tf); g_oled.drawStr(0,7,"PATTERN OPS"); g_oled.drawHLine(0,9,128);
  char rows[7][24];
  snprintf(rows[0],24,"Clear Track %d",selTrack+1);
  snprintf(rows[1],24,"Clear Pattern");
  snprintf(rows[2],24,"Randomize Track");
  snprintf(rows[3],24,"Euclid Pulses: %d",euclidPulses);
  snprintf(rows[4],24,"Apply Euclid->T%d",selTrack+1);
  snprintf(rows[5],24,"Copy From Pat: %d",copySrcPat+1);
  snprintf(rows[6],24,"Paste Into P%d",g_curPattern+1);
  int top=(cursor>4)?(cursor-4):0;
  for(int r=0;r<5&&(top+r)<7;r++){int i=top+r;int y=18+r*9;
    if(i==cursor){g_oled.drawBox(0,y-7,128,9);g_oled.setDrawColor(0);g_oled.drawStr(2,y,rows[i]);if(editing)g_oled.drawStr(122,y,"<");g_oled.setDrawColor(1);}
    else g_oled.drawStr(2,y,rows[i]); }
}

// ============================================================================
//  SONG  (FUNC inserts a slot, SHIFT+FUNC deletes - see handleButtons)
// ============================================================================
static void songIn(int d,bool p,bool sh){
  Song& sg=g_proj.song;
  if(sg.length==0){ if(p){LOCK();sg.length=1;sg.slot[0].pattern=0;sg.slot[0].repeats=1;UNLOCK();cursor=0;} return; }
  int nFields=sg.length*2;
  if(cursor>nFields-1) cursor=nFields-1;   // stay in range (e.g. after a delete)
  if(editing){ int slot=cursor/2,field=cursor%2,step=sh?4:1; LOCK();
    if(field==0) sg.slot[slot].pattern=wrapi(sg.slot[slot].pattern+d,NUM_PATTERNS);
    else         sg.slot[slot].repeats=clampi(sg.slot[slot].repeats+d*step,1,255);
    UNLOCK(); if(p)editing=false; return; }
  if(d)cursor=clampi(cursor+d,0,nFields-1);
  if(p)editing=true;
}
static void songDr(){
  Song& sg=g_proj.song;
  g_oled.setFont(u8g2_font_5x7_tf); char h[24]; snprintf(h,sizeof(h),"SONG len%d pos%d",sg.length,g_songPos+1); g_oled.drawStr(0,7,h); g_oled.drawHLine(0,9,128);
  g_oled.setFont(u8g2_font_4x6_tf);
  if(sg.length==0){ g_oled.drawStr(0,30,"Empty - press encoder"); g_oled.drawStr(0,38,"to add first slot"); return; }
  int slotTop=(cursor/2>6)?(cursor/2-6):0;
  for(int r=0;r<7&&(slotTop+r)<sg.length;r++){int s=slotTop+r;int y=16+r*6;char b[24];
    snprintf(b,sizeof(b),"%2d: P%02d x%d",s+1,sg.slot[s].pattern+1,sg.slot[s].repeats); g_oled.drawStr(2,y,b);
    if(cursor/2==s){ int fx=(cursor%2==0)?20:44; g_oled.drawHLine(fx,y+1,18); }
    if((uint8_t)s==g_songPos) g_oled.drawStr(120,y,">"); }
  g_oled.drawStr(0,63,"Func=ins  Sh+Func=del");
}

// ============================================================================
//  FILE
// ============================================================================
static void fileIn(int d,bool p,bool sh){
  const int n=NUM_SAVE_SLOTS;
  if(d) fileSlot=clampi((int)fileSlot+d,0,n-1);
  fileLoadMode = sh;                 // hold SHIFT = load, else save
  if(p){ if(fileLoadMode){ if(storage_load(fileSlot)){selTrack=0;cursor=0;setToast("Loaded");}else setToast("Empty slot"); }
         else { if(storage_save(fileSlot))setToast("Saved"); else setToast("Save failed"); } }
}
static void fileDr(){
  g_oled.setFont(u8g2_font_6x10_tf);
  g_oled.drawStr(0,9, fileLoadMode?"LOAD (hold SHIFT)":"SAVE  (Shift=Load)"); g_oled.drawHLine(0,11,128);
  g_oled.setFont(u8g2_font_5x7_tf);
  int top=(fileSlot>4)?(fileSlot-4):0;
  for(int r=0;r<5&&(top+r)<NUM_SAVE_SLOTS;r++){int i=top+r;int y=22+r*8;char b[24];
    snprintf(b,sizeof(b),"Slot %d  %s",i+1,storage_slot_used(i)?"[used]":"[empty]");
    if(i==fileSlot){g_oled.drawBox(0,y-7,128,9);g_oled.setDrawColor(0);g_oled.drawStr(4,y,b);g_oled.setDrawColor(1);}
    else g_oled.drawStr(4,y,b); }
}

// ============================================================================
//  Public
// ============================================================================
void ui_begin(){
  Wire1.setSDA(PIN_OLED_SDA); Wire1.setSCL(PIN_OLED_SCL);
  g_oled.setBusClock(400000); g_oled.begin();
  g_oled.clearBuffer(); g_oled.setFont(u8g2_font_6x10_tf);
  g_oled.drawStr(8,28,"RP2040 VS1053b"); g_oled.drawStr(20,44,"STEP SEQUENCER");
  g_oled.sendBuffer();
}

void ui_task(){
  input_poll();
  int  d        = input_enc_delta();
  bool encPress = input_btn_pressed(BTN_ENC);
  (void)input_btn_long(BTN_ENC);   // consume; not used currently
  bool shift    = input_shift();

  handleButtons();
  handlePots();

  // ---- input phase ----
  switch(scr){
    case SC_MAIN:   mainInput(d,encPress,shift);         break;
    case SC_STEP:   stepIn(d,encPress,shift);            break;
    case SC_MENU:   menuIn(d,encPress);                  break;
    case SC_TRACK:  trackIn(d,encPress,shift);           break;
    case SC_PATOPS: patopsIn(d,encPress,shift);          break;
    case SC_SONG:   songIn(d,encPress,shift);            break;
    case SC_FILE:   fileIn(d,encPress,shift);            break;
    case SC_GLOBAL: globalIn(d,encPress,shift);          break;
  }

  // ---- draw phase (~33 fps) ----
  uint32_t ms=millis();
  if (ms-lastDraw < 30) return;
  lastDraw=ms;

  g_oled.clearBuffer();
  switch(scr){
    case SC_MAIN:   mainDraw(); break;
    case SC_STEP:   stepDr();   break;
    case SC_MENU:   menuDr();   break;
    case SC_TRACK:  trackDr();  break;
    case SC_PATOPS: patopsDr(); break;
    case SC_SONG:   songDr();   break;
    case SC_FILE:   fileDr();   break;
    case SC_GLOBAL: globalDr(); break;
  }
  if (toast[0] && ms<toastUntil){
    g_oled.setFont(u8g2_font_5x7_tf);
    int w=strlen(toast)*5+6;
    g_oled.setDrawColor(0); g_oled.drawBox(64-w/2,26,w,12); g_oled.setDrawColor(1);
    g_oled.drawFrame(64-w/2,26,w,12); g_oled.drawStr(64-w/2+3,35,toast);
  } else if (ms>=toastUntil) toast[0]=0;
  g_oled.sendBuffer();
}
