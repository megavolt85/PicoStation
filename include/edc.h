#ifndef _EDC_H
#define _EDC_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void eccedc_init();
//int audio_guess(const uint8_t* sector);
void edc_computeblock(const uint8_t* src, size_t size, uint8_t* dest);
void eccedc_generate(uint8_t* sector);

#ifdef __cplusplus
}
#endif

#endif
