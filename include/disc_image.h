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
        SDCard
    };

    void buildSector(const int sector, uint32_t *buffer, uint16_t *userData, const uint16_t *scramling, bool pregap = false);
    FRESULT load(const TCHAR *targetCue);
    void unload();
    SubQ::Data generateSubQ(const int sector);
    bool hasData() { return m_hasData; };
    void makeDummyCue();
    void readSector(void *buffer, const int sector, DataLocation location, const uint16_t *scramling);
    void readSectorRAM(void *buffer, const int sector, const uint16_t *scramling);
    void readSectorSD(void *buffer, const int sector, const uint16_t *scramling);
    void set_skip_bootsector(bool skip) { skip_bootsector =  skip; }
	void set_skip_edc(bool skip) { skip_edc =  skip; }

  private:
    CueDisc m_cueDisc;
    bool m_hasData = false;
    int m_currentLogicalTrack = 0;
    bool skip_bootsector = false;
    bool skip_edc = false;
};

extern DiscImage g_discImage;
}  // namespace picostation
