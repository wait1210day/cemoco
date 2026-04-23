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

#ifndef CEMOCO_HOSTIF_PROTOCOL_H
#define CEMOCO_HOSTIF_PROTOCOL_H


// Protocol Notes:
//   1. all the data are encoded in big-endian format.
//   2. frame has fixed size 8 bytes
//

#define CAN_MAKE_ID(pri, type, dst_addr, src_addr)  \
    ( ((uint32_t)(pri) & 0x1f)      << 24  |        \
      ((uint32_t)(type) & 0xff)     << 16  |        \
      ((uint32_t)(dst_addr) & 0xff) << 8   |        \
      ((uint32_t)(src_addr) & 0xff) )

#define CAN_ID_GET_PRI(id)  (((id) >> 24) & 0x1f)
#define CAN_ID_GET_TYPE(id) (((id) >> 16) & 0xff)
#define CAN_ID_GET_DSTADDR(id) (((id) >> 8) & 0xff)
#define CAN_ID_GET_SRCADDR(id) (((id) >> 0) & 0xff)

// CAN message priorities (determines arbitration)
#define ID_PRI_EMER         0x00
#define ID_PRI_HIGH         0x01
#define ID_PRI_NORMAL       0x02
#define ID_PRI_LOW          0x03

#define ID_DSTADDR_BROADCAST  0xff


// Protection stats.
// Format:
//   00 00 xx xx . 00 00 yy yy
//
// where xx: software protection flags.
//       yy: fault flags.
//
// Fault flags `HOSTIF_CAN_PROT_FLT_*`:
//   bit 0: input OVP
//       1: over temperature
//
// Software protection flags `HOSTIF_CAN_PROT_SWP_*`:
//   bit 0: input UVP
//       1: input OCP
//       2: output OVP
//       3: output OCP
//       4: temperature derating
//
#define ID_TYPE_PROT_EVENT  0x01

// Enable or disable the PSU output.
// Format:
//   00 00 00 00 . 00 00 00 xx
//
// where xx: output will be disabled if this byte is 0, otherwise, enable.
//
#define ID_TYPE_OUT_ENABLE  0x10

// Set the CV/CC setpoint. Has no effect if data is invalid.
// Format:
//   00 00 xx xx . 00 00 yy yy
//
// where xx: CC setpoint x1000 (e.g. `0x17 70` = 6000 => 6.00A)
//       yy: CV setpoint x1000
#define ID_TYPE_OUT_SET     0x11

// Output electrical measurements.
// Format:
//   00 ll vv vv . ii ii pp pp
//
// where ll: currently active loop (0 = none, 1 = burst mode, 2 = CV, 3 = CC)
//       vv: output voltage in Volt x1000
//       ii: output current in Amp x1000
//       pp: output power in Watt x10
#define ID_TYPE_STAT_OUT    0x12

// Input electrical measurements.
// Format:
//   00 00 vv vv . ii ii pp pp
//
// where vv: input voltage in Volt x1000
//       ii: input current in Amp x1000
//       pp: input power in Watt x10
#define ID_TYPE_STAT_IN     0x13

// Miscellaneous information.
// Format:
//   ee ee tt bb . 00 00 00 00
//
// where ee: normalized efficiency (0~1 range) x1000 (e.g. `0x03 8e` = 910 = 91.0%)
//       tt: temperature in Celsius degree, two's complement code
//       bb: Burst Ratio x100
#define ID_TYPE_STAT_MISC1  0x14

// To make a CAN ID:
//   `CAN_MAKE_ID(ID_PRI_NORMAL, ID_TYPE_STAT_OUT, ID_DSTADDR_BROADCAST, node_addr)`


#define HOSTIF_CAN_PROT_FLT_IN_OVP  (1 << 0)
#define HOSTIF_CAN_PROT_FLT_OTP     (1 << 1)

#define HOSTIF_CAN_PROT_SWP_IN_UVP  (1 << 0)
#define HOSTIF_CAN_PROT_SWP_IN_OCP  (1 << 1)
#define HOSTIF_CAN_PROT_SWP_OUT_OVP (1 << 2)
#define HOSTIF_CAN_PROT_SWP_OUT_OCP (1 << 3)
#define HOSTIF_CAN_PROT_SWP_DERATE  (1 << 4)


#endif //CEMOCO_HOSTIF_PROTOCOL_H
