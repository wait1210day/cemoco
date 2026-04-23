/**
 * This file is part of cemoco.
 *
 * cemoco is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * cemoco is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cemoco. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef CEMOCO_SENSORS_H
#define CEMOCO_SENSORS_H

#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_ll_dma.h"

#include "ewma_filter.h"
#include "cali.h"

struct ctrloop_measures;

struct sensors_measures
{
    // Input and output voltage, in volts
    float Vin;
    float Vout;

    // Input and output current, in amps
    float Iin;
    float Iout;
    uint16_t Iin_raw;

    // Input and output power, in watts
    float Pin;
    float Pout;

    // Realtime efficiency normalized in [0, 1]
    float efficiency;

    // Temperature in degC
    int32_t temp;
};

struct sensors_context
{
    const struct cali_params *cali_params;

    struct ewma_filter Vin_filter;
    struct ewma_filter Vout_filter;
    struct ewma_filter Iin_filter;
    struct ewma_filter Iout_filter;
    struct ewma_filter temp_filter;

    struct sensors_measures results;
};

struct sensors_config
{
    const struct cali_params *cali_params;
};

/**
 * Sensors module is a data filtering and postprocess layer on the top
 * of `ctrloop` module, which provides the raw (unfiltered and very fast)
 * measurements.
 *
 * @param ctx     A context structure to be initialized.
 * @param config  Config parameters.
 */
err_t sensors_init(struct sensors_context *ctx, const struct sensors_config *config);

/**
 * Collects all the measurements from the managed ADC conversion group
 * and data from ctrloop.
 *
 * This function can be called in a timer ISR.
 *
 * @param ctx           Context structure.
 * @param ctrloop_data  Fast, unfiltered measurements given by ctrloop.
 */
void sensors_update(struct sensors_context *ctx, struct ctrloop_measures *ctrloop_data);

static inline struct sensors_measures sensors_get_measures(struct sensors_context *ctx)
{
    return ctx->results;
}

#endif //CEMOCO_SENSORS_H
