#pragma once

#include <stdint.h>

#include "../third_party/cueparser/cueparser.h"
#include "../third_party/cueparser/disc.h"
#include "../third_party/cueparser/scheduler.h"
#include "../third_party/posix_file.h"
#include "ff.h"
#include "subq.h"

namespace picostation {
class DiscImage {
  public:
    DiscImage() {};
    ~DiscImage() {};

    enum DataLocation {
        RAM,
        SDCard,
        USBSerial,
        USBStorage,
    };

    void buildSector(const int sector, uint32_t *buffer, uint16_t *userData, const uint16_t *scramling);
    FRESULT load(const TCHAR *targetCue);
    SubQ::Data generateSubQ(const int sector);
    bool hasData() { return m_hasData; };
    bool isCurrentTrackData() {
        return m_cueDisc.tracks[m_currentLogicalTrack].trackType == CueTrackType::TRACK_TYPE_DATA;
    };
    void makeDummyCue();
    void readSector(void *buffer, const int sector, DataLocation location, const uint16_t *scramling);
    void readSectorRAM(void *buffer, const int sector, const uint16_t *scramling);
    void readSectorSD(void *buffer, const int sector, const uint16_t *scramling);

  private:
    CueDisc m_cueDisc;
    bool m_hasData = false;
    int m_currentLogicalTrack = 0;
};

extern DiscImage g_discImage;
}  // namespace picostation
