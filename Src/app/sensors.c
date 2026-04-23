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

#include <string.h>
#include <math.h>

#include "stm32g4xx_ll_adc.h"
#include "stm32g4xx_ll_dma.h"

#include "ewma_filter.h"
#include "ll_helper.h"
#include "ctrloop.h"
#include "sensors.h"

#define TO_FIXED_I32(x)         ((int32_t)((x) * 1000.0f))
#define FROM_FIXED_I32(x)       ((float)(x) / 1000.0f)

#define ROUND_100x(x)           (roundf(x * 100.0f) / 100.0f)

static struct ewma_filter flt;

err_t sensors_init(struct sensors_context *ctx, const struct sensors_config *config)
{
    ewma_filter_init(&flt, 6, 0);

    ctx->cali_params = config->cali_params;

    ewma_filter_init(&ctx->Vin_filter, 3, 0);
    ewma_filter_init(&ctx->Iin_filter, 6, 0);
    ewma_filter_init(&ctx->Vout_filter, 3, 0);
    ewma_filter_init(&ctx->Iout_filter, 6, 0);
    ewma_filter_init(&ctx->temp_filter, 4, 0);
    memset(&ctx->results, 0, sizeof(ctx->results));

    return ERR_STATUS_SUCCESS;
}

void sensors_update(struct sensors_context *ctx, struct ctrloop_measures *ctrloop_data)
{
    const struct cali_params *cali = ctx->cali_params;

    // Convert the remaining measurements
    const float Iin = (float) ctrloop_data->Iin_raw * cali->adc_Iin_coeffs[0] + cali->adc_Iin_coeffs[1];
    const int32_t temp = (int32_t)
        ((float) ctrloop_data->temp_raw * cali->adc_temp_coeffs[0] + cali->adc_temp_coeffs[1]);

    const int32_t Vout_fixed = TO_FIXED_I32(ctrloop_data->Vout),
                  Iout_fixed = TO_FIXED_I32(ctrloop_data->Iout),
                  Vin_fixed = TO_FIXED_I32(ctrloop_data->Vin),
                  Iin_fixed = TO_FIXED_I32(Iin);

    struct sensors_measures filtered;
    filtered.temp = ewma_filter_update(&ctx->temp_filter, temp);
    filtered.Vin = FROM_FIXED_I32(ewma_filter_update(&ctx->Vin_filter, Vin_fixed));
    filtered.Iin = FROM_FIXED_I32(ewma_filter_update(&ctx->Iin_filter, Iin_fixed));
    filtered.Vout = FROM_FIXED_I32(ewma_filter_update(&ctx->Vout_filter, Vout_fixed));
    filtered.Iout = FROM_FIXED_I32(ewma_filter_update(&ctx->Iout_filter, Iout_fixed));

    filtered.Iin_raw = ewma_filter_update(&flt, ctrloop_data->Iin_raw);

    filtered.Vin = ROUND_100x(filtered.Vin);
    filtered.Iin = ROUND_100x(filtered.Iin);
    filtered.Vout = ROUND_100x(filtered.Vout);
    filtered.Iout = ROUND_100x(filtered.Iout);

    if (filtered.Iin <= 0)
        filtered.Iin = 0;
    if (filtered.Iout <= 0)
        filtered.Iout = 0;

    filtered.Pin = filtered.Vin * filtered.Iin;
    filtered.Pout = filtered.Vout * filtered.Iout;

    if (filtered.Pin >= CEMOCO_EFF_MEASURE_P_IN_THRESH)
        filtered.efficiency = filtered.Pout / filtered.Pin;
    else
        filtered.efficiency = 0;

    memcpy(&ctx->results, &filtered, sizeof(struct sensors_measures));
}
