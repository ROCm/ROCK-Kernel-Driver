/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/*
 *  Minimums, maximums, defaults, and other definitions for MC_PARAMS.
 */

#define FO_INSPECTION_INTERVAL_MIN                     0
#define FO_INSPECTION_INTERVAL_MAX               1000000
#define FO_INSPECTION_INTERVAL_DEF                   600

#define FO_MAX_PATHS_PER_DEVICE_MIN                    1
#define FO_MAX_PATHS_PER_DEVICE_MAX                    8
#define FO_MAX_PATHS_PER_DEVICE_DEF                    8

#define FO_MAX_RETRIES_PER_PATH_MIN                    1
#define FO_MAX_RETRIES_PER_PATH_MAX                    8
#define FO_MAX_RETRIES_PER_PATH_DEF                    3

#define FO_MAX_RETRIES_PER_IO_MIN          ((FO_MAX_PATHS_PER_DEVICE_MIN * FO_MAX_RETRIES_PER_PATH_MIN) + 1)
#define FO_MAX_RETRIES_PER_IO_MAX          ((FO_MAX_PATHS_PER_DEVICE_MAX * FO_MAX_RETRIES_PER_PATH_MAX) + 1)
#define FO_MAX_RETRIES_PER_IO_DEF          ((FO_MAX_PATHS_PER_DEVICE_DEF * FO_MAX_RETRIES_PER_PATH_DEF) + 1)

#define FO_DEVICE_ERROR_THRESHOLD_MIN                  1
#define FO_DEVICE_ERROR_THRESHOLD_MAX                255
#define FO_DEVICE_ERROR_THRESHOLD_DEF                  4

#define FO_DEVICE_TIMEOUT_THRESHOLD_MIN                1
#define FO_DEVICE_TIMEOUT_THRESHOLD_MAX              255
#define FO_DEVICE_TIMEOUT_THRESHOLD_DEF                4

#define FO_FRAME_ERROR_THRESHOLD_MIN                   1
#define FO_FRAME_ERROR_THRESHOLD_MAX                 255
#define FO_FRAME_ERROR_THRESHOLD_DEF                   4

#define FO_LINK_ERROR_THRESHOLD_MIN                    1
#define FO_LINK_ERROR_THRESHOLD_MAX                  255
#define FO_LINK_ERROR_THRESHOLD_DEF                    4

#define FO_ROLLING_AVERAGE_INTERVALS_MIN               1
#define FO_ROLLING_AVERAGE_INTERVALS_MAX              10
#define FO_ROLLING_AVERAGE_INTERVALS_DEF               1

#define FO_MAX_DEVICES_TO_MIGRATE_MIN                  0
#define FO_MAX_DEVICES_TO_MIGRATE_MAX                255
#define FO_MAX_DEVICES_TO_MIGRATE_DEF                  4

#define FO_BALANCE_METHOD_NONE                         0
#define FO_BALANCE_METHOD_IOS                          1
#define FO_BALANCE_METHOD_MBS                          2

#define FO_BALANCE_METHOD_MIN                      FO_BALANCE_METHOD_NONE
#define FO_BALANCE_METHOD_MAX                      FO_BALANCE_METHOD_MBS
#define FO_BALANCE_METHOD_DEF                      FO_BALANCE_METHOD_IOS

#define FO_LOAD_SHARE_MIN_PERCENTAGE_MIN              25
#define FO_LOAD_SHARE_MIN_PERCENTAGE_MAX              99
#define FO_LOAD_SHARE_MIN_PERCENTAGE_DEF              75

#define FO_LOAD_SHARE_MAX_PERCENTAGE_MIN             101
#define FO_LOAD_SHARE_MAX_PERCENTAGE_MAX             500
#define FO_LOAD_SHARE_MAX_PERCENTAGE_DEF             150

#define FO_NOTIFY_TYPE_NONE                   0
#define FO_NOTIFY_TYPE_LUN_RESET              1
#define FO_NOTIFY_TYPE_CDB                    2
#define FO_NOTIFY_TYPE_LOGOUT_OR_LUN_RESET    3
#define FO_NOTIFY_TYPE_LOGOUT_OR_CDB          4
#define FO_NOTIFY_TYPE_SPINUP		      5

#define FO_NOTIFY_TYPE_MIN                FO_NOTIFY_TYPE_NONE
#define FO_NOTIFY_TYPE_MAX                FO_NOTIFY_TYPE_LOGOUT_OR_CDB
#define FO_NOTIFY_TYPE_DEF                FO_NOTIFY_TYPE_NONE

#define FO_NOTIFY_CDB_LENGTH_MIN              6
#define FO_NOTIFY_CDB_LENGTH_MAX             16

