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

#include "ewma_filter.h"

void ewma_filter_init(struct ewma_filter *ctx, uint8_t shift, int32_t initial_value)
{
    if (!ctx)
        return;

    ctx->shift = shift;
    // To fill up the filer, avoiding the "climbing" process of filter.
    ctx->accumulator = (int64_t) initial_value << shift;
}

int32_t ewma_filter_update(struct ewma_filter *ctx, int32_t input)
{
    if (!ctx)
        return 0;

    int64_t acc = ctx->accumulator;
    uint8_t shift = ctx->shift;

    acc += ((int64_t)input - (acc >> shift));
    ctx->accumulator = acc;

    if (shift > 0)
        return (int32_t)((acc + (1LL << (shift - 1))) >> shift);

    return (int32_t) acc;
}
