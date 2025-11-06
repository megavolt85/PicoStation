#include "i2s.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <array>

#include "cmd.h"
#include "directory_listing.h"
#include "disc_image.h"
#include "drive_mechanics.h"
#include "ff.h"
#include "global.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "modchip.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "subq.h"
#include "values.h"
#include "listingBuilder.h"

#if DEBUG_I2S
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

pseudoatomic<picostation::FileListingStates> needFileCheckAction;
pseudoatomic<int> listReadyState;
pseudoatomic<int> g_entryOffset;

picostation::DiscImage::DataLocation s_dataLocation = picostation::DiscImage::DataLocation::RAM;

static uint16_t *__time_critical_func(generateScramblingLUT)()
{
    static uint16_t ScramblingLUT[1176] = {0};
    int shift = 1;
	
	for (int i = 0; i < 6; i++)
	{
		ScramblingLUT[i] = 0;
	}
	
    for (size_t i = 6; i < 1176; i++)
    {
        uint8_t upper = shift & 0xFF;
        for (size_t j = 0; j < 8; j++)
        {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }

        uint8_t lower = shift & 0xFF;

        ScramblingLUT[i] = (lower << 8) | upper;

        for (size_t j = 0; j < 8; j++)
        {
            unsigned bit = ((shift & 1) ^ ((shift & 2) >> 1)) << 15;
            shift = (bit | shift) >> 1;
        }
    }

    return ScramblingLUT;
}

int __time_critical_func(picostation::I2S::initDMA)(const volatile void *read_addr, unsigned int transfer_count)
{
    int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(channel);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    const unsigned int i2sDREQ = PIOInstance::I2S_DATA == pio0 ? DREQ_PIO0_TX0 : DREQ_PIO1_TX0;
    channel_config_set_dreq(&c, i2sDREQ);
    dma_channel_configure(channel, &c, &PIOInstance::I2S_DATA->txf[SM::I2S_DATA], read_addr, transfer_count, false);

    return channel;
}

[[noreturn]] void __time_critical_func(picostation::I2S::start)(MechCommand &mechCommand)
{
    picostation::ModChip modChip;
    static uint32_t pioSamples[CACHED_SECS][1176];
    static uint32_t pioDummy[1176];
    static uint16_t *cdScramblingLUT = generateScramblingLUT();

    static uint8_t bufferForDMA = 1;
    static uint8_t bufferForSDRead = 0;
    static int currentSector = -1;
    lastSector = -1;
    m_sectorSending = -1;
    static uint32_t loadedImageIndex = 0;
    static uint16_t img_count;

    needFileCheckAction = picostation::FileListingStates::IDLE;
    listReadyState = 1;
    g_entryOffset = 0;
    
    menu_active = true;
    s_doorPending = false;
    
    reinitI2S();
	
    dmaChannel = initDMA(pioSamples[0], 1176);

    g_coreReady[1] = true;          // Core 1 is ready
    while (!g_coreReady[0].Load())  // Wait for Core 0 to be ready
    {
        tight_loop_contents();
    }

    modChip.init();

#if DEBUG_I2S
    uint64_t startTime;
    uint64_t endTime;
#endif

    g_discImage.makeDummyCue();
	
    // this need to be moved to diskimage
    picostation::DirectoryListing::init();
    picostation::DirectoryListing::getDirectoryEntries(0);

    while (true)
    {
        // Sector could change during the loop, so we need to keep track of it
        currentSector = g_driveMechanics.getSector();
        
        modChip.sendLicenseString(currentSector, mechCommand);
		
		if (menu_active && needFileCheckAction.Load() != picostation::FileListingStates::IDLE)
		{
			switch (needFileCheckAction.Load())
			{
				case picostation::FileListingStates::GOTO_ROOT:
				{
					//printf("Processing GOTO_ROOT\n");
					picostation::DirectoryListing::gotoRoot();
					g_entryOffset = 0;
					needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					break;
				}
				
				case picostation::FileListingStates::GOTO_PARENT:
				{
					//printf("Processing GOTO_PARENT\n");
					picostation::DirectoryListing::gotoParentDirectory();
					g_entryOffset = 0;
					needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					break;
				}
				
				case picostation::FileListingStates::GOTO_DIRECTORY:
				{
					//printf("Processing GOTO_DIRECTORY %i\n", g_fileArg.Load());
					picostation::DirectoryListing::gotoDirectory(g_fileArg.Load());
					g_entryOffset = 0;
					needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					break;
				}
				
				case picostation::FileListingStates::GET_NEXT_CONTENTS:
				{
					//printf("Processing GET_NEXT_CONTENTS\n");
					g_entryOffset = g_fileArg.Load();
					needFileCheckAction = picostation::FileListingStates::PROCESS_FILES;
					break;
				}
				
				case picostation::FileListingStates::MOUNT_FILE:
				{
					//printf("Processing MOUNT_FILE\n");
					s_dataLocation = picostation::DiscImage::DataLocation::SDCard;
					char filePath[c_maxFilePathLength + 1];
					loadedImageIndex = g_fileArg.Load();
					picostation::DirectoryListing::getPath(loadedImageIndex, filePath);
					//printf("image cue name:%s\n", filePath);
					g_discImage.load(filePath);
					needFileCheckAction = picostation::FileListingStates::IDLE;
					img_count = DirectoryListing::getDirectoryEntriesCount();
					menu_active = false;
					reinitI2S();
					g_driveMechanics.resetDrive();
					mechCommand.setFirstClvModeStopKickPattern(true);

					continue;
					break;
				}
				
				case picostation::FileListingStates::PROCESS_FILES:
				{
					if (!listReadyState.Load())
					{
						picostation::DirectoryListing::getDirectoryEntries(g_entryOffset.Load());
						listReadyState = 1;
					}
					break;
				}
				
				default:
					break;
			}
		}
		else if (s_doorPending && !menu_active)
		{
			s_doorPending = false;
			if (++loadedImageIndex > img_count)
			{
				loadedImageIndex = 0;
			}
			
			char filePath[c_maxFilePathLength + 1];
			picostation::DirectoryListing::getPath(loadedImageIndex, filePath);
			g_discImage.unload();
			g_discImage.load(filePath);
			
			reinitI2S();
			g_driveMechanics.resetDrive();
		}
		
        // Data sent via DMA, load the next sector
        if (currentSector != lastSector && currentSector >= 4503 && currentSector < c_sectorMax)
        {
			if (!menu_active)
			{
				for (int i = 0; i < CACHED_SECS; i++)
				{
					if (loadedSector[i] == currentSector)
					{
						// already in cache
						bufferForDMA = i;
						lastSector = currentSector;
#if DEBUG_I2S0
						DEBUG_PRINT("sector %d in cache\n", currentSector);
#endif
						goto continue_transfer;
					}
				}
			}
			
			while (bufferForSDRead == bufferForDMA)
			{
				++bufferForSDRead &= (CACHED_SECS-1);
			}
			
			if (menu_active && needFileCheckAction.Load() == picostation::FileListingStates::PROCESS_FILES && listReadyState.Load() && currentSector == 4750)
			{
				g_discImage.buildSector(currentSector - c_leadIn, pioSamples[bufferForSDRead], picostation::DirectoryListing::getFileListingData(), cdScramblingLUT);
				needFileCheckAction = picostation::FileListingStates::IDLE;
			}
			else
			{
#if DEBUG_I2S
				startTime = time_us_64();
#endif
				// Load the next sector
				g_discImage.readSector(pioSamples[bufferForSDRead], currentSector - c_leadIn, s_dataLocation, cdScramblingLUT);
#if DEBUG_I2S
				endTime = time_us_64()-startTime;
				
				if (endTime > 5000)
				{
					DEBUG_PRINT("read time: %lluus (%d)\n", endTime, currentSector);
				}
#endif
			}
			
			loadedSector[bufferForSDRead] = currentSector;
			bufferForDMA = bufferForSDRead;
			lastSector = currentSector;
		}

continue_transfer:

        // Start the next transfer if the DMA channel is not busy
        if (!dma_channel_is_busy(dmaChannel) && i2s_state)
        {
			if (currentSector >= 4503 && currentSector < c_sectorMax)
			{
				m_sectorSending = loadedSector[bufferForDMA];
				m_lastSectorTime = time_us_64();

				dma_hw->ch[dmaChannel].read_addr = (uint32_t)pioSamples[bufferForDMA];

				// Sync with the I2S clock
				while (gpio_get(Pin::LRCK) == 1)
				{
					tight_loop_contents();
				}
				
				while (gpio_get(Pin::LRCK) == 0)
				{
					tight_loop_contents();
				}

				dma_channel_start(dmaChannel);
			}
			else if(picostation::g_subqDelay == false)
			{
				m_sectorSending = currentSector;
				m_lastSectorTime = time_us_64();
			}
        }
    }
    __builtin_unreachable();
}
