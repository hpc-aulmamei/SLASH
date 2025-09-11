/**
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef LIBSLASH_DRIVER_ERRORS_H
#define LIBSLASH_DRIVER_ERRORS_H

#include <stdint.h>

enum slash_error {
    SLASH_ERROR_OK,
    SLASH_ERROR_SYS, /* Check errno */
};

#endif /* LIBSLASH_DRIVER_ERRORS_H */
