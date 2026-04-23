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

#ifndef CEMOCO_HERMES_MESSAGES_H
#define CEMOCO_HERMES_MESSAGES_H

#include "hermes.h"
#include "ctrloop.h"

enum hermes_topic_enum
{
    HERMES_TOPIC_CONVERTER_STAT,
    HERMES_TOPIC_ELEC_MEASURES,
    HERMES_TOPIC_OUTPUT_SET,
    HERMES_TOPIC_OUTPUT_ENABLE,
    HERMES_TOPIC_SOFTPROT_SET,
    HERMES_TOPIC_SOFTPROT_CLEAR
};

enum convstat_softprot
{
    CONVSTAT_SOFTPROT_NONE = 0x00,
    CONVSTAT_SOFTPROT_IN_UVP = 0x01,
    CONVSTAT_SOFTPROT_IN_OCP = 0x02,
    CONVSTAT_SOFTPROT_OUT_OVP = 0x04,
    CONVSTAT_SOFTPROT_OUT_OCP = 0x08,
    CONVSTAT_SOFTPROT_TEMP_DERATE = 0x10
};

enum convstat_fault
{
    CONVSTAT_FAULT_NONE = 0x00,
    CONVSTAT_FAULT_CBCOCP = 0x01,
    CONVSTAT_FAULT_IN_OVP = 0x02,
    CONVSTAT_FAULT_OTP = 0x04
};

struct hermes_msg_converter_stat
{
    enum ctrloop_active_loop active_loop;
    float burst_ratio;
    uint32_t soft_prot_flags;
    uint32_t fault_flags;
};

enum softprot_recovery_policy
{
    SOFTPROT_RECOVERY_NONE = 0,
    SOFTPROT_RECOVERY_HICCUP,
};

struct hermes_msg_softprot_conf
{
    float output_ovp;
    float output_ocp;
    float input_uvp;
    float input_ocp;
    enum softprot_recovery_policy output_ovp_recover;
    enum softprot_recovery_policy output_ocp_recover;
    enum softprot_recovery_policy input_uvp_recover;
    enum softprot_recovery_policy input_ocp_recover;
};

struct hermes_msg_softprot_clear
{
    uint32_t flags_to_clear;
};

struct hermes_msg_elec_measures
{
    float iL;
    float Vin;
    float Iin;
    float Pin;
    float Vout;
    float Iout;
    float Pout;
    float efficiency;
    int32_t temp;

    uint16_t Iin_raw;
};

struct hermes_msg_output_set
{
    float Vout;
    float Iout;
};

struct hermes_msg_output_enable
{
    uint8_t enable;
};

#endif //CEMOCO_HERMES_MESSAGES_H
