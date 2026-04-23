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

#ifndef CEMOCO_CALI_H
#define CEMOCO_CALI_H

struct cali_params
{
    float adc_Vin_coeffs[2];
    float adc_Iin_coeffs[2];
    float adc_Vout_coeffs[2];
    float adc_Iout_coeffs[2];
    float adc_iL_coeffs[2];
    float adc_temp_coeffs[2];

    float vout_drop_compensation_k;
};

#endif //CEMOCO_CALI_H
