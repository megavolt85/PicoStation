#include "modchip.h"

#include <stdio.h>

#include "cmd.h"
#include "disc_image.h"
#include "hardware/uart.h"
#include "logging.h"
#include "pico/stdlib.h"
#include "picostation.h"
#include "values.h"

#if DEBUG_MODCHIP
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) while (0)
#endif

void picostation::ModChip::endLicenseSequence() {
    gpio_put(Pin::SCEX_DATA, 0);
    m_modchipTimer = time_us_64();
    DEBUG_PRINT("-SCEX\n");
}

void picostation::ModChip::init() {
    uart_init(uart1, 250);
    gpio_set_function(Pin::SCEX_DATA, GPIO_FUNC_UART);
    gpio_set_outover(Pin::SCEX_DATA, GPIO_OVERRIDE_INVERT);
    uart_set_hw_flow(uart1, false, false);
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart1, false);

    m_modchipTimer = time_us_64();
}

void picostation::ModChip::sendLicenseString(const int sector, MechCommand &mechCommand) {
    static constexpr char s_licenseData[3][5] = {"SCEA", "SCEE", "SCEI"};
    static int modchip_hysteresis = 0;

    // Gather all conditions in one place
    const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
    const bool isDataDisc = g_discImage.hasData();
    const bool soctDisabled = !mechCommand.getSoct();
    const bool gfsSet = mechCommand.getSens(SENS::GFS);
    const bool shouldActivateModchip = inWobbleGroove && gfsSet && soctDisabled && isDataDisc;
    const uint64_t timeElapsed = time_us_64() - m_modchipTimer;

    // Helper function to wait with abort conditions
    auto waitWithAbort = [&mechCommand, this](const uint64_t waitTimeUS) -> bool {
        const uint64_t startTime = time_us_64();
        while ((time_us_64() - startTime) < waitTimeUS) {
            const int sector = g_driveMechanics.getSector();
            const bool inWobbleGroove = (sector > 0) && (sector < c_leadIn);
            const bool soctDisabled = !mechCommand.getSoct();
            const bool gfsSet = mechCommand.getSens(SENS::GFS);

            if (!soctDisabled || !gfsSet || !inWobbleGroove) {
                return false;
            }
            sleep_us(50);
        }
        return true;
    };

    if (shouldActivateModchip) {
        if (timeElapsed > 13333) {
            modchip_hysteresis++;
            m_modchipTimer = time_us_64();

            if (modchip_hysteresis > 100) {
                modchip_hysteresis = 0;
                DEBUG_PRINT("+SCEX\n");

                // Send the 3 license strings, twice each
                for (int i = 0; i < 6 && waitWithAbort(90000U); i++) {
                    DEBUG_PRINT("%s\n", s_licenseData[i % 3]);
                    uart_puts(uart1, s_licenseData[i % 3]);
                    uart_tx_wait_blocking(uart1);
                }

                endLicenseSequence();
                mechCommand.setBootSectorPattern(255);
            }
        }
    } else {
        modchip_hysteresis = 0;
        m_modchipTimer = time_us_64();
    }
};
