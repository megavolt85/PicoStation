#include "cmd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "i2s.h"
#include "drive_mechanics.h"
#include "hardware/pio.h"
#include "logging.h"
#include "main.pio.h"
#include "pico/bootrom.h"
#include "picostation.h"
#include "pseudo_atomics.h"
#include "values.h"
#include "directory_listing.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif
extern picostation::I2S m_i2s;
extern pseudoatomic<uint32_t> g_fileArg;
extern pseudoatomic<picostation::FileListingStates> needFileCheckAction;
extern pseudoatomic<int> listReadyState;

static bool dir = 0;
static bool trk_dir = 0;
static bool prev_dir = 0;
static bool sled_break = 0;
static uint16_t m_jumpTrack = 0;

void __time_critical_func(picostation::MechCommand::processLatchedCommand)()
{
    static mech_cmd command;
    command.raw = m_latched;
    m_latched = 0;
    
	switch (command.cmd.id)
    {
		case MECH_CMD_TRACKING_MODE:
		{
			if(!g_driveMechanics.isSledStopped())
			{
				DEBUG_PRINT("%c\n", (dir == 0) ? '+' : '-');

				if((sled_break == 0) || (prev_dir == dir))
				{
					g_driveMechanics.setSector(g_driveMechanics.get_track_count(), dir);
				}
				
				g_driveMechanics.stopSled();
				sled_break = 1;
				prev_dir = dir;
			}

			switch(command.tracking_mode.sled)
			{		
				case SLED_FORWARD:
				{
					DEBUG_PRINT("SLED FORWARD\n");
					dir = 0;
					m_i2s.i2s_set_state(0);
					g_driveMechanics.startSled();
					break;
				}

				case SLED_REVERSE:
				{
					DEBUG_PRINT("SLED REVERSE\n");
					dir = 1;
					m_i2s.i2s_set_state(0);
					g_driveMechanics.startSled();
					break;
				}
			}
			break;
		}
		
		case MECH_CMD_AUTO_SEQUENCE:
		{
	        switch(command.aseq_cmd.cmd)
	        {
	            case ASEQ_CMD_CANCEL:
					setSens(SENS::XBUSY, false);
	            	m_i2s.i2s_set_state(1);
					return;
				
	            case ASEQ_CMD_FOCUS_ON:
					DEBUG_PRINT("ASEQ FOCUS ON\n");
					setSens(SENS::FOK, true);
					m_i2s.i2s_set_state(0);
					setSens(SENS::XBUSY, true);
					add_alarm_in_ms(15, [](alarm_id_t id, void *user_data) -> int64_t 
					{
						picostation::MechCommand *mechCommand = static_cast<picostation::MechCommand *>(user_data);
						mechCommand->resetXBUSY();
						return 0;
					}, this, true);
					break;

				case ASEQ_CMD_1TRK_JUMP:
					DEBUG_PRINT("ASEQ 1TRK %c\n", command.aseq_cmd.dir ? '-' : '+');
					g_driveMechanics.setSector(1, command.aseq_cmd.dir);
					break;

	            case ASEQ_CMD_2NTRK_JUMP:
					DEBUG_PRINT("ASEQ 2NTRK (%d) %c\n", m_jumpTrack << 1, command.aseq_cmd.dir ? '-' : '+');
	            	g_driveMechanics.setSector(m_jumpTrack << 1, command.aseq_cmd.dir);
					break;
			}
			break;
		}
		
		case MECH_CMD_ASEQ_TRACK_COUNT:
		{
			m_jumpTrack = command.aseq_track_count.count;
			break;
		}
		
		case MECH_CMD_MODE_SPECIFICATION:
		{
			setSoct(command.mode_specification.SOCT);
			
			if (command.mode_specification.SOCT)
			{
				pio_sm_set_enabled(PIOInstance::SUBQ, SM::SUBQ, false);
				soct_program_init(PIOInstance::SOCT, SM::SOCT, g_soctOffset, Pin::SQSO, Pin::SQCK);
				pio_sm_set_enabled(PIOInstance::SOCT, SM::SOCT, true);
				pio_sm_put_blocking(PIOInstance::SOCT, SM::SOCT, 0xFFFFFFF);
			}
			break;
		}
		
		case MECH_CMD_FUNCTION_SPECIFICATION:
		{
			g_targetPlaybackSpeed = command.function_specification.DSPB + 1;
			break;
		}
		
		case MECH_CMD_CLV_MODE:
		{
			sled_break = 0;
			switch(command.clv_mode.mode)
			{
				case CLV_MODE_STOP:
					setSens(SENS::GFS, false);
					m_i2s.i2s_set_state(0);
					DEBUG_PRINT("Stop\n");
					break;
				
				case CLV_MODE_BRAKE:
					DEBUG_PRINT("Brake\n");
					break;
					
				case CLV_MODE_KICK:
					DEBUG_PRINT("Kick\n");
					break;

				case CLV_MODE_CLVS:
				case CLV_MODE_CLVH:
				case CLV_MODE_CLVP:
				case CLV_MODE_CLVA:
					setSens(SENS::GFS, true);
					m_i2s.i2s_set_state(1);
					setSens(SENS::XBUSY, false);
					break;
			}
			break;
		}
		
		case MECH_CMD_CUSTOM:
		{
			g_fileArg = command.custom_cmd.arg;

			switch (command.custom_cmd.cmd)
			{
				case COMMAND_NONE:
				{
					needFileCheckAction = FileListingStates::IDLE;
					break;
				}
					
				case COMMAND_GOTO_ROOT:
				{
					DEBUG_PRINT("GOTO_ROOT\n");
					needFileCheckAction = FileListingStates::GOTO_ROOT;
					listReadyState = 0;
					break;
				}
					
				case COMMAND_GOTO_PARENT:
				{
					DEBUG_PRINT("GOTO_PARENT\n");
					needFileCheckAction = FileListingStates::GOTO_PARENT;
					listReadyState = 0;
					break;
				}
					
				case COMMAND_GOTO_DIRECTORY:
				{
					DEBUG_PRINT("GOTO_DIRECTORY\n");
					needFileCheckAction = FileListingStates::GOTO_DIRECTORY;
					listReadyState = 0;
					break;
				}
					
				case COMMAND_GET_NEXT_CONTENTS:
				{
					DEBUG_PRINT("GET_NEXT_CONTENTS\n");
					needFileCheckAction = FileListingStates::GET_NEXT_CONTENTS;
					listReadyState = 0;
					break;
				}
					
				case COMMAND_MOUNT_FILE:
				{
					DEBUG_PRINT("MOUNT_FILE\n");
					needFileCheckAction = FileListingStates::MOUNT_FILE;
					break;
				}
					
				case COMMAND_IO_COMMAND:
				{
					DEBUG_PRINT("COMMAND_IO_COMMAND %x\n", command.custom_cmd.arg);
					break;
				}
					
				case COMMAND_IO_DATA:
				{
					DEBUG_PRINT("COMMAND_IO_DATA %x\n", command.custom_cmd.arg);
					break;
				}
				
				case COMMAND_EXTENDED:
				{
					switch (command.custom_cmd.arg >> 1)
					{
						case EXTENDED_SKIP_BOOTSECTOR:
							g_discImage.set_skip_bootsector(command.custom_cmd.arg & 1);
							break;
						
						case EXTENDED_SKIP_EDC:
							g_discImage.set_skip_edc(command.custom_cmd.arg & 1);
							break;
						
						default:
							break;
					}
					break;
				}
				
				case COMMAND_GET_COVER:
				{
					needFileCheckAction = FileListingStates::GET_COVER;
					listReadyState = 0;
					break;
				}
				
				case COMMAND_BOOTLOADER:
				{
					if (command.custom_cmd.arg == 0xBEEF)
					{
						// Restart into bootloader
						rom_reset_usb_boot_extra(Pin::LED, 0, false);
					}
					break;
				}
				
				default:
					break;
			}
			break;
		}
		
		default:
			if (!((1 << command.cmd.id) & 0x6A))
			{
				setBootSectorPattern(command.cmd.id);
			}
			break;
	}
}

void __time_critical_func(picostation::MechCommand::setBootSectorPattern) (uint8_t value)
{
	if (!isBootSectorPattern())
	{
		static int current_value = -1;
		if (current_value != value || value == 10)
		{
			current_value = value;
			switch (value)
			{
			case 10:
				if (m_bootSectorPattern == 0b0011)
				{
					m_bootSectorPattern |= 1 << 2;
				}
				else
				{
					m_bootSectorPattern = 1 << 0;	
				}

				break;

			case 255:
				if (m_bootSectorPattern == 0b0001)
				{
					m_bootSectorPattern |= 1 << 1;
				}
				break;
			
			default:
				m_bootSectorPattern = 0;
				break;
			}

			if (m_bootSectorPattern == 0b0111)
			{
				g_discImage.set_skip_bootsector(true); 
				DEBUG_PRINT("BOOTSECTOR PATTERN\n");
				return;
			}
		}
	}
}

bool __time_critical_func(picostation::MechCommand::getSens)(const size_t what) const { return m_sensData[what]; }

void __time_critical_func(picostation::MechCommand::setSens)(const size_t what, const bool new_value)
{
    m_sensData[what] = new_value;
    if (what == m_currentSens)
    {
        gpio_put(Pin::SENS, new_value);
    }
}

bool picostation::MechCommand::getSoct() { return m_soctEnabled.Load(); }
void picostation::MechCommand::setSoct(const bool new_value) { m_soctEnabled = new_value; }

void __time_critical_func(picostation::MechCommand::resetXBUSY)()
{
	m_i2s.i2s_set_state(1);
	setSens(SENS::XBUSY, false);
}

void __time_critical_func(picostation::MechCommand::updateMech)() 
{
    while (pio_sm_get_rx_fifo_level(PIOInstance::MECHACON, SM::MECHACON))
    {
        const uint32_t c = pio_sm_get_blocking(PIOInstance::MECHACON, SM::MECHACON) >> 24;
        m_latched = m_latched >> 8;
        m_latched = m_latched | (c << 16);
        m_currentSens = c >> 4;
    }
    gpio_put(Pin::SENS, m_sensData[m_currentSens]);
}

