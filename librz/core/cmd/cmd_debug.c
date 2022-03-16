// SPDX-FileCopyrightText: 2009-2020 pancake <pancake@nopcode.org>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_core.h>
#include <rz_debug.h>
#include <sdb.h>
#define TN_KEY_LEN 32
#define TN_KEY_FMT "%" PFMT64u
#ifndef SIGKILL
#define SIGKILL 9
#endif

#include "rz_heap_glibc.h"

#if HAVE_JEMALLOC
#include "rz_heap_jemalloc.h"
#include "../linux_heap_jemalloc.c"
#endif

#include "../core_private.h"

static const char *help_msg_dcu[] = {
	"Usage:", "dcu", " Continue until address",
	"dcu.", "", "Alias for dcu $$ (continue until current address",
	"dcu", " address", "Continue until address",
	"dcu", " [..tail]", "Continue until the range",
	"dcu", " [from] [to]", "Continue until the range",
	NULL
};

struct dot_trace_ght {
	RzGraph *graph;
	Sdb *graphnodes;
};

struct trace_node {
	ut64 addr;
	int refs;
};

// XXX those tmp files are never removed and we shuoldnt use files for this
static void setRarunProfileString(RzCore *core, const char *str) {
	char *file = rz_file_temp("rz-run");
	char *s = strdup(str);
	rz_config_set(core->config, "dbg.profile", file);
	rz_str_replace_char(s, ',', '\n');
	rz_file_dump(file, (const ut8 *)s, strlen(s), 0);
	rz_file_dump(file, (const ut8 *)"\n", 1, 1);
	free(file);
}

static void cmd_debug_cont_syscall(RzCore *core, const char *_str) {
	// TODO : handle more than one stopping syscall
	int i, *syscalls = NULL;
	int count = 0;
	if (_str && *_str) {
		char *str = strdup(_str);
		count = rz_str_word_set0(str);
		syscalls = calloc(sizeof(int), count);
		for (i = 0; i < count; i++) {
			const char *sysnumstr = rz_str_word_get0(str, i);
			int sig = (int)rz_num_math(core->num, sysnumstr);
			if (sig == -1) { // trace ALL syscalls
				syscalls[i] = -1;
			} else if (sig == 0) {
				sig = rz_syscall_get_num(core->analysis->syscall, sysnumstr);
				if (sig == -1) {
					RZ_LOG_ERROR("Unknown syscall number\n");
					free(str);
					free(syscalls);
					return;
				}
				syscalls[i] = sig;
			}
		}
		RZ_LOG_INFO("Running child until syscalls:");
		for (i = 0; i < count; i++) {
			RZ_LOG_INFO("%d ", syscalls[i]);
		}
		RZ_LOG_INFO("\n");
		free(str);
	} else {
		RZ_LOG_INFO("Running child until next syscall\n");
	}
	rz_reg_arena_swap(core->dbg->reg, true);
	rz_debug_continue_syscalls(core->dbg, syscalls, count);
	free(syscalls);
}

static RzGraphNode *get_graphtrace_node(RzGraph *g, Sdb *nodes, struct trace_node *tn) {
	RzGraphNode *gn;
	char tn_key[TN_KEY_LEN];

	snprintf(tn_key, TN_KEY_LEN, TN_KEY_FMT, tn->addr);
	gn = (RzGraphNode *)(size_t)sdb_num_get(nodes, tn_key, NULL);
	if (!gn) {
		gn = rz_graph_add_node(g, tn);
		sdb_num_set(nodes, tn_key, (ut64)(size_t)gn, 0);
	}
	return gn;
}

static void dot_trace_create_node(RTreeNode *n, RTreeVisitor *vis) {
	struct dot_trace_ght *data = (struct dot_trace_ght *)vis->data;
	struct trace_node *tn = n->data;
	if (tn)
		get_graphtrace_node(data->graph, data->graphnodes, tn);
}

static void dot_trace_discover_child(RTreeNode *n, RTreeVisitor *vis) {
	struct dot_trace_ght *data = (struct dot_trace_ght *)vis->data;
	RzGraph *g = data->graph;
	Sdb *gnodes = data->graphnodes;
	RTreeNode *parent = n->parent;
	struct trace_node *tn = n->data;
	struct trace_node *tn_parent = parent->data;

	if (tn && tn_parent) {
		RzGraphNode *gn = get_graphtrace_node(g, gnodes, tn);
		RzGraphNode *gn_parent = get_graphtrace_node(g, gnodes, tn_parent);

		if (!rz_graph_adjacent(g, gn_parent, gn))
			rz_graph_add_edge(g, gn_parent, gn);
	}
}

static void dot_trace_traverse(RzCore *core, RTree *t, int fmt) {
	const char *gfont = rz_config_get(core->config, "graph.font");
	struct dot_trace_ght aux_data;
	RTreeVisitor vis = { 0 };
	const RzList *nodes;
	RzListIter *iter;
	RzGraphNode *n;

	if (fmt == 'i') {
		rz_core_agraph_reset(core);
		rz_core_cmd0(core, ".dtg*");
		rz_core_agraph_print_interactive(core);
		return;
	}
	aux_data.graph = rz_graph_new();
	aux_data.graphnodes = sdb_new0();

	/* build a callgraph from the execution trace */
	vis.data = &aux_data;
	vis.pre_visit = (RTreeNodeVisitCb)dot_trace_create_node;
	vis.discover_child = (RTreeNodeVisitCb)dot_trace_discover_child;
	rz_tree_bfs(t, &vis);

	/* traverse the callgraph to print the dot file */
	nodes = rz_graph_get_nodes(aux_data.graph);
	if (fmt == 0) {
		rz_cons_printf("digraph code {\n"
			       "graph [bgcolor=white];\n"
			       "    node [color=lightgray, style=filled"
			       " shape=box fontname=\"%s\" fontsize=\"8\"];\n",
			gfont);
	}
	rz_list_foreach (nodes, iter, n) {
		struct trace_node *tn = (struct trace_node *)n->data;
		const RzList *neighbours = rz_graph_get_neighbours(aux_data.graph, n);
		RzListIter *it_n;
		RzGraphNode *w;

		if (!fmt && tn) {
			rz_cons_printf("\"0x%08" PFMT64x "\" [URL=\"0x%08" PFMT64x
				       "\" color=\"lightgray\" label=\"0x%08" PFMT64x
				       " (%d)\"]\n",
				tn->addr, tn->addr, tn->addr, tn->refs);
		}
		rz_list_foreach (neighbours, it_n, w) {
			struct trace_node *tv = (struct trace_node *)w->data;

			if (tv && tn) {
				if (fmt) {
					rz_cons_printf("agn 0x%08" PFMT64x "\n", tn->addr);
					rz_cons_printf("agn 0x%08" PFMT64x "\n", tv->addr);
					rz_cons_printf("age 0x%08" PFMT64x " 0x%08" PFMT64x "\n",
						tn->addr, tv->addr);
				} else {
					rz_cons_printf("\"0x%08" PFMT64x "\" -> \"0x%08" PFMT64x
						       "\" [color=\"red\"];\n",
						tn->addr, tv->addr);
				}
			}
		}
	}
	if (!fmt) {
		rz_cons_printf("}\n");
	}

	rz_graph_free(aux_data.graph);
	sdb_free(aux_data.graphnodes);
}

/* TODO: refactor all those step_until* function into a single one
 * TODO: handle when the process is dead
 * TODO: handle ^C */
static int step_until(RzCore *core, ut64 addr) {
	ut64 off = rz_debug_reg_get(core->dbg, "PC");
	if (!off) {
		RZ_LOG_ERROR("Cannot 'drn PC'\n");
		return false;
	}
	if (!addr) {
		RZ_LOG_ERROR("Cannot continue until address 0\n");
		return false;
	}
	rz_cons_break_push(NULL, NULL);
	do {
		if (rz_cons_is_breaked()) {
			core->break_loop = true;
			break;
		}
		if (rz_debug_is_dead(core->dbg)) {
			core->break_loop = true;
			break;
		}
		rz_debug_step(core->dbg, 1);
		off = rz_debug_reg_get(core->dbg, "PC");
		// check breakpoint here
	} while (off != addr);
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
	return true;
}

static int step_until_esil(RzCore *core, const char *esilstr) {
	if (!core || !esilstr || !core->dbg || !core->dbg->analysis || !core->dbg->analysis->esil) {
		RZ_LOG_ERROR("Not initialized %p. Run 'aei' first.\n", core->analysis->esil);
		return false;
	}
	rz_cons_break_push(NULL, NULL);
	for (;;) {
		if (rz_cons_is_breaked()) {
			core->break_loop = true;
			break;
		}
		if (rz_debug_is_dead(core->dbg)) {
			core->break_loop = true;
			break;
		}
		rz_debug_step(core->dbg, 1);
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_ANY, false);
		if (rz_analysis_esil_condition(core->analysis->esil, esilstr)) {
			RZ_LOG_ERROR("ESIL BREAK!\n");
			break;
		}
	}
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
	return true;
}

static bool is_repeatable_inst(RzCore *core, ut64 addr) {
	// we have read the bytes already
	RzAnalysisOp *op = rz_core_op_analysis(core, addr, RZ_ANALYSIS_OP_MASK_ALL);
	bool ret = op && ((op->prefix & RZ_ANALYSIS_OP_PREFIX_REP) || (op->prefix & RZ_ANALYSIS_OP_PREFIX_REPNE));
	rz_analysis_op_free(op);
	return ret;
}

static bool step_until_inst(RzCore *core, const char *instr, bool regex) {
	rz_return_val_if_fail(core, false);
	instr = rz_str_trim_head_ro(instr);
	if (!instr || !core->dbg) {
		RZ_LOG_ERROR("Wrong debugger state\n");
		return false;
	}
	RzAsmOp asmop;
	ut8 buf[32];
	ut64 pc;
	int ret;
	bool is_x86 = rz_str_startswith(rz_config_get(core->config, "asm.arch"), "x86");
	rz_cons_break_push(NULL, NULL);
	for (;;) {
		if (rz_cons_is_breaked()) {
			break;
		}
		if (rz_debug_is_dead(core->dbg)) {
			break;
		}
		pc = rz_debug_reg_get(core->dbg, "PC");
		if (is_x86 && is_repeatable_inst(core, pc)) {
			rz_debug_step_over(core->dbg, 1);
		} else {
			rz_debug_step(core->dbg, 1);
		}
		pc = rz_debug_reg_get(core->dbg, "PC");
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_ANY, false);
		/* TODO: disassemble instruction and strstr */
		rz_asm_set_pc(core->rasm, pc);
		// TODO: speedup if instructions are in the same block as the previous
		rz_io_read_at(core->io, pc, buf, sizeof(buf));
		ret = rz_asm_disassemble(core->rasm, &asmop, buf, sizeof(buf));
		RZ_LOG_ERROR("0x%08" PFMT64x " %d %s\n", pc, ret, rz_asm_op_get_asm(&asmop)); // asmop.buf_asm);
		if (ret > 0) {
			const char *buf_asm = rz_asm_op_get_asm(&asmop);
			if (regex) {
				if (rz_regex_match(instr, "e", buf_asm)) {
					RZ_LOG_ERROR("Stop.\n");
					break;
				}
			} else {
				if (strstr(buf_asm, instr)) {
					RZ_LOG_ERROR("Stop.\n");
					break;
				}
			}
		}
	}
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
	return true;
}

RZ_IPI void rz_core_dbg_follow_seek_register(RzCore *core) {
	int follow = rz_config_get_i(core->config, "dbg.follow");
	if (follow > 0) {
		ut64 pc = rz_debug_reg_get(core->dbg, "PC");
		if ((pc < core->offset) || (pc > (core->offset + follow))) {
			rz_core_seek_to_register(core, "PC", false);
		}
		rz_core_debug_sync_bits(core);
	}
}

static int step_until_optype(RzCore *core, RzList *optypes_list) {
	RzAnalysisOp op;
	ut8 buf[32];
	ut64 pc;
	int res = true;

	RzListIter *iter;
	char *optype;

	if (!core || !core->dbg) {
		RZ_LOG_ERROR("Wrong state\n");
		res = false;
		goto end;
	}
	if (!optypes_list) {
		RZ_LOG_ERROR("Missing optypes. Usage example: 'dsuo ucall ujmp'\n");
		res = false;
		goto end;
	}

	bool debugMode = rz_config_get_b(core->config, "cfg.debug");

	rz_cons_break_push(NULL, NULL);
	for (;;) {
		if (rz_cons_is_breaked()) {
			core->break_loop = true;
			break;
		}
		if (debugMode) {
			if (rz_debug_is_dead(core->dbg)) {
				core->break_loop = true;
				break;
			}
			rz_debug_step(core->dbg, 1);
			pc = rz_debug_reg_get(core->dbg, core->dbg->reg->name[RZ_REG_NAME_PC]);
			// 'Copy' from rz_debug_step_soft
			if (!core->dbg->iob.read_at) {
				RZ_LOG_ERROR("ERROR\n");
				res = false;
				goto cleanup_after_push;
			}
			if (!core->dbg->iob.read_at(core->dbg->iob.io, pc, buf, sizeof(buf))) {
				RZ_LOG_ERROR("ERROR\n");
				res = false;
				goto cleanup_after_push;
			}
		} else {
			rz_core_esil_step(core, UT64_MAX, NULL, NULL, false);
			pc = rz_reg_getv(core->analysis->reg, "PC");
		}
		rz_io_read_at(core->io, pc, buf, sizeof(buf));

		if (!rz_analysis_op(core->dbg->analysis, &op, pc, buf, sizeof(buf), RZ_ANALYSIS_OP_MASK_BASIC)) {
			RZ_LOG_ERROR("Error: rz_analysis_op failed\n");
			res = false;
			goto cleanup_after_push;
		}

		// This is slow because we do lots of strcmp's.
		// To improve this, the function rz_analysis_optype_string_to_int should be implemented
		// I also don't check if the opcode type exists.
		const char *optype_str = rz_analysis_optype_to_string(op.type);
		rz_list_foreach (optypes_list, iter, optype) {
			if (!strcmp(optype_str, optype)) {
				goto cleanup_after_push;
			}
		}
	}

cleanup_after_push:
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
end:
	return res;
}

static int step_until_flag(RzCore *core, const char *instr) {
	const RzList *list;
	RzListIter *iter;
	RzFlagItem *f;
	ut64 pc;

	instr = rz_str_trim_head_ro(instr);
	if (!core || !instr || !core->dbg) {
		RZ_LOG_ERROR("Wrong state\n");
		return false;
	}
	rz_cons_break_push(NULL, NULL);
	for (;;) {
		if (rz_cons_is_breaked()) {
			break;
		}
		if (rz_debug_is_dead(core->dbg)) {
			break;
		}
		rz_debug_step(core->dbg, 1);
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_ANY, false);
		pc = rz_debug_reg_get(core->dbg, "PC");
		list = rz_flag_get_list(core->flags, pc);
		rz_list_foreach (list, iter, f) {
			if (!instr || !*instr || (f->realname && strstr(f->realname, instr))) {
				rz_cons_printf("[ 0x%08" PFMT64x " ] %s\n",
					f->offset, f->realname);
				goto beach;
			}
		}
	}
beach:
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
	return true;
}

/* until end of frame */
static int step_until_eof(RzCore *core) {
	int maxLoops = 200000;
	ut64 off, now = rz_debug_reg_get(core->dbg, "SP");
	rz_cons_break_push(NULL, NULL);
	do {
		// XXX (HACK!)
		rz_debug_step_over(core->dbg, 1);
		off = rz_debug_reg_get(core->dbg, "SP");
		// check breakpoint here
		if (--maxLoops < 0) {
			RZ_LOG_ERROR("Step loop limit exceeded\n");
			break;
		}
	} while (off <= now);
	rz_core_reg_update_flags(core);
	rz_cons_break_pop();
	return true;
}

static int step_line(RzCore *core, int times) {
	char file[512], file2[512];
	int find_meta, line = -1, line2 = -1;
	char *tmp_ptr = NULL;
	ut64 off = rz_debug_reg_get(core->dbg, "PC");
	if (off == 0LL) {
		RZ_LOG_ERROR("Cannot 'drn PC'\n");
		return false;
	}
	file[0] = 0;
	file2[0] = 0;
	if (rz_bin_addr2line(core->bin, off, file, sizeof(file), &line)) {
		char *ptr = rz_file_slurp_line(file, line, 0);
		RZ_LOG_ERROR("--> 0x%08" PFMT64x " %s : %d\n", off, file, line);
		RZ_LOG_ERROR("--> %s\n", ptr);
		find_meta = false;
		free(ptr);
	} else {
		RZ_LOG_ERROR("--> Stepping until dwarf line\n");
		find_meta = true;
	}
	do {
		rz_debug_step(core->dbg, 1);
		off = rz_debug_reg_get(core->dbg, "PC");
		if (!rz_bin_addr2line(core->bin, off, file2, sizeof(file2), &line2)) {
			if (find_meta) {
				continue;
			}
			rz_core_reg_update_flags(core);
			RZ_LOG_ERROR("Cannot retrieve dwarf info at 0x%08" PFMT64x "\n", off);
			return false;
		}
	} while (!strcmp(file, file2) && line == line2);

	RZ_LOG_ERROR("--> 0x%08" PFMT64x " %s : %d\n", off, file2, line2);
	tmp_ptr = rz_file_slurp_line(file2, line2, 0);
	RZ_LOG_ERROR("--> %s\n", tmp_ptr);
	free(tmp_ptr);
	rz_core_reg_update_flags(core);

	return true;
}

static void cmd_debug_backtrace(RzCore *core, ut64 len) {
	RzAnalysisOp analop;
	ut64 addr;
	if (!len) {
		rz_bp_traptrace_list(core->dbg->bp);
	} else {
		ut64 oaddr = 0LL;
		RZ_LOG_ERROR("Trap tracing 0x%08" PFMT64x "-0x%08" PFMT64x "\n",
			core->offset, core->offset + len);
		rz_reg_arena_swap(core->dbg->reg, true);
		rz_bp_traptrace_reset(core->dbg->bp, true);
		rz_bp_traptrace_add(core->dbg->bp, core->offset, core->offset + len);
		rz_bp_traptrace_enable(core->dbg->bp, true);
		do {
			ut8 buf[32];
			rz_debug_continue(core->dbg);
			addr = rz_debug_reg_get(core->dbg, "PC");
			if (!addr) {
				RZ_LOG_ERROR("pc=0\n");
				break;
			}
			if (addr == oaddr) {
				RZ_LOG_ERROR("pc=opc\n");
				break;
			}
			oaddr = addr;
			/* XXX Bottleneck..we need to reuse the bytes read by traptrace */
			// XXX Do asm.arch should define the max size of opcode?
			rz_io_read_at(core->io, addr, buf, 32); // XXX longer opcodes?
			rz_analysis_op(core->analysis, &analop, addr, buf, sizeof(buf), RZ_ANALYSIS_OP_MASK_BASIC);
		} while (rz_bp_traptrace_at(core->dbg->bp, addr, analop.size));
		rz_bp_traptrace_enable(core->dbg->bp, false);
	}
}

#define MAX_MAP_SIZE (1024 * 1024 * 512)
static int dump_maps(RzCore *core, int perm, const char *filename) {
	RzDebugMap *map;
	RzListIter *iter;
	rz_debug_map_sync(core->dbg); // update process memory maps
	ut64 addr = core->offset;
	int do_dump = false;
	int ret = !rz_list_empty(core->dbg->maps);
	rz_list_foreach (core->dbg->maps, iter, map) {
		do_dump = false;
		if (perm == -1) {
			if (addr >= map->addr && addr < map->addr_end) {
				do_dump = true;
			}
		} else if (perm == 0) {
			do_dump = true;
		} else if (perm == (map->perm & perm)) {
			do_dump = true;
		}
		if (do_dump) {
			ut8 *buf = malloc(map->size);
			// TODO: use mmap here. we need a portable implementation
			if (!buf) {
				RZ_LOG_ERROR("Cannot allocate 0x%08" PFMT64x " bytes\n", map->size);
				free(buf);
				/// XXX: TODO: read by blocks!!1
				continue;
			}
			if (map->size > MAX_MAP_SIZE) {
				RZ_LOG_ERROR("Do not dumping 0x%08" PFMT64x " because it's too big\n", map->addr);
				free(buf);
				continue;
			}
			rz_io_read_at(core->io, map->addr, buf, map->size);
			char *file = filename
				? strdup(filename)
				: rz_str_newf("0x%08" PFMT64x "-0x%08" PFMT64x "-%s.dmp",
					  map->addr, map->addr_end, rz_str_rwx_i(map->perm));
			if (!rz_file_dump(file, buf, map->size, 0)) {
				RZ_LOG_ERROR("Cannot write '%s'\n", file);
				ret = 0;
			} else {
				RZ_LOG_ERROR("Dumped %d byte(s) into %s\n", (int)map->size, file);
			}
			free(file);
			free(buf);
		}
	}
	// RZ_LOG_ERROR ("No debug region found here\n");
	return ret;
}

// "dmm"
static void cmd_debug_current_modules(RzCore *core, RzOutputMode mode) {
	ut64 addr = core->offset;
	RzDebugMap *map;
	RzList *list;
	RzListIter *iter;
	list = rz_debug_modules_list(core->dbg);
	rz_list_foreach (list, iter, map) {
		if (!(addr >= map->addr && addr < map->addr_end)) {
			continue;
		}
		if (mode == RZ_OUTPUT_MODE_STANDARD) {
			rz_cons_printf("0x%08" PFMT64x " 0x%08" PFMT64x "  %s\n", map->addr, map->addr_end, map->file);
		} else if (mode == RZ_OUTPUT_MODE_RIZIN) {
			/* Escape backslashes (e.g. for Windows). */
			char *escaped_path = rz_str_escape(map->file);
			char *filtered_name = strdup(map->name);
			rz_name_filter(filtered_name, 0, true);
			rz_cons_printf("f mod.%s @ 0x%08" PFMT64x "\n",
				filtered_name, map->addr);
			rz_cons_printf("oba 0x%08" PFMT64x " %s\n", map->addr, escaped_path);
			free(escaped_path);
			free(filtered_name);
		}
	}
	rz_list_free(list);
}

static void cmd_debug_modules(RzCore *core, RzCmdStateOutput *state) { // "dmm"
	RzDebugMap *map;
	RzList *list;
	RzListIter *iter;
	PJ *pj = state->d.pj;
	RzOutputMode mode = state->mode;
	rz_cmd_state_output_array_start(state);
	list = rz_debug_modules_list(core->dbg);
	rz_list_foreach (list, iter, map) {
		if (mode == RZ_OUTPUT_MODE_STANDARD) {
			rz_cons_printf("0x%08" PFMT64x " 0x%08" PFMT64x "  %s\n", map->addr, map->addr_end, map->file);
		} else if (mode == RZ_OUTPUT_MODE_JSON) {
			/* Escape backslashes (e.g. for Windows). */
			pj_o(pj);
			pj_kn(pj, "addr", map->addr);
			pj_kn(pj, "addr_end", map->addr_end);
			pj_ks(pj, "file", map->file);
			pj_ks(pj, "name", map->name);
			pj_end(pj);
		} else if (mode == RZ_OUTPUT_MODE_RIZIN) {
			/* Escape backslashes (e.g. for Windows). */
			char *escaped_path = rz_str_escape(map->file);
			char *filtered_name = strdup(map->name);
			rz_name_filter(filtered_name, 0, true);
			rz_cons_printf("f mod.%s @ 0x%08" PFMT64x "\n",
				filtered_name, map->addr);
			rz_cons_printf("oba 0x%08" PFMT64x " %s\n", map->addr, escaped_path);
			free(escaped_path);
			free(filtered_name);
		}
	}
	rz_cmd_state_output_array_end(state);
	rz_list_free(list);
}

static ut64 addroflib(RzCore *core, const char *libname) {
	RzListIter *iter;
	RzDebugMap *map;
	if (!core || !libname) {
		return UT64_MAX;
	}
	rz_debug_map_sync(core->dbg);
	// RzList *list = rz_debug_native_modules_get (core->dbg);
	RzList *list = rz_debug_modules_list(core->dbg);
	rz_list_foreach (list, iter, map) {
		if (strstr(rz_file_basename(map->name), libname)) {
			return map->addr;
		}
	}
	rz_list_foreach (core->dbg->maps, iter, map) {
		if (strstr(rz_file_basename(map->name), libname)) {
			return map->addr;
		}
	}
	return UT64_MAX;
}

static RzDebugMap *get_closest_map(RzCore *core, ut64 addr) {
	RzListIter *iter;
	RzDebugMap *map;

	rz_debug_map_sync(core->dbg);
	RzList *list = rz_debug_modules_list(core->dbg);
	rz_list_foreach (list, iter, map) {
		if (addr != UT64_MAX && (addr >= map->addr && addr < map->addr_end)) {
			return map;
		}
	}
	rz_list_foreach (core->dbg->maps, iter, map) {
		if (addr != UT64_MAX && (addr >= map->addr && addr < map->addr_end)) {
			return map;
		}
	}
	return NULL;
}

static bool get_bin_info(RzCore *core, const char *file, ut64 baseaddr, RzCmdStateOutput *state, bool symbols_only, RzCoreBinFilter *filter, bool apply_info) {
	int fd;
	if ((fd = rz_io_fd_open(core->io, file, RZ_PERM_R, 0)) == -1) {
		return false;
	}
	RzBinOptions opt = { 0 };
	opt.obj_opts.elf_load_sections = true;
	opt.obj_opts.elf_checks_sections = true;
	opt.obj_opts.elf_checks_segments = true;
	opt.fd = fd;
	opt.sz = rz_io_fd_size(core->io, fd);
	opt.obj_opts.baseaddr = baseaddr;
	RzBinFile *obf = rz_bin_cur(core->bin);
	RzBinFile *bf = rz_bin_open_io(core->bin, &opt);
	if (!bf) {
		rz_io_fd_close(core->io, fd);
		return false;
	}
	int action = RZ_CORE_BIN_ACC_ALL & ~RZ_CORE_BIN_ACC_INFO;
	if (symbols_only || filter->name) {
		action = RZ_CORE_BIN_ACC_SYMBOLS;
	} else if (apply_info || (state && (state->mode & RZ_OUTPUT_MODE_RIZIN))) {
		action &= ~RZ_CORE_BIN_ACC_ENTRIES & ~RZ_CORE_BIN_ACC_MAIN & ~RZ_CORE_BIN_ACC_MAPS;
	}
	if (apply_info) {
		rz_core_bin_apply_info(core, core->bin->cur, action);
	} else {
		rz_core_bin_print(core, bf, action, filter, state, NULL);
		rz_cmd_state_output_print(state);
		rz_cmd_state_output_fini(state);
	}
	rz_bin_file_delete(core->bin, bf);
	rz_bin_file_set_cur_binfile(core->bin, obf);
	rz_io_fd_close(core->io, fd);
	return true;
}

// dm
RZ_IPI RzCmdStatus rz_cmd_debug_list_maps_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_debug_map_sync(core->dbg); // update process memory maps
	rz_debug_map_print(core->dbg, core->offset, state);
	return RZ_CMD_STATUS_OK;
}

// dma
RZ_IPI RzCmdStatus rz_cmd_debug_allocate_maps_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	ut64 addr = core->offset;
	int size = (int)rz_num_math(core->num, argv[1]);
	rz_debug_map_alloc(core->dbg, addr, size, false);
	return RZ_CMD_STATUS_OK;
}

// dmm
RZ_IPI RzCmdStatus rz_cmd_debug_modules_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	CMD_CHECK_DEBUG_DEAD(core);
	cmd_debug_modules(core, state);
	return RZ_CMD_STATUS_OK;
}

// dmm.
RZ_IPI RzCmdStatus rz_cmd_debug_current_modules_handler(RzCore *core, int argc, const char **argv, RzOutputMode mode) {
	CMD_CHECK_DEBUG_DEAD(core);
	cmd_debug_current_modules(core, mode);
	return RZ_CMD_STATUS_OK;
}

// dm-
RZ_IPI RzCmdStatus rz_cmd_debug_deallocate_map_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	RzListIter *iter;
	RzDebugMap *map;
	ut64 addr = core->offset;
	rz_list_foreach (core->dbg->maps, iter, map) {
		if (addr >= map->addr && addr < map->addr_end) {
			rz_debug_map_dealloc(core->dbg, map);
			rz_debug_map_sync(core->dbg);
			return RZ_CMD_STATUS_OK;
		}
	}
	RZ_LOG_ERROR("The address doesn't match with any map.\n");
	return RZ_CMD_STATUS_ERROR;
}

// dm=
RZ_IPI RzCmdStatus rz_cmd_debug_list_maps_ascii_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_debug_map_sync(core->dbg);
	rz_debug_map_list_visual(core->dbg, core->offset, argv[0] + 2,
		rz_config_get_i(core->config, "scr.color"));
	return RZ_CMD_STATUS_OK;
}

// dm.
RZ_IPI RzCmdStatus rz_cmd_debug_map_current_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	ut64 addr = core->offset;
	// RZ_OUTPUT_MODE_LONG is workaround for '.'
	RzCmdStateOutput state = { 0 };
	rz_cmd_state_output_init(&state, RZ_OUTPUT_MODE_LONG);
	rz_debug_map_print(core->dbg, addr, &state);
	rz_cmd_state_output_print(&state);
	rz_cmd_state_output_fini(&state);
	rz_cons_flush();
	return RZ_CMD_STATUS_OK;
}

// dmd
RZ_IPI RzCmdStatus rz_cmd_debug_dump_maps_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	if (argc == 2) {
		dump_maps(core, -1, argv[1]);
	} else if (argc == 1) {
		dump_maps(core, -1, NULL);
	}
	return RZ_CMD_STATUS_OK;
}

// dmda
RZ_IPI RzCmdStatus rz_cmd_debug_dump_maps_all_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	dump_maps(core, 0, NULL);
	return RZ_CMD_STATUS_OK;
}

// dmdw
RZ_IPI RzCmdStatus rz_cmd_debug_dump_maps_writable_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	dump_maps(core, RZ_PERM_RW, NULL);
	return RZ_CMD_STATUS_OK;
}

static inline void module_info(RzCore *core, const char *libname, const char *symname, RzCmdStateOutput *state, bool symbols_only, bool apply_info) {
	ut64 addr = addroflib(core, rz_file_basename(libname));
	if (addr == UT64_MAX) {
		RZ_LOG_ERROR("Unknown library, or not found in dm\n");
	}
	RzDebugMap *map = get_closest_map(core, addr);
	if (!map) {
		return;
	}

	RzCoreBinFilter filter;
	filter.offset = UT64_MAX;
	filter.name = (char *)symname;

	const char *file = map->file ? map->file : map->name;
	char *newfile = NULL;
	if (!rz_file_exists(file)) {
		newfile = rz_file_temp("memlib");
		if (newfile) {
			file = newfile;
			rz_core_dump(core, file, map->addr, map->size, false);
		}
	}
	get_bin_info(core, file, map->addr, state, symbols_only, &filter, apply_info);
	if (newfile) {
		if (!rz_file_rm(newfile)) {
			RZ_LOG_ERROR("when removing %s\n", newfile);
		}
		free(newfile);
	}
}

// dmi
RZ_IPI RzCmdStatus rz_cmd_debug_symbols_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	CMD_CHECK_DEBUG_DEAD(core);
	const char *symname = argc > 2 ? argv[2] : NULL;
	module_info(core, argv[1], symname, state, true, false);
	return RZ_CMD_STATUS_OK;
}

// dmia
RZ_IPI RzCmdStatus rz_cmd_debug_all_info_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	CMD_CHECK_DEBUG_DEAD(core);
	module_info(core, argv[1], NULL, state, false, false);
	return RZ_CMD_STATUS_OK;
}

// dmias
RZ_IPI RzCmdStatus rz_cmd_debug_apply_info_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	module_info(core, argv[1], NULL, NULL, false, true);
	return RZ_CMD_STATUS_OK;
}

// dmi.
RZ_IPI RzCmdStatus rz_cmd_debug_closest_symbol_handler(RzCore *core, int argc, const char **argv) {
	ut64 addr = core->offset;
	RzDebugMap *map = get_closest_map(core, addr);
	if (map) {
		ut64 closest_addr = UT64_MAX;
		RzList *symbols = rz_bin_get_symbols(core->bin);
		RzListIter *iter;
		RzBinSymbol *symbol, *closest_symbol = NULL;

		rz_list_foreach (symbols, iter, symbol) {
			if (symbol->vaddr > addr) {
				if (symbol->vaddr - addr < closest_addr) {
					closest_addr = symbol->vaddr - addr;
					closest_symbol = symbol;
				}
			} else {
				if (addr - symbol->vaddr < closest_addr) {
					closest_addr = addr - symbol->vaddr;
					closest_symbol = symbol;
				}
			}
		}
		RzBinFile *bf = rz_bin_cur(core->bin);
		if (closest_symbol && bf) {
			RzCoreBinFilter filter;
			filter.offset = UT64_MAX;
			filter.name = (char *)closest_symbol->name;

			rz_bin_set_baddr(core->bin, map->addr);
			RzCmdStateOutput state;
			rz_cmd_state_output_init(&state, RZ_OUTPUT_MODE_STANDARD);
			rz_core_bin_print(core, bf, RZ_CORE_BIN_ACC_SYMBOLS, &filter, &state, NULL);
			rz_cmd_state_output_print(&state);
			rz_cmd_state_output_fini(&state);
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dmp
RZ_IPI RzCmdStatus rz_debug_memory_permission_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	RzListIter *iter;
	RzDebugMap *map;
	ut64 addr = 0, size = 0;
	int perms;
	if (argc == 3) { // dmp <size> <perms> @ <addr>
		addr = core->offset;
		size = rz_num_math(core->num, argv[1]);
		perms = rz_str_rwx(argv[2]);
		rz_debug_map_protect(core->dbg, addr, (int)size, perms);
	} else if (argc == 2) { // dmp <perms>
		addr = UT64_MAX;
		rz_list_foreach (core->dbg->maps, iter, map) {
			if (core->offset >= map->addr && core->offset < map->addr_end) {
				addr = map->addr;
				size = map->size;
				break;
			}
		}
		perms = rz_str_rwx(argv[1]);
		if (addr != UT64_MAX && perms >= 0) {
			rz_debug_map_protect(core->dbg, addr, (int)size, perms);
		} else {
			return RZ_CMD_STATUS_ERROR;
		}
	}
	return RZ_CMD_STATUS_OK;
}

RZ_IPI RzCmdStatus rz_cmd_debug_dmS_handler(RzCore *core, int argc, const char **argv, RzOutputMode m) {
	CMD_CHECK_DEBUG_DEAD(core);
	RzListIter *iter;
	RzDebugMap *map;
	ut64 addr;
	const char *libname = NULL, *sectname = NULL, *mode = "";
	ut64 baddr = 0LL;
	if (m == RZ_OUTPUT_MODE_RIZIN) {
		mode = "-r ";
	}
	addr = UT64_MAX;
	if (argc == 3) {
		sectname = argv[2];
	}
	if (argc >= 2) {
		if (IS_DIGIT(*argv[1])) {
			const char *a0 = argv[1];
			addr = rz_num_math(core->num, a0);
		} else {
			addr = UT64_MAX;
		}
		if (!addr || addr == UT64_MAX) {
			libname = argv[1];
		}
	}
	rz_debug_map_sync(core->dbg); // update process memory maps
	RzList *list = rz_debug_modules_list(core->dbg);
	rz_list_foreach (list, iter, map) {
		if ((!libname ||
			    (addr != UT64_MAX && (addr >= map->addr && addr < map->addr_end)) ||
			    (libname != NULL && (strstr(map->name, libname))))) {
			baddr = map->addr;
			char *res;
			const char *file = map->file ? map->file : map->name;
			char *name = rz_str_escape((char *)rz_file_basename(file));
			char *filesc = rz_str_escape(file);
			/* TODO: do not spawn. use RzBin API */
			if (sectname) {
				char *sect = rz_str_escape(sectname);
				res = rz_sys_cmd_strf("env RZ_BIN_PREFIX=\"%s\" rz-bin %s-B 0x%08" PFMT64x " -S \"%s\" | grep \"%s\"", name, mode, baddr, filesc, sect);
				free(sect);
			} else {
				res = rz_sys_cmd_strf("env RZ_BIN_PREFIX=\"%s\" rz-bin %s-B 0x%08" PFMT64x " -S \"%s\"", name, mode, baddr, filesc);
			}
			free(filesc);
			rz_cons_println(res);
			free(name);
			free(res);
			if (libname || addr != UT64_MAX) { // only single match requested
				break;
			}
		}
	}
	return RZ_CMD_STATUS_OK;
}
// dml
RZ_IPI RzCmdStatus rz_cmd_debug_dml_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	RzListIter *iter;
	RzDebugMap *map;
	ut64 addr = core->offset;
	rz_debug_map_sync(core->dbg); // update process memory maps
	rz_list_foreach (core->dbg->maps, iter, map) {
		if (addr >= map->addr && addr < map->addr_end) {
			size_t sz;
			char *buf = rz_file_slurp(argv[1], &sz);
			// TODO: use mmap here. we need a portable implementation
			if (!buf) {
				RZ_LOG_ERROR("Cannot allocate 0x%08" PFMT64x " byte(s)\n", map->size);
				return RZ_CMD_STATUS_ERROR;
			}
			rz_io_write_at(core->io, map->addr, (const ut8 *)buf, sz);
			if (sz != map->size)
				RZ_LOG_ERROR("File size differs from region size (%" PFMT64u " vs %" PFMT64d ")\n",
					(ut64)sz, map->size);
			rz_cons_printf("Loaded %" PFMT64u " byte(s) into the map region at 0x%08" PFMT64x "\n",
				(ut64)sz, map->addr);
			free(buf);
			return RZ_CMD_STATUS_OK;
		}
	}
	RZ_LOG_ERROR("No debug region found here\n");
	return RZ_CMD_STATUS_ERROR;
}

// dmL
RZ_IPI RzCmdStatus rz_cmd_debug_dmL_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	int size;
	ut64 addr;
	addr = core->offset;
	size = (int)rz_num_math(core->num, argv[1]);
	rz_debug_map_alloc(core->dbg, addr, size, true);
	return RZ_CMD_STATUS_OK;
}

// dmx
RZ_IPI int rz_cmd_debug_heap_jemalloc(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	CMD_CHECK_DEBUG_DEAD(core);
#if HAVE_JEMALLOC
	if (core->rasm->bits == 64) {
		return cmd_dbg_map_jemalloc_64(core, input);
	} else {
		return cmd_dbg_map_jemalloc_32(core, input);
	}
#endif
	return RZ_CMD_STATUS_ERROR;
}

#include "../linux_heap_glibc.c"

static void foreach_reg_set_or_clear(RzCore *core, bool set) {
	RzReg *reg = rz_core_reg_default(core);
	const RzList *regs = rz_reg_get_list(reg, RZ_REG_TYPE_GPR);
	RzListIter *it;
	RzRegItem *reg_item;
	rz_list_foreach (regs, it, reg_item) {
		if (set) {
			const ut64 value = rz_reg_get_value(reg, reg_item);
			rz_flag_set(core->flags, reg_item->name, value, reg_item->size / 8);
		} else {
			rz_flag_unset_name(core->flags, reg_item->name);
		}
	}
}

RZ_API void rz_core_debug_set_register_flags(RzCore *core) {
	rz_flag_space_push(core->flags, RZ_FLAGS_FS_REGISTERS);
	foreach_reg_set_or_clear(core, true);
	rz_flag_space_pop(core->flags);
}

RZ_API void rz_core_debug_clear_register_flags(RzCore *core) {
	foreach_reg_set_or_clear(core, false);
}

static void backtrace_vars(RzCore *core, RzList *frames) {
	RzDebugFrame *f;
	RzListIter *iter;
	// analysis vs debug ?
	const char *sp = rz_reg_get_name(core->analysis->reg, RZ_REG_NAME_SP);
	const char *bp = rz_reg_get_name(core->analysis->reg, RZ_REG_NAME_BP);
	if (!sp) {
		sp = "SP";
	}
	if (!bp) {
		bp = "BP";
	}
	RzReg *r = core->analysis->reg;
	ut64 dsp = rz_reg_getv(r, sp);
	ut64 dbp = rz_reg_getv(r, bp);
	int n = 0;
	rz_list_foreach (frames, iter, f) {
		ut64 s = f->sp ? f->sp : dsp;
		ut64 b = f->bp ? f->bp : dbp;
		rz_reg_setv(r, bp, s);
		rz_reg_setv(r, sp, b);
		//////////
		char flagdesc[1024], flagdesc2[1024];
		RzFlagItem *fi = rz_flag_get_at(core->flags, f->addr, true);
		flagdesc[0] = flagdesc2[0] = 0;
		if (fi) {
			if (fi->offset != f->addr) {
				int delta = (int)(f->addr - fi->offset);
				if (delta > 0) {
					snprintf(flagdesc, sizeof(flagdesc),
						"%s+%d", fi->name, delta);
				} else if (delta < 0) {
					snprintf(flagdesc, sizeof(flagdesc),
						"%s%d", fi->name, delta);
				} else {
					snprintf(flagdesc, sizeof(flagdesc),
						"%s", fi->name);
				}
			} else {
				snprintf(flagdesc, sizeof(flagdesc),
					"%s", fi->name);
			}
		}
		//////////
		RzAnalysisFunction *fcn = rz_analysis_get_fcn_in(core->analysis, f->addr, 0);
		// char *str = rz_str_newf ("[frame %d]", n);
		rz_cons_printf("%d  0x%08" PFMT64x " sp: 0x%08" PFMT64x " %-5d"
			       "[%s]  %s %s\n",
			n, f->addr, f->sp, (int)f->size,
			fcn ? fcn->name : "??", flagdesc, flagdesc2);
		rz_cons_push();
		char *res = rz_core_analysis_all_vars_display(core, fcn, true);
		rz_cons_pop();
		rz_cons_printf("%s", res);
		free(res);
		n++;
	}
	rz_reg_setv(r, bp, dbp);
	rz_reg_setv(r, sp, dsp);
}

static void asciiart_backtrace(RzCore *core, RzList *frames) {
	// TODO: show local variables
	// TODO: show function/flags/symbols related
	// TODO: show contents of stack
	// TODO: honor scr.color
	RzDebugFrame *f;
	RzListIter *iter;
	bool mymap = false;
	// analysis vs debug ?
	const char *sp = rz_reg_get_name(core->analysis->reg, RZ_REG_NAME_SP);
	const char *bp = rz_reg_get_name(core->analysis->reg, RZ_REG_NAME_BP);
	if (!sp) {
		sp = "SP";
	}
	if (!bp) {
		bp = "BP";
	}
	ut64 dsp = rz_reg_getv(core->analysis->reg, sp);
	ut64 dbp = rz_reg_getv(core->analysis->reg, bp);
	RzDebugMap *map = rz_debug_map_get(core->dbg, dsp);
	if (!map) {
		mymap = true;
		map = RZ_NEW0(RzDebugMap);
		map->addr = UT64_MAX;
		map->addr_end = UT64_MAX;
	}

	rz_cons_printf("0x%016" PFMT64x "  STACK END  ^^^\n", map->addr);
	rz_cons_printf("0x%016" PFMT64x "  STACK POINTER: %s\n", dsp, sp);
	rz_cons_printf("                    .------------------------.\n");
	int n = 0;
	rz_list_foreach (frames, iter, f) {
		ut64 s = f->sp ? f->sp : dsp;
		ut64 b = f->bp ? f->bp : dbp;
		char *str = rz_str_newf("[frame %d]", n);
		rz_cons_printf("0x%016" PFMT64x "  |%4s    %10s      | ; size %" PFMTDPTR "\n", s, sp, str, (ptrdiff_t)(s - b));
		free(str);
		rz_cons_printf("                    |            ...         |\n");
		rz_cons_printf("0x%016" PFMT64x "  |%4s 0x%016" PFMT64x " | %s\n", b, bp, f->addr, "; return address");
		rz_cons_printf("                    )------------------------(\n");
		// rz_cons_printf("0x%08llx 0x%08llx 0x%08llx\n", f->addr, s, b);
		n++;
	}
	rz_cons_printf("                    |           ...          |\n");
	rz_cons_printf("                    `------------------------'\n");
	rz_cons_printf("0x%016" PFMT64x "  STACK BOTTOM\n", map->addr_end);
	if (mymap) {
		rz_debug_map_free(map);
	}
}

static void get_backtrace_info(RzCore *core, RzDebugFrame *frame, ut64 addr, char **flagdesc, char **flagdesc2, char **pcstr, char **spstr) {
	RzFlagItem *f = rz_flag_get_at(core->flags, frame->addr, true);
	*flagdesc = NULL;
	*flagdesc2 = NULL;
	if (f) {
		if (f->offset != addr) {
			int delta = (int)(frame->addr - f->offset);
			if (delta > 0) {
				*flagdesc = rz_str_newf("%s+%d", f->name, delta);
			} else if (delta < 0) {
				*flagdesc = rz_str_newf("%s%d", f->name, delta);
			} else {
				*flagdesc = rz_str_newf("%s", f->name);
			}
		} else {
			*flagdesc = rz_str_newf("%s", f->name);
		}
	}
	f = rz_flag_get_at(core->flags, frame->addr, true);
	if (f && !strchr(f->name, '.')) {
		f = rz_flag_get_at(core->flags, frame->addr - 1, true);
	}
	if (f) {
		if (f->offset != addr) {
			int delta = (int)(frame->addr - 1 - f->offset);
			if (delta > 0) {
				*flagdesc2 = rz_str_newf("%s+%d", f->name, delta + 1);
			} else if (delta < 0) {
				*flagdesc2 = rz_str_newf("%s%d", f->name, delta + 1);
			} else {
				*flagdesc2 = rz_str_newf("%s+1", f->name);
			}
		} else {
			*flagdesc2 = rz_str_newf("%s", f->name);
		}
	}
	if (!rz_str_cmp(*flagdesc, *flagdesc2, -1)) {
		free(*flagdesc2);
		*flagdesc2 = NULL;
	}
	if (pcstr && spstr) {
		if (core->dbg->bits & RZ_SYS_BITS_64) {
			*pcstr = rz_str_newf("0x%-16" PFMT64x, frame->addr);
			*spstr = rz_str_newf("0x%-16" PFMT64x, frame->sp);
		} else if (core->dbg->bits & RZ_SYS_BITS_32) {
			*pcstr = rz_str_newf("0x%-8" PFMT64x, frame->addr);
			*spstr = rz_str_newf("0x%-8" PFMT64x, frame->sp);
		} else {
			*pcstr = rz_str_newf("0x%" PFMT64x, frame->addr);
			*spstr = rz_str_newf("0x%" PFMT64x, frame->sp);
		}
	}
}

RZ_IPI void rz_core_static_debug_stop(void *u) {
	RzDebug *dbg = (RzDebug *)u;
	rz_debug_stop(dbg);
}

#if __WINDOWS__
#include "..\debug\p\native\windows\windows_message.h"
#endif

RZ_IPI void rz_core_debug_bp_add(RzCore *core, ut64 addr, const char *arg_perm, bool hwbp, bool watch) {
	RzBreakpointItem *bpi;
	int rw = 0;

	if (watch) {
		rw = rz_str_rwx(arg_perm);
		rw &= RZ_PERM_RWX; // filter out the rwx bits only
		if (rw == 0) {
			RZ_LOG_WARN("Invalid permissions provided for setting watchpoint. Defaulting to \"rw\".\n");
			rw = RZ_PERM_RW;
		}
	}
	bpi = rz_debug_bp_add(core->dbg, addr, hwbp, watch, rw, NULL, 0);
	if (!bpi) {
		RZ_LOG_ERROR("Cannot set breakpoint at 0x%" PFMT64x "\n", addr);
		return;
	}
	RzFlagItem *f = rz_core_flag_get_by_spaces(core->flags, addr);
	if (f) {
		if (addr > f->offset) {
			char *name = rz_str_newf("%s+0x%" PFMT64x, f->name, addr - f->offset);
			rz_bp_item_set_name(bpi, name);
			free(name);
		} else {
			bpi->name = strdup(f->name);
		}
	} else {
		char *name = rz_str_newf("0x%08" PFMT64x, addr);
		rz_bp_item_set_name(bpi, name);
		free(name);
	}
}

static RTreeNode *add_trace_tree_child(HtUP *ht, RTree *t, RTreeNode *cur, ut64 addr) {
	struct trace_node *t_node = ht_up_find(ht, addr, NULL);
	if (!t_node) {
		t_node = RZ_NEW0(struct trace_node);
		if (t_node) {
			t_node->addr = addr;
			t_node->refs = 1;
			ht_up_insert(ht, addr, t_node);
		}
	} else {
		t_node->refs++;
	}
	return rz_tree_add_node(t, cur, t_node);
}

static RzCore *_core = NULL;

static void trace_traverse_pre(RTreeNode *n, RTreeVisitor *vis) {
	const char *name = "";
	struct trace_node *tn = n->data;
	unsigned int i;
	if (!tn)
		return;
	for (i = 0; i < n->depth - 1; i++) {
		rz_cons_printf("  ");
	}
	if (_core) {
		RzFlagItem *f = rz_flag_get_at(_core->flags, tn->addr, true);
		if (f) {
			name = f->name;
		}
	}
	rz_cons_printf(" 0x%08" PFMT64x " refs %d %s\n", tn->addr, tn->refs, name);
}

static void trace_traverse(RTree *t) {
	RTreeVisitor vis = { 0 };

	/* clear the line on stderr, because somebody has written there */
	fprintf(stderr, "\x1b[2K\r");
	fflush(stderr);
	vis.pre_visit = (RTreeNodeVisitCb)trace_traverse_pre;
	rz_tree_dfs(t, &vis);
}

static void do_debug_trace_calls(RzCore *core, ut64 from, ut64 to, ut64 final_addr) {
	bool trace_libs = rz_config_get_i(core->config, "dbg.trace.libs");
	bool shallow_trace = rz_config_get_i(core->config, "dbg.trace.inrange");
	HtUP *tracenodes = core->dbg->tracenodes;
	RTree *tr = core->dbg->tree;
	RzDebug *dbg = core->dbg;
	ut64 debug_to = UT64_MAX;
	RTreeNode *cur;
	ut64 addr = 0;
	int n = 0;

	if (!trace_libs) {
#if NOOP
		RzList *bounds = rz_core_get_boundaries_prot(core, -1, "dbg.program", "search");
		rz_list_free(bounds);
#endif
	}

	/* set root if not already present */
	rz_tree_add_node(tr, NULL, NULL);
	cur = tr->root;

	while (true) {
		ut8 buf[32];
		RzAnalysisOp aop;
		int addr_in_range;

		if (rz_cons_is_breaked()) {
			break;
		}
		if (rz_debug_is_dead(dbg)) {
			break;
		}
		if (debug_to != UT64_MAX && !rz_debug_continue_until(dbg, debug_to)) {
			break;
		}
		if (!rz_debug_step(dbg, 1)) {
			break;
		}
		debug_to = UT64_MAX;
		if (!rz_debug_reg_sync(dbg, RZ_REG_TYPE_GPR, false)) {
			break;
		}
		addr = rz_debug_reg_get(dbg, "PC");
		if (addr == final_addr) {
			// we finished the tracing so break the loop
			break;
		}
		addr_in_range = addr >= from && addr < to;

		rz_io_read_at(core->io, addr, buf, sizeof(buf));
		rz_analysis_op(core->analysis, &aop, addr, buf, sizeof(buf), RZ_ANALYSIS_OP_MASK_BASIC);
		rz_cons_printf("%d %" PFMT64x "\r", n++, addr);
		switch (aop.type) {
		case RZ_ANALYSIS_OP_TYPE_UCALL:
		case RZ_ANALYSIS_OP_TYPE_ICALL:
		case RZ_ANALYSIS_OP_TYPE_RCALL:
		case RZ_ANALYSIS_OP_TYPE_IRCALL: {
			ut64 called_addr;
			int called_in_range;
			// store regs
			// step into
			// get pc
			rz_debug_step(dbg, 1);
			rz_debug_reg_sync(dbg, RZ_REG_TYPE_GPR, false);
			called_addr = rz_debug_reg_get(dbg, "PC");
			called_in_range = called_addr >= from && called_addr < to;
			if (!called_in_range && addr_in_range && !shallow_trace) {
				debug_to = addr + aop.size;
			}
			if (addr_in_range || shallow_trace) {
				cur = add_trace_tree_child(tracenodes, tr, cur, addr);
				if (debug_to != UT64_MAX) {
					cur = cur->parent;
				}
			}
			// TODO: push pc+aop.length into the call path stack
			break;
		}
		case RZ_ANALYSIS_OP_TYPE_CALL: {
			int called_in_range = aop.jump >= from && aop.jump < to;
			if (!called_in_range && addr_in_range && !shallow_trace) {
				debug_to = aop.addr + aop.size;
			}
			if (addr_in_range || shallow_trace) {
				cur = add_trace_tree_child(tracenodes, tr, cur, addr);
				if (debug_to != UT64_MAX) {
					cur = cur->parent;
				}
			}
			break;
		}
		case RZ_ANALYSIS_OP_TYPE_RET:
#if 0
			// TODO: we must store ret value for each call in the graph path to do this check
			rz_debug_step (dbg, 1);
			rz_debug_reg_sync (dbg, RZ_REG_TYPE_GPR, false);
			addr = rz_debug_reg_get (dbg, "PC");
			// TODO: step into and check return address if correct
			// if not correct we are hijacking the control flow (exploit!)
#endif
			if (cur != tr->root) {
				cur = cur->parent;
			}
#if 0
			if (addr != gn->addr) {
				RZ_LOG_ERROR("Oops. invalid return address 0x%08"PFMT64x
						"\n0x%08"PFMT64x"\n", addr, gn->addr);
			}
#endif
			break;
		}
	}
}

static void debug_trace_calls(RzCore *core, const char *input) {
	RzBreakpointItem *bp_final = NULL;
	int t = core->dbg->trace->enabled;
	ut64 from = 0, to = UT64_MAX, final_addr = UT64_MAX;

	if (rz_debug_is_dead(core->dbg)) {
		RZ_LOG_ERROR("No process to debug.");
		return;
	}
	if (*input == ' ') {
		input = rz_str_trim_head_ro(input);
		ut64 first_n = rz_num_math(core->num, input);
		input = strchr(input, ' ');
		if (input) {
			input = rz_str_trim_head_ro(input);
			from = first_n;
			to = rz_num_math(core->num, input);
			input = strchr(input, ' ');
			if (input) {
				input = rz_str_trim_head_ro(input);
				final_addr = rz_num_math(core->num, input);
			}
		} else {
			final_addr = first_n;
		}
	}
	core->dbg->trace->enabled = 0;
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	rz_reg_arena_swap(core->dbg->reg, true);
	if (final_addr != UT64_MAX) {
		bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
		bp_final = rz_debug_bp_add(core->dbg, final_addr, hwbp, false, 0, NULL, 0);
		if (!bp_final) {
			RZ_LOG_ERROR("Cannot set breakpoint at final address (%" PFMT64x ")\n", final_addr);
		}
	}
	do_debug_trace_calls(core, from, to, final_addr);
	if (bp_final) {
		rz_bp_del(core->dbg->bp, final_addr);
	}
	_core = core;
	trace_traverse(core->dbg->tree);
	core->dbg->trace->enabled = t;
	rz_cons_break_pop();
}

static bool cmd_dcu(RzCore *core, const char *input) {
	const char *ptr = NULL;
	ut64 from, to, pc;
	bool dcu_range = false;
	bool invalid = (!input[0] || !input[1] || !input[2]);
	if (invalid || (input[2] != ' ' && input[2] != '.')) {
		rz_core_cmd_help(core, help_msg_dcu);
		return false;
	}
	to = UT64_MAX;
	if (input[2] == '.') {
		ptr = strchr(input + 3, ' ');
		if (ptr) { // TODO: put '\0' in *ptr to avoid
			from = rz_num_tail(core->num, core->offset, input + 2);
			if (ptr[1] == '.') {
				to = rz_num_tail(core->num, core->offset, ptr + 2);
			} else {
				to = rz_num_math(core->num, ptr + 1);
			}
			dcu_range = true;
		} else {
			from = rz_num_tail(core->num, core->offset, input + 2);
		}
	} else {
		ptr = strchr(input + 3, ' ');
		if (ptr) { // TODO: put '\0' in *ptr to avoid
			from = rz_num_math(core->num, input + 3);
			if (ptr[1] == '.') {
				to = rz_num_tail(core->num, core->offset, ptr + 2);
			} else {
				to = rz_num_math(core->num, ptr + 1);
			}
			dcu_range = true;
		} else {
			from = rz_num_math(core->num, input + 3);
		}
	}
	if (core->num->nc.errors && rz_cons_is_interactive()) {
		RZ_LOG_ERROR("Cannot continue until unknown address '%s'\n", core->num->nc.calc_buf);
		return false;
	}
	if (to == UT64_MAX) {
		to = from;
	}
	if (dcu_range) {
		rz_cons_break_push(NULL, NULL);
		do {
			if (rz_cons_is_breaked()) {
				break;
			}
			rz_debug_step(core->dbg, 1);
			rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_GPR, false);
			pc = rz_debug_reg_get(core->dbg, "PC");
			RZ_LOG_ERROR("Continue 0x%08" PFMT64x " > 0x%08" PFMT64x " < 0x%08" PFMT64x "\n",
				from, pc, to);
		} while (pc < from || pc > to);
		rz_cons_break_pop();
	} else {
		return rz_core_debug_continue_until(core, from, to);
	}
	return true;
}

// dsu
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_handler(RzCore *core, int argc, const char **argv) {
	rz_reg_arena_swap(core->dbg->reg, true);
	step_until(core, rz_num_math(core->num, argv[1]));
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsui
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_instr_handler(RzCore *core, int argc, const char **argv) {
	if (!step_until_inst(core, argv[1], false)) {
		return RZ_CMD_STATUS_ERROR;
	}
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsuir
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_instr_regex_handler(RzCore *core, int argc, const char **argv) {
	if (!step_until_inst(core, argv[1], true)) {
		return RZ_CMD_STATUS_ERROR;
	}
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsuo
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_optype_handler(RzCore *core, int argc, const char **argv) {
	RzList *optypes_list = rz_list_new_from_array((const void **)argv + 1, argc - 1);
	step_until_optype(core, optypes_list);
	rz_core_dbg_follow_seek_register(core);
	rz_list_free(optypes_list);
	return RZ_CMD_STATUS_OK;
}

// dsue
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_esil_handler(RzCore *core, int argc, const char **argv) {
	step_until_esil(core, argv[1]);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsuf
RZ_IPI RzCmdStatus rz_cmd_debug_step_until_flag_handler(RzCore *core, int argc, const char **argv) {
	step_until_flag(core, argv[1]);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dt
RZ_IPI RzCmdStatus rz_cmd_debug_traces_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_trace_list(core->dbg, 0, core->offset);
	return RZ_CMD_STATUS_OK;
}

// dt% TODO: ?
RZ_IPI RzCmdStatus rz_cmd_debug_trace_pers_handler(RzCore *core, int argc, const char **argv) {
	return RZ_CMD_STATUS_OK;
}

// dt*
RZ_IPI RzCmdStatus rz_cmd_debug_trace_star_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_trace_list(core->dbg, 1, core->offset);
	return RZ_CMD_STATUS_OK;
}

// dt+
RZ_IPI RzCmdStatus rz_cmd_debug_trace_add_handler(RzCore *core, int argc, const char **argv) {
	ut64 addr = rz_num_math(core->num, argv[1]);
	int count = argc > 2 ? rz_num_math(core->num, argv[2]) : 1;
	RzAnalysisOp *op = rz_core_op_analysis(core, addr, RZ_ANALYSIS_OP_MASK_HINT);
	if (op) {
		RzDebugTracepoint *tp = rz_debug_trace_add(core->dbg, addr, op->size);
		if (!tp) {
			rz_analysis_op_free(op);
			return RZ_CMD_STATUS_OK;
		}
		tp->count = count;
		rz_analysis_trace_bb(core->analysis, addr);
		rz_analysis_op_free(op);
	} else {
		RZ_LOG_ERROR("Cannot analyze opcode at 0x%08" PFMT64x "\n", addr);
	}
	return RZ_CMD_STATUS_OK;
}

// dt++
RZ_IPI RzCmdStatus rz_cmd_debug_trace_add_addrs_handler(RzCore *core, int argc, const char **argv) {
	for (int i = 1; i < argc; ++i) {
		ut64 addr = rz_num_get(NULL, argv[i]);
		(void)rz_debug_trace_add(core->dbg, addr, 1);
	}
	return RZ_CMD_STATUS_OK;
}

// dt-
RZ_IPI RzCmdStatus rz_cmd_debug_traces_reset_handler(RzCore *core, int argc, const char **argv) {
	rz_tree_reset(core->dbg->tree);
	rz_debug_trace_free(core->dbg->trace);
	rz_debug_tracenodes_reset(core->dbg);
	core->dbg->trace = rz_debug_trace_new();
	return RZ_CMD_STATUS_OK;
}

// dt=
RZ_IPI RzCmdStatus rz_cmd_debug_trace_equal_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_trace_list(core->dbg, '=', core->offset);
	return RZ_CMD_STATUS_OK;
}

// dta
RZ_IPI int rz_cmd_debug_trace_addr(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	rz_debug_trace_at(core->dbg, input + 3);
	return 0;
}

// dtc
RZ_IPI int rz_cmd_debug_trace_dtc(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	debug_trace_calls(core, input + 2);
	return RZ_CMD_STATUS_OK;
}

// dtd
RZ_IPI RzCmdStatus rz_cmd_debug_traces_dtd_handler(RzCore *core, int argc, const char **argv, RzOutputMode mode) {
	int min = argc > 1 ? (int)rz_num_math(core->num, argv[1]) : 0;
	RzDebugTracepoint *trace;
	RzListIter *iter;
	RzAnalysisOp *op;
	int n = 0;

	switch (mode) {
	case RZ_OUTPUT_MODE_QUIET:
		rz_list_foreach (core->dbg->trace->traces, iter, trace) {
			if (n >= min) {
				rz_cons_printf("%d  ", trace->count);
				rz_cons_printf("0x%08" PFMT64x "\n", trace->addr);
				break;
			}
			n++;
		}
		break;
	case RZ_OUTPUT_MODE_STANDARD:
		rz_list_foreach (core->dbg->trace->traces, iter, trace) {
			op = rz_core_analysis_op(core, trace->addr, RZ_ANALYSIS_OP_MASK_BASIC | RZ_ANALYSIS_OP_MASK_DISASM);
			if (n >= min) {
				rz_cons_printf("0x%08" PFMT64x " %s\n", trace->addr, op->mnemonic);
			}
			n++;
			rz_analysis_op_free(op);
		}
		break;
	default:
		rz_warn_if_reached();
		break;
	}
	return RZ_CMD_STATUS_OK;
}

// dte
RZ_IPI RzCmdStatus rz_cmd_debug_traces_esil_handler(RzCore *core, int argc, const char **argv) {
	rz_analysis_esil_trace_list(core->analysis->esil);
	return RZ_CMD_STATUS_OK;
}

// dte-*
RZ_IPI RzCmdStatus rz_cmd_debug_traces_esil_delete_handler(RzCore *core, int argc, const char **argv) {
	if (core->analysis->esil) {
		rz_pvector_free(core->analysis->esil->trace->instructions);
		core->analysis->esil->trace->instructions = rz_pvector_new((RzPVectorFree)rz_analysis_il_trace_instruction_free);
	}
	return RZ_CMD_STATUS_OK;
}

// dtei
RZ_IPI RzCmdStatus rz_cmd_debug_traces_esil_i_handler(RzCore *core, int argc, const char **argv) {
	ut64 addr = argc > 1 ? rz_num_math(core->num, argv[1]) : core->offset;
	RzAnalysisOp *op = rz_core_analysis_op(core, addr, RZ_ANALYSIS_OP_MASK_ESIL);
	if (op) {
		rz_analysis_esil_trace_op(core->analysis->esil, op);
	}
	rz_analysis_op_free(op);
	return RZ_CMD_STATUS_OK;
}

// dtg
RZ_IPI RzCmdStatus rz_cmd_debug_trace_graph_handler(RzCore *core, int argc, const char **argv) {
	dot_trace_traverse(core, core->dbg->tree, '\0');
	return RZ_CMD_STATUS_OK;
}

// dtg*
RZ_IPI RzCmdStatus rz_cmd_debug_trace_graph_star_handler(RzCore *core, int argc, const char **argv) {
	dot_trace_traverse(core, core->dbg->tree, '*');
	return RZ_CMD_STATUS_OK;
}

// dtgi
RZ_IPI RzCmdStatus rz_cmd_debug_trace_interactive_handler(RzCore *core, int argc, const char **argv) {
	dot_trace_traverse(core, core->dbg->tree, 'i');
	return RZ_CMD_STATUS_OK;
}

// dts+
RZ_IPI RzCmdStatus rz_cmd_debug_start_trace_session_handler(RzCore *core, int argc, const char **argv) {
	if (rz_debug_is_dead(core->dbg)) {
		RZ_LOG_ERROR("Cannot start session outside of debug mode, run ood?\n");
		return RZ_CMD_STATUS_ERROR;
	}
	if (core->dbg->session) {
		RZ_LOG_ERROR("Session already started\n");
		return RZ_CMD_STATUS_ERROR;
	}
	core->dbg->session = rz_debug_session_new();
	rz_debug_add_checkpoint(core->dbg);
	return RZ_CMD_STATUS_OK;
}

// dts-
RZ_IPI RzCmdStatus rz_cmd_debug_stop_trace_session_handler(RzCore *core, int argc, const char **argv) {
	if (!core->dbg->session) {
		RZ_LOG_ERROR("No session started\n");
		return RZ_CMD_STATUS_ERROR;
	}
	rz_debug_session_free(core->dbg->session);
	core->dbg->session = NULL;
	return RZ_CMD_STATUS_OK;
}

// dtst
RZ_IPI RzCmdStatus rz_cmd_debug_save_trace_session_handler(RzCore *core, int argc, const char **argv) {
	if (!core->dbg->session) {
		RZ_LOG_ERROR("No session started\n");
		return RZ_CMD_STATUS_ERROR;
	}
	rz_debug_session_save(core->dbg->session, argv[1]);
	return RZ_CMD_STATUS_OK;
}

// dtsf
RZ_IPI RzCmdStatus rz_cmd_debug_load_trace_session_handler(RzCore *core, int argc, const char **argv) {
	if (core->dbg->session) {
		rz_debug_session_free(core->dbg->session);
		core->dbg->session = NULL;
	}
	core->dbg->session = rz_debug_session_new();
	rz_debug_session_load(core->dbg, argv[1]);
	return RZ_CMD_STATUS_OK;
}

// dtsm
RZ_IPI RzCmdStatus rz_cmd_debug_list_trace_session_mmap_handler(RzCore *core, int argc, const char **argv) {
	if (core->dbg->session) {
		rz_debug_session_list_memory(core->dbg);
	}
	return RZ_CMD_STATUS_OK;
}

// dtt
RZ_IPI RzCmdStatus rz_cmd_debug_trace_tag_handler(RzCore *core, int argc, const char **argv) {
	int tag = atoi(argv[1]);
	rz_debug_trace_tag(core->dbg, tag);
	return RZ_CMD_STATUS_OK;
}

static char *get_corefile_name(const char *raw_name, int pid) {
	return (!*raw_name) ? rz_str_newf("core.%u", pid) : rz_str_trim_dup(raw_name);
}

// ds
RZ_IPI RzCmdStatus rz_cmd_debug_step_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	rz_core_debug_step_one(core, times);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsb
RZ_IPI RzCmdStatus rz_cmd_debug_step_back_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	if (rz_config_get_b(core->config, "cfg.debug")) {
		if (!core->dbg->session) {
			RZ_LOG_ERROR("Session has not started\n");
		} else if (rz_debug_step_back(core->dbg, times) < 0) {
			RZ_LOG_ERROR("Error: stepping back failed\n");
		} else {
			rz_core_reg_update_flags(core);
		}
	} else {
		if (!rz_core_esil_step_back(core)) {
			RZ_LOG_ERROR("cannot step back\n");
		}
	}
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsf
RZ_IPI RzCmdStatus rz_cmd_debug_step_frame_handler(RzCore *core, int argc, const char **argv) {
	step_until_eof(core);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}
// dsi
RZ_IPI RzCmdStatus rz_cmd_debug_step_cond_handler(RzCore *core, int argc, const char **argv) {
	int n = 0;
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	do {
		if (rz_cons_is_breaked()) {
			break;
		}
		rz_debug_step(core->dbg, 1);
		if (rz_debug_is_dead(core->dbg)) {
			core->break_loop = true;
			break;
		}
		rz_core_reg_update_flags(core);
		n++;
	} while (!rz_num_conditional(core->num, argv[1]));
	rz_cons_break_pop();
	RZ_LOG_ERROR("Stopped after %d instructions\n", n);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsl
RZ_IPI RzCmdStatus rz_cmd_debug_step_line_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	rz_reg_arena_swap(core->dbg->reg, true);
	step_line(core, times);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dso
RZ_IPI RzCmdStatus rz_cmd_debug_step_over_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	if (rz_config_get_i(core->config, "dbg.skipover")) {
		rz_core_cmdf(core, "dss %s", argv[1]);
	} else {
		if (rz_core_is_debug(core)) {
			bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
			ut64 addr = rz_debug_reg_get(core->dbg, "PC");
			RzBreakpointItem *bpi = rz_bp_get_at(core->dbg->bp, addr);
			rz_bp_del(core->dbg->bp, addr);
			rz_reg_arena_swap(core->dbg->reg, true);
			rz_debug_step_over(core->dbg, times);
			if (bpi) {
				(void)rz_debug_bp_add(core->dbg, addr, hwbp, false, 0, NULL, 0);
			}
			rz_core_reg_update_flags(core);
		} else {
			for (int i = 0; i < times; i++) {
				rz_core_analysis_esil_step_over(core);
			}
		}
	}
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dsp
RZ_IPI RzCmdStatus rz_cmd_debug_step_prog_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	rz_reg_arena_swap(core->dbg->reg, true);
	for (int i = 0; i < times; i++) {
		ut8 buf[64];
		ut64 addr;
		RzAnalysisOp aop;
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_GPR, false);
		addr = rz_debug_reg_get(core->dbg, "PC");
		rz_io_read_at(core->io, addr, buf, sizeof(buf));
		rz_analysis_op(core->analysis, &aop, addr, buf, sizeof(buf), RZ_ANALYSIS_OP_MASK_BASIC);
		if (aop.type == RZ_ANALYSIS_OP_TYPE_CALL) {
			RzBinObject *o = rz_bin_cur_object(core->bin);
			RzBinSection *s = rz_bin_get_section_at(o, aop.jump, true);
			if (!s) {
				rz_debug_step_over(core->dbg, times);
				continue;
			}
		}
		rz_debug_step(core->dbg, 1);
	}
	rz_core_reg_update_flags(core);
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dss
RZ_IPI RzCmdStatus rz_cmd_debug_step_skip_handler(RzCore *core, int argc, const char **argv) {
	const int times = argc > 1 ? rz_num_math(core->num, argv[1]) : 1;
	bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
	ut64 addr = rz_debug_reg_get(core->dbg, "PC");
	ut8 buf[64];
	RzAnalysisOp aop;
	RzBreakpointItem *bpi = rz_bp_get_at(core->dbg->bp, addr);
	rz_reg_arena_swap(core->dbg->reg, true);
	for (int i = 0; i < times; i++) {
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_GPR, false);
		rz_io_read_at(core->io, addr, buf, sizeof(buf));
		rz_analysis_op(core->analysis, &aop, addr, buf, sizeof(buf), RZ_ANALYSIS_OP_MASK_BASIC);
#if 0
				if (aop.jump != UT64_MAX && aop.fail != UT64_MAX) {
					RZ_LOG_ERROR ("Don't know how to skip this instruction\n");
					if (bpi) rz_core_cmd0 (core, delb);
					break;
				}
#endif
		addr += aop.size;
	}
	rz_debug_reg_set(core->dbg, "PC", addr);
	rz_reg_setv(core->analysis->reg, "PC", addr);
	rz_core_reg_update_flags(core);
	if (bpi) {
		(void)rz_debug_bp_add(core->dbg, addr, hwbp, false, 0, NULL, 0);
	}
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

static ut8 *getFileData(RzCore *core, const char *arg) {
	if (*arg == '$') {
		return (ut8 *)rz_cmd_alias_get(core->rcmd, arg, 1);
	}
	return (ut8 *)rz_file_slurp(arg, NULL);
}

static void consumeBuffer(RzBuffer *buf, const char *cmd, const char *errmsg) {
	if (!buf) {
		if (errmsg) {
			rz_cons_printf("%s\n", errmsg);
		}
		return;
	}
	if (cmd) {
		rz_cons_printf("%s", cmd);
	}
	int i;
	rz_buf_seek(buf, 0, RZ_BUF_SET);
	for (i = 0; i < rz_buf_size(buf); i++) {
		ut8 tmp;
		if (!rz_buf_read8(buf, &tmp)) {
			return;
		}
		rz_cons_printf("%02x", tmp);
	}
	rz_cons_printf("\n");
}

// db
RZ_IPI RzCmdStatus rz_cmd_debug_add_bp_handler(RzCore *core, int argc, const char **argv) {
	bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
	rz_core_debug_bp_add(core, core->offset, NULL, hwbp, false);
	return RZ_CMD_STATUS_OK;
}

// dbl
RZ_IPI RzCmdStatus rz_cmd_debug_list_bp_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	rz_return_val_if_fail(state && core->dbg && core->dbg->bp, RZ_CMD_STATUS_ERROR);
	RzBreakpointItem *b;
	RzListIter *iter;
	PJ *pj = state->d.pj;
	RzTable *t = state->d.t;
	rz_cmd_state_output_array_start(state);
	rz_cmd_state_output_set_columnsf(state, "XXdsssssssss", "start", "end", "size",
		"perm", "hwsw", "type", "state", "valid", "cmd", "cond", "name", "module");

	rz_list_foreach (core->dbg->bp->bps, iter, b) {
		switch (state->mode) {
		case RZ_OUTPUT_MODE_STANDARD:
			rz_cons_printf("0x%08" PFMT64x " - 0x%08" PFMT64x
				       " %d %s %s %s %s %s cmd=\"%s\" cond=\"%s\" "
				       "name=\"%s\" module=\"%s\"\n",
				b->addr, b->addr + b->size, b->size,
				rz_str_rwx_i(b->perm),
				b->hw ? "hw" : "sw",
				b->trace ? "trace" : "break",
				b->enabled ? "enabled" : "disabled",
				rz_bp_is_valid(core->dbg->bp, b) ? "valid" : "invalid",
				rz_str_get(b->data),
				rz_str_get(b->cond),
				rz_str_get(b->name),
				rz_str_get(b->module_name));
			break;
		case RZ_OUTPUT_MODE_TABLE:
			rz_table_add_rowf(t, "XXdsssssssss", b->addr, b->addr + b->size, b->size,
				rz_str_rwx_i(b->perm), b->hw ? "hw" : "sw", b->trace ? "trace" : "break",
				b->enabled ? "enabled" : "disabled", rz_bp_is_valid(core->dbg->bp, b) ? "valid" : "invalid",
				rz_str_get(b->data), rz_str_get(b->cond), rz_str_get(b->name), rz_str_get(b->module_name));
			break;
		case RZ_OUTPUT_MODE_RIZIN:
			if (b->module_name) {
				rz_cons_printf("dbm %s %" PFMT64d "\n", b->module_name, b->module_delta);
			} else {
				rz_cons_printf("db @ 0x%08" PFMT64x "\n", b->addr);
			}
			break;
		case RZ_OUTPUT_MODE_JSON:
			pj_o(pj);
			pj_kN(pj, "addr", b->addr);
			pj_ki(pj, "size", b->size);
			pj_ks(pj, "perm", rz_str_rwx_i(b->perm));
			pj_kb(pj, "hw", b->hw);
			pj_kb(pj, "trace", b->trace);
			pj_kb(pj, "enabled", b->enabled);
			pj_kb(pj, "valid", rz_bp_is_valid(core->dbg->bp, b));
			pj_ks(pj, "data", rz_str_get(b->data));
			pj_ks(pj, "cond", rz_str_get(b->cond));
			pj_end(pj);
			break;
		case RZ_OUTPUT_MODE_QUIET:
			rz_cons_printf("0x%08" PFMT64x "\n", b->addr);
			break;
		default:
			rz_warn_if_reached();
			break;
		}
	}
	rz_cmd_state_output_array_end(state);

	return RZ_CMD_STATUS_OK;
}

// dbH
RZ_IPI RzCmdStatus rz_cmd_debug_add_hw_bp_handler(RzCore *core, int argc, const char **argv) {
	rz_core_debug_bp_add(core, core->offset, NULL, true, false);
	return RZ_CMD_STATUS_OK;
}

// db-
RZ_IPI RzCmdStatus rz_cmd_debug_remove_bp_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_del(core->dbg->bp, core->offset)) {
		RZ_LOG_ERROR("Failed to delete breakpoint at 0x%" PFMT64x "\n", core->offset);
	}
	return RZ_CMD_STATUS_OK;
}

// db-*
RZ_IPI RzCmdStatus rz_cmd_debug_remove_all_bp_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_del_all(core->dbg->bp)) {
		RZ_LOG_ERROR("Failed to delete all breakpoints\n");
		return RZ_CMD_STATUS_ERROR;
	}
	return RZ_CMD_STATUS_OK;
}

// db.
RZ_IPI RzCmdStatus rz_cmd_debug_show_cur_bp_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *cur = rz_bp_get_at(core->dbg->bp, core->offset);
	if (!cur) {
		rz_cons_printf("No breakpoint found at current offset (0x%" PFMT64x ")\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	}
	rz_cons_printf("breakpoint %s %s %s\n", rz_str_rwx_i(cur->perm), cur->enabled ? "enabled" : "disabled", cur->name ? cur->name : "");
	return RZ_CMD_STATUS_OK;
}

// dbc
RZ_IPI RzCmdStatus rz_cmd_debug_command_bp_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bp = rz_bp_get_at(core->dbg->bp, core->offset);
	if (!bp) {
		RZ_LOG_ERROR("No breakpoint defined at 0x%08" PFMT64x "\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	}
	if (!rz_bp_item_set_data(bp, argv[1])) {
		RZ_LOG_ERROR("Failed to set data for breakpoint at 0x%08" PFMT64x "\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	}
	return RZ_CMD_STATUS_OK;
}

// dbC
RZ_IPI RzCmdStatus rz_cmd_debug_add_cond_bp_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bp = rz_bp_get_at(core->dbg->bp, core->offset);
	if (!bp) {
		RZ_LOG_ERROR("No breakpoint defined at 0x%08" PFMT64x "\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	}
	if (!rz_bp_item_set_cond(bp, argv[1])) {
		RZ_LOG_ERROR("Failed to set condition for breakpoint at 0x%08" PFMT64x "\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	}
	return RZ_CMD_STATUS_OK;
}

// dbd
RZ_IPI RzCmdStatus rz_cmd_debug_disable_bp_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_enable(core->dbg->bp, core->offset, false, 1)) {
		RZ_LOG_ERROR("Failed to disable breakpoint at 0x%" PFMT64x "\n", core->offset);
	}
	return RZ_CMD_STATUS_OK;
}

// dbe
RZ_IPI RzCmdStatus rz_cmd_debug_enable_bp_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_enable(core->dbg->bp, core->offset, true, 1)) { // correct value of count?
		RZ_LOG_ERROR("Failed to enable breakpoint at 0x%" PFMT64x "\n", core->offset);
	}
	return RZ_CMD_STATUS_OK;
}

// dbs
RZ_IPI RzCmdStatus rz_cmd_debug_toggle_bp_handler(RzCore *core, int argc, const char **argv) {
	rz_core_debug_breakpoint_toggle(core, core->offset);
	return RZ_CMD_STATUS_OK;
}

// dbf
RZ_IPI RzCmdStatus rz_cmd_debug_add_bp_noreturn_func_handler(RzCore *core, int argc, const char **argv) {
	rz_core_debug_bp_add_noreturn_func(core);
	return RZ_CMD_STATUS_OK;
}

// dbm
RZ_IPI RzCmdStatus rz_cmd_debug_add_bp_module_handler(RzCore *core, int argc, const char **argv) {
	bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
	ut64 delta = rz_num_math(core->num, argv[2]);
	RzBreakpointItem *bp = rz_debug_bp_add(core->dbg, 0, hwbp, false, 0, argv[1], delta);
	if (!bp) {
		RZ_LOG_ERROR("Cannot set breakpoint.\n");
	}
	return RZ_CMD_STATUS_OK;
}

// dbn
RZ_IPI RzCmdStatus rz_cmd_debug_name_bp_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bp = rz_bp_get_at(core->dbg->bp, core->offset);
	if (!bp) {
		RZ_LOG_ERROR("No breakpoint found at 0x%08" PFMT64x "\n", core->offset);
		return RZ_CMD_STATUS_ERROR;
	} else if (argc == 1) {
		if (bp->name) {
			rz_cons_println(bp->name);
		}
	} else if (argc == 2) {
		rz_bp_item_set_name(bp, argv[1]);
	}
	return RZ_CMD_STATUS_OK;
}

// dbi
RZ_IPI RzCmdStatus rz_cmd_debug_show_bp_index_handler(RzCore *core, int argc, const char **argv) {
	const int index = rz_bp_get_index_at(core->dbg->bp, core->offset);
	if (index == -1) {
		RZ_LOG_ERROR("No breakpoint found at %" PFMT64x "\n", core->offset);
	} else {
		rz_cons_printf("%d\n", index);
	}
	return RZ_CMD_STATUS_OK;
}

// dbil
RZ_IPI RzCmdStatus rz_cmd_debug_list_bp_indexes_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	RzListIter *iter;
	unsigned int index = 0;
	rz_list_foreach (core->dbg->bp->bps, iter, bpi) {
		if (!bpi) {
			RZ_LOG_ERROR("Unable to find breakpoint at index %d\n", index);
		} else {
			rz_cons_printf("%d 0x%08" PFMT64x " E:%d T:%d\n", index, bpi->addr, bpi->enabled, bpi->trace);
		}
		index++;
	}
	return RZ_CMD_STATUS_OK;
}

// dbi-
RZ_IPI RzCmdStatus rz_cmd_debug_remove_bp_index_handler(RzCore *core, int argc, const char **argv) {
	ut64 addr;
	for (int i = 1; i < argc; i++) {
		addr = rz_num_math(core->num, argv[i]);
		if (!rz_bp_del_index(core->dbg->bp, addr)) {
			RZ_LOG_ERROR("No breakpoint found at %" PFMT64x "\n", addr);
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dbix
RZ_IPI RzCmdStatus rz_cmd_debug_set_expr_bp_index_handler(RzCore *core, int argc, const char **argv) {
	int index = rz_num_math(core->num, argv[1]);
	RzBreakpointItem *bpi = rz_bp_get_index(core->dbg->bp, index);
	rz_bp_item_set_expr(bpi, argv[2]);
	return RZ_CMD_STATUS_OK;
}

// dbic
RZ_IPI RzCmdStatus rz_cmd_debug_run_command_bp_index_handler(RzCore *core, int argc, const char **argv) {
	int index = rz_num_math(core->num, argv[1]);
	RzBreakpointItem *bpi = rz_bp_get_index(core->dbg->bp, index);
	rz_bp_item_set_data(bpi, argv[2]);
	return RZ_CMD_STATUS_OK;
}

// dbie
RZ_IPI RzCmdStatus rz_cmd_debug_enable_bp_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->enabled = true;
	return RZ_CMD_STATUS_OK;
}

// dbid
RZ_IPI RzCmdStatus rz_cmd_debug_disable_bp_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->enabled = false;
	return RZ_CMD_STATUS_OK;
}

// dbis
RZ_IPI RzCmdStatus rz_cmd_debug_toggle_bp_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->enabled = !bpi->enabled;
	return RZ_CMD_STATUS_OK;
}

// dbite
RZ_IPI RzCmdStatus rz_cmd_debug_enable_bp_trace_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->trace = true;
	return RZ_CMD_STATUS_OK;
}

// dbitd
RZ_IPI RzCmdStatus rz_cmd_debug_disable_bp_trace_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->trace = false;
	return RZ_CMD_STATUS_OK;
}

// dbits
RZ_IPI RzCmdStatus rz_cmd_debug_toggle_bp_trace_index_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi;
	int index = rz_num_math(core->num, argv[1]);
	bpi = rz_bp_get_index(core->dbg->bp, index);
	if (!bpi) {
		RZ_LOG_ERROR("Unable to find breakpoint with index %d\n", index);
		return RZ_CMD_STATUS_ERROR;
	}
	bpi->trace = !bpi->enabled;
	return RZ_CMD_STATUS_OK;
}

// dbh
RZ_IPI RzCmdStatus rz_cmd_debug_bp_plugin_handler(RzCore *core, int argc, const char **argv) {
	if (argc == 1) {
		rz_bp_plugin_list(core->dbg->bp);
	} else if (argc == 2) {
		if (!rz_bp_use(core->dbg->bp, argv[1])) {
			RZ_LOG_ERROR("Failed to set breakpoint plugin handler to %s\n", argv[1]);
			return RZ_CMD_STATUS_ERROR;
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dbh-
RZ_IPI RzCmdStatus rz_cmd_debug_remove_bp_plugin_handler(RzCore *core, int argc, const char **argv) {
	for (int i = 1; i < argc; i++) {
		if (!rz_bp_plugin_del(core->dbg->bp, argv[i])) {
			RZ_LOG_ERROR("Failed to delete breakpoint plugin handler: %s\n", argv[i]);
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dbt
RZ_IPI RzCmdStatus rz_cmd_debug_display_bt_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	RzList *list = rz_debug_frames(core->dbg, UT64_MAX);
	if (!list) {
		RZ_LOG_ERROR("Unable to find debug backtrace frames\n");
		return RZ_CMD_STATUS_ERROR;
	}
	int i = 0;
	RzListIter *iter;
	RzDebugFrame *frame;
	RzOutputMode mode = state->mode;
	PJ *pj = state->d.pj;

	if (mode == RZ_OUTPUT_MODE_RIZIN) {
		rz_list_reverse(list);
		rz_cons_printf("f-bt.*\n");
	}
	rz_cmd_state_output_array_start(state);
	rz_list_foreach (list, iter, frame) {
		switch (mode) {
		case RZ_OUTPUT_MODE_STANDARD: {
			char *flagdesc, *flagdesc2, *pcstr, *spstr;
			get_backtrace_info(core, frame, UT64_MAX, &flagdesc, &flagdesc2, &pcstr, &spstr);
			RzAnalysisFunction *fcn = rz_analysis_get_fcn_in(core->analysis, frame->addr, 0);
			rz_cons_printf("%d  %s sp: %s  %-5d"
				       "[%s]  %s %s\n",
				i++, pcstr, spstr, (int)frame->size, fcn ? fcn->name : "??", flagdesc ? flagdesc : "", flagdesc2 ? flagdesc2 : "");
			free(flagdesc);
			free(flagdesc2);
			free(pcstr);
			free(spstr);
			break;
		}
		case RZ_OUTPUT_MODE_RIZIN: {
			rz_cons_printf("f bt.frame%d @ 0x%08" PFMT64x "\n", i, frame->addr);
			rz_cons_printf("f bt.frame%d.stack %d @ 0x%08" PFMT64x "\n", i, frame->size, frame->sp);
			i++;
			break;
		}
		case RZ_OUTPUT_MODE_JSON: {
			char *flagdesc, *flagdesc2, *desc;
			get_backtrace_info(core, frame, UT64_MAX, &flagdesc, &flagdesc2, NULL, NULL);
			RzAnalysisFunction *fcn = rz_analysis_get_fcn_in(core->analysis, frame->addr, 0);
			desc = rz_str_newf("%s%s", rz_str_get_null(flagdesc), rz_str_get_null(flagdesc2));
			pj_o(pj);
			pj_ki(pj, "idx", i);
			pj_kn(pj, "pc", frame->addr);
			pj_kn(pj, "sp", frame->sp);
			pj_ki(pj, "frame_size", frame->size);
			pj_ks(pj, "fname", fcn ? fcn->name : "");
			pj_ks(pj, "desc", desc);
			pj_end(pj);
			i++;
			free(flagdesc);
			free(flagdesc2);
			free(desc);
			break;
		}
		case RZ_OUTPUT_MODE_QUIET: {
			char *flagdesc, *flagdesc2, *pcstr, *spstr;
			get_backtrace_info(core, frame, UT64_MAX, &flagdesc, &flagdesc2, &pcstr, &spstr);
			rz_cons_printf("%s\n", pcstr);
			free(flagdesc);
			free(flagdesc2);
			free(pcstr);
			free(spstr);
			break;
		}
		default:
			rz_warn_if_reached();
			break;
		}
	}
	rz_cmd_state_output_array_end(state);
	rz_list_free(list);
	return RZ_CMD_STATUS_OK;
}

// dbt=
RZ_IPI RzCmdStatus rz_cmd_debug_display_bt_oneline_handler(RzCore *core, int argc, const char **argv) {
	int mode = 0;
	if (argc > 1) {
		if (!strcmp(argv[1], "b")) {
			mode = 1;
		} else if (!strcmp(argv[1], "s")) {
			mode = 2;
		}
	}
	RzList *list = rz_debug_frames(core->dbg, UT64_MAX);
	if (!list) {
		RZ_LOG_ERROR("Unable to find debug backtrace frames\n");
		return RZ_CMD_STATUS_ERROR;
	}
	int i = 0;
	RzListIter *iter;
	RzDebugFrame *frame;
	rz_list_reverse(list);
	rz_list_foreach (list, iter, frame) {
		if (i != 0) {
			rz_cons_printf(" ");
		}
		switch (mode) {
		case 0:
			rz_cons_printf("0x08%" PFMT64x, frame->addr);
			break;
		case 1:
			rz_cons_printf("0x08%" PFMT64x, frame->bp);
			break;
		case 2:
			rz_cons_printf("0x08%" PFMT64x, frame->sp);
			break;
		}
	}
	rz_cons_newline();
	rz_list_free(list);
	return RZ_CMD_STATUS_OK;
}

// dbtv
RZ_IPI RzCmdStatus rz_cmd_debug_display_bt_local_vars_handler(RzCore *core, int argc, const char **argv) {
	RzList *list = rz_debug_frames(core->dbg, UT64_MAX);
	if (!list) {
		RZ_LOG_ERROR("Unable to find debug backtrace frames\n");
		return RZ_CMD_STATUS_ERROR;
	}
	backtrace_vars(core, list);
	rz_list_free(list);
	return RZ_CMD_STATUS_OK;
}

// dbta
RZ_IPI RzCmdStatus rz_cmd_debug_display_bt_ascii_handler(RzCore *core, int argc, const char **argv) {
	RzList *list = rz_debug_frames(core->dbg, UT64_MAX);
	if (!list) {
		RZ_LOG_ERROR("Unable to find debug backtrace frames\n");
		return RZ_CMD_STATUS_ERROR;
	}
	asciiart_backtrace(core, list);
	rz_list_free(list);
	return RZ_CMD_STATUS_OK;
}

// dbte
RZ_IPI RzCmdStatus rz_cmd_debug_bt_enable_bp_trace_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_set_trace(core->dbg->bp, core->offset, true)) {
		RZ_LOG_ERROR("Failed to enable trace for breakpoint at 0x%" PFMT64x "\n", core->offset);
	}
	return RZ_CMD_STATUS_OK;
}

// dbtd
RZ_IPI RzCmdStatus rz_cmd_debug_bt_disable_bp_trace_handler(RzCore *core, int argc, const char **argv) {
	if (!rz_bp_set_trace(core->dbg->bp, core->offset, false)) {
		RZ_LOG_ERROR("Failed to enable trace for breakpoint at 0x%" PFMT64x "\n", core->offset);
	}
	return RZ_CMD_STATUS_OK;
}

// dbts
RZ_IPI RzCmdStatus rz_cmd_debug_bt_toggle_bp_trace_handler(RzCore *core, int argc, const char **argv) {
	RzBreakpointItem *bpi = rz_bp_get_in(core->dbg->bp, core->offset, 0);
	if (!bpi) {
		RZ_LOG_ERROR("No breakpoint found at 0x%" PFMT64x "\n", core->offset);
	} else {
		bpi->trace = !bpi->trace;
	}
	return RZ_CMD_STATUS_OK;
}

// dbx
RZ_IPI RzCmdStatus rz_cmd_debug_bp_set_expr_cur_offset_handler(RzCore *core, int argc, const char **argv) {
	if (argc == 1) {
		RzBreakpointItem *bpi;
		RzListIter *iter;
		rz_list_foreach (core->dbg->bp->bps, iter, bpi) {
			rz_cons_printf("0x%08" PFMT64x " %s\n", bpi->addr, rz_str_get(bpi->expr));
		}
	} else if (argc == 2) {
		RzBreakpointItem *bpi = rz_bp_get_at(core->dbg->bp, core->offset);
		if (!bpi) {
			RZ_LOG_ERROR("No breakpoint found at current offset: 0x%" PFMT64x "\n", core->offset);
			return RZ_CMD_STATUS_ERROR;
		}
		if (!rz_bp_item_set_expr(bpi, argv[1])) {
			RZ_LOG_ERROR("Failed to set expression\n");
			return RZ_CMD_STATUS_ERROR;
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dbw
RZ_IPI RzCmdStatus rz_cmd_debug_add_watchpoint_handler(RzCore *core, int argc, const char **argv) {
	bool hwbp = rz_config_get_b(core->config, "dbg.hwbp");
	rz_core_debug_bp_add(core, core->offset, argv[1], hwbp, true);
	return RZ_CMD_STATUS_OK;
}

// dbW
RZ_IPI RzCmdStatus rz_cmd_debug_set_cond_bp_win_handler(RzCore *core, int argc, const char **argv) {
#if __WINDOWS__
	bool res;
	if (argc > 2) {
		res = rz_w32_add_winmsg_breakpoint(core->dbg, argv[1], argv[2]);
	} else {
		res = rz_w32_add_winmsg_breakpoint(core->dbg, argv[1], NULL);
	}
	if (res) {
		rz_cons_print("Breakpoint set.\n");
	} else {
		rz_cons_print("Breakpoint not set.\n");
	}
#else
	RZ_LOG_ERROR("This command is only meant for Windows systems and cannot be used by your system\n");
#endif

	return RZ_CMD_STATUS_OK;
}

// dc
RZ_IPI RzCmdStatus rz_cmd_debug_continue_execution_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	if (argc == 2) {
		int old_pid = core->dbg->pid;
		// using rz_num instead of atoi
		int pid = rz_num_math(core->num, argv[1]);
		rz_reg_arena_swap(core->dbg->reg, true);
		rz_debug_select(core->dbg, pid, core->dbg->tid);
		rz_debug_continue(core->dbg);
		rz_debug_select(core->dbg, old_pid, core->dbg->tid);
	} else {
		rz_reg_arena_swap(core->dbg->reg, true);
#if __linux__
		core->dbg->continue_all_threads = true;
#endif
		rz_debug_continue(core->dbg);
	}

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcb
RZ_IPI RzCmdStatus rz_cmd_debug_continue_back_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	if (!rz_debug_continue_back(core->dbg)) {
		RZ_LOG_ERROR("cannot continue back\n");
		return RZ_CMD_STATUS_ERROR;
	}

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcc
RZ_IPI RzCmdStatus rz_cmd_debug_continue_call_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	rz_reg_arena_swap(core->dbg->reg, true);

	rz_debug_continue_until_optype(core->dbg, RZ_ANALYSIS_OP_TYPE_CALL, 0);

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dccu
RZ_IPI RzCmdStatus rz_cmd_debug_continue_unknown_call_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	rz_debug_continue_until_optype(core->dbg, RZ_ANALYSIS_OP_TYPE_UCALL, 0);

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dce
RZ_IPI RzCmdStatus rz_cmd_debug_continue_exception_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

#if __WINDOWS__
	rz_reg_arena_swap(core->dbg->reg, true);
	rz_debug_continue_pass_exception(core->dbg);
#else
	RZ_LOG_ERROR("dce not available on this platform\n");
#endif

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcf
RZ_IPI RzCmdStatus rz_cmd_debug_continue_fork_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	RZ_LOG_ERROR("[+] Running 'dcs vfork fork clone' behind the scenes...\n");
	// we should stop in fork, vfork, and clone syscalls
	cmd_debug_cont_syscall(core, "vfork fork clone");

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dck
RZ_IPI RzCmdStatus rz_cmd_debug_continue_send_signal_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	rz_reg_arena_swap(core->dbg->reg, true);
	int signum = rz_num_math(core->num, argv[1]);

	if (argc == 3) {
		int old_pid = core->dbg->pid;
		int old_tid = core->dbg->tid;
		int pid = rz_num_math(core->num, argv[2]);
		int tid = pid; // XXX
		rz_debug_select(core->dbg, pid, tid);
		rz_debug_continue_kill(core->dbg, signum);
		rz_debug_select(core->dbg, old_pid, old_tid);
	} else {
		rz_debug_continue_kill(core->dbg, signum);
	}

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcp
RZ_IPI RzCmdStatus rz_cmd_debug_continue_mapped_io_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	RzIOMap *s;
	ut64 pc;
	int n = 0;
	bool t = core->dbg->trace->enabled;
	core->dbg->trace->enabled = false;
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	do {
		rz_debug_step(core->dbg, 1);
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_GPR, false);
		pc = rz_debug_reg_get(core->dbg, "PC");
		rz_cons_printf(" %d %" PFMT64x "\r", n++, pc);
		s = rz_io_map_get(core->io, pc);
		if (rz_cons_is_breaked()) {
			break;
		}
	} while (!s);
	rz_cons_printf("\n");
	core->dbg->trace->enabled = t;
	rz_cons_break_pop();
	return RZ_CMD_STATUS_OK;
}

// dcr
RZ_IPI RzCmdStatus rz_cmd_debug_continue_ret_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);

	rz_reg_arena_swap(core->dbg->reg, true);
	rz_debug_continue_until_optype(core->dbg, RZ_ANALYSIS_OP_TYPE_RET, 1);

	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

static inline RzCmdStatus continue_syscall(RzCore *core, const char *str) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	cmd_debug_cont_syscall(core, str);
	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcs
RZ_IPI RzCmdStatus rz_cmd_debug_continue_syscall_handler(RzCore *core, int argc, const char **argv) {
	const char *str = argc > 1 ? argv[1] : NULL;
	return continue_syscall(core, str);
}

// dcs*
RZ_IPI RzCmdStatus rz_cmd_debug_trace_syscall_handler(RzCore *core, int argc, const char **argv) {
	return continue_syscall(core, "-1");
}

// dct
RZ_IPI RzCmdStatus rz_cmd_debug_continue_traptrace_handler(RzCore *core, int argc, const char **argv) {
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	if (argc == 1) {
		cmd_debug_backtrace(core, 0);
	} else {
		cmd_debug_backtrace(core, rz_num_math(core->num, argv[1]));
	}
	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dcu
RZ_IPI int rz_cmd_debug_continue_until(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	CMD_CHECK_DEBUG_DEAD(core);
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	if (input[0] == '?') {
		rz_core_cmd_help(core, help_msg_dcu);
	} else if (input[0] == '.' || input[0] == '\0') {
		cmd_dcu(core, "cu $$");
	} else {
		char *tmpinp = rz_str_newf("cu %s", input + 1);
		cmd_dcu(core, tmpinp);
		free(tmpinp);
	}
	rz_cons_break_pop();
	rz_core_dbg_follow_seek_register(core);
	return RZ_CMD_STATUS_OK;
}

// dd
RZ_IPI RzCmdStatus rz_cmd_debug_dd_handler(RzCore *core, int argc, const char **argv) {
	// TODO: handle read, readwrite, append
	RzBuffer *buf = rz_core_syscallf(core, "open", "%s, %d, %d", argv[1], 2, 0644);
	consumeBuffer(buf, "dx ", "Cannot open");
	// open file
	return RZ_CMD_STATUS_OK;
}

// ddl
RZ_IPI RzCmdStatus rz_cmd_debug_ddl_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	switch (state->mode) {
	case RZ_OUTPUT_MODE_STANDARD:
		rz_debug_desc_list(core->dbg, 0);
		break;
	case RZ_OUTPUT_MODE_RIZIN:
		rz_debug_desc_list(core->dbg, 1);
		break;
	default: rz_warn_if_reached(); break;
	}
	return RZ_CMD_STATUS_OK;
}

// dd-
RZ_IPI RzCmdStatus rz_cmd_debug_fd_close_handler(RzCore *core, int argc, const char **argv) {
	// close file
	int fd = atoi(argv[1]);
	// rz_core_cmdf (core, "dxs close %d", (int)rz_num_math ( core->num, argv[1]));
	RzBuffer *buf = rz_core_syscallf(core, "close", "%d", fd);
	consumeBuffer(buf, "dx ", "Cannot close");
	return RZ_CMD_STATUS_OK;
}

// dds
RZ_IPI RzCmdStatus rz_cmd_debug_fd_seek_handler(RzCore *core, int argc, const char **argv) {
	int fd = atoi(argv[1]);
	ut64 off = argc > 2 ? rz_num_math(core->num, argv[2]) : UT64_MAX;
	if (off == UT64_MAX || !rz_debug_desc_seek(core->dbg, fd, off)) {
		RzBuffer *buf = rz_core_syscallf(core, "lseek", "%d, 0x%" PFMT64x ", %d", fd, off, 0);
		consumeBuffer(buf, "dx ", "Cannot seek");
	}
	return RZ_CMD_STATUS_OK;
}

// ddd
RZ_IPI RzCmdStatus rz_cmd_debug_dup2_handler(RzCore *core, int argc, const char **argv) {
	int fd = atoi(argv[1]);
	ut64 newfd = argc > 2 ? rz_num_math(core->num, argv[2]) : UT64_MAX;
	if (newfd == UT64_MAX || !rz_debug_desc_dup(core->dbg, fd, newfd)) {
		RzBuffer *buf = rz_core_syscallf(core, "dup2", "%d, %d", fd, (int)newfd);
		if (buf) {
			consumeBuffer(buf, "dx ", NULL);
		} else {
			RZ_LOG_ERROR("Cannot dup %d %d\n", fd, (int)newfd);
		}
	}
	return RZ_CMD_STATUS_OK;
}

// ddr
RZ_IPI RzCmdStatus rz_cmd_debug_fd_read_handler(RzCore *core, int argc, const char **argv) {
	int fd = atoi(argv[1]);
	ut64 off = argc > 3 ? rz_num_math(core->num, argv[2]) : UT64_MAX;
	ut64 len = argc > 4 ? rz_num_math(core->num, argv[3]) : UT64_MAX;
	if (len == UT64_MAX || off == UT64_MAX ||
		!rz_debug_desc_read(core->dbg, fd, off, len)) {
		consumeBuffer(rz_core_syscallf(core, "read", "%d, 0x%" PFMT64x ", %d",
				      fd, off, (int)len),
			"dx ", "Cannot read");
	}
	return RZ_CMD_STATUS_OK;
}

// ddw
RZ_IPI RzCmdStatus rz_cmd_debug_fd_write_handler(RzCore *core, int argc, const char **argv) {
	int fd = atoi(argv[1]);
	ut64 off = argc > 3 ? rz_num_math(core->num, argv[2]) : UT64_MAX;
	ut64 len = argc > 4 ? rz_num_math(core->num, argv[3]) : UT64_MAX;
	if (len == UT64_MAX || off == UT64_MAX ||
		!rz_debug_desc_write(core->dbg, fd, off, len)) {
		RzBuffer *buf = rz_core_syscallf(core, "write", "%d, 0x%" PFMT64x ", %d", fd, off, (int)len);
		consumeBuffer(buf, "dx ", "Cannot write");
	}
	return RZ_CMD_STATUS_OK;
}

// de
RZ_IPI RzCmdStatus rz_cmd_debug_esil_list_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_esil_watch_list(core->dbg);
	return RZ_CMD_STATUS_OK;
}

// dee
RZ_IPI int rz_cmd_debug_esil_stop(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	char *line = strdup(input + 1);
	char *p, *q;
	int done = 0;
	int perm = 0, dev = 0;
	p = strchr(line, ' ');
	if (p) {
		*p++ = 0;
		if (strchr(line, 'r'))
			perm |= RZ_PERM_R;
		if (strchr(line, 'w'))
			perm |= RZ_PERM_W;
		if (strchr(line, 'x'))
			perm |= RZ_PERM_X;
		q = strchr(p, ' ');
		if (q) {
			*q++ = 0;
			dev = p[0];
			if (q) {
				rz_debug_esil_watch(core->dbg, perm, dev, q);
				done = 1;
			}
		}
	}
	if (!done) {
		const char *help_dee_msg[] = {
			"Usage:", "dee", " [perm] [reg|mem] [expr]",
			NULL
		};
		rz_core_cmd_help(core, help_dee_msg);
	}
	return 0;
}

// de-*
RZ_IPI RzCmdStatus rz_cmd_debug_esil_delete_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_esil_watch_reset(core->dbg);
	return RZ_CMD_STATUS_OK;
}

// dec
RZ_IPI RzCmdStatus rz_cmd_debug_esil_continue_handler(RzCore *core, int argc, const char **argv) {
	if (rz_debug_esil_watch_empty(core->dbg)) {
		RZ_LOG_ERROR("No esil watchpoints defined\n");
	} else {
		rz_core_analysis_esil_reinit(core);
		rz_debug_esil_prestep(core->dbg, rz_config_get_i(core->config, "esil.prestep"));
		rz_debug_esil_continue(core->dbg);
	}
	return RZ_CMD_STATUS_OK;
}

// des
RZ_IPI RzCmdStatus rz_cmd_debug_esil_step_handler(RzCore *core, int argc, const char **argv) {
	rz_core_analysis_esil_reinit(core);
	rz_debug_esil_prestep(core->dbg, rz_config_get_i(core->config, "esil.prestep"));
	// continue
	const ut32 count = rz_num_math(core->num, argv[1]);
	rz_debug_esil_step(core->dbg, count);
	return RZ_CMD_STATUS_OK;
}

// desu
RZ_IPI RzCmdStatus rz_cmd_debug_esil_until_handler(RzCore *core, int argc, const char **argv) {
	ut64 addr, naddr, fin = rz_num_math(core->num, argv[1]);
	rz_core_analysis_esil_reinit(core);
	addr = rz_debug_reg_get(core->dbg, "PC");
	while (addr != fin) {
		rz_debug_esil_prestep(core->dbg, rz_config_get_i(core->config, "esil.prestep"));
		rz_debug_esil_step(core->dbg, 1);
		naddr = rz_debug_reg_get(core->dbg, "PC");
		if (naddr == addr) {
			RZ_LOG_ERROR("Detected loophole\n");
			break;
		}
		addr = naddr;
	}
	return RZ_CMD_STATUS_OK;
}

// dg
RZ_IPI RzCmdStatus rz_cmd_debug_core_gen_handler(RzCore *core, int argc, const char **argv) {
	if (core->dbg->cur && core->dbg->cur->gcore) {
		if (core->dbg->pid == -1) {
			RZ_LOG_ERROR("Not debugging, can't write core.\n");
			return RZ_CMD_STATUS_OK;
		}
		char *corefile = get_corefile_name(argv[1], core->dbg->pid);
		rz_cons_printf("Writing to file '%s'\n", corefile);
		rz_file_rm(corefile);
		RzBuffer *dst = rz_buf_new_file(corefile, O_RDWR | O_CREAT, 0644);
		if (dst) {
			if (!core->dbg->cur->gcore(core->dbg, corefile, dst)) {
				RZ_LOG_ERROR("dg: coredump failed\n");
			}
			rz_buf_free(dst);
		} else {
			perror("rz_buf_new_file");
		}
		free(corefile);
	}
	return RZ_CMD_STATUS_OK;
}

// dH
RZ_IPI RzCmdStatus rz_cmd_debug_handler_new_handler(RzCore *core, int argc, const char **argv) {
	RZ_LOG_ERROR("TODO: transplant process\n");
	return RZ_CMD_STATUS_OK;
}

// di
RZ_IPI RzCmdStatus rz_cmd_debug_info_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {

#define P rz_cons_printf
#define PS(X, Y) \
	{ \
		escaped_str = rz_str_escape(Y); \
		rz_cons_printf(X, escaped_str); \
		free(escaped_str); \
	}

	RzDebugInfo *rdi = argc > 1 ? rz_debug_info(core->dbg, argv[1]) : NULL;
	RzDebugReasonType stop = rz_debug_stop_reason(core->dbg);
	char *escaped_str;
	const char *reason;

	switch (state->mode) {
	case RZ_OUTPUT_MODE_QUIET:
		reason = rz_debug_reason_to_string(core->dbg->reason.type);
		if (!reason) {
			reason = "none";
		}
		rz_cons_printf("%s at 0x%08" PFMT64x "\n", reason, core->dbg->stopaddr);
		break;
	case RZ_OUTPUT_MODE_STANDARD:
		if (rdi) {
			const char *s = rz_signal_to_string(core->dbg->reason.signum);
			P("type=%s\n", rz_debug_reason_to_string(core->dbg->reason.type));
			P("signal=%s\n", s ? s : "none");
			P("signum=%d\n", core->dbg->reason.signum);
			P("sigpid=%d\n", core->dbg->reason.tid);
			P("addr=0x%" PFMT64x "\n", core->dbg->reason.addr);
			P("bp_addr=0x%" PFMT64x "\n", core->dbg->reason.bp_addr);
			P("inbp=%s\n", rz_str_bool(core->dbg->reason.bp_addr));
			P("baddr=0x%" PFMT64x "\n", rz_debug_get_baddr(core->dbg, NULL));
			P("pid=%d\n", rdi->pid);
			P("tid=%d\n", rdi->tid);
			P("stopaddr=0x%" PFMT64x "\n", core->dbg->stopaddr);
			if (rdi->uid != -1) {
				P("uid=%d\n", rdi->uid);
			}
			if (rdi->gid != -1) {
				P("gid=%d\n", rdi->gid);
			}
			if (rdi->usr) {
				P("usr=%s\n", rdi->usr);
			}
			if (rdi->exe && *rdi->exe) {
				P("exe=%s\n", rdi->exe);
			}
			if (rdi->cmdline && *rdi->cmdline) {
				P("cmdline=%s\n", rdi->cmdline);
			}
			if (rdi->cwd && *rdi->cwd) {
				P("cwd=%s\n", rdi->cwd);
			}
			if (rdi->kernel_stack && *rdi->kernel_stack) {
				P("kernel_stack=\n%s\n", rdi->kernel_stack);
			}
		}
		if (stop != -1) {
			P("stopreason=%d\n", stop);
		}
		break;
	case RZ_OUTPUT_MODE_RIZIN:
		if (rdi) {
			rz_cons_printf("f dbg.signal @ %d\n", core->dbg->reason.signum);
			rz_cons_printf("f dbg.sigpid @ %d\n", core->dbg->reason.tid);
			rz_cons_printf("f dbg.inbp @ %d\n", core->dbg->reason.bp_addr ? 1 : 0);
			rz_cons_printf("f dbg.sigaddr @ 0x%" PFMT64x "\n", core->dbg->reason.addr);
			rz_cons_printf("f dbg.baddr @ 0x%" PFMT64x "\n", rz_debug_get_baddr(core->dbg, NULL));
			rz_cons_printf("f dbg.pid @ %d\n", rdi->pid);
			rz_cons_printf("f dbg.tid @ %d\n", rdi->tid);
			rz_cons_printf("f dbg.uid @ %d\n", rdi->uid);
			rz_cons_printf("f dbg.gid @ %d\n", rdi->gid);
		}
		break;
	case RZ_OUTPUT_MODE_JSON:
		P("{");
		if (rdi) {
			const char *s = rz_signal_to_string(core->dbg->reason.signum);
			P("\"type\":\"%s\",", rz_debug_reason_to_string(core->dbg->reason.type));
			P("\"signal\":\"%s\",", s ? s : "none");
			P("\"signum\":%d,", core->dbg->reason.signum);
			P("\"sigpid\":%d,", core->dbg->reason.tid);
			P("\"addr\":%" PFMT64d ",", core->dbg->reason.addr);
			P("\"inbp\":%s,", rz_str_bool(core->dbg->reason.bp_addr));
			P("\"baddr\":%" PFMT64d ",", rz_debug_get_baddr(core->dbg, NULL));
			P("\"stopaddr\":%" PFMT64d ",", core->dbg->stopaddr);
			P("\"pid\":%d,", rdi->pid);
			P("\"tid\":%d,", rdi->tid);
			P("\"uid\":%d,", rdi->uid);
			P("\"gid\":%d,", rdi->gid);
			if (rdi->usr) {
				PS("\"usr\":\"%s\",", rdi->usr);
			}
			if (rdi->exe) {
				PS("\"exe\":\"%s\",", rdi->exe);
			}
			if (rdi->cmdline) {
				PS("\"cmdline\":\"%s\",", rdi->cmdline);
			}
			if (rdi->cwd) {
				PS("\"cwd\":\"%s\",", rdi->cwd);
			}
		}
		P("\"stopreason\":%d}\n", stop);
		break;
	default:
		rz_warn_if_reached();
		break;
	}
	rz_debug_info_free(rdi);

#undef P
#undef PS

	return RZ_CMD_STATUS_OK;
}

// dk
RZ_IPI RzCmdStatus rz_cmd_debug_signal_kill_handler(RzCore *core, int argc, const char **argv) {
	int sig = atoi(argv[1]);
	rz_debug_kill(core->dbg, core->dbg->pid, core->dbg->tid, sig);
	return RZ_CMD_STATUS_OK;
}

// dkl
RZ_IPI RzCmdStatus rz_cmd_debug_signal_list_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	rz_debug_signal_list(core->dbg, state->mode);
#if 0
	RzListIter *iter;
	RzDebugSignal *ds;
	rz_cons_printf ("TODO: list signal handlers of child\n");
	RzList *list = rz_debug_kill_list (core->dbg);
	rz_list_foreach (list, iter, ds) {
		// TODO: resolve signal name by number and show handler offset
		rz_cons_printf ("--> %d\n", ds->num);
	}
	rz_list_free (list);
#endif
	return RZ_CMD_STATUS_OK;
}

// dkr
RZ_IPI RzCmdStatus rz_cmd_debug_signal_resolver_handler(RzCore *core, int argc, const char **argv) {
	const char *signame, *arg = argv[1];
	int signum = atoi(arg);
	if (signum > 0) {
		signame = rz_signal_to_string(signum);
		if (signame)
			rz_cons_println(signame);
	} else {
		signum = rz_signal_from_string(arg);
		if (signum > 0) {
			rz_cons_printf("%d\n", signum);
		}
	}
	return RZ_CMD_STATUS_OK;
}

// dL
RZ_IPI RzCmdStatus rz_cmd_debug_handler_list_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	if (argc > 1) {
		rz_config_set(core->config, "dbg.backend", argv[1]);
		// implicit by config.set rz_debug_use (core->dbg, str);
		return RZ_CMD_STATUS_OK;
	}

	rz_core_debug_plugins_print(core, state);
	rz_cmd_state_output_print(state);
	rz_cmd_state_output_fini(state);
	rz_cons_flush();
	return RZ_CMD_STATUS_OK;
}

static const char *help_msg_do[] = {
	"Usage:", "do", " # Debug (re)open commands",
	"dor", " [rz-run]", "Comma separated list of k=v rz-run profile options (e dbg.profile)",
	"doe", "", "Show rz-run startup profile",
	"doe!", "", "Edit rz-run startup profile with $EDITOR",
	"doo", " [args]", "Reopen in debug mode with args (alias for 'ood')",
	"doof", " [args]", "Reopen in debug mode from file (alias for 'oodf')",
	"doc", "", "Close debug session",
	NULL
};

// dor
RZ_IPI int rz_cmd_debug_process_dor(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	if (input && input[0] == '?') {
		rz_core_cmd_help(core, help_msg_do);
		return 0;
	}
	if (input) {
		setRarunProfileString(core, input);
	} else {
		// TODO use the api
		rz_sys_xsystem("rz-run -h");
	}
	return 0;
}

// doe
RZ_IPI RzCmdStatus rz_cmd_debug_process_profile_handler(RzCore *core, int argc, const char **argv) {
	if (core->io->envprofile) {
		rz_cons_println(core->io->envprofile);
	}
	return RZ_CMD_STATUS_OK;
}

// doe!
RZ_IPI RzCmdStatus rz_cmd_debug_process_profile_edit_handler(RzCore *core, int argc, const char **argv) {
	char *out = rz_core_editor(core, NULL, core->io->envprofile);
	if (out) {
		free(core->io->envprofile);
		core->io->envprofile = out;
		rz_cons_printf("%s\n", core->io->envprofile);
	}
	return RZ_CMD_STATUS_OK;
}

// doo
RZ_IPI int rz_cmd_debug_process_doo(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	if (input && input[0] == '?') {
		rz_core_cmd_help(core, help_msg_do);
		return 0;
	}
	rz_core_file_reopen_debug(core, input);
	return 0;
}

// doof
RZ_IPI int rz_cmd_debug_process_doof(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	if (input && input[0] == '?') {
		rz_core_cmd_help(core, help_msg_do);
		return 0;
	}
	rz_config_set_b(core->config, "cfg.debug", true);
	rz_core_cmd0(core, sdb_fmt("oodf %s", input));
	return 0;
}

// doc
RZ_IPI RzCmdStatus rz_cmd_debug_process_close_handler(RzCore *core, int argc, const char **argv) {
	if (!core || !core->io || !core->io->desc || !rz_config_get_b(core->config, "cfg.debug")) {
		RZ_LOG_ERROR("No open debug session\n");
		return RZ_CMD_STATUS_ERROR;
	}
	// Stop trace session
	if (core->dbg->session) {
		rz_debug_session_free(core->dbg->session);
		core->dbg->session = NULL;
	}
	// Kill debugee and all child processes
	if (core->dbg && core->dbg->cur && core->dbg->cur->pids && core->dbg->pid != -1) {
		RzList *list = core->dbg->cur->pids(core->dbg, core->dbg->pid);
		RzListIter *iter;
		RzDebugPid *p;
		if (list) {
			rz_list_foreach (list, iter, p) {
				rz_debug_kill(core->dbg, p->pid, p->pid, SIGKILL);
				rz_debug_detach(core->dbg, p->pid);
			}
		} else {
			rz_debug_kill(core->dbg, core->dbg->pid, core->dbg->pid, SIGKILL);
			rz_debug_detach(core->dbg, core->dbg->pid);
		}
	}
	// Remove the target's registers from the flag list
	rz_core_cmd0(core, ".dr-");
	// Reopen and rebase the original file
	rz_core_io_file_open(core, core->io->desc->fd);
	return RZ_CMD_STATUS_OK;
}

// dp
RZ_IPI RzCmdStatus rz_cmd_debug_pid_list_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	rz_cons_printf("Selected: %d %d\n", core->dbg->pid, core->dbg->tid);

	const int pid = argc > 0 ? (int)RZ_MAX(0, (int)rz_num_math(core->num, argv[1])) : core->dbg->pid;
	const char fmt = (char)rz_output_mode_to_char(state->mode);
	rz_debug_pid_list(core->dbg, pid, fmt);
	return RZ_CMD_STATUS_OK;
}

// dpl
RZ_IPI RzCmdStatus
rz_cmd_debug_pid_attachable_handler(RzCore *core, int argc, const char **argv, RzCmdStateOutput *state) {
	rz_debug_pid_list(core->dbg, 0, (char)rz_output_mode_to_char(state->mode));
	return RZ_CMD_STATUS_OK;
}

// dp-
RZ_IPI RzCmdStatus rz_cmd_debug_pid_detach_handler(RzCore *core, int argc, const char **argv) {
	ut64 pid = argc > 1 ? rz_num_math(core->num, argv[1]) : core->dbg->pid;
	rz_debug_detach(core->dbg, pid);
	return RZ_CMD_STATUS_OK;
}

// dp=
RZ_IPI RzCmdStatus rz_cmd_debug_pid_sel_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_select(core->dbg,
		(int)rz_num_math(core->num, argv[1]), core->dbg->tid);
	core->dbg->main_pid = rz_num_math(core->num, argv[1]);
	return RZ_CMD_STATUS_OK;
}

// dpa
RZ_IPI RzCmdStatus rz_cmd_debug_pid_attach_handler(RzCore *core, int argc, const char **argv) {
	rz_core_debug_attach(core, argc > 1 ? rz_num_math(core->num, argv[1]) : 0);
	return RZ_CMD_STATUS_OK;
}

// dpc
RZ_IPI RzCmdStatus rz_cmd_debug_pid_forked_sel_handler(RzCore *core, int argc, const char **argv) {
	if (core->dbg->forked_pid != -1) {
		rz_debug_select(core->dbg, core->dbg->forked_pid, core->dbg->tid);
		core->dbg->main_pid = core->dbg->forked_pid;
		core->dbg->n_threads = 0;
		core->dbg->forked_pid = -1;
	} else {
		RZ_LOG_ERROR("No recently forked children\n");
	}
	return RZ_CMD_STATUS_OK;
}

// dpc*
RZ_IPI RzCmdStatus rz_cmd_debug_pid_forked_handler(RzCore *core, int argc, const char **argv) {
	rz_cons_printf("dp %d\n", core->dbg->forked_pid);
	return RZ_CMD_STATUS_OK;
}

// dpe
RZ_IPI RzCmdStatus rz_cmd_debug_exec_path_handler(RzCore *core, int argc, const char **argv) {
	int pid = argc > 1 ? (int)rz_num_math(core->num, argv[1]) : (int)core->dbg->pid;
	char *exe = rz_sys_pid_to_path(pid);
	if (exe) {
		rz_cons_println(exe);
		free(exe);
	}
	return RZ_CMD_STATUS_OK;
}

// dpf
RZ_IPI RzCmdStatus rz_cmd_debug_pid_f_handler(RzCore *core, int argc, const char **argv) {
	if (core->file && core->io) {
		rz_debug_select(core->dbg, rz_io_fd_get_pid(core->io, core->file->fd),
			rz_io_fd_get_tid(core->io, core->file->fd));
	}
	return RZ_CMD_STATUS_OK;
}

// dpk
RZ_IPI RzCmdStatus rz_cmd_debug_pid_kill_handler(RzCore *core, int argc, const char **argv) {
	/* stop, print, pass -- just use flags*/
	/* XXX: not for threads? signal is for a whole process!! */
	/* XXX: but we want fine-grained access to process resources */
	int pid = rz_num_math(core->num, argv[1]);
	if (pid > 0) {
		int sig = argc > 2 ? atoi(argv[2]) : 0;
		RZ_LOG_ERROR("Sending signal '%d' to pid '%d'\n", sig, pid);
		rz_debug_kill(core->dbg, pid, false, sig);
	} else {
		RZ_LOG_ERROR("cmd_debug_pid: Invalid arguments (%s)\n", argv[1]);
	}
	return RZ_CMD_STATUS_OK;
}

// dpn
RZ_IPI RzCmdStatus rz_cmd_debug_process_new_handler(RzCore *core, int argc, const char **argv) {
	RZ_LOG_ERROR("TODO: debug_fork: %d\n", rz_debug_child_fork(core->dbg));
	return RZ_CMD_STATUS_OK;
}

// dptn
RZ_IPI RzCmdStatus rz_cmd_debug_thread_new_handler(RzCore *core, int argc, const char **argv) {
	RZ_LOG_ERROR("TODO: debug_clone: %d\n", rz_debug_child_clone(core->dbg));
	return RZ_CMD_STATUS_OK;
}

// dpt
RZ_IPI RzCmdStatus rz_cmd_debug_threads_list_handler(RzCore *core, int argc, const char **argv, RzOutputMode mode) {
	const char fmt = (char)rz_output_mode_to_char(mode);
	const int pid = argc > 1 ? atoi(argv[1]) : core->dbg->pid;
	rz_debug_thread_list(core->dbg, pid, fmt);
	return RZ_CMD_STATUS_OK;
}

// dpt=
RZ_IPI RzCmdStatus rz_cmd_debug_thread_attach_handler(RzCore *core, int argc, const char **argv) {
	rz_debug_select(core->dbg, core->dbg->pid, (int)rz_num_math(core->num, argv[1]));
	return RZ_CMD_STATUS_OK;
}

// dw
RZ_IPI RzCmdStatus rz_cmd_debug_prompt_until_handler(RzCore *core, int argc, const char **argv) {
	rz_cons_break_push(rz_core_static_debug_stop, core->dbg);
	for (; !rz_cons_is_breaked();) {
		int pid = atoi(argv[1]);
		// int opid = core->dbg->pid = pid;
		int res = rz_debug_kill(core->dbg, pid, 0, 0);
		if (!res) {
			break;
		}
		rz_sys_usleep(200);
	}
	rz_cons_break_pop();
	return RZ_CMD_STATUS_OK;
}

// dW
RZ_IPI RzCmdStatus rz_cmd_debug_window_process_list_handler(RzCore *core, int argc, const char **argv) {
#if __WINDOWS__
	rz_w32_print_windows(core->dbg);
#else
	RZ_LOG_ERROR("Not Windows\n");
#endif
	return RZ_CMD_STATUS_OK;
}

// dWi
RZ_IPI RzCmdStatus rz_cmd_debug_window_identify_handler(RzCore *core, int argc, const char **argv) {
#if __WINDOWS__
	rz_w32_identify_window();
#else
	RZ_LOG_ERROR("Not Windows\n");
#endif
	return RZ_CMD_STATUS_OK;
}

// dx
RZ_IPI RzCmdStatus rz_cmd_debug_inject_handler(RzCore *core, int argc, const char **argv) {
	ut8 bytes[4096];
	if (strlen(argv[1]) < 4096) {
		int bytes_len = rz_hex_str2bin(argv[1], bytes);
		if (bytes_len > 0)
			rz_debug_execute(core->dbg,
				bytes, bytes_len, 0);
		else
			RZ_LOG_ERROR("Invalid hexpairs\n");
	} else {
		RZ_LOG_ERROR("Injection opcodes so long\n");
	}
	return RZ_CMD_STATUS_OK;
}

// dxa
RZ_IPI RzCmdStatus rz_cmd_debug_inject_assemble_handler(RzCore *core, int argc, const char **argv) {
	RzAsmCode *acode;
	rz_asm_set_pc(core->rasm, core->offset);
	acode = rz_asm_massemble(core->rasm, argv[1]);
	if (acode) {
		rz_reg_arena_push(core->dbg->reg);
		rz_debug_execute(core->dbg, acode->bytes, acode->len, 0);
		rz_reg_arena_pop(core->dbg->reg);
	}
	rz_asm_code_free(acode);
	return RZ_CMD_STATUS_OK;
}

// dxe
RZ_IPI RzCmdStatus rz_cmd_debug_inject_egg_handler(RzCore *core, int argc, const char **argv) {
	RzEgg *egg = core->egg;
	RzBuffer *b;
	const char *asm_arch = rz_config_get(core->config, "asm.arch");
	int asm_bits = rz_config_get_i(core->config, "asm.bits");
	const char *asm_os = rz_config_get(core->config, "asm.os");
	rz_egg_setup(egg, asm_arch, asm_bits, 0, asm_os);
	rz_egg_reset(egg);
	rz_egg_load(egg, argv[1], 0);
	rz_egg_compile(egg);
	b = rz_egg_get_bin(egg);
	rz_asm_set_pc(core->rasm, core->offset);
	rz_reg_arena_push(core->dbg->reg);
	ut64 tmpsz;
	const ut8 *tmp = rz_buf_data(b, &tmpsz);
	rz_debug_execute(core->dbg, tmp, tmpsz, 0);
	rz_reg_arena_pop(core->dbg->reg);
	return RZ_CMD_STATUS_OK;
}

// dxr
RZ_IPI RzCmdStatus rz_cmd_debug_inject_restore_handler(RzCore *core, int argc, const char **argv) {
	rz_reg_arena_push(core->dbg->reg);
	if (argc > 1) {
		ut8 bytes[4096];
		if (strlen(argv[1]) < 4096) {
			int bytes_len = rz_hex_str2bin(argv[1], bytes);
			if (bytes_len > 0) {
				rz_debug_execute(core->dbg,
					bytes, bytes_len,
					0);
			} else {
				RZ_LOG_ERROR("Invalid hexpairs\n");
			}
		} else
			RZ_LOG_ERROR("Injection opcodes so long\n");
	}
	rz_reg_arena_pop(core->dbg->reg);
	return RZ_CMD_STATUS_OK;
}

// dxs
RZ_IPI RzCmdStatus rz_cmd_debug_inject_syscall_handler(RzCore *core, int argc, const char **argv) {
	char *str = rz_core_cmd_str(core, sdb_fmt("gs %s", argv[1]));
	rz_core_cmdf(core, "dx %s", str); //`gs %s`", input + 2);
	free(str);
	return RZ_CMD_STATUS_OK;
}

#define CMD_REGS_PREFIX   debug
#define CMD_REGS_REG_PATH dbg->reg
static bool cmd_regs_sync(RzCore *core, RzRegisterType type, bool write) {
	return rz_debug_reg_sync(core->dbg, type, write);
}
#define CMD_REGS_SYNC cmd_regs_sync
#include "cmd_regs_meta.inc"
#undef CMD_REGS_PREFIX
#undef CMD_REGS_REG_PATH
#undef CMD_REGS_SYNC

RZ_API void rz_core_debug_ri(RzCore *core) {
	const RzList *list = rz_reg_get_list(core->dbg->reg, RZ_REG_TYPE_GPR);
	rz_regs_show_valgroup(core, core->dbg->reg, cmd_regs_sync, list);
}

RZ_IPI RzCmdStatus rz_debug_drx_handler(RzCore *core, int argc, const char **argv) {
	if (argc <= 1) {
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, false);
		rz_debug_drx_list(core->dbg);
		return RZ_CMD_STATUS_OK;
	}
	if (argc != 5) {
		return RZ_CMD_STATUS_WRONG_ARGS;
	}
	int n = (int)rz_num_math(core->num, argv[1]);
	ut64 off = rz_num_math(core->num, argv[2]);
	int len = (int)rz_num_math(core->num, argv[3]);
	int perm = rz_str_rwx(argv[4]);
	if (len == -1) {
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, false);
		rz_debug_drx_set(core->dbg, n, 0, 0, 0, 0);
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, true);
	} else {
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, false);
		rz_debug_drx_set(core->dbg, n, off, len, perm, 0);
		rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, true);
	}
	return RZ_CMD_STATUS_OK;
}

RZ_IPI RzCmdStatus rz_debug_drx_unset_handler(RzCore *core, int argc, const char **argv) {
	rz_return_val_if_fail(argc > 1, RZ_CMD_STATUS_WRONG_ARGS);
	rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, false);
	rz_debug_drx_unset(core->dbg, atoi(argv[1] + 2));
	rz_debug_reg_sync(core->dbg, RZ_REG_TYPE_DRX, true);
	return RZ_CMD_STATUS_OK;
}
