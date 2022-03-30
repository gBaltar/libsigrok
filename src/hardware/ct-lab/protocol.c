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

#include <config.h>
#include "protocol.h"
#include <math.h>


double ct_lab_send_cmd(const struct sr_dev_inst *sdi, int ct_lab_module, const char *cmd, int ret_subchannel) {
	const struct sr_serial_dev_inst *serial;
	int len;
	char line[MAX_LINE_LENGTH];

	serial = sdi->conn;
	len = snprintf(line, sizeof(line), "%d:%s\r\n", ct_lab_module, cmd);

	if (serial_write_blocking(serial, line, len, SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send command.");
		serial_read_nonblocking(serial, line, sizeof(line));
		return NAN;
	}

	int num_chr_read = serial_read_blocking(serial, line, sizeof(line), 50);
	
	char *val = strchr(line, '=');

	if (strlen(line) < 5 || line[0] != '#' || line[1] < '0' || line[1] > '7' || line[2] != ':' || !val) {
		printf("Error parsing %s", line);
		return NAN;
	}

	val[0] = '\0';
	line[2] = '\0';
	
	int ct_lab_channel = atoi(line+1);
	int ct_lab_subchannel = atoi(line+3);
	double ret_val = atof(val+1);

	if (ct_lab_module != ct_lab_channel) {
		printf("Read response for wrong module: %d != %d\n", ct_lab_channel, ct_lab_module);
		return NAN;
	}
	if (ct_lab_subchannel != ret_subchannel) {
		printf("Did not receive response for command: %s, %d != %d\n", cmd, ret_subchannel, ct_lab_subchannel);
		return NAN;
	}
	return ret_val;
}

void ct_lab_send_querry(struct sr_dev_inst *sdi, int ct_lab_module, const char *querry) {
	struct sr_serial_dev_inst *serial;
	int len;
	char buf[MAX_LINE_LENGTH];

	serial = sdi->conn;
	len = snprintf(buf, sizeof(buf), "%d:%s?\r\n", ct_lab_module, querry);

	if (serial_write_blocking(serial, buf, len, SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send querry.");
		serial_read_nonblocking(serial, buf, sizeof(buf));
		return;
	}
}

void ct_lab_parse_line(struct sr_dev_inst *sdi, char *line) {
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	
	devc = sdi->priv;
		
	char *val = strchr(line, '=');

	if (strlen(line) < 5 || line[0] != '#' || line[1] < '0' || line[1] > '7' || line[2] != ':' || !val) {
		printf("Parse error\n");
		return;
	}

	val[0] = '\0';
	line[2] = '\0';
	
	int ct_lab_channel = atoi(line+1);
	int ct_lab_subchannel = atoi(line+3);
	float fvalue = atof(val+1);

	int sr_channel = -1;
	sr_analog_init(&analog, &encoding, &meaning, &spec, 4);
	analog.data = &fvalue;
	analog.num_samples = 1;

	for (int module_idx = 0; module_idx < devc->num_modules; module_idx++) {
		struct ctlab_module* cur_module = &devc->modules[module_idx];
		if (cur_module->ct_lab_channel != ct_lab_channel) 
			continue;
		
		switch (cur_module->module_type) {
		case CTLAB_DCG:
		case CTLAB_EDL:
				switch (ct_lab_subchannel) {
				case 10:
					analog.meaning->mq = SR_MQ_VOLTAGE;
					analog.meaning->mqflags = SR_MQFLAG_DC;
					analog.meaning->unit = SR_UNIT_VOLT;
					sr_channel = cur_module->sr_start_channel + 0; break;
				case 11:
					analog.meaning->mq = SR_MQ_CURRENT;
					analog.meaning->mqflags = SR_MQFLAG_DC;
					analog.meaning->unit = SR_UNIT_AMPERE;
					sr_channel = cur_module->sr_start_channel + 1; break;
				case 18:
					analog.meaning->mq = SR_MQ_POWER;
					//analog.meaning->mqflags = SR_MQFLAG_DC;
					analog.meaning->unit = SR_UNIT_WATT;
					sr_channel = cur_module->sr_start_channel + 2; break;
				case 233:
					analog.meaning->mq = SR_MQ_TEMPERATURE;
					analog.meaning->unit = SR_UNIT_CELSIUS;
					sr_channel = cur_module->sr_start_channel + 3; break;
				}
			break;
		default:
		}	
	}

	if (sr_channel < 0 || sr_channel >= g_slist_length(sdi->channels)) {
		printf("Cannot find channel for module %d channel %d\n", ct_lab_channel, ct_lab_subchannel);
		return;
	}

	analog.meaning->channels = g_slist_append(NULL, g_slist_nth_data(sdi->channels, sr_channel));
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	sr_sw_limits_update_samples_read(&devc->limits, 1);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int ct_lab_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents != G_IO_IN) {
		/*return FALSE;*/
	}

	int64_t now_us = g_get_monotonic_time();
	if (!devc->acq_start_us) {
		devc->acq_start_us = now_us;
	}
	int64_t elapsed_us = now_us - devc->acq_start_us;

	if (elapsed_us >= devc->num_samples * 1000000 / devc->cur_samplerate) {
		for (int module_idx = 0; module_idx < devc->num_modules; module_idx++) {
			struct ctlab_module* cur_module = &devc->modules[module_idx];

			switch (cur_module->module_type) {
			case CTLAB_DCG:
			case CTLAB_EDL:
				ct_lab_send_querry(sdi, cur_module->ct_lab_channel, "MSV");
				ct_lab_send_querry(sdi, cur_module->ct_lab_channel, "MSA");
				ct_lab_send_querry(sdi, cur_module->ct_lab_channel, "MSW");
				ct_lab_send_querry(sdi, cur_module->ct_lab_channel, "TMP");
				break;
			default:
			}	
		}

		if (sr_sw_limits_check(&devc->limits))
			sr_dev_acquisition_stop(sdi);

		++devc->num_samples;
	}

	char buf[MAX_BUFFERED_LINES*MAX_LINE_LENGTH];
	char **lines;
	int line = 0;

	serial = sdi->conn;
	devc = sdi->priv;

	strcpy(buf, devc->remaining);
	int num_chr_remaining = strlen(buf);
	int num_chr_read = serial_read_nonblocking(serial, buf + num_chr_remaining, sizeof(buf) - num_chr_remaining);
	buf[num_chr_remaining + num_chr_read] = '\0';
	if (num_chr_read) {
		devc->remaining[0] = '\0';
	}
	else {
		return TRUE;
	}
	lines = g_strsplit(buf, "\r\n", 0);
	while (lines[line] && lines[line+1]) {
		ct_lab_parse_line(sdi, lines[line]);
		++line;
	}
	if (lines[line] && strlen(lines[line]) > 0) {
		strcpy(devc->remaining, lines[line]);
	}
	g_strfreev(lines);

	return TRUE;
}
