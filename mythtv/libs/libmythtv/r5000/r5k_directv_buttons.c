/* Copyright 2008 Alan Nisota <alannisota@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

//Support for DirectTV remotes

#include "r5000_internal.h"

struct r5000_buttons directv_button_cmd =
{
0x49, //len
400000, //delay
//button 0:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 1:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 2:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 3:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 4:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 5:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 6:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 7:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 8:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//button 9:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//Clear:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//Enter:
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
//Power
{
    0x00, 0x43, 0x0a, 0x80, 0xca, 0x00, 0x5a, 0x2d, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05,
    0x05, 0x0f, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x0f, 0x05, 0x05, 0x05, 0x0f,
    0x05, 0x05, 0x05, 0x0f, 0x05, 0x0f, 0x05, 0x0f, 0x05,
},
};
