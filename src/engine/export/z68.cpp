/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2026 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "z68.h"
#include "../engine.h"
#include "../ta-log.h"
#include <fmt/printf.h>

/// DivZ68 definitions

#define Z68_HEADER_SIZE 14
#define Z68_VERSION 1
#define Z68_YM_CMD 0x40
#define Z68_DELAY_CMD 0x80
#define Z68_YM_MAX_WRITES 63
#define Z68_SYNC_MAX_WRITES 31
#define Z68_DELAY_MAX 127
#define Z68_EOF Z68_DELAY_CMD

#define Z68_EXT Z68_YM_CMD
#define Z68_EXT_ADPCM 0x00
#define Z68_EXT_CHIP 0x40
#define Z68_EXT_SYNC 0x80
#define Z68_EXT_CUSTOM 0xC0

enum YM_STATE { ym_PREV, ym_NEW, ym_STATES };
//enum PSG_STATE { psg_PREV, psg_NEW, psg_STATES };

class DivZ68 {
  private:
    struct S_adpcmInst {
      int geometry;
      unsigned int offset, length, loopPoint;
      bool isLooped;
    };
    int ymState[ym_STATES][256];
    int adpcmRateCache;
    int adpcmClockCache;
//    std::vector<DivRegWrite> syncCache;
    int loopOffset;
    int ticks;
    int ymMask;
    int psgMask;
  public:
    DivZ68();
    ~DivZ68();
    SafeWriter* w;
    std::vector<DivRegWrite> ymwrites;
    std::vector<DivRegWrite> adpcmMeta;
    bool havesamples;
    int numWrites;
    void init();
    int getoffset();
    void writeYM(unsigned char a, unsigned char v);
    void writeADPCM(unsigned int a, unsigned int v);
    void tick(int numticks = 1);
    void setLoopPoint();
    SafeWriter* finish(DivEngine* e);
    void flushWrites();
  private:
    void flushTicks();
};

/// DivZ68 implementation

DivZ68::DivZ68() {
  w=NULL;
  init();
}

DivZ68::~DivZ68() {
}

void DivZ68::init() {
  if (w!=NULL) delete w;
  w=new SafeWriter;
  w->init();
  // write default Z68 data header
  w->write("z68",3); // magic header
  // 3
  w->writeC(Z68_VERSION);
  // no loop offset
  // 4
  w->writeI_BE(0);
  // no ADPCM
  // 8
  w->writeI_BE(0);
  loopOffset=-1;
  numWrites=0;
  ticks=0;
  // Initialize YM/PSG states
  memset(&ymState,-1,sizeof(ymState));
  // Initialize ADPCM states
  adpcmRateCache=-1;
  adpcmClockCache=-1;

  // Channel masks
  ymMask=0;
  psgMask=0;
}

int DivZ68::getoffset() {
  return w->tell();
}

void DivZ68::writeYM(unsigned char a, unsigned char v) {
  int lastMask=ymMask;
  if (a==0x19 && v>=0x80) a=0x1a; // AMD/PSD use same reg addr. store PMD as 0x1a
  if (a==0x08 && (v&0xf8)) ymMask|=(1<<(v&0x07)); // mark chan as in-use if keyDN
  if (a!=0x08) ymState[ym_NEW][a]=v; // cache the newly-written value
  bool writeit=false; // used to suppress spurious writes to unused channels
  if (a<0x20) {
    if (a==0x08) {
      // write keyUPDN messages if channel is active.
      writeit=(ymMask&(1<<(v&0x07)))>0;
    } else {
      // do not suppress global registers
      writeit=true;
    }
  } else {
    writeit=(ymMask&(1<<(a&0x07)))>0; // a&0x07 = chan ID for regs >=0x20
  }
  if (lastMask!=ymMask) {
    // if the ymMask just changed, then the channel has become active.
    // This can only happen on a KeyDN event, so voice=v&0x07
    // insert a keyUP just to be safe.
    ymwrites.push_back(DivRegWrite(0x08,v&0x07));
    numWrites++;
    // flush the ym_NEW cached states for this channel into the Z68....
    for (int i=0x20+(v&0x07); i<=0xff; i+=8) {
      if (ymState[ym_NEW][i]!=ymState[ym_PREV][i]) {
        ymwrites.push_back(DivRegWrite(i,ymState[ym_NEW][i]));
        numWrites++;
        // ...and update the shadow
        ymState[ym_PREV][i]=ymState[ym_NEW][i];
      }
    }
  }
  // Handle the current write if channel is active
  if (writeit && ((ymState[ym_NEW][a]!=ymState[ym_PREV][a]) || a==0x08)) {
    // update YM shadow if not the KeyUPDN register.
    if (a!=8) ymState[ym_PREV][a]=ymState[ym_NEW][a];
    // if reg=PMD, then change back to real register 0x19
    if (a==0x1a) a=0x19;
    ymwrites.push_back(DivRegWrite(a,v));
    numWrites++;
  }
}

/*

 0,0 = off
 0,1 = on
 1,x = sample

 4,x = ratesel (0 to 2) > (2 to 4)
 3,x = clocksel (0 = full, 1 = half)
 2,x = panning
       01 = 2 = right  > 2
       10 = 1 = left   > 1
       11 = 0 = center > 3
*/
void DivZ68::writeADPCM(unsigned int a, unsigned int v) {
  if (a==12) {        // ADPCM Rate
    if (adpcmRateCache!=v) {
      adpcmMeta.push_back(DivRegWrite(4,v + 2));
      adpcmRateCache=v;
      numWrites++;
    }
  } else if (a==8) { // Clock sel
    if (adpcmClockCache!=v) {
      adpcmMeta.push_back(DivRegWrite(3,v));
      adpcmClockCache=v;
      numWrites++;
    }
  } else if (a==2) { // Panning
    adpcmMeta.push_back(DivRegWrite(a,v ? v: 3));
    numWrites++;
  } else if (a==1) { // Sample
    havesamples = true;
    adpcmMeta.push_back(DivRegWrite(a,v));
    numWrites++;
  } else if (a==0) { // On/Off
    if(v == 1)
    {
        adpcmMeta.push_back(DivRegWrite(a,v - 1));
        numWrites++;
    }
  }
}

void DivZ68::tick(int numticks) {
  flushWrites();
  ticks+=numticks;
}

void DivZ68::setLoopPoint() {
  tick(0); // flush any ticks+writes
  flushTicks(); // flush ticks incase no writes were pending
  logI("Z68: loop at file offset %d bytes",w->tell());
  loopOffset=w->tell();
  // update the Z68 header's loop offset value
  w->seek(0x04,SEEK_SET);
  w->writeI_BE(loopOffset - Z68_HEADER_SIZE);
  w->seek(loopOffset,SEEK_SET);
  // reset the ADPCM caches that would inhibit dupes
  adpcmRateCache=-1;
  adpcmClockCache=-1;
  // reset the YM shadow....
  memset(&ymState[ym_PREV],-1,sizeof(ymState[ym_PREV]));
  // ... and cache (except for unused channels)
  memset(&ymState[ym_NEW],-1,0x20);
  for (int chan=0; chan<8; chan++) {
    // do not clear state for as-yet-unused channels
    if (!(ymMask&(1<<chan))) continue;
    // clear the state for channels in use so they match the unknown state
    // of the YM shadow.
    for (int i=0x20+chan; i<=0xff; i+=8) {
      ymState[ym_NEW][i]=-1;
    }
  }
}

SafeWriter* DivZ68::finish(DivEngine* e) {
  unsigned int sample_len;
  int sample_offset;
  tick(0); // flush any pending writes / ticks
  flushTicks(); // flush ticks in case there were no writes pending
  w->writeC(Z68_EOF);
  if (havesamples) { // if exists, write ADPCM instruments and blob to the end of file
    unsigned int adpcmOff=w->tell();
    // Make sure it's on an even boundary
    if(adpcmOff & 1)
    {
        adpcmOff += 1;
        w->writeC(0);
    }
    // Number of samples
    w->writeS_BE(e->song.sampleLen);
    // Offset after the table
    sample_offset = e->song.sampleLen * 8;
    // Write lengths
    for (int i =0; i < e->song.sampleLen; i++) {
      DivSample* s=e->getSample(i);
      w->writeI_BE(sample_offset);
      w->writeI_BE(s->lengthVOX);
      sample_offset += s->lengthVOX;
    }
    // Write samples data
    for (int i = 0; i < e->song.sampleLen; i++) {
      DivSample* s=e->getSample(i);
      for(int j =0; j < s->lengthVOX; j++)
      {
        w->writeC(s->dataVOX[(j)]);
      }
    }
    // update ADPCM offset in file
    w->seek(0x08,SEEK_SET);
    w->writeI_BE(adpcmOff);
  }
  // update channel use masks.
//  w->seek(0x0c,SEEK_SET);
 // w->writeC((unsigned char)(ymMask&0xff));
//  w->writeS((short)(psgMask&0xffff));
  return w;
}

void DivZ68::flushWrites() {
  logD("Z68: flushWrites.... numwrites=%d ticks=%d ymwrites=%d adpcmMeta=%d",numWrites,ticks,ymwrites.size(),adpcmMeta.size());
  if (numWrites==0) return;
  bool hasFlushed=false;
  int n=0; // n=completed YM writes. used to determine when to write the CMD byte...
  for (DivRegWrite& write: ymwrites) {
        if (!hasFlushed) {
          flushTicks();
          hasFlushed=true;
        }
        if (n%Z68_YM_MAX_WRITES==0) {
          if (ymwrites.size()-n>Z68_YM_MAX_WRITES) {
            w->writeC((unsigned char)(Z68_YM_CMD+Z68_YM_MAX_WRITES));
            logD("Z68: YM-write: %d (%02x) [max]",Z68_YM_MAX_WRITES,Z68_YM_MAX_WRITES+Z68_YM_CMD);
          } else {
            w->writeC((unsigned char)(Z68_YM_CMD+ymwrites.size()-n));
            logD("Z68: YM-write: %d (%02x)",ymwrites.size()-n,Z68_YM_CMD+ymwrites.size()-n);
          }
        }
        n++;
        w->writeC(write.addr);
        w->writeC(write.val);
  }
  ymwrites.clear();
  if (adpcmMeta.size()) { // we have some PCM events to write
    if (!hasFlushed) {
      flushTicks();
      hasFlushed=true;
    }
    for (DivRegWrite& write: adpcmMeta) {
      w->writeC(write.addr);
      // We don't need any extra data for a note off
      if(write.addr != 0)
      {
        w->writeC(write.val);
      }
    }
    adpcmMeta.clear();
  }

/*  n=0;
  for (DivRegWrite& write: syncCache) {
    if (!hasFlushed) {
      flushTicks();
      hasFlushed=true;
    }
    if (n%Z68_SYNC_MAX_WRITES==0) {
 //     w->writeC(Z68_EXT);
      if (syncCache.size()-n>Z68_SYNC_MAX_WRITES) {
        w->writeC((unsigned char)(Z68_EXT_SYNC|(Z68_SYNC_MAX_WRITES<<1)));
      } else {
        w->writeC((unsigned char)(Z68_EXT_SYNC|((syncCache.size()-n)<<1)));
      }
    }
    n++;
    w->writeC(write.addr);
    w->writeC(write.val);
  }*/
  //syncCache.clear();
  numWrites=0;
}

void DivZ68::flushTicks() {
  while (ticks>Z68_DELAY_MAX) {
    logD("Z68: write delay %d (max)",Z68_DELAY_MAX);
    ticks-=Z68_DELAY_MAX;
  }
  if (ticks>0) {
    logD("Z68: write delay %d",ticks);
  }
  ticks=0;
}

/// Z68 export

void DivExportZ68::run() {
  // settings
  bool loop=conf.getBool("loop",true);

  // system IDs
  int OKI=-1;
  int YM=-1;
  int IGNORED=0;

  // find indexes for YM and OKI. Ignore other systems.
  for (int i=0; i<e->song.systemLen; i++) {
    switch (e->song.system[i]) {
      case DIV_SYSTEM_MSM6258:
        if (OKI>=0) {
          IGNORED++;
          break;
        }
        OKI=i;
        logAppendf("OKI6258 detected as chip id %d",i);
        break;
      case DIV_SYSTEM_YM2151:
        if (YM>=0) {
          IGNORED++;
          break;
        }
        YM=i;
        logAppendf("YM detected as chip id %d",i);
        break;
      default:
        IGNORED++;
        logAppendf("Ignoring chip %d systemID %d",i,(int)e->song.system[i]);
        break;
    }
  }
  if (OKI<0 && YM<0) {
    logAppend("ERROR: No supported systems for Z68");
    failed=true;
    running=false;
    return;
  }
  if (IGNORED>0) {
    logAppendf("Z68 export ignoring %d unsupported system%c",IGNORED,IGNORED>1?'s':' ');
  }

  DivZ68 z68;

  e->stop();
  e->repeatPattern=false;
  unsigned char oldorder = e->curOrder;
  e->setOrder(0);
  e->synchronizedSoft([&]() {
    double origRate=e->got.rate;

    // determine loop point
    e->calcSongTimestamps();
    int loopOrder=e->curSubSong->ts.loopStart.order;
    int loopRow=e->curSubSong->ts.loopStart.row;
    logAppendf("loop point: %d %d",loopOrder,loopRow);

    z68.init();

    // reset the playback state
    e->curOrder=0;
    e->prevOrder=0;
    e->curRow=0;
    e->freelance=false;
    e->playing=false;
    e->extValuePresent=false;
    e->remainingLoops=-1;

    // Prepare to write song data
    e->playSub(false);
    bool done=false;
    bool loopNow=false;
    int loopPos=-1;
    int old_speed = -1;
    int old_tempo = -1;

    z68.havesamples = false;
    z68.ymwrites.clear();
    z68.adpcmMeta.clear();
    z68.numWrites = 0;
    // Speeds
    // 12
    z68.w->seek(0x12, SEEK_SET);
    z68.w->writeS_BE(e->speeds.val[0]);
    old_speed = e->speeds.val[0];
    // 14

    if (OKI>=0) e->disCont[OKI].dispatch->toggleRegisterDump(true);
    if (YM>=0) {
      e->disCont[YM].dispatch->toggleRegisterDump(true);
      // emit LFO initialization commands
      z68.writeYM(0x18,0);    // freq=0
      z68.writeYM(0x19,0x7F); // AMD =7F
      z68.writeYM(0x19,0xFF); // PMD =7F

      z68.w->writeC((unsigned char)(Z68_EXT_SYNC|1));
      z68.flushWrites();
      // End of row data
      z68.w->writeC((unsigned char)(Z68_EXT_SYNC|2));
      // TODO: incorporate the Furnace meta-command for init data and filter
      //       out writes to otherwise-unused channels.
    }
    // Indicate the song's tuning as a sync meta-event
    // specified in terms of how many 1/256th semitones
    // the song is offset from standard A-440 tuning.
    // This is mainly to benefit visualizations in players
    // for non-standard tunings so that they can avoid
    // displaying the entire song held in pitch bend.
    // Tunings offsets that exceed a half semitone
    // will simply be represented in a different key
    // by nature of overflowing the signed char value
//    signed char tuningoffset=(signed char)(round(3072*(log(e->song.tuning/440.0)/log(2))))&0xff;
//    z68.writeSync(0x01,tuningoffset);

    // Flush'em
    if(YM != -1) e->disCont[YM].dispatch->getRegisterWrites().clear();
    if(OKI != -1) e->disCont[OKI].dispatch->getRegisterWrites().clear();

    while (!done) {
      if (loopPos==-1) {
        if (loopOrder==e->curOrder && loopRow==e->curRow && loop)
          loopNow=true;
        if (loopNow) {
          // If Virtual Tempo is in use, our exact loop point
          // might be skipped due to quantization error.
          // If this happens, the tick immediately following is our loop point.
          if (e->ticks==1 || !(loopOrder==e->curOrder && loopRow==e->curRow)) {
            loopPos=z68.getoffset();
            z68.setLoopPoint();
            loopNow=false;
          }
        }
      }
      if (e->nextTick(false,true) || !e->playing) {
        done=true;
        if (!loop) {
          for (int i=0; i<e->song.systemLen; i++) {
            e->disCont[i].dispatch->getRegisterWrites().clear();
          }
          break;
        }
        if (!e->playing) {
          loopPos=-1;
        }
      }
      if(done)
      {
        if(e->GoTick)
        {
          z68.flushWrites();
          e->GoTick = 0;
        }
        break;
      }
      if(e->NewSpeed != -1)
      {
        if(e->NewSpeed != old_speed)
        {
          z68.w->writeC(Z68_EXT_CHIP);
          z68.w->writeC(0);
          z68.w->writeC(e->NewSpeed);
          old_speed = e->NewSpeed;
        }
      }
      if(e->NewTempo != -1)
      {
        if(e->NewTempo != old_tempo)
        {
          z68.w->writeC(Z68_EXT_CHIP);
          z68.w->writeC(1);
          z68.w->writeC((unsigned short) (round(256.0f - (((1.0f / (float) e->NewTempo * 1000.0f) * 4000.0f) / 1024.0f))));
          old_tempo = e->NewTempo;
        }
      }
      // get register dumps
      for (int j=0; j<2; j++) {
        int i=0;
        // dump YM writes first
        if (j==0) {
          if (YM<0) {
            continue;
          } else {
            i=YM;
          }
        }
        // dump OKI writes second
        if (j==1) {
          if (OKI<0) {
            continue;
          } else {
            i= OKI;
          }
        }
        std::vector<DivRegWrite>& writes=e->disCont[i].dispatch->getRegisterWrites();
        if (writes.size()>0)
          logD("z68Ops: Writing %d messages to chip %d",writes.size(),i);
        for (DivRegWrite& write: writes) {
          if (i==YM) {
            if (done && write.addr==0x08 && (write.val&0x78)>0) continue; // don't process keydown on lookahead
            z68.writeYM(write.addr&0xff,write.val);
          }
          if (i==OKI) {
            z68.writeADPCM(write.addr,write.val);
          }
        }
        writes.clear();
      }
      if(e->GoTick)
      {
        // New row
        if (z68.numWrites)
        {
          z68.w->writeC((unsigned char)(Z68_EXT_SYNC|1));
          z68.flushWrites();
          // End of row data
          z68.w->writeC((unsigned char)(Z68_EXT_SYNC|2));
        }
        else
        {
          z68.w->writeC((unsigned char)(Z68_EXT_SYNC|3));
        }
      }
      else
      {
          // Drifting
        z68.flushWrites();
      }
    }
    // end of song

    // done - close out.
    e->got.rate=origRate;
    if (OKI>=0) e->disCont[OKI].dispatch->toggleRegisterDump(false);
    if (YM>=0) e->disCont[YM].dispatch->toggleRegisterDump(false);

    e->remainingLoops=-1;
    e->playing=false;
    e->freelance=false;
    e->extValuePresent=false;
    output.push_back(DivROMExportOutput("out.z68", z68.finish(e)));
  });

  e->setOrder(oldorder);
  progress[0].amount=1.0f;

  logAppend("finished!");

  running=false;
}

/// DivExpottZ68 - FRONTEND

bool DivExportZ68::go(DivEngine* eng) {
  progress[0].name="Generate";
  progress[0].amount=0.0f;

  e=eng;
  running=true;
  failed=false;
  mustAbort=false;
  exportThread=new std::thread(&DivExportZ68::run,this);
  return true;
}

void DivExportZ68::wait() {
  if (exportThread!=NULL) {
    exportThread->join();
    delete exportThread;
  }
}

void DivExportZ68::abort() {
  mustAbort=true;
  wait();
}

bool DivExportZ68::isRunning() {
  return running;
}

bool DivExportZ68::hasFailed() {
  return failed;
}

DivROMExportProgress DivExportZ68::getProgress(int index) {
  if (index<0 || index>1) return progress[1];
  return progress[index];
}
