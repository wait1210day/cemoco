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

#pragma once

#include <stdint.h>

struct ewma_filter
{
    int64_t accumulator;
    uint8_t shift;
};

void ewma_filter_init(struct ewma_filter *filter, uint8_t shift, int32_t initial_value);
int32_t ewma_filter_update(struct ewma_filter *filter, int32_t data);
