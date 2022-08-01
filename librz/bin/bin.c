// SPDX-FileCopyrightText: 2009-2020 pancake <pancake@nopcode.org>
// SPDX-FileCopyrightText: 2009-2020 nibble <nibble.ds@gmail.com>
// SPDX-FileCopyrightText: 2009-2020 dso <dso@rice.edu>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_bin.h>
#include <rz_demangler.h>
#include <rz_util.h>
#include <rz_lib.h>
#include <rz_io.h>
#include <config.h>
#include "i/private.h"

RZ_LIB_VERSION(rz_bin);

#define DB a->sdb;
#define RBINLISTFREE(x) \
	if (x) { \
		rz_list_free(x); \
		(x) = NULL; \
	}

#define ARCHS_KEY "archs"

static RzBinPlugin *bin_static_plugins[] = { RZ_BIN_STATIC_PLUGINS };
static RzBinXtrPlugin *bin_xtr_static_plugins[] = { RZ_BIN_XTR_STATIC_PLUGINS };

static ut64 __getoffset(RzBin *bin, int type, int idx) {
	RzBinFile *a = rz_bin_cur(bin);
	RzBinPlugin *plugin = rz_bin_file_cur_plugin(a);
	if (plugin && plugin->get_offset) {
		return plugin->get_offset(a, type, idx);
	}
	return UT64_MAX;
}

static char *__getname(RzBin *bin, int type, int idx) {
	RzBinFile *a = rz_bin_cur(bin);
	RzBinPlugin *plugin = rz_bin_file_cur_plugin(a);
	if (plugin && plugin->get_name) {
		return plugin->get_name(a, type, idx);
	}
	return NULL;
}

// TODO: move these two function do a different file
RZ_API RzBinXtrData *rz_bin_xtrdata_new(RzBuffer *buf, ut64 offset, ut64 size, ut32 file_count, RzBinXtrMetadata *metadata) {
	RzBinXtrData *data = RZ_NEW0(RzBinXtrData);
	if (data) {
		data->offset = offset;
		data->size = size;
		data->file_count = file_count;
		data->metadata = metadata;
		data->loaded = 0;
		// don't slice twice TODO. review this
		data->buf = rz_buf_ref(buf); // rz_buf_new_slice (buf, offset, size);
	}
	return data;
}

RZ_API void rz_bin_xtrdata_free(void /*RzBinXtrData*/ *data_) {
	RzBinXtrData *data = data_;
	rz_return_if_fail(data);
	if (data->metadata) {
		free(data->metadata->libname);
		free(data->metadata->arch);
		free(data->metadata->machine);
		free(data->metadata);
	}
	free(data->file);
	rz_buf_free(data->buf);
	free(data);
}

RZ_API void rz_bin_options_init(RzBinOptions *opt, int fd, ut64 baseaddr, ut64 loadaddr, bool patch_relocs) {
	memset(opt, 0, sizeof(*opt));
	opt->obj_opts.baseaddr = baseaddr;
	opt->obj_opts.loadaddr = loadaddr;
	opt->obj_opts.patch_relocs = patch_relocs;
	opt->obj_opts.elf_load_sections = true;
	opt->fd = fd;
}

RZ_API void rz_bin_arch_options_init(RzBinArchOptions *opt, const char *arch, int bits) {
	opt->arch = arch ? arch : RZ_SYS_ARCH;
	opt->bits = bits ? bits : RZ_SYS_BITS;
}

RZ_API void rz_bin_file_hash_free(RzBinFileHash *fhash) {
	if (fhash) {
		RZ_FREE(fhash->type);
		RZ_FREE(fhash->hex);
		free(fhash);
	}
}

RZ_API void rz_bin_info_free(RzBinInfo *rb) {
	if (!rb) {
		return;
	}

	rz_list_free(rb->file_hashes);
	free(rb->intrp);
	free(rb->file);
	free(rb->type);
	free(rb->bclass);
	free(rb->rclass);
	free(rb->arch);
	free(rb->cpu);
	free(rb->machine);
	free(rb->os);
	free(rb->features);
	free(rb->subsystem);
	free(rb->default_cc);
	free(rb->rpath);
	free(rb->guid);
	free(rb->debug_file_name);
	free(rb->actual_checksum);
	free(rb->claimed_checksum);
	free(rb->compiler);
	free(rb->head_flag);
	free(rb);
}

RZ_API RzBinImport *rz_bin_import_clone(RzBinImport *o) {
	rz_return_val_if_fail(o, NULL);

	RzBinImport *res = rz_mem_dup(o, sizeof(*o));
	if (res) {
		res->name = RZ_STR_DUP(o->name);
		res->classname = RZ_STR_DUP(o->classname);
		res->descriptor = RZ_STR_DUP(o->descriptor);
	}
	return res;
}

RZ_API void rz_bin_import_free(RzBinImport *imp) {
	if (imp) {
		RZ_FREE(imp->name);
		RZ_FREE(imp->libname);
		RZ_FREE(imp->classname);
		RZ_FREE(imp->descriptor);
		free(imp);
	}
}

RZ_API void rz_bin_resource_free(RzBinResource *res) {
	if (!res) {
		return;
	}
	RZ_FREE(res->name);
	RZ_FREE(res->time);
	RZ_FREE(res->type);
	RZ_FREE(res->language);
	free(res);
}

RZ_API const char *rz_bin_symbol_name(RzBinSymbol *s) {
	if (s->dup_count) {
		return sdb_fmt("%s_%d", s->name, s->dup_count);
	}
	return s->name;
}

RZ_API RzBinSymbol *rz_bin_symbol_new(const char *name, ut64 paddr, ut64 vaddr) {
	RzBinSymbol *sym = RZ_NEW0(RzBinSymbol);
	if (sym) {
		sym->name = name ? strdup(name) : NULL;
		sym->paddr = paddr;
		sym->vaddr = vaddr;
	}
	return sym;
}

RZ_API void rz_bin_symbol_free(RzBinSymbol *sym) {
	if (!sym) {
		return;
	}

	free(sym->name);
	free(sym->dname);
	free(sym->libname);
	free(sym->classname);
	free(sym->visibility_str);
	free(sym);
}

RZ_API void rz_bin_reloc_free(RzBinReloc *reloc) {
	free(reloc);
}

RZ_API void rz_bin_string_free(void *_str) {
	RzBinString *str = (RzBinString *)_str;
	if (str) {
		free(str->string);
		free(str);
	}
}

RZ_API RzBinFile *rz_bin_open(RzBin *bin, const char *file, RzBinOptions *opt) {
	rz_return_val_if_fail(bin && bin->iob.io && opt, NULL);

	RzIOBind *iob = &(bin->iob);
	if (!iob->desc_get(iob->io, opt->fd)) {
		opt->fd = iob->fd_open(iob->io, file, RZ_PERM_R, 0644);
	}
	if (opt->fd < 0) {
		RZ_LOG_ERROR("Couldn't open bin for file '%s'\n", file);
		return NULL;
	}
	opt->sz = 0;
	opt->pluginname = NULL;
	return rz_bin_open_io(bin, opt);
}

RZ_API RzBinFile *rz_bin_reload(RzBin *bin, RzBinFile *bf, ut64 baseaddr) {
	rz_return_val_if_fail(bin && bf, NULL);

	bool big_endian = bf->o ? bf->o->opts.big_endian : false;
	bool patch_relocs = bf->o ? bf->o->opts.patch_relocs : false;
	bool elf_load_sections = bf->o ? bf->o->opts.elf_load_sections : false;
	bool elf_checks_sections = bf->o ? bf->o->opts.elf_checks_sections : false;
	bool elf_checks_segments = bf->o ? bf->o->opts.elf_checks_segments : false;

	RzBinOptions opt;
	rz_bin_options_init(&opt, bf->fd, baseaddr, bf->loadaddr, patch_relocs);
	opt.obj_opts.elf_load_sections = elf_load_sections;
	opt.obj_opts.elf_checks_sections = elf_checks_sections;
	opt.obj_opts.elf_checks_segments = elf_checks_segments;
	opt.obj_opts.big_endian = big_endian;
	opt.filename = bf->file;
	rz_buf_seek(bf->buf, 0, RZ_BUF_SET);
	RzBinFile *nbf = rz_bin_open_buf(bin, bf->buf, &opt);
	rz_bin_file_delete(bin, bf);
	return nbf;
}

RZ_API RzBinFile *rz_bin_open_buf(RzBin *bin, RzBuffer *buf, RzBinOptions *opt) {
	rz_return_val_if_fail(bin && opt, NULL);

	RzListIter *it;
	RzBinXtrPlugin *xtr;

	bin->file = opt->filename;
	if (opt->obj_opts.loadaddr == UT64_MAX) {
		opt->obj_opts.loadaddr = 0;
	}

	RzBinFile *bf = NULL;
	if (bin->use_xtr && !opt->pluginname) {
		// XXX - for the time being this is fine, but we may want to
		// change the name to something like
		// <xtr_name>:<bin_type_name>
		rz_list_foreach (bin->binxtrs, it, xtr) {
			if (!xtr->check_buffer) {
				RZ_LOG_ERROR("Missing check_buffer callback for '%s'\n", xtr->name);
				continue;
			}
			if (xtr->check_buffer(buf)) {
				if (xtr->extract_from_buffer || xtr->extractall_from_buffer ||
					xtr->extract_from_bytes || xtr->extractall_from_bytes) {
					bf = rz_bin_file_xtr_load_buffer(bin, xtr,
						bin->file, buf, &opt->obj_opts,
						opt->xtr_idx, opt->fd);
				}
			}
		}
	}
	if (!bf) {
		// Uncomment for this speedup: 20s vs 22s
		// RzBuffer *buf = rz_buf_new_slurp (bin->file);
		bf = rz_bin_file_new_from_buffer(bin, bin->file, buf,
			&opt->obj_opts, opt->fd, opt->pluginname);
		if (!bf) {
			return NULL;
		}
	}
	rz_bin_file_set_cur_binfile(bin, bf);
	rz_id_storage_set(bin->ids, bin->cur, bf->id);
	return bf;
}

RZ_API RzBinFile *rz_bin_open_io(RzBin *bin, RzBinOptions *opt) {
	rz_return_val_if_fail(bin && opt && bin->iob.io, NULL);
	rz_return_val_if_fail(opt->fd >= 0 && (st64)opt->sz >= 0, NULL);

	RzIOBind *iob = &(bin->iob);
	RzIO *io = iob ? iob->io : NULL;

	bool is_debugger = iob->fd_is_dbg(io, opt->fd);
	const char *fname = iob->fd_get_name(io, opt->fd);
	if (opt->obj_opts.loadaddr == UT64_MAX) {
		opt->obj_opts.loadaddr = 0;
	}

	// Create RzBuffer from the opened file
	// When debugging something, we want to open the backed file because
	// not all binary info are mapped in the virtual space. If that is not
	// possible (e.g. remote file) just try to load bin info from the
	// debugee process.
	RzBuffer *buf = NULL;
	if (is_debugger) {
		buf = rz_buf_new_file(fname, O_RDONLY, 0);
		is_debugger = false;
	}
	if (!buf) {
		buf = rz_buf_new_with_io_fd(&bin->iob, opt->fd);
	}
	if (!buf) {
		return NULL;
	}

	if (!opt->sz) {
		opt->sz = rz_buf_size(buf);
	}

	// Slice buffer if necessary
	RzBuffer *slice = buf;
	if (!is_debugger && (opt->obj_opts.loadaddr != 0 || opt->sz != rz_buf_size(buf))) {
		slice = rz_buf_new_slice(buf, opt->obj_opts.loadaddr, opt->sz);
	} else if (is_debugger && opt->obj_opts.baseaddr != UT64_MAX && opt->obj_opts.baseaddr != 0) {
		slice = rz_buf_new_slice(buf, opt->obj_opts.baseaddr, opt->sz);
	}
	if (slice != buf) {
		rz_buf_free(buf);
		buf = slice;
	}

	opt->filename = fname;
	RzBinFile *bf = rz_bin_open_buf(bin, buf, opt);
	rz_buf_free(buf);
	return bf;
}

RZ_IPI RzBinPlugin *rz_bin_get_binplugin_by_name(RzBin *bin, const char *name) {
	RzBinPlugin *plugin;
	RzListIter *it;

	rz_return_val_if_fail(bin && name, NULL);

	rz_list_foreach (bin->plugins, it, plugin) {
		if (!strcmp(plugin->name, name)) {
			return plugin;
		}
	}
	return NULL;
}

RZ_API RzBinPlugin *rz_bin_get_binplugin_by_buffer(RzBin *bin, RzBuffer *buf) {
	RzBinPlugin *plugin;
	RzListIter *it;

	rz_return_val_if_fail(bin && buf, NULL);

	rz_list_foreach (bin->plugins, it, plugin) {
		if (plugin->check_buffer) {
			if (plugin->check_buffer(buf)) {
				return plugin;
			}
		}
	}
	return NULL;
}

RZ_IPI RzBinPlugin *rz_bin_get_binplugin_by_filename(RzBin *bin) {
	RzBinPlugin *plugin;
	RzListIter *it;

	rz_return_val_if_fail(bin, NULL);

	const char *filename = strrchr(bin->file, RZ_SYS_DIR[0]);
	filename = filename ? filename + 1 : bin->file;
	rz_list_foreach (bin->plugins, it, plugin) {
		if (plugin->check_filename) {
			if (plugin->check_filename(filename)) {
				return plugin;
			}
		}
	}
	return NULL;
}

RZ_IPI RzBinXtrPlugin *rz_bin_get_xtrplugin_by_name(RzBin *bin, const char *name) {
	RzBinXtrPlugin *xtr;
	RzListIter *it;

	rz_return_val_if_fail(bin && name, NULL);

	// TODO: use a hashtable here
	rz_list_foreach (bin->binxtrs, it, xtr) {
		if (!strcmp(xtr->name, name)) {
			return xtr;
		}
		// must be set to null
		xtr = NULL;
	}
	return NULL;
}

static void rz_bin_plugin_free(RzBinPlugin *p) {
	RZ_FREE(p);
}

RZ_API bool rz_bin_plugin_add(RzBin *bin, RzBinPlugin *foo) {
	RzListIter *it;
	RzBinPlugin *plugin;

	rz_return_val_if_fail(bin && foo, false);

	rz_list_foreach (bin->plugins, it, plugin) {
		if (!strcmp(plugin->name, foo->name)) {
			return false;
		}
	}
	plugin = RZ_NEW0(RzBinPlugin);
	memcpy(plugin, foo, sizeof(RzBinPlugin));
	rz_list_append(bin->plugins, plugin);
	return true;
}

RZ_API bool rz_bin_xtr_add(RzBin *bin, RzBinXtrPlugin *foo) {
	RzListIter *it;
	RzBinXtrPlugin *xtr;

	rz_return_val_if_fail(bin && foo, false);

	if (foo->init) {
		foo->init(bin->user);
	}
	// avoid duplicates
	rz_list_foreach (bin->binxtrs, it, xtr) {
		if (!strcmp(xtr->name, foo->name)) {
			return false;
		}
	}
	rz_list_append(bin->binxtrs, foo);
	return true;
}

RZ_API void rz_bin_free(RzBin *bin) {
	if (!bin) {
		return;
	}
	bin->file = NULL;
	free(bin->force);
	free(bin->srcdir);
	free(bin->strenc);
	// rz_bin_free_bin_files (bin);
	rz_list_free(bin->binfiles);
	rz_list_free(bin->binxtrs);
	rz_list_free(bin->plugins);
	sdb_free(bin->sdb);
	rz_id_storage_free(bin->ids);
	rz_hash_free(bin->hash);
	rz_event_free(bin->event);
	rz_str_constpool_fini(&bin->constpool);
	rz_demangler_free(bin->demangler);
	free(bin);
}

static bool rz_bin_print_plugin_details(RzBin *bin, RzBinPlugin *bp, PJ *pj, int json) {
	if (json == 'q') {
		bin->cb_printf("%s\n", bp->name);
	} else if (json) {
		pj_o(pj);
		pj_ks(pj, "name", bp->name);
		pj_ks(pj, "description", bp->desc);
		pj_ks(pj, "license", bp->license ? bp->license : "???");
		pj_end(pj);
	} else {
		bin->cb_printf("Name: %s\n", bp->name);
		bin->cb_printf("Description: %s\n", bp->desc);
		if (bp->license) {
			bin->cb_printf("License: %s\n", bp->license);
		}
		if (bp->version) {
			bin->cb_printf("Version: %s\n", bp->version);
		}
		if (bp->author) {
			bin->cb_printf("Author: %s\n", bp->author);
		}
	}
	return true;
}

static void __printXtrPluginDetails(RzBin *bin, RzBinXtrPlugin *bx, int json) {
	if (json == 'q') {
		bin->cb_printf("%s\n", bx->name);
	} else if (json) {
		PJ *pj = pj_new();
		if (!pj) {
			return;
		}
		pj_o(pj);
		pj_ks(pj, "name", bx->name);
		pj_ks(pj, "description", bx->desc);
		pj_ks(pj, "license", bx->license ? bx->license : "???");
		pj_end(pj);
		bin->cb_printf("%s\n", pj_string(pj));
		pj_free(pj);
	} else {
		bin->cb_printf("Name: %s\n", bx->name);
		bin->cb_printf("Description: %s\n", bx->desc);
		if (bx->license) {
			bin->cb_printf("License: %s\n", bx->license);
		}
	}
}

RZ_API bool rz_bin_list_plugin(RzBin *bin, const char *name, PJ *pj, int json) {
	RzListIter *it;
	RzBinPlugin *bp;
	RzBinXtrPlugin *bx;

	rz_return_val_if_fail(bin && name, false);

	rz_list_foreach (bin->plugins, it, bp) {
		if (rz_str_cmp(name, bp->name, strlen(name))) {
			continue;
		}
		return rz_bin_print_plugin_details(bin, bp, pj, json);
	}
	rz_list_foreach (bin->binxtrs, it, bx) {
		if (rz_str_cmp(name, bx->name, strlen(name))) {
			continue;
		}
		__printXtrPluginDetails(bin, bx, json);
		return true;
	}

	RZ_LOG_ERROR("Cannot find plugin %s\n", name);
	return false;
}

/* returns the base address of bin or UT64_MAX in case of errors */
RZ_API ut64 rz_bin_get_baddr(RzBin *bin) {
	rz_return_val_if_fail(bin, UT64_MAX);
	return rz_bin_file_get_baddr(bin->cur);
}

/* returns the load address of bin or UT64_MAX in case of errors */
RZ_API ut64 rz_bin_get_laddr(RzBin *bin) {
	rz_return_val_if_fail(bin, UT64_MAX);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? o->opts.loadaddr : UT64_MAX;
}

// TODO: should be RzBinFile specific imho
RZ_API void rz_bin_set_baddr(RzBin *bin, ut64 baddr) {
	rz_return_if_fail(bin);
	RzBinFile *bf = rz_bin_cur(bin);
	RzBinObject *o = rz_bin_cur_object(bin);
	if (!o || !o->plugin || !o->plugin->baddr) {
		return;
	}
	ut64 file_baddr = o->plugin->baddr(bf);
	if (baddr == UT64_MAX) {
		o->opts.baseaddr = file_baddr;
		o->baddr_shift = 0; // o->baddr; // - file_baddr;
	} else if (file_baddr != UT64_MAX) {
		o->opts.baseaddr = baddr;
		o->baddr_shift = baddr - file_baddr;
	}
}

// XXX: those accessors are redundant
RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_entries(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_entries(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_fields(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_fields(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_imports(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_imports(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzBinInfo *rz_bin_get_info(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzBinInfo *)rz_bin_object_get_info(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_libs(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_libs(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_sections(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_sections_all(o) : NULL;
}

/**
 * \brief Find the binary section at offset \p off.
 *
 * \param o Reference to the \p RzBinObject instance
 * \param off Address to search
 * \param va When 0 the offset \p off is considered a physical address, otherwise a virtual address
 * \return Pointer to a \p RzBinSection containing the address
 */
RZ_API RZ_BORROW RzBinSection *rz_bin_get_section_at(RzBinObject *o, ut64 off, int va) {
	RzBinSection *section;
	RzListIter *iter;
	ut64 from, to;

	rz_return_val_if_fail(o, NULL);
	// TODO: must be O(1) .. use sdb here
	rz_list_foreach (o->sections, iter, section) {
		if (section->is_segment) {
			continue;
		}
		from = va ? rz_bin_object_addr_with_base(o, section->vaddr) : section->paddr;
		to = from + (va ? section->vsize : section->size);
		if (off >= from && off < to) {
			return section;
		}
	}
	return NULL;
}

/**
 * \brief Find the last binary map at offset \p off .
 *
 * This function returns the last binary map that contains offset \p off,
 * because it assumes that maps are sorted by priority, thus the last one will
 * be the most important one.
 *
 * \param o Reference to the \p RzBinObject instance
 * \param off Address to search
 * \param va When false the offset \p off is considered a physical address, otherwise a virtual address
 * \return Pointer to a \p RzBinMap containing the address
 */
RZ_API RZ_BORROW RzBinMap *rz_bin_object_get_map_at(RzBinObject *o, ut64 off, bool va) {
	rz_return_val_if_fail(o, NULL);

	RzBinMap *map;
	RzListIter *iter;
	ut64 from, to;

	rz_list_foreach_prev(o->maps, iter, map) {
		from = va ? rz_bin_object_addr_with_base(o, map->vaddr) : map->paddr;
		to = from + (va ? map->vsize : map->psize);
		if (off >= from && off < to) {
			return map;
		}
	}
	return NULL;
}

/**
 * \brief Find all binary maps at offset \p off .
 *
 * \param o Reference to the \p RzBinObject instance
 * \param off Address to search
 * \param va When false the offset \p off is considered a physical address, otherwise a virtual address
 * \return Vector of \p RzBinMap pointers
 */
RZ_API RZ_OWN RzPVector *rz_bin_object_get_maps_at(RzBinObject *o, ut64 off, bool va) {
	rz_return_val_if_fail(o, NULL);

	RzBinMap *map;
	RzListIter *iter;
	ut64 from, to;

	RzPVector *res = rz_pvector_new(NULL);
	if (!res) {
		return NULL;
	}

	rz_list_foreach (o->maps, iter, map) {
		from = va ? rz_bin_object_addr_with_base(o, map->vaddr) : map->paddr;
		to = from + (va ? map->vsize : map->psize);
		if (off >= from && off < to) {
			rz_pvector_push(res, map);
		}
	}
	return res;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_strings(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_strings(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_symbols(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_symbols(o) : NULL;
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList *rz_bin_get_mem(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? (RzList *)rz_bin_object_get_mem(o) : NULL;
}

RZ_DEPRECATE RZ_API int rz_bin_is_static(RzBin *bin) {
	rz_return_val_if_fail(bin, false);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? rz_bin_object_is_static(o) : false;
}

RZ_IPI void rz_bin_file_free(void /*RzBinFile*/ *_bf);

RZ_API RzBin *rz_bin_new(void) {
	int i;
	RzBinXtrPlugin *static_xtr_plugin;
	RzBin *bin = RZ_NEW0(RzBin);
	if (!bin) {
		return NULL;
	}
	/* demanglers */
	bin->demangler = rz_demangler_new();
	if (!bin->demangler) {
		goto trashbin;
	}
	if (!rz_str_constpool_init(&bin->constpool)) {
		goto trashbin_demangler;
	}
	bin->event = rz_event_new(bin);
	if (!bin->event) {
		goto trashbin_constpool;
	}
	bin->force = NULL;
	bin->filter_rules = UT64_MAX;
	bin->sdb = sdb_new0();
	bin->cb_printf = (PrintfCallback)printf;
	bin->plugins = rz_list_newf((RzListFree)rz_bin_plugin_free);
	bin->minstrlen = 0;
	bin->strpurge = NULL;
	bin->strenc = NULL;
	bin->want_dbginfo = true;
	bin->cur = NULL;
	bin->hash = rz_hash_new();
	if (!bin->hash) {
		goto trashbin_event;
	}

	bin->ids = rz_id_storage_new(0, ST32_MAX);

	/* bin parsers */
	bin->binfiles = rz_list_newf((RzListFree)rz_bin_file_free);
	for (i = 0; i < RZ_ARRAY_SIZE(bin_static_plugins); i++) {
		rz_bin_plugin_add(bin, bin_static_plugins[i]);
	}
	/* extractors */
	bin->binxtrs = rz_list_new();
	bin->binxtrs->free = free;
	for (i = 0; i < RZ_ARRAY_SIZE(bin_xtr_static_plugins); i++) {
		static_xtr_plugin = RZ_NEW0(RzBinXtrPlugin);
		if (!static_xtr_plugin) {
			goto trashbin_binxtrs;
		}
		*static_xtr_plugin = *bin_xtr_static_plugins[i];
		if (!rz_bin_xtr_add(bin, static_xtr_plugin)) {
			free(static_xtr_plugin);
		}
	}
	return bin;

trashbin_binxtrs:
	rz_list_free(bin->binxtrs);
	rz_list_free(bin->binfiles);
	rz_id_storage_free(bin->ids);
trashbin_event:
	rz_event_free(bin->event);
trashbin_constpool:
	rz_str_constpool_fini(&bin->constpool);
trashbin_demangler:
	rz_demangler_free(bin->demangler);
trashbin:
	free(bin);
	return NULL;
}

RZ_API bool rz_bin_use_arch(RzBin *bin, const char *arch, int bits, const char *name) {
	rz_return_val_if_fail(bin && arch, false);

	RzBinFile *binfile = rz_bin_file_find_by_arch_bits(bin, arch, bits);
	if (!binfile) {
		RZ_LOG_WARN("Cannot find binfile with arch/bits %s/%d\n", arch, bits);
		return false;
	}

	RzBinObject *obj = rz_bin_object_find_by_arch_bits(binfile, arch, bits, name);
	if (!obj && binfile->xtr_data) {
		RzBinXtrData *xtr_data = rz_list_get_n(binfile->xtr_data, 0);
		if (xtr_data && !xtr_data->loaded) {
			RzBinObjectLoadOptions obj_opts = {
				.baseaddr = UT64_MAX,
				.loadaddr = rz_bin_get_laddr(bin)
			};
			if (!rz_bin_file_object_new_from_xtr_data(bin, binfile, &obj_opts, xtr_data)) {
				return false;
			}
		}
		obj = binfile->o;
	}
	return rz_bin_file_set_obj(bin, binfile, obj);
}

RZ_API bool rz_bin_select(RzBin *bin, const char *arch, int bits, const char *name) {
	rz_return_val_if_fail(bin, false);

	RzBinFile *cur = rz_bin_cur(bin);
	RzBinObject *obj = NULL;
	name = !name && cur ? cur->file : name;
	RzBinFile *binfile = rz_bin_file_find_by_arch_bits(bin, arch, bits);
	if (binfile && name) {
		obj = rz_bin_object_find_by_arch_bits(binfile, arch, bits, name);
	}
	return rz_bin_file_set_obj(bin, binfile, obj);
}

RZ_API int rz_bin_select_object(RzBinFile *binfile, const char *arch, int bits, const char *name) {
	rz_return_val_if_fail(binfile, false);
	RzBinObject *obj = rz_bin_object_find_by_arch_bits(binfile, arch, bits, name);
	return rz_bin_file_set_obj(binfile->rbin, binfile, obj);
}

// NOTE: this functiona works as expected, but  we need to merge bfid and boid
RZ_API bool rz_bin_select_bfid(RzBin *bin, ut32 bf_id) {
	rz_return_val_if_fail(bin, false);
	RzBinFile *bf = rz_bin_file_find_by_id(bin, bf_id);
	return bf ? rz_bin_file_set_obj(bin, bf, NULL) : false;
}

RZ_API void rz_bin_set_user_ptr(RzBin *bin, void *user) {
	bin->user = user;
}

static RzBinSection *__get_vsection_at(RzBin *bin, ut64 vaddr) {
	rz_return_val_if_fail(bin, NULL);
	if (!bin->cur) {
		return NULL;
	}
	return rz_bin_get_section_at(bin->cur->o, vaddr, true);
}

RZ_API void rz_bin_bind(RzBin *bin, RzBinBind *b) {
	if (b) {
		b->bin = bin;
		b->get_offset = __getoffset;
		b->get_name = __getname;
		b->get_sections = rz_bin_get_sections;
		b->get_vsect_at = __get_vsection_at;
		b->demangle = rz_bin_demangle;
	}
}

RZ_API RzBuffer *rz_bin_create(RzBin *bin, const char *p,
	const ut8 *code, int codelen,
	const ut8 *data, int datalen,
	RzBinArchOptions *opt) {

	rz_return_val_if_fail(bin && p && opt, NULL);

	RzBinPlugin *plugin = rz_bin_get_binplugin_by_name(bin, p);
	if (!plugin) {
		RZ_LOG_WARN("Cannot find RzBin plugin named '%s'.\n", p);
		return NULL;
	}
	if (!plugin->create) {
		RZ_LOG_WARN("RzBin plugin '%s' does not implement \"create\" method.\n", p);
		return NULL;
	}
	codelen = RZ_MAX(codelen, 0);
	datalen = RZ_MAX(datalen, 0);
	return plugin->create(bin, code, codelen, data, datalen, opt);
}

RZ_DEPRECATE RZ_API RZ_BORROW RzList /*<RzBinClass>*/ *rz_bin_get_classes(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? o->classes : NULL;
}

RZ_API ut64 rz_bin_get_size(RzBin *bin) {
	rz_return_val_if_fail(bin, UT64_MAX);
	RzBinObject *o = rz_bin_cur_object(bin);
	return o ? o->size : 0;
}

RZ_API RzBinFile *rz_bin_cur(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	return bin->cur;
}

RZ_API RzBinObject *rz_bin_cur_object(RzBin *bin) {
	rz_return_val_if_fail(bin, NULL);
	RzBinFile *binfile = rz_bin_cur(bin);
	return binfile ? binfile->o : NULL;
}

RZ_API void rz_bin_force_plugin(RzBin *bin, const char *name) {
	rz_return_if_fail(bin);
	free(bin->force);
	bin->force = (name && *name) ? strdup(name) : NULL;
}

RZ_API const char *rz_bin_entry_type_string(int etype) {
	switch (etype) {
	case RZ_BIN_ENTRY_TYPE_PROGRAM:
		return "program";
	case RZ_BIN_ENTRY_TYPE_MAIN:
		return "main";
	case RZ_BIN_ENTRY_TYPE_INIT:
		return "init";
	case RZ_BIN_ENTRY_TYPE_FINI:
		return "fini";
	case RZ_BIN_ENTRY_TYPE_TLS:
		return "tls";
	case RZ_BIN_ENTRY_TYPE_PREINIT:
		return "preinit";
	}
	return NULL;
}

RZ_API void rz_bin_load_filter(RzBin *bin, ut64 rules) {
	bin->filter_rules = rules;
}

/* RzBinField */
RZ_API RzBinField *rz_bin_field_new(ut64 paddr, ut64 vaddr, int size, const char *name, const char *comment, const char *format, bool format_named) {
	RzBinField *ptr = RZ_NEW0(RzBinField);
	if (ptr) {
		ptr->name = strdup(name);
		ptr->comment = (comment && *comment) ? strdup(comment) : NULL;
		ptr->format = (format && *format) ? strdup(format) : NULL;
		ptr->format_named = format_named;
		ptr->paddr = paddr;
		ptr->size = size;
		//	ptr->visibility = any default visibility?
		ptr->vaddr = vaddr;
	}
	return ptr;
}

RZ_API void rz_bin_field_free(RzBinField *field) {
	if (field) {
		free(field->name);
		free(field->visibility_str);
		free(field->type);
		free(field->comment);
		free(field->format);
		free(field);
	}
}

RZ_API const char *rz_bin_get_meth_flag_string(ut64 flag, bool compact) {
	switch (flag) {
	case RZ_BIN_METH_CLASS:
		return compact ? "c" : "class";
	case RZ_BIN_METH_STATIC:
		return compact ? "s" : "static";
	case RZ_BIN_METH_PUBLIC:
		return compact ? "p" : "public";
	case RZ_BIN_METH_PRIVATE:
		return compact ? "P" : "private";
	case RZ_BIN_METH_PROTECTED:
		return compact ? "r" : "protected";
	case RZ_BIN_METH_INTERNAL:
		return compact ? "i" : "internal";
	case RZ_BIN_METH_OPEN:
		return compact ? "o" : "open";
	case RZ_BIN_METH_FILEPRIVATE:
		return compact ? "e" : "fileprivate";
	case RZ_BIN_METH_FINAL:
		return compact ? "f" : "final";
	case RZ_BIN_METH_VIRTUAL:
		return compact ? "v" : "virtual";
	case RZ_BIN_METH_CONST:
		return compact ? "k" : "const";
	case RZ_BIN_METH_MUTATING:
		return compact ? "m" : "mutating";
	case RZ_BIN_METH_ABSTRACT:
		return compact ? "a" : "abstract";
	case RZ_BIN_METH_SYNCHRONIZED:
		return compact ? "y" : "synchronized";
	case RZ_BIN_METH_NATIVE:
		return compact ? "n" : "native";
	case RZ_BIN_METH_BRIDGE:
		return compact ? "b" : "bridge";
	case RZ_BIN_METH_VARARGS:
		return compact ? "g" : "varargs";
	case RZ_BIN_METH_SYNTHETIC:
		return compact ? "h" : "synthetic";
	case RZ_BIN_METH_STRICT:
		return compact ? "t" : "strict";
	case RZ_BIN_METH_MIRANDA:
		return compact ? "A" : "miranda";
	case RZ_BIN_METH_CONSTRUCTOR:
		return compact ? "C" : "constructor";
	case RZ_BIN_METH_DECLARED_SYNCHRONIZED:
		return compact ? "Y" : "declared_synchronized";
	default:
		return NULL;
	}
}

RZ_API void rz_bin_virtual_file_free(RzBinVirtualFile *vfile) {
	if (!vfile) {
		return;
	}
	if (vfile->buf_owned) {
		rz_buf_free(vfile->buf);
	}
	free(vfile->name);
	free(vfile);
}

RZ_API void rz_bin_map_free(RzBinMap *map) {
	if (!map) {
		return;
	}
	free(map->vfile_name);
	free(map->name);
	free(map);
}

/**
 * \brief Create a list of RzBinMap from RzBinSections queried from the given file
 *
 * Some binary formats have a 1:1 correspondence of mapping and
 * their RzBinSections. This is not always the case (e.g. ELF)
 * but if it is, plugins can use this function as their maps callback,
 * which will generate mappings for sections.
 * */
RZ_API RZ_OWN RzList *rz_bin_maps_of_file_sections(RZ_NONNULL RzBinFile *binfile) {
	rz_return_val_if_fail(binfile, NULL);
	if (!binfile->o || !binfile->o->plugin || !binfile->o->plugin->sections) {
		return NULL;
	}
	RzList *sections = binfile->o->plugin->sections(binfile);
	if (!sections) {
		return NULL;
	}
	RzList *r = rz_list_newf((RzListFree)rz_bin_map_free);
	if (!r) {
		goto hcf;
	}
	RzBinSection *sec;
	RzListIter *it;
	rz_list_foreach (sections, it, sec) {
		RzBinMap *map = RZ_NEW0(RzBinMap);
		if (!map) {
			goto hcf;
		}
		map->name = sec->name ? strdup(sec->name) : NULL;
		map->paddr = sec->paddr;
		map->psize = sec->size;
		map->vaddr = sec->vaddr;
		map->vsize = sec->vsize;
		map->perm = sec->perm;
		rz_list_push(r, map);
	}
hcf:
	rz_list_free(sections);
	return r;
}

/**
 * \brief Create a list of RzBinSection from RzBinMaps
 *
 * Some binary formats have a 1:1 correspondence of mapping and
 * some of their RzBinSections, but also want to add some unmapped sections.
 * In this case, they can implement their mapped sections in their maps callback,
 * then in their sections callback use this function to create sections from them
 * and add some additional ones.
 * See also rz_bin_maps_of_file_sections() for the inverse, when no additional
 * sections should be added.
 * */
RZ_API RzList *rz_bin_sections_of_maps(RzList /*<RzBinMap>*/ *maps) {
	rz_return_val_if_fail(maps, NULL);
	RzList *ret = rz_list_newf((RzListFree)rz_bin_section_free);
	if (!ret) {
		return NULL;
	}
	RzListIter *it;
	RzBinMap *map;
	rz_list_foreach (maps, it, map) {
		RzBinSection *sec = RZ_NEW0(RzBinSection);
		if (!sec) {
			break;
		}
		sec->name = map->name ? strdup(map->name) : NULL;
		sec->paddr = map->paddr;
		sec->size = map->psize;
		sec->vaddr = map->vaddr;
		sec->vsize = map->vsize;
		sec->perm = map->perm;
		rz_list_append(ret, sec);
	}
	return ret;
}

RZ_IPI RzBinSection *rz_bin_section_new(const char *name) {
	RzBinSection *s = RZ_NEW0(RzBinSection);
	if (s) {
		s->name = name ? strdup(name) : NULL;
	}
	return s;
}

RZ_IPI void rz_bin_section_free(RzBinSection *bs) {
	if (bs) {
		free(bs->name);
		free(bs->format);
		free(bs);
	}
}

/**
 * \brief Converts the RzBinSection type to the string representation
 *
 * Some binary formats have a function interface called "section_type_to_string"
 * The returned string type name is different between formats
 *
 * \param bin RzBin instance
 * \param type A type field of the RzBinSection (differs between formats)
 * */
RZ_API RZ_OWN char *rz_bin_section_type_to_string(RzBin *bin, int type) {
	RzBinFile *a = rz_bin_cur(bin);
	RzBinPlugin *plugin = rz_bin_file_cur_plugin(a);
	if (plugin && plugin->section_type_to_string) {
		return plugin->section_type_to_string(type);
	}
	return NULL;
}

/**
 * \brief Converts the RzBinSection flags to a list of string representations
 *
 * Some binary formats have a function interface called "section_flag_to_rzlist"
 * The returned string flag names are different between formats
 *
 * \param bin RzBin instance
 * \param flag A flag field of the RzBinSection (differs between formats)
 * */
RZ_API RZ_OWN RzList *rz_bin_section_flag_to_list(RzBin *bin, ut64 flag) {
	RzBinFile *a = rz_bin_cur(bin);
	RzBinPlugin *plugin = rz_bin_file_cur_plugin(a);
	if (plugin && plugin->section_flag_to_rzlist) {
		return plugin->section_flag_to_rzlist(flag);
	}
	return NULL;
}

RZ_API RzBinFile *rz_bin_file_at(RzBin *bin, ut64 at) {
	RzListIter *it;
	RzBinFile *bf;
	rz_list_foreach (bin->binfiles, it, bf) {
		if (!bf->o) {
			continue;
		}
		RzBinMap *map = rz_bin_object_get_map_at(bf->o, at, true);
		if (map) {
			return bf;
		}
		if (at >= bf->o->opts.baseaddr && at < (bf->o->opts.baseaddr + bf->size)) {
			return bf;
		}
	}
	return NULL;
}

RZ_API RzBinTrycatch *rz_bin_trycatch_new(ut64 source, ut64 from, ut64 to, ut64 handler, ut64 filter) {
	RzBinTrycatch *tc = RZ_NEW0(RzBinTrycatch);
	if (tc) {
		tc->source = source;
		tc->from = from;
		tc->to = to;
		tc->handler = handler;
		tc->filter = filter;
	}
	return tc;
}

RZ_API void rz_bin_trycatch_free(RzBinTrycatch *tc) {
	free(tc);
}

/**
 * \brief Get a RzBinPlugin by name
 */
RZ_API const RzBinPlugin *rz_bin_plugin_get(RzBin *bin, const char *name) {
	rz_return_val_if_fail(bin && name, NULL);

	RzListIter *iter;
	RzBinPlugin *bp;

	rz_list_foreach (bin->plugins, iter, bp) {
		if (!strcmp(bp->name, name)) {
			return bp;
		}
	}
	return NULL;
}

/**
 * \brief Get a RzBinXtrPlugin by name
 */
RZ_API const RzBinXtrPlugin *rz_bin_xtrplugin_get(RzBin *bin, const char *name) {
	rz_return_val_if_fail(bin && name, NULL);

	RzListIter *iter;
	RzBinXtrPlugin *bp;

	rz_list_foreach (bin->binxtrs, iter, bp) {
		if (!strcmp(bp->name, name)) {
			return bp;
		}
	}
	return NULL;
}

#if WITH_GPL
static char *bin_demangle_cxx(RzBinFile *bf, const char *symbol, ut64 vaddr) {
	char *out = rz_demangler_cxx(symbol);
	if (!out || !bf) {
		return out;
	}
	char *sign = (char *)strchr(out, '(');
	if (!sign) {
		return out;
	}

	char *str = out;
	char *ptr = NULL;
	char *method_name = NULL;
	for (;;) {
		ptr = strstr(str, "::");
		if (!ptr || ptr > sign) {
			break;
		}
		method_name = ptr;
		str = ptr + 1;
	}

	if (RZ_STR_ISEMPTY(method_name)) {
		return out;
	}

	*method_name = 0;
	RzBinSymbol *sym = rz_bin_file_add_method(bf, out, method_name + 2, 0);
	if (sym) {
		if (sym->vaddr != 0 && sym->vaddr != vaddr) {
			RZ_LOG_INFO("Duplicated method found: %s\n", sym->name);
		}
		if (sym->vaddr == 0) {
			sym->vaddr = vaddr;
		}
	}
	*method_name = ':';
	return out;
}

static char *bin_demangle_rust(RzBinFile *binfile, const char *symbol, ut64 vaddr) {
	char *str = NULL;
	if (!(str = bin_demangle_cxx(binfile, symbol, vaddr))) {
		return str;
	}
	free(str);
	return rz_demangler_rust(symbol);
}
#endif

/**
 * \brief Demangles a symbol based on the language or the RzBinFile data
 *
 * This function demangles a symbol based on the language or the RzBinFile data
 * When C++ or rust is selected as the language, it will add methods into the
 * RzBinFile structure based on the demangled symbol.
 * When libs is set to true, the demangled symbol will be appended to the
 * library name <libname>_<demangled symbol>.
 *
 * \param bf RzBinFile data to be used for demangling
 * \param language Language to be used for demanglind
 * \param symbol Symbol to be demangled
 * \param vaddr vaddr of the \p symbol to be demangled
 * \param libs Append the library name to the demangled symbol, if set to true
 * \return char* Demangled name of the \p symbol
 */
RZ_API RZ_OWN char *rz_bin_demangle(RZ_NULLABLE RzBinFile *bf, RZ_NULLABLE const char *language, RZ_NULLABLE const char *symbol, ut64 vaddr, bool libs) {
	if (RZ_STR_ISEMPTY(symbol)) {
		return NULL;
	}

	RzBinLanguage type = RZ_BIN_LANGUAGE_UNKNOWN;
	RzBin *bin = bf ? bf->rbin : NULL;
	RzBinObject *o = bf ? bf->o : NULL;

	if (!language && o && o->info && o->info->lang) {
		language = o->info->lang;
	}

	RzListIter *iter;
	const char *lib = NULL;
	if (!strncmp(symbol, "reloc.", 6)) {
		symbol += 6;
	}
	if (!strncmp(symbol, "sym.", 4)) {
		symbol += 4;
	}
	if (!strncmp(symbol, "imp.", 4)) {
		symbol += 4;
	}
	if (!strncmp(symbol, "target.", 7)) {
		symbol += 7;
	}
	if (o) {
		bool found = false;
		rz_list_foreach (o->libs, iter, lib) {
			size_t len = strlen(lib);
			if (!rz_str_ncasecmp(symbol, lib, len)) {
				symbol += len;
				if (*symbol == '_') {
					symbol++;
				}
				found = true;
				break;
			}
		}
		if (!found) {
			lib = NULL;
		}
		size_t len = strlen(bin->file);
		if (!rz_str_ncasecmp(symbol, bin->file, len)) {
			lib = bin->file;
			symbol += len;
			if (*symbol == '_') {
				symbol++;
			}
		}
	}

	if (RZ_STR_ISEMPTY(symbol)) {
		return NULL;
	}

	if (!strncmp(symbol, "__", 2)) {
		if (symbol[2] == 'T') {
			type = RZ_BIN_LANGUAGE_SWIFT;
		} else {
			type = RZ_BIN_LANGUAGE_CXX;
		}
	}

	if (type == RZ_BIN_LANGUAGE_UNKNOWN) {
		type = rz_bin_language_to_id(language);
		// ignore "with blocks"
		type = RZ_BIN_LANGUAGE_MASK(type);
		language = rz_bin_language_to_string(type);
	}
	if (!language) {
		return NULL;
	}
	char *demangled = NULL;
	switch (type) {
	case RZ_BIN_LANGUAGE_UNKNOWN: return NULL;
	case RZ_BIN_LANGUAGE_KOTLIN:
		/* fall-thru */
	case RZ_BIN_LANGUAGE_GROOVY:
		/* fall-thru */
	case RZ_BIN_LANGUAGE_DART:
		/* fall-thru */
	case RZ_BIN_LANGUAGE_JAVA: demangled = rz_demangler_java(symbol); break;
	case RZ_BIN_LANGUAGE_OBJC: demangled = rz_demangler_objc(symbol); break;
	case RZ_BIN_LANGUAGE_MSVC: demangled = rz_demangler_msvc(symbol); break;
#if WITH_GPL
	case RZ_BIN_LANGUAGE_RUST: demangled = bin_demangle_rust(bf, symbol, vaddr); break;
	case RZ_BIN_LANGUAGE_CXX: demangled = bin_demangle_cxx(bf, symbol, vaddr); break;
#else
	case RZ_BIN_LANGUAGE_RUST: demangled = NULL; break;
	case RZ_BIN_LANGUAGE_CXX: demangled = NULL; break;
#endif
	default: rz_demangler_resolve(bin->demangler, symbol, language, &demangled);
	}
	if (libs && demangled && lib) {
		char *d = rz_str_newf("%s_%s", lib, demangled);
		free(demangled);
		demangled = d;
	}
	return demangled;
}
