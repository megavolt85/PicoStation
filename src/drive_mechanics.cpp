#include "drive_mechanics.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include "i2s.h"
#include "cmd.h"
#include "values.h"
#include "logging.h"

#if DEBUG_CMD
#define DEBUG_PRINT printf
#else
#define DEBUG_PRINT(...) while (0)
#endif

#define ZONE_CNT 	16
#define ZONE_MAX 	ZONE_CNT-1

extern picostation::I2S m_i2s;
uint32_t zone[ZONE_CNT] = 			{4500, 7750, 13500, 27000, 45000, 63000, 85500, 103500, 130500, 153000, 175500, 207000, 234000, 265500, 297000, 999999};
uint32_t sect_per_track[ZONE_CNT] = {	8,    9,    10,    11,    12,    13,    14,     15,     16,     17,     18,     19,     20,     21,     22,     23};

picostation::DriveMechanics picostation::g_driveMechanics;

void __time_critical_func(picostation::DriveMechanics::moveToNextSector)()
{
	if(m_sector < c_sectorMax){
		m_sector++;
	}
	
	if (m_sector > zone[cur_zone] && cur_zone < ZONE_MAX)
	{
		cur_zone++;
	}
}

void __time_critical_func(picostation::DriveMechanics::setSector)(uint32_t step, bool rev)
{
	m_i2s.i2s_set_state(0);

	while(step != 0)
	{
        if(rev == 0)
        {
            uint32_t sbze = zone[cur_zone] - m_sector;
            uint32_t sbreq = step * sect_per_track[cur_zone];
            uint32_t do_steps = ((sbze > sbreq) ? sbreq : sbze) / sect_per_track[cur_zone];
            m_sector += do_steps * sect_per_track[cur_zone];
            
            if(m_sector > c_sectorMax)
            {
                static uint32_t last_sector_max = 0;
                static uint8_t last_zone_max = 0;

                if (!last_sector_max || last_sector_max != m_sector)
                {
                    last_sector_max = m_sector;
                    for (last_zone_max = 0; last_zone_max <= ZONE_MAX; last_zone_max++)
                    {
                        if (zone[last_zone_max] > c_sectorMax)
                        {
                            last_zone_max--;
                            break;
                        }
                    }
                }

                m_sector = c_sectorMax;
                cur_zone = last_zone_max;
                break;
            }
            
            step -= do_steps;
            if(step > 0 && cur_zone < ZONE_MAX)
            {
                cur_zone++;
            }
        }
        else
        {
            uint32_t sect_before_ze;
            uint32_t do_steps;
            uint32_t req_sect_step = step * sect_per_track[cur_zone];

            if(cur_zone > 0)
            {
                sect_before_ze = m_sector - zone[cur_zone - 1];
            }
            else
            {
                sect_before_ze = req_sect_step;
            }

            do_steps = ((sect_before_ze > req_sect_step) ? req_sect_step : sect_before_ze) / sect_per_track[cur_zone];
            
            if(m_sector > (do_steps * sect_per_track[cur_zone]))
            {
                m_sector -= do_steps * sect_per_track[cur_zone];
            }
            else
            {
                m_sector = 0;
                cur_zone = 0;
                break;
            }
            
			step -= do_steps;
           
			if((cur_zone > 0) && (step > 0))
			{
                cur_zone--;
            }
        }
	}
	
	skip_subq = m_sector;
#ifdef DEBUG_CMD	
	DEBUG_PRINT("set sector %d\n", m_sector-4500);
#endif
}

bool __time_critical_func(picostation::DriveMechanics::servo_valid)()
{
	return m_sector < c_sectorMax;

}

void __time_critical_func(picostation::DriveMechanics::moveSled)(picostation::MechCommand &mechCommand){
    if ((time_us_64() - m_sledTimer) > c_MaxTrackMoveTime)
    {
		cur_track_counter++;
		
		if (!(cur_track_counter & 255))
		{
			mechCommand.setSens(SENS::COUT, !mechCommand.getSens(SENS::COUT));
		}
		
		m_sledTimer = time_us_64();
    }
}

void __time_critical_func(picostation::DriveMechanics::startSled)()
{
	sled_work = true;
	cur_track_counter = 0;
	m_sledTimer = time_us_64();
}

