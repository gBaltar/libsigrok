/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Benjamin Langmann <b.langmann@gmx.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_CT_LAB_PROTOCOL_H
#define LIBSIGROK_HARDWARE_CT_LAB_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ct-lab"

#define SERIALCOMM "38400/8n1"
#define SERIAL_WRITE_TIMEOUT_MS 50
#define MAX_NUM_MODULES 8
#define MAX_BUFFERED_LINES 16
#define MAX_LINE_LENGTH 32

/* Supported modules */
enum {
	CTLAB_DCG = 1,
	CTLAB_EDL,
};

struct ctlab_module {
	int module_type;
	int ct_lab_channel;
	int sr_start_channel;
	char module_name[16];
};

struct dev_context {
    struct ctlab_module modules[MAX_NUM_MODULES];
    int num_modules;
	struct sr_sw_limits limits;
    uint64_t cur_samplerate;
	int64_t acq_start_us;
	int64_t num_samples;
	char remaining[MAX_LINE_LENGTH];
};

SR_PRIV int ct_lab_receive_data(int fd, int revents, void *cb_data);

double ct_lab_send_cmd(const struct sr_dev_inst *sdi, int ct_lab_module, const char *cmd, int ret_subchannel);

#endif
