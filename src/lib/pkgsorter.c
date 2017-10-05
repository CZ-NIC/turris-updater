/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the turris updater.
 *
 * Updater is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * Updater is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Updater.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pkgsorter.h"
#include "inject.h"
#include "logging.h"

#include <stdbool.h>
#include <string.h>
#include <lauxlib.h>
#include <lualib.h>
#include <uthash.h>
#include <utarray.h>

#define PKGSORTER_META "updater_pkgsorter_meta"
#define ITERATOR_DATA_META "updater_pkgsorter_iterator_data_meta"

#define EDGE_TYPES \
	X(CONFLICTS) \
	X(PROVIDES) \
	X(DEPENDS) \
	X(FORCE)

enum edge_type {
#define X(VAL) ET_##VAL,
	EDGE_TYPES
	ET_LAST // Just technical last element, not a real edge type.
#undef X
};

struct edge {
	enum edge_type type;
	struct node *to;
	bool rev, active;
};

struct node {
	char *name;
	int priority;
	unsigned branch; // Number of edges pointing to this node
	UT_array *edges; // All edges originating from this node
	UT_hash_handle hh;
};

struct pkgsorter {
	struct node *nodes;
	bool pruned;
};

UT_icd edge_icd = {sizeof(struct edge), NULL, NULL, NULL};
UT_icd node_icd = {sizeof(struct node*), NULL, NULL, NULL};

static int lua_pkgsorter_new(lua_State *L) {
	struct pkgsorter *psort = lua_newuserdata(L, sizeof *psort);
	psort->nodes = NULL;
	psort->pruned = false;
	// Set corresponding meta table
	luaL_getmetatable(L, PKGSORTER_META);
	lua_setmetatable(L, -2);
	return 1;
}

static const struct inject_func funcs[] = {
	{ lua_pkgsorter_new, "new" }
};

static int lua_node(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	const char *name = luaL_checkstring(L, 2);
	int priority = luaL_checkinteger(L, 3);
	// Note: Adding new node doesn't influence pruned status
	struct node *n = malloc(sizeof *n);
	n->name = strdup(name);
	n->branch = 0;
	n->priority = priority;
	utarray_new(n->edges, &edge_icd);
	HASH_ADD_KEYPTR(hh, psort->nodes, n->name, strlen(n->name), n);
	return 0;
}

static int lua_edge(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	int type = luaL_checkint(L, 2);
	const char *from = luaL_checkstring(L, 3);
	const char *to = luaL_checkstring(L, 4);
	bool rev = lua_toboolean(L, 5);
	if (type < 0 || type >= ET_LAST)
		return luaL_error(L, "Specified unknown type %d", type);
	struct node *nfrom, *nto;
	HASH_FIND_STR(psort->nodes, from, nfrom);
	HASH_FIND_STR(psort->nodes, to, nto);
	if (!nfrom)
		return luaL_error(L, "Argument 'from' specifies nonexistent node");
	if (!nto)
		return luaL_error(L, "Argument 'to' specifies nonexistent node");
	psort->pruned = false; // invalidate prune status
	nto->branch++;
	struct edge e;
	e.to = nto;
	e.type = type;
	e.rev = rev;
	e.active = true;
	if (!rev && nto->priority > nfrom->priority)
		nfrom->priority = nto->priority; // Elevate node priority
	utarray_push_back(nfrom->edges, &e);
	return 0;
}

static int edge_sort(const void *a, const void *b) {
	struct edge *na = (struct edge*)a;
	struct edge *nb = (struct edge*)b;
	if (na->type != nb->type)
		return (na->type > nb->type) ? 1 : -1;
	else
		return (na->to->priority > nb->to->priority) - (na->to->priority < nb->to->priority);
}

struct prune_nodes {
	struct node *n;
	struct edge *we; // Edge we are working on (also marks if we are working on this node)
	UT_hash_handle hh;
};

static int prune_sort(const void *a, const void *b) {
	struct prune_nodes *na = *(struct prune_nodes**)a;
	struct prune_nodes *nb = *(struct prune_nodes**)b;
	return edge_sort(na->we, nb->we);
}

static void prune_recurse(lua_State *L, struct prune_nodes **pn, struct node *node) {
	// Check if we are not in cycle
	struct prune_nodes *cycle;
	HASH_FIND_PTR(*pn, &node, cycle);
	if (cycle) { // Already visited node
		if (cycle->we) { // Node we are working on
			UT_array *trace;
			utarray_new(trace, &((UT_icd){sizeof(struct prune_nodes*), NULL, NULL, NULL}));
			struct prune_nodes *w = cycle, *w_find;
			do {
				utarray_push_back(trace, &w);
				if (!w->we)
					return; // This can happen in we have already broken cycle. In such case this is not cycle.
				HASH_FIND_PTR(*pn, &w->we->to, w_find);
				w = w_find;
				ASSERT(w);
			} while (w != cycle);
			utarray_sort(trace, prune_sort);
			struct prune_nodes *tocut = *(struct prune_nodes**)utarray_front(trace); // Take edge with lowest priority
			// Push edge and trace to return table
			lua_pushinteger(L, lua_objlen(L, -1) + 1); // Index in returned table
			lua_newtable(L);
			lua_pushinteger(L, tocut->we->type);
			lua_setfield(L, -2, "type");
			lua_pushstring(L, tocut->n->name);
			lua_setfield(L, -2, "from");
			lua_pushstring(L, tocut->we->to->name);
			lua_setfield(L, -2, "to");
			lua_newtable(L);
			struct prune_nodes **wi = NULL;
			while ((wi = (struct prune_nodes**)utarray_next(trace, wi))) {
				w = *wi;
				lua_pushstring(L, w->n->name);
				lua_pushboolean(L, true);
				lua_settable(L, -3);
			}
			lua_setfield(L, -2, "cycle");
			lua_settable(L, -3); // Push created table to returned one
			utarray_free(trace);
			// Decrement number of edges pointing to target node
			tocut->we->to->branch--;
			// Deactivate given node
			tocut->we->active = false;
			tocut->we = NULL;
		}
		return;
	}

	// Push current node to hash table
	struct prune_nodes *cn = malloc(sizeof *cn);
	cn->n = node;
	HASH_ADD_PTR(*pn, n, cn);
	// Sort edges (this is additional step for iterator)
	utarray_sort(node->edges, edge_sort);
	// Recurse trough all edges
	struct edge *e = NULL;
	while ((e = (struct edge*)utarray_prev(cn->n->edges, e))) {
		cn->we = e;
		prune_recurse(L, pn, e->to);
	}
	cn->we = NULL; // Mark that we are no longer working on this node
}

static int lua_prune(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	lua_newtable(L); // Table with disabled edges and cycles
	struct prune_nodes *pn = NULL, *pn_cur = NULL, *pn_tmp = NULL;
	struct node *w;
	for(w= psort->nodes; w != NULL; w= w->hh.next)
		prune_recurse(L, &pn, w);
	HASH_ITER(hh, pn, pn_cur, pn_tmp) { // Free hash table
		HASH_DEL(pn, pn_cur);
		free(pn_cur);
	}
	psort->pruned = true;
	return 1;
}

struct iterator_data {
	struct pkgsorter *psort;
	struct node **olist;
	bool *expanded;
	size_t size, allocated;
};

static int lua_iterator_data_gc(lua_State *L) {
	struct iterator_data *dt = luaL_checkudata(L, 1, ITERATOR_DATA_META);
	free(dt->olist);
	free(dt->expanded);
	return 0;
}

#define OLIST_EXTEND(ITERATOR_DATA) do { \
		if ((ITERATOR_DATA)->size == (ITERATOR_DATA)->allocated) { \
			(ITERATOR_DATA)->allocated *= 2; \
			(ITERATOR_DATA)->olist = realloc((ITERATOR_DATA)->olist, \
					sizeof *(ITERATOR_DATA)->olist * (ITERATOR_DATA)->allocated); \
			(ITERATOR_DATA)->expanded = realloc((ITERATOR_DATA)->expanded, \
					sizeof *(ITERATOR_DATA)->expanded * (ITERATOR_DATA)->allocated); \
		} \
	} while(0)

static struct edge *iterator_next_edge(bool rev, UT_array *edges, struct edge *n) {
	if (rev)
		return (struct edge*)utarray_prev(edges, n);
	else
		return (struct edge*)utarray_next(edges, n);
}

static void iterator_expand(struct iterator_data *dt, UT_array *edges, bool rev) {
	// Note: edges are sorted as part of prune process and iterator can't be executed if it's not pruned.
	struct edge *n = NULL;
	while ((n = iterator_next_edge(rev, edges, n))) {
		if (n->rev != rev || !n->active)
			continue;
		OLIST_EXTEND(dt);
		dt->olist[dt->size] = n->to;
		dt->expanded[dt->size] = false;
		dt->size++;
	}
}

static int lua_iterator_internal(lua_State *L) {
	struct iterator_data *dt = luaL_checkudata(L, 1, ITERATOR_DATA_META);
	if (!dt->psort->pruned)
		return luaL_error(L, "Adding new edges durring iteration is not supported");
	if (dt->size == 0) {
		lua_pushnil(L);
		return 1;
	}
	while (!dt->expanded[dt->size - 1]) {
		struct node *we = dt->olist[--dt->size]; // Temporally drop current node
		iterator_expand(dt, we->edges, true); // Push reverse edges
		OLIST_EXTEND(dt);
		dt->olist[dt->size] = we; // Push current node back
		dt->expanded[dt->size] = true;
		dt->size++;
		iterator_expand(dt, we->edges, false); // Push direct edges
	}
	dt->size--;
	lua_pushstring(L, dt->olist[dt->size]->name);
	return 1;
}

static int compare_node_priority(const void *a, const void *b) {
	struct node *na = *(struct node**)a;
	struct node *nb = *(struct node**)b;
	// Note that this causes unstable order for packages with same priority but we don't care about those.
	return (na->priority > nb->priority) - (na->priority < nb->priority);
}

static int lua_isnode(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	const char *name = luaL_checkstring(L, 1);
	struct node *node = NULL;
	HASH_FIND_STR(psort->nodes, name, node);
	lua_pushboolean(L, node != NULL);
	return 1;
}

static int lua_iterator(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	if (!psort->pruned)
		return luaL_error(L, "Before iterating you have to prune pkgsorter.");
	struct node *root = NULL;
	const char *nroot = lua_tostring(L, 2);
	if (nroot) {
		HASH_FIND_STR(psort->nodes, nroot, root);
		if (!root)
			luaL_error(L, "Requested unknown iterator root: %s", nroot);
	}
	// First returned value is iterator function
	lua_pushcfunction(L, lua_iterator_internal);
	// Second returned value is iterator data
	struct iterator_data *idt = lua_newuserdata(L, sizeof *idt);
	luaL_getmetatable(L, ITERATOR_DATA_META);
	lua_setmetatable(L, -2);

	idt->psort = psort;
	idt->size = 0;
	idt->allocated = 4;
	idt->olist = malloc(sizeof *idt->olist * idt->allocated);
	idt->expanded = malloc(sizeof *idt->expanded * idt->allocated);
	if (root) {
		OLIST_EXTEND(idt);
		idt->olist[idt->size] = root;
		idt->expanded[idt->size] = false;
		idt->size++;
	} else {
		struct node *w;
		for(w = psort->nodes; w != NULL; w = w->hh.next) { // Push roots to open list
			if (w->branch || (root && root->priority >= w->priority))
				continue;
			OLIST_EXTEND(idt);
			idt->olist[idt->size] = w;
			idt->expanded[idt->size] = false;
			idt->size++;
		}
		// Sort roots by priority
		qsort(idt->olist, idt->size, sizeof *idt->olist, compare_node_priority); // Sort roots by priority
	}
	return 2;
}

static int lua_index(lua_State *L) {
	if (luaL_getmetafield(L, 1, luaL_checkstring(L, 2)) == 0)
		lua_pushnil(L);
	return 1;
}

static int lua_pkgsorter_gc(lua_State *L) {
	struct pkgsorter *psort = luaL_checkudata(L, 1, PKGSORTER_META);
	TRACE("Freeing pkgsorter");
	struct node *w = NULL, *t = NULL;
	HASH_ITER(hh, psort->nodes, w, t) {
		HASH_DEL(psort->nodes, w);
		utarray_free(w->edges);
		free(w->name);
		free(w);
	}
	return 0;
}

static const struct inject_func pkgsorter_meta[] = {
	{ lua_node, "node" },
	{ lua_edge, "edge" },
	{ lua_prune, "prune" },
	{ lua_isnode, "isnode" },
	{ lua_iterator, "iterator" },
	{ lua_index, "__index" },
	{ lua_pkgsorter_gc, "__gc" }
};

static const struct inject_func iterator_meta[] = {
	{ lua_iterator_data_gc, "__gc" }
};

void pkgsorter_mod_init(lua_State *L) {
	TRACE("Orderer module init");
	lua_newtable(L);
#define X(VAL) TRACE("Injecting edge types constants." #VAL); lua_pushinteger(L, ET_##VAL); lua_setfield(L, -2, #VAL);
	EDGE_TYPES
#undef X
	inject_func_n(L, "pkgsorter", funcs, sizeof funcs / sizeof *funcs);
	inject_module(L, "pkgsorter");
	ASSERT(luaL_newmetatable(L, PKGSORTER_META) == 1);
	inject_func_n(L, PKGSORTER_META, pkgsorter_meta, sizeof pkgsorter_meta / sizeof *pkgsorter_meta);
	ASSERT(luaL_newmetatable(L, ITERATOR_DATA_META) == 1);
	inject_func_n(L, PKGSORTER_META, iterator_meta, sizeof iterator_meta / sizeof *iterator_meta);
}
