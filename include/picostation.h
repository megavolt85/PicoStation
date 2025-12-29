#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "pseudo_atomics.h"

namespace picostation {

namespace audioControlModes {
enum : unsigned int {
    NORMAL = 0b00,
    LEVELMETER = 0b01,
    PEAKMETER = 0b10,
    ALTNORMAL = 0b11,
};
}

enum class FileListingStates {
    IDLE,
    GOTO_ROOT,
    GOTO_PARENT,
    GOTO_DIRECTORY,
    GET_NEXT_CONTENTS,
    MOUNT_FILE,
    PROCESS_FILES,
    GET_COVER
};

extern pseudoatomic<FileListingStates> g_fileListingState;
extern pseudoatomic<uint32_t> g_fileArg;

struct PWMSettings {
    const unsigned int gpio;
    unsigned int sliceNum;
    pwm_config config;
    const uint16_t wrap;
    const unsigned int clkdiv;
    const bool invert;
    const uint16_t level;
};

extern mutex_t g_mechaconMutex;
extern pseudoatomic<bool> g_coreReady[2];

extern unsigned int g_soctOffset;
extern unsigned int g_subqOffset;

extern bool g_subqDelay;
extern int g_targetPlaybackSpeed;
extern unsigned int g_audioCtrlMode;

[[noreturn]] void core0Entry();  // Reset, playback speed, Sled, soct, subq
[[noreturn]] void core1Entry();  // I2S, sdcard, modchip

void initHW();
void updatePlaybackSpeed();
void reset();
}  // namespace picostation

