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

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
	SR_CONF_ELECTRONIC_LOAD,
	//SR_CONF_MULTIMETER,
	//SR_CONF_SIGNAL_GENERATOR,
	//SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(5),
	SR_HZ(10),
	SR_HZ(50),
	SR_HZ(100),
};

static struct sr_dev_driver ct_lab_driver_info;

static const struct ctlab_module supported_modules[] = {
	{ CTLAB_DCG, 0, 0, "DCG" },
	{ CTLAB_EDL, 0, 0, "EDL" },
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	GSList *l, *devices;
	int len, line, i, num_channels;
	const char *conn, *serialcomm;
	char *buf, **tokens, **lines;

	devices = NULL;
	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	if (serial_write_blocking(serial, "*:IDN?\r\n", 8, SERIAL_WRITE_TIMEOUT_MS) < 8) {
		sr_err("Unable to send identification string.");
		return NULL;
	}

	len = 1024;
	buf = g_malloc(len);

	if (serial_read_blocking(serial, buf, len, 250) > 0 && len > 0) {
    	lines = g_strsplit(buf, "\r\n", 8);
		line = 0;
		num_channels = 0;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->channel_groups = NULL;
		devc = g_malloc0(sizeof(struct dev_context));

		while (line < 8 && lines[line]) {
			tokens = g_strsplit(lines[line], " ", 3);
			if (tokens[0] && tokens[0][0] == '#' && strncmp(":254=", tokens[0] + 2, 5) == 0 && 
			      tokens[1] && tokens[2]) {
				for (i = 0; supported_modules[i].module_type; i++) {
					if (strcmp(supported_modules[i].module_name, tokens[1] + 1))
						continue;			

					struct ctlab_module* cur_module = &devc->modules[devc->num_modules];
					cur_module->module_type = supported_modules[i].module_type;
					cur_module->ct_lab_channel = tokens[0][1] - 48;
					cur_module->sr_start_channel = num_channels;
					snprintf(cur_module->module_name, 32, "%s v%s", supported_modules[i].module_name, tokens[0]+7);
					
					switch (supported_modules[i].module_type) {
					case CTLAB_DCG:
					case CTLAB_EDL:
						cg = sr_channel_group_new(sdi, NULL, NULL);
						cg->name = g_strdup_printf("%d:%s", cur_module->ct_lab_channel, supported_modules[i].module_name);
						cg->priv = g_variant_new_int32(devc->num_modules);
						char ch_name[32];
						sprintf(ch_name, "V (%d:%s)", cur_module->ct_lab_channel, supported_modules[i].module_name);
						ch = sr_channel_new(sdi, num_channels++, SR_CHANNEL_ANALOG, TRUE, ch_name);
						cg->channels = g_slist_append(cg->channels, ch);
						sprintf(ch_name, "I (%d:%s)", cur_module->ct_lab_channel, supported_modules[i].module_name);
						ch = sr_channel_new(sdi, num_channels++, SR_CHANNEL_ANALOG, TRUE, ch_name);
						cg->channels = g_slist_append(cg->channels, ch);
						sprintf(ch_name, "P (%d:%s)", cur_module->ct_lab_channel, supported_modules[i].module_name);
						ch = sr_channel_new(sdi, num_channels++, SR_CHANNEL_ANALOG, TRUE, ch_name);
						cg->channels = g_slist_append(cg->channels, ch);
						sprintf(ch_name, "Temp (%d:%s)", cur_module->ct_lab_channel, supported_modules[i].module_name);
						ch = sr_channel_new(sdi, num_channels++, SR_CHANNEL_ANALOG, TRUE, ch_name);
						cg->channels = g_slist_append(cg->channels, ch);
					   break;
					default:
					}
					devc->num_modules++;
					break;
				}
			}
			g_strfreev(tokens);
			++line;
		}
		g_strfreev(lines);

		if (num_channels) {
			devc->cur_samplerate = 1;
			devc->acq_start_us = 0;
			devc->num_samples = 0;
			devc->remaining[0] = '\0';
			
			sdi->vendor = g_strdup("ct-lab");
			sdi->model = g_strdup("");	
			sdi->version = g_strdup("");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_SERIAL;
			sdi->conn = serial;
			sdi->priv = devc;
			sr_sw_limits_init(&devc->limits);
			devices = g_slist_append(devices, sdi);
		}
		else {
			g_free(devc);
			g_free(sdi);		
		}
	}
	
	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct ctlab_module* cur_module = NULL;

	devc = sdi->priv;

	if (cg) {
		int module_idx = g_variant_get_int32(cg->priv);
		if (module_idx>= 0 && module_idx < devc->num_modules) {
			cur_module = &devc->modules[module_idx];
		}
		else {
			sr_err("Unable to get channel group data");
			return SR_ERR_NA;
		}
	}

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		*data = g_variant_new_boolean(1);
		break;
	case SR_CONF_VOLTAGE:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "MSV?", 10));
		break;
	case SR_CONF_CURRENT:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "MSA?", cur_module->module_type == CTLAB_DCG ? 11 : 16));
		break;
	case SR_CONF_POWER:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "MSW?", 18));
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "DCV?", cur_module->module_type == CTLAB_DCG ? 0 : 4));
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "DCA?", 1));
		break;
	case SR_CONF_POWER_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "DCP?", 3));
		break;
	case SR_CONF_RESISTANCE_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		*data = g_variant_new_double(ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, "DCR?", 5));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t samplerate;
	char cmd[128];
	struct ctlab_module* cur_module = NULL;

	devc = sdi->priv;

	if (cg) {
		int module_idx = g_variant_get_int32(cg->priv);
		if (module_idx>= 0 && module_idx < devc->num_modules) {
			cur_module = &devc->modules[module_idx];
		}
		else {
			sr_err("Unable to get channel group data");
			return SR_ERR_NA;
		}
	}

	switch (key) {
	case SR_CONF_SAMPLERATE:
		samplerate = g_variant_get_uint64(data);
		if (samplerate < samplerates[0] || samplerate > samplerates[1])
			return SR_ERR_ARG;
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_ENABLED:
		break;
	case SR_CONF_VOLTAGE_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		sprintf(cmd, "DCV=%f!", g_variant_get_double(data));
		if (ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, cmd, 255) != 0) {
			sr_err("Unable to send command");
		}
		break;
	case SR_CONF_CURRENT_LIMIT:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		sprintf(cmd, "DCA=%f!", g_variant_get_double(data));
		if (ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, cmd, 255) != 0) {
			sr_err("Unable to send command");
		}
		break;
	case SR_CONF_POWER_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		sprintf(cmd, "DCP=%f!", g_variant_get_double(data));
		if (ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, cmd, 255) != 0) {
			sr_err("Unable to send command");
		}
		break;
	case SR_CONF_RESISTANCE_TARGET:
		if (!cg || !cur_module) {
			return SR_ERR_NA;
		}
		sprintf(cmd, "DCR=%f!", g_variant_get_double(data));
		if (ct_lab_send_cmd(sdi, cur_module->ct_lab_channel, cmd, 255) != 0) {
			sr_err("Unable to send command");
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint32_t devopts_cg[8];
	int num_devopts_cg = 0;
	struct ctlab_module* cur_module = NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		if (!sdi) {
			return SR_ERR_NA;
		}
		devc = sdi->priv;
		int module_idx = g_variant_get_int32(cg->priv);
		if (module_idx>= 0 && module_idx < devc->num_modules) {
			cur_module = &devc->modules[module_idx];
		}
		else {
			sr_err("Unable to get channel group data");
			return SR_ERR_NA;
		}

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			//devopts_cg[num_devopts_cg++] = SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET;
			devopts_cg[num_devopts_cg++] = SR_CONF_VOLTAGE | SR_CONF_GET;
			devopts_cg[num_devopts_cg++] = SR_CONF_CURRENT | SR_CONF_GET;
			devopts_cg[num_devopts_cg++] = SR_CONF_POWER | SR_CONF_GET;
			devopts_cg[num_devopts_cg++] = SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST;
			devopts_cg[num_devopts_cg++] = SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST;
			if (cur_module->module_type == CTLAB_EDL) {
				devopts_cg[num_devopts_cg++] = SR_CONF_POWER_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST;
				devopts_cg[num_devopts_cg++] = SR_CONF_RESISTANCE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST;
			}
			*data = std_gvar_array_u32(devopts_cg, num_devopts_cg);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = std_gvar_min_max_step(0, 20, 0.01);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = std_gvar_min_max_step(0, 2, 0.01);
			break;
		case SR_CONF_POWER_TARGET:
			*data = std_gvar_min_max_step(0, 40, 0.01);
			break;
		case SR_CONF_RESISTANCE_TARGET:
			*data = std_gvar_min_max_step(0, 10000, 0.01);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	devc = sdi->priv;

	/* Do not start polling device here, the read function will do it in 100 ms. */

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 100,
			ct_lab_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver ct_lab_driver_info = {
	.name = "ct-lab",
	.longname = "c't Lab",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(ct_lab_driver_info);
