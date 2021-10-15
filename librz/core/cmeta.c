// SPDX-FileCopyrightText: 2008-2020 nibble <nibble.ds@gmail.com>
// SPDX-FileCopyrightText: 2008-2020 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2008-2020 thestr4ng3r <info@florianmaerkl.de>
// SPDX-FileCopyrightText: 2021 Anton Kochkov <anton.kochkov@gmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_core.h>

static char *meta_string_escape(RzCore *core, RzAnalysisMetaItem *mi) {
	char *esc_str = NULL;
	RzStrEscOptions opt = { 0 };
	opt.show_asciidot = false;
	opt.esc_bslash = core->print->esc_bslash;
	switch (mi->subtype) {
	case RZ_STRING_ENC_UTF16LE:
	case RZ_STRING_ENC_UTF16BE:
	case RZ_STRING_ENC_UTF32LE:
	case RZ_STRING_ENC_UTF32BE:
	case RZ_STRING_ENC_UTF8:
		// All strings that are put into the metadata are already converted
		esc_str = rz_str_escape_utf8(mi->str, &opt);
		break;
	case RZ_STRING_ENC_LATIN1:
		esc_str = rz_str_escape_latin1(mi->str, false, &opt);
		break;
	default:
		rz_warn_if_reached();
	}
	return esc_str;
}

RZ_IPI void rz_core_meta_print(RzCore *core, RzAnalysisMetaItem *d, ut64 start, ut64 size, bool show_full, RzCmdStateOutput *state) {
	if (rz_spaces_current(&core->analysis->meta_spaces) &&
		rz_spaces_current(&core->analysis->meta_spaces) != d->space) {
		return;
	}
	PJ *pj = state->d.pj;
	RzOutputMode mode = state->mode;
	char *pstr, *base64_str;
	char *str = NULL;
	if (d->str) {
		if (d->type == RZ_META_TYPE_STRING) {
			str = meta_string_escape(core, d);
		} else {
			str = rz_str_escape(d->str);
		}
	}
	if (str || d->type == RZ_META_TYPE_DATA) {
		if (d->type == RZ_META_TYPE_STRING && !*str) {
			free(str);
			return;
		}
		if (!str) {
			pstr = "";
		} else if (d->type == RZ_META_TYPE_FORMAT) {
			pstr = str;
		} else if (d->type == RZ_META_TYPE_STRING) {
			pstr = str;
		} else if (d->type == RZ_META_TYPE_VARTYPE) {
			// Sanitize (don't escape) Ct comments so we can see "char *", etc.
			free(str);
			str = strdup(d->str);
			rz_str_sanitize(str);
			pstr = str;
		} else if (d->type != RZ_META_TYPE_COMMENT) {
			rz_name_filter(str, 0, true);
			pstr = str;
		} else {
			pstr = d->str;
		}
		switch (mode) {
		case RZ_OUTPUT_MODE_JSON:
			pj_o(pj);
			pj_kn(pj, "offset", start);
			pj_ks(pj, "type", rz_meta_type_to_string(d->type));

			if (d->type == RZ_META_TYPE_HIGHLIGHT) {
				pj_k(pj, "color");
				ut8 r = 0, g = 0, b = 0, A = 0;
				const char *esc = strchr(d->str, '\x1b');
				if (esc) {
					rz_cons_rgb_parse(esc, &r, &g, &b, &A);
					char *rgb_str = rz_cons_rgb_tostring(r, g, b);
					base64_str = rz_base64_encode_dyn((const ut8 *)rgb_str, strlen(rgb_str));
					if (d->type == RZ_META_TYPE_STRING && base64_str) {
						pj_s(pj, base64_str);
						free(base64_str);
					} else {
						pj_s(pj, rgb_str);
					}
					free(rgb_str);
				} else {
					pj_s(pj, str);
				}
			} else {
				pj_k(pj, "name");
				if (d->type == RZ_META_TYPE_STRING && (base64_str = rz_base64_encode_dyn((const ut8 *)d->str, strlen(d->str)))) {
					pj_s(pj, base64_str);
				} else {
					pj_s(pj, rz_str_get(str));
				}
			}
			if (d->type == RZ_META_TYPE_DATA) {
				pj_kn(pj, "size", size);
			} else if (d->type == RZ_META_TYPE_STRING) {
				const char *enc = rz_str_enc_as_string(d->subtype);
				pj_ks(pj, "enc", enc);
				pj_kb(pj, "ascii", rz_str_is_ascii(d->str));
			}
			pj_end(pj);
			break;
		case RZ_OUTPUT_MODE_RIZIN:
		default:
			switch (d->type) {
			case RZ_META_TYPE_COMMENT: {
				const char *type = rz_meta_type_to_string(d->type);
				char *s = sdb_encode((const ut8 *)pstr, -1);
				if (!s) {
					s = strdup(pstr);
				}
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					if (!strcmp(type, "CCu")) {
						rz_cons_printf("%s base64:%s @ 0x%08" PFMT64x "\n",
							type, s, start);
					} else {
						rz_cons_printf("%s %s @ 0x%08" PFMT64x "\n",
							type, pstr, start);
					}
				} else {
					if (!strcmp(type, "CCu")) {
						char *mys = rz_str_escape(pstr);
						rz_cons_printf("0x%08" PFMT64x " %s \"%s\"\n",
							start, type, mys);
						free(mys);
					} else {
						rz_cons_printf("0x%08" PFMT64x " %s \"%s\"\n",
							start, type, pstr);
					}
				}
				free(s);
			} break;
			case RZ_META_TYPE_STRING:
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					char cmd[] = "Cs#";
					switch (d->subtype) {
					case RZ_STRING_ENC_LATIN1:
					case RZ_STRING_ENC_UTF8:
						cmd[2] = d->subtype;
						break;
					case RZ_STRING_ENC_UTF16LE:
					case RZ_STRING_ENC_UTF16BE:
						cmd[2] = 'w';
						break;
					case RZ_STRING_ENC_UTF32LE:
					case RZ_STRING_ENC_UTF32BE:
						cmd[2] = 'W';
						break;
					default:
						cmd[2] = 0;
					}
					rz_cons_printf("%s %" PFMT64u " @ 0x%08" PFMT64x " # %s\n",
						cmd, size, start, pstr);
				} else {
					const char *enc;
					if (d->subtype == RZ_STRING_ENC_LATIN1) {
						enc = rz_str_is_ascii(d->str) ? "ascii" : "latin1";
					} else {
						enc = rz_str_enc_as_string(d->subtype);
					}
					if (show_full || mode == RZ_OUTPUT_MODE_LONG) {
						rz_cons_printf("0x%08" PFMT64x " %s[%" PFMT64u "] \"%s\"\n",
							start, enc, size, pstr);
					} else if (mode == RZ_OUTPUT_MODE_STANDARD) {
						rz_cons_printf("%s[%" PFMT64u "] \"%s\"\n",
							enc, size, pstr);
					} else {
						rz_cons_printf("\"%s\"\n", pstr);
					}
				}
				break;
			case RZ_META_TYPE_HIDE:
			case RZ_META_TYPE_DATA:
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					rz_cons_printf("%s %" PFMT64u " @ 0x%08" PFMT64x "\n",
						rz_meta_type_to_string(d->type),
						size, start);
				} else {
					if (show_full || mode == RZ_OUTPUT_MODE_LONG) {
						const char *dtype = d->type == RZ_META_TYPE_HIDE ? "hidden" : "data";
						rz_cons_printf("0x%08" PFMT64x " %s %s %" PFMT64u "\n",
							start, dtype,
							rz_meta_type_to_string(d->type), size);
					} else {
						rz_cons_printf("%" PFMT64u "\n", size);
					}
				}
				break;
			case RZ_META_TYPE_MAGIC:
			case RZ_META_TYPE_FORMAT:
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					rz_cons_printf("%s %" PFMT64u " %s @ 0x%08" PFMT64x "\n",
						rz_meta_type_to_string(d->type),
						size, pstr, start);
				} else {
					if (show_full || mode == RZ_OUTPUT_MODE_LONG) {
						const char *dtype = d->type == RZ_META_TYPE_MAGIC ? "magic" : "format";
						rz_cons_printf("0x%08" PFMT64x " %s %" PFMT64u " %s\n",
							start, dtype, size, pstr);
					} else {
						rz_cons_printf("%" PFMT64u " %s\n", size, pstr);
					}
				}
				break;
			case RZ_META_TYPE_VARTYPE:
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					rz_cons_printf("%s %s @ 0x%08" PFMT64x "\n",
						rz_meta_type_to_string(d->type), pstr, start);
				} else {
					rz_cons_printf("0x%08" PFMT64x " %s\n", start, pstr);
				}
				break;
			case RZ_META_TYPE_HIGHLIGHT: {
				ut8 r = 0, g = 0, b = 0, A = 0;
				const char *esc = strchr(d->str, '\x1b');
				rz_cons_rgb_parse(esc, &r, &g, &b, &A);
				rz_cons_printf("%s rgb:%02x%02x%02x @ 0x%08" PFMT64x "\n",
					rz_meta_type_to_string(d->type), r, g, b, start);
				// TODO: d->size
			} break;
			default:
				if (mode == RZ_OUTPUT_MODE_RIZIN) {
					rz_cons_printf("%s %" PFMT64u " 0x%08" PFMT64x " # %s\n",
						rz_meta_type_to_string(d->type),
						size, start, pstr);
				} else {
					// TODO: use b64 here
					rz_cons_printf("0x%08" PFMT64x " array[%" PFMT64u "] %s %s\n",
						start, size,
						rz_meta_type_to_string(d->type), pstr);
				}
				break;
			}
			break;
		}
		if (str) {
			free(str);
		}
	}
}

typedef struct {
	RzAnalysisMetaType type;
	const RzSpace *space;

	RzPVector /*RzIntervalNode*/ *result;
} CollectCtx;

static bool item_matches_filter(RzAnalysisMetaItem *item, RzAnalysisMetaType type, RZ_NULLABLE const RzSpace *space) {
	return (type == RZ_META_TYPE_ANY || item->type == type) && (!space || item->space == space);
}

static bool collect_nodes_cb(RzIntervalNode *node, void *user) {
	CollectCtx *ctx = user;
	if (item_matches_filter(node->data, ctx->type, ctx->space)) {
		rz_pvector_push(ctx->result, node);
	}
	return true;
}

static RzPVector *collect_nodes_at(RzAnalysis *analysis, RzAnalysisMetaType type, RZ_NULLABLE const RzSpace *space, ut64 addr) {
	CollectCtx ctx = {
		.type = type,
		.space = space,
		.result = rz_pvector_new(NULL)
	};
	if (!ctx.result) {
		return NULL;
	}
	rz_interval_tree_all_at(&analysis->meta, addr, collect_nodes_cb, &ctx);
	return ctx.result;
}

RZ_IPI void rz_core_meta_print_list_at(RzCore *core, ut64 addr, RzCmdStateOutput *state) {
	RzPVector *nodes = collect_nodes_at(core->analysis, RZ_META_TYPE_ANY,
		rz_spaces_current(&core->analysis->meta_spaces), addr);
	if (!nodes) {
		return;
	}
	rz_cmd_state_output_array_start(state);
	void **it;
	rz_pvector_foreach (nodes, it) {
		RzIntervalNode *node = *it;
		rz_core_meta_print(core, node->data, node->start, rz_meta_node_size(node), true, state);
	}
	rz_pvector_free(nodes);
	rz_cmd_state_output_array_end(state);
}

static void print_meta_list(RzCore *core, RzAnalysisMetaType type, ut64 addr, RzCmdStateOutput *state) {
	RzAnalysisFunction *fcn = NULL;
	if (addr != UT64_MAX) {
		fcn = rz_analysis_get_fcn_in(core->analysis, addr, 0);
		if (!fcn) {
			return;
		}
	}
	RzIntervalTreeIter it;
	RzAnalysisMetaItem *item;
	rz_interval_tree_foreach (&core->analysis->meta, it, item) {
		RzIntervalNode *node = rz_interval_tree_iter_get(&it);
		if (type != RZ_META_TYPE_ANY && item->type != type) {
			continue;
		}
		if (fcn && !rz_analysis_function_contains(fcn, node->start)) {
			continue;
		}
		rz_core_meta_print(core, item, node->start, rz_meta_node_size(node), true, state);
	}
}

RZ_IPI void rz_core_meta_print_list_all(RzCore *core, RzAnalysisMetaType type, RzCmdStateOutput *state) {
	rz_cmd_state_output_array_start(state);
	print_meta_list(core, type, UT64_MAX, state);
	rz_cmd_state_output_array_end(state);
}

RZ_IPI void rz_core_meta_print_list_in_function(RzCore *core, RzAnalysisMetaType type, ut64 addr, RzCmdStateOutput *state) {
	rz_cmd_state_output_array_start(state);
	print_meta_list(core, type, addr, state);
	rz_cmd_state_output_array_end(state);
}