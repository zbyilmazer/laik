/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <laik-internal.h>
#include <laik-backend-mpi.h>
#include <laik-backend-single.h>
#include <laik-backend-tcp.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


//program name
extern const char *__progname;

//-----------------------------------------------------------------------
// LAIK init/finalize
//
// see corresponding backend code for non-generic initialization of LAIK

// generic LAIK init function
Laik_Instance* laik_init (int* argc, char*** argv)
{
    const char* override = getenv("LAIK_BACKEND");
    Laik_Instance* inst = 0;

#ifdef USE_MPI
    if (inst == 0) {
        // default to MPI if available, or if explicitly wanted
        if ((override == 0) || (strcmp(override, "mpi") == 0)) {
            inst = laik_init_mpi(argc, argv);
        }
    }
#endif

    if (inst == 0) {
        // fall-back to "single" backend as default if MPI is not available, or
        // if "single" backend is explicitly requested
        if ((override == 0) || (strcmp(override, "single") == 0)) {
            (void) argc;
            (void) argv;
            inst = laik_init_single();
        }
    }

#ifdef USE_TCP
    if (inst == 0) {
        if ((override == 0) || (strcmp(override, "tcp") == 0)) {
            inst = laik_init_tcp(argc, argv);
        }
    }
#endif

    if (inst == 0) {
        // Error: unknown backend wanted
        assert(override != 0);

        // create dummy backend for laik_log to work
        laik_init_single();
        laik_log(LAIK_LL_Panic,
                 "Unknwown backend '%s' requested by LAIK_BACKEND", override);
        exit (1);
    }

    // wait for debugger to attach?
    char* rstr = getenv("LAIK_DEBUG_RANK");
    if (rstr) {
        int wrank = atoi(rstr);
        if ((wrank < 0) || (wrank == inst->myid)) {
            // as long as "wait" is 1, wait in loop for debugger
            volatile int wait = 1;
            while(wait) { usleep(10000); }
        }
    }

    return inst;
}


int laik_size(Laik_Group* g)
{
    return g->size;
}

int laik_myid(Laik_Group* g)
{
    return g->myid;
}

void laik_finalize(Laik_Instance* inst)
{
    laik_log(1, "finalizing...");
    if (inst->backend && inst->backend->finalize)
        (*inst->backend->finalize)(inst);

    if (inst->repart_ctrl){
        laik_ext_cleanup(inst);
    }

    if (laik_log_begin(2)) {
        laik_log_append("switch statistics (this task):\n");
        Laik_SwitchStat* ss = laik_newSwitchStat();
        for(int i=0; i<inst->data_count; i++) {
            Laik_Data* d = inst->data[i];
            laik_addSwitchStat(ss, d->stat);

            laik_log_append("  data '%s': ", d->name);
            laik_log_SwitchStat(d->stat);
        }
        if (inst->data_count > 1) {
            laik_log_append("  summary: ");
            laik_log_SwitchStat(ss);
        }
        free(ss);

        laik_log_flush(0);
    }

    laik_close_profiling_file(inst);
    laik_free_profiling(inst);
    free(inst->control);

    laik_log_cleanup(inst);
}

// return a backend-dependant string for the location of the calling task
char* laik_mylocation(Laik_Instance* inst)
{
    return inst->mylocation;
}

// allocate space for a new LAIK instance
Laik_Instance* laik_new_instance(const Laik_Backend* b,
                                 int size, int myid,
                                 char* location, void* data, void *gdata)
{
    Laik_Instance* instance;
    instance = malloc(sizeof(Laik_Instance));
    if (!instance) {
        laik_panic("Out of memory allocating Laik_Instance object");
        exit(1); // not actually needed, laik_panic never returns
    }

    instance->backend = b;
    instance->backend_data = data;
    instance->size = size;
    instance->myid = myid;
    instance->mylocation = strdup(location);
    instance->locationStore = 0;

    // for logging wall-clock time since LAIK initialization
    gettimeofday(&(instance->init_time), NULL);

    instance->firstSpaceForInstance = 0;

    instance->group_count = 0;
    instance->data_count = 0;
    instance->mapping_count = 0;

    laik_space_init();
    laik_data_init(); // initialize the data module

    instance->control = laik_program_control_init();
    instance->profiling = laik_init_profiling();

    instance->repart_ctrl = 0;

    // logging (TODO: multiple instances)
    laik_log_init(instance);

    if (laik_log_begin(2)) {
        laik_log_append_info();
        laik_log_flush(0);
    }

    // Create a group in this instance with same parameters as the instance.
    // Since it's the first group, this is what laik_world() will return.
    Laik_Group* first_group = laik_create_group (instance);
    first_group->size         = size;
    first_group->myid         = myid;
    first_group->backend_data = gdata;

    // Assign default location mappings
    for(int i = 0; i <first_group->size; i++) {
        first_group->toLocation[i] = i;
    }

    return instance;
}

// add/remove space to/from instance
void laik_addSpaceForInstance(Laik_Instance* inst, Laik_Space* s)
{
    assert(s->nextSpaceForInstance == 0);
    s->nextSpaceForInstance = inst->firstSpaceForInstance;
    inst->firstSpaceForInstance = s;
}

void laik_removeSpaceFromInstance(Laik_Instance* inst, Laik_Space* s)
{
    if (inst->firstSpaceForInstance == s) {
        inst->firstSpaceForInstance = s->nextSpaceForInstance;
    }
    else {
        // search for previous item
        Laik_Space* ss = inst->firstSpaceForInstance;
        while(ss->nextSpaceForInstance != s)
            ss = ss->nextSpaceForInstance;
        assert(ss != 0); // not found, should not happen
        ss->nextSpaceForInstance = s->nextSpaceForInstance;
    }
    s->nextSpaceForInstance = 0;
}

void laik_addDataForInstance(Laik_Instance* inst, Laik_Data* d)
{
    assert(inst->data_count < MAX_DATAS);
    inst->data[inst->data_count] = d;
    inst->data_count++;
}


// create a group to be used in this LAIK instance
Laik_Group* laik_create_group(Laik_Instance* i)
{
    assert(i->group_count < MAX_GROUPS);

    Laik_Group* g;

    g = malloc(sizeof(Laik_Group) + 3 * (i->size) * sizeof(int));
    if (!g) {
        laik_panic("Out of memory allocating Laik_Group object");
        exit(1); // not actually needed, laik_panic never returns
    }
    i->group[i->group_count] = g;

    g->inst = i;
    g->gid = i->group_count;
    g->size = 0; // yet invalid
    g->backend_data = 0;
    g->parent = 0;

    // space after struct
    g->toParent   = (int*) (((char*)g) + sizeof(Laik_Group));
    g->fromParent = g->toParent + i->size;
    g->toLocation = g->fromParent + i->size;

    i->group_count++;
    return g;
}

Laik_Instance* laik_inst(Laik_Group* g)
{
    return g->inst;
}

Laik_Group* laik_world(Laik_Instance* i)
{
    // world must have been added by backend
    assert(i->group_count > 0);

    Laik_Group* g = i->group[0];
    assert(g->gid == 0);
    assert(g->inst == i);
    assert(g->size == i->size);

    return g;
}

// create a clone of <g>, derived from <g>.
Laik_Group* laik_clone_group(Laik_Group* g)
{
    Laik_Group* g2 = laik_create_group(g->inst);
    g2->parent = g;
    g2->size = g->size;
    g2->myid = g->myid;

    for(int i=0; i < g->size; i++) {
        g2->toParent[i] = i;
        g2->fromParent[i] = i;
        g2->toLocation[i] = g->toLocation[i];
    }

    return g2;
}


// Shrinking (collective)
Laik_Group* laik_new_shrinked_group(Laik_Group* g, int len, int* list)
{
    Laik_Group* g2 = laik_clone_group(g);

    for(int i = 0; i < g->size; i++)
        g2->fromParent[i] = 0; // init

    for(int i = 0; i < len; i++) {
        assert((list[i] >= 0) && (list[i] < g->size));
        g2->fromParent[list[i]] = -1; // mark removed
    }
    int o = 0;
    for(int i = 0; i < g->size; i++) {
        if (g2->fromParent[i] < 0) continue;
        g2->fromParent[i] = o;
        g2->toParent[o] = i;
        g2->toLocation[o] = g->toLocation[i];
        o++;
    }
    g2->size = o;
    g2->myid = (g->myid < 0) ? -1 : g2->fromParent[g->myid];

    if (g->inst->backend->updateGroup)
        (g->inst->backend->updateGroup)(g2);

    if (laik_log_begin(1)) {
        laik_log_append("shrink group: "
                        "%d (size %d, myid %d) => %d (size %d, myid %d):",
                        g->gid, g->size, g->myid, g2->gid, g2->size, g2->myid);
        laik_log_append("\n  fromParent (to shrinked)  : ");
        laik_log_IntList(g->size, g2->fromParent);
        laik_log_append("\n  toParent   (from shrinked): ");
        laik_log_IntList(g2->size, g2->toParent);
        laik_log_append("\n  toLocation (in shrinked): ");
        laik_log_IntList(g2->size, g2->toLocation);
        laik_log_flush(0);
    }

    return g2;
}

int laik_group_locationid(Laik_Group *group, int id)
{
    assert(id >= 0 && id < group->size);
    return group->toLocation[id];
}

static char* locationkey(int loc) {
    static char key[10];
    snprintf(key, 10, "%i", loc);
    return key;
}

// synchronize location strings via KVS among processes in current world
void laik_sync_location(Laik_Instance *instance)
{
    if (instance->locationStore == NULL)
        instance->locationStore = laik_kvs_new("location", instance);

    Laik_Group* world = laik_world(instance);
    char* mylocation = laik_mylocation(instance);
    char* myKey = locationkey(laik_group_locationid(world, laik_myid(world)));

    laik_kvs_sets(instance->locationStore, myKey, mylocation);
    laik_kvs_sync(instance->locationStore);
}

// get location string identifier from process index in given group
char* laik_group_location(Laik_Group *group, int id)
{
    if (group->inst->locationStore == NULL)
        return NULL;

    char* myKey = locationkey(laik_group_locationid(group, id));
    return laik_kvs_get(group->inst->locationStore, myKey, NULL);
}


// Utilities

char* laik_get_guid(Laik_Instance* i){
    return i->guid;
}



//--------------------------------------------------------
// KV Store
//
// Key value store with explicit synchronous update among
// all processes in current LAIK world of a LAIK instance.
// Additions/deletions/changes are recorded in a change journal
// which is sent to all other processes when laik_kvs_sync()
// is called by all processes collectivly.


// Laik_KVS_Changes
// internal API for KVS change journal

// return new empty change journal
Laik_KVS_Changes* laik_kvs_changes_new()
{
    Laik_KVS_Changes* c;
    c = (Laik_KVS_Changes*) malloc(sizeof(Laik_KVS_Changes));
    laik_kvs_changes_init(c);

    return c;
}

void laik_kvs_changes_init(Laik_KVS_Changes* c)
{
    c->offSize = 0;
    c->offUsed = 0;
    c->off = 0;

    c->dataSize = 0;
    c->dataUsed = 0;
    c->data = 0;

    c->entrySize = 0;
    c->entryUsed = 0;
    c->entry = 0;
}

// ensure reserved space for <n> offsets and <dlen> bytes for key/values
void laik_kvs_changes_ensure_size(Laik_KVS_Changes* c, int n, int dlen)
{
    // only allowed if entry array not set yet, as re-allocation could
    // invalidate pointers in entries
    assert(c->entryUsed == 0);

    // n offsets: space for n/2 keys and values
    if (c->offSize < n) {
        c->off = (int*) realloc(c->off, (unsigned) n * sizeof(int));
        c->offSize = n;
    }
    if (c->dataSize < dlen) {
        c->data = (char*) realloc(c->data, (unsigned) dlen);
        c->dataSize = dlen;
    }
    int entries = n/2;
    if (c->entrySize < entries) {
        c->entry = (Laik_KVS_Entry*) realloc(c->entry,
                                             (unsigned) entries * sizeof(Laik_KVS_Entry));
        c->entrySize = entries;
    }
}

// set size, to be called after ...ensure_size and setting data
void laik_kvs_changes_set_size(Laik_KVS_Changes* c, int n, int dlen)
{
    assert((n == 0) || ((n & 1) == 1)); // must be zero or odd number of offsets
    assert(c->offSize >= n);
    assert(c->dataSize >= dlen);
    assert(c->entrySize >= n/2);

    c->offUsed = n;
    c->dataUsed = dlen;
    c->entryUsed = 0; // will be populated by sorting the changes
}


// free resources used, but not the change journal struct itself
void laik_kvs_changes_free(Laik_KVS_Changes* c)
{
    if (c->off) {
        free(c->off);
        c->off = 0;
        c->offSize = 0;
    }
    if (c->data) {
        free(c->data);
        c->data = 0;
        c->dataSize = 0;
    }
    if (c->entry) {
        free(c->entry);
        c->entry = 0;
        c->entrySize = 0;
    }
}

// allocate own space in <c>, and does deep copy of <key> and <data>
void laik_kvs_changes_add(Laik_KVS_Changes* c,
                          char* key, int dlen, char* data,
                          bool do_alloc, bool append_sorted)
{
    // queue this update for propagation on next sync:
    // this adds 2 data blocks with key and value, and offsets

    int klen = (int) strlen(key) + 1; // with terminating zero at end

    if (do_alloc) {
        int offNeeded  = (c->offUsed == 0) ? 3 : (c->offUsed + 2);
        int dataNeeded = c->dataUsed + klen + dlen;
        if ((c->offSize < offNeeded) || (c->dataSize < dataNeeded)) {
            // allocate a bit more space for changes than required
            laik_kvs_changes_ensure_size(c, 2 * offNeeded, 2 * dataNeeded);
        }
    }

    if (c->offUsed == 0) {
        // with non-zero number of entries, number of offsets are odd:
        // last offset points to end of data array. Need to set this for entry 0.
        c->off[0] = 0;
        c->offUsed++;
    }

    assert(c->offUsed + 2 <= c->offSize); // 2 new entries
    assert(c->dataUsed + klen + dlen <= c->dataSize); // name and data must fit
    assert(c->off[c->offUsed - 1] == c->dataUsed); // marker correct?
    char* newkey = c->data + c->dataUsed;
    memcpy(newkey, key, (size_t) klen); // includes terminating zero at end
    c->dataUsed += klen;
    c->off[c->offUsed] = c->dataUsed;
    char* newdata = c->data + c->dataUsed;
    memcpy(newdata, data, (size_t) dlen);
    c->dataUsed += dlen;
    c->off[c->offUsed + 1] = c->dataUsed;
    c->offUsed += 2;

    // sorted is true if called within merge operation
    if (!append_sorted) return;

    // append to entry array
    if (c->entryUsed > 0) {
        // check that given key really sorts after last entry
        assert(strcmp(c->entry[c->entryUsed - 1].key, key) < 0);
    }
    assert(c->entrySize > c->entryUsed);
    Laik_KVS_Entry* e = &(c->entry[c->entryUsed]);
    c->entryUsed++;
    e->key = newkey;
    e->data = newdata;
    e->dlen = (unsigned) dlen;
    e->updated = false;
}

// for qsort in laik_kvs_changes_sort and laik_kvs_sort
static int entrycmp(const void * v1, const void * v2)
{
    const Laik_KVS_Entry* e1 = (const Laik_KVS_Entry*) v1;
    const Laik_KVS_Entry* e2 = (const Laik_KVS_Entry*) v2;
    return strcmp(e1->key, e2->key);
}

void laik_kvs_changes_sort(Laik_KVS_Changes* c)
{
    // first fill entry array from data/offset array, then sort
    // this array by keys

    if (c->offUsed == 0) return;
    assert((c->offUsed & 1) == 1); // must be odd number if not 0

    assert(c->entryUsed == 0);
    int off = 0;
    while(off + 2 < c->offUsed) {
        // space should be reserved
        assert(c->entryUsed < c->entrySize);
        Laik_KVS_Entry* e = c->entry + c->entryUsed;
        c->entryUsed++;

        assert(c->off[off] < c->off[off + 1]); // key range
        assert(c->off[off + 1] < c->off[off + 2]); // value range
        assert(c->off[off + 2] <= c->dataUsed);
        // key must be terminated by 0
        assert(c->data[c->off[off + 1] - 1] == 0);

        e->key = c->data + c->off[off];
        e->data = c->data + c->off[off + 1];
        e->dlen = (unsigned) (c->off[off + 2] - c->off[off + 1]);
        e->updated = false;

        off += 2;
    }
    assert(c->entryUsed * 2 + 1 == c->offUsed);

    // now sort
    qsort(c->entry, (size_t) c->entryUsed, sizeof(Laik_KVS_Entry), entrycmp);
}

void laik_kvs_changes_merge(Laik_KVS_Changes* dst,
                            Laik_KVS_Changes* src1, Laik_KVS_Changes* src2)
{
    // clear dst, but ensure dst to be large enough for merge result
    laik_kvs_changes_set_size(dst, 0, 0);
    laik_kvs_changes_ensure_size(dst,
                                 src1->offUsed + src2->offUsed,
                                 src1->dataUsed + src2->dataUsed);

    int off1 = 0, off2 = 0;
    while((off1 < src1->entryUsed) && (off2 < src2->entryUsed)) {
        Laik_KVS_Entry* e1 = src1->entry + off1;
        Laik_KVS_Entry* e2 = src2->entry + off2;
        int res = strcmp(e1->key, e2->key);
        if (res < 0) {
            laik_kvs_changes_add(dst, e1->key, e1->dlen, e1->data, false, true);
            off1++;
        }
        else if (res > 0) {
            laik_kvs_changes_add(dst, e2->key, e2->dlen, e2->data, false, true);
            off2++;
        }
        else {
            if ((e1->dlen != e2->dlen) || (strncmp(e1->data, e2->data, e1->dlen) != 0)) {
                laik_log(LAIK_LL_Panic,
                         "Merging KV changes at key '%s': update inconsistency\n",
                         e1->key);
                exit(1);
            }
            laik_kvs_changes_add(dst, e1->key, e1->dlen, e1->data, false, true);
            off1++;
            off2++;
        }
    }
    while(off1 < src1->entryUsed) {
        Laik_KVS_Entry* e = src1->entry + off1;
        laik_kvs_changes_add(dst, e->key, e->dlen, e->data, false, true);
        off1++;
    }
    while(off2 < src2->entryUsed) {
        Laik_KVS_Entry* e = src2->entry + off2;
        laik_kvs_changes_add(dst, e->key, e->dlen, e->data, false, true);
        off2++;
    }
}

void laik_kvs_changes_apply(Laik_KVS_Changes* c, Laik_KVStore *kvs)
{
    if (c->offUsed == 0) return;
    assert((c->offUsed & 1) == 1); // must be odd number if not 0

    assert(c->dataUsed > 0);
    assert(c->off != 0);
    assert(c->data != 0);
    for(int i = 0; i + 1 < c->offUsed; i += 2) {
        assert(c->off[i+1] > c->off[i]);
        assert(c->off[i+2] > c->off[i+1]);
        laik_kvs_set(kvs,
                     c->data + c->off[i],
                     (unsigned)(c->off[i+2] - c->off[i+1]), // data size
                     c->data + c->off[i+1]);
    }
}


//
// Laik_KVStore
//

Laik_KVStore* laik_kvs_new(const char* name, Laik_Instance *inst)
{
    Laik_KVStore* kvs = (Laik_KVStore*) malloc(sizeof(Laik_KVStore));

    kvs->name = name;
    kvs->inst = inst;

    kvs->size = 1000;
    kvs->entry = (Laik_KVS_Entry*) malloc(kvs->size * sizeof(Laik_KVS_Entry));
    kvs->used = 0;
    kvs->sorted_upto = 0;

    laik_kvs_changes_init(&(kvs->changes));
    laik_kvs_changes_ensure_size(&(kvs->changes), 10, 1000);
    kvs->in_sync = false;

    return kvs;
}

void laik_kvs_free(Laik_KVStore* kvs)
{
    assert(kvs);

    free(kvs->entry);
    laik_kvs_changes_free(&(kvs->changes));
    free(kvs);
}

// set a binary data blob as value for key (deep copy, overwrites if key exists)
// returns false if key is already set to given value
bool laik_kvs_set(Laik_KVStore* kvs, char* key, unsigned int size, char* data)
{
    assert(data != 0);

    Laik_KVS_Entry* e = laik_kvs_entry(kvs, key);
    if (e && (memcmp(e->data, data, (size_t) size) == 0)) {
        laik_log(1, "in KVS '%s' set entry '%s' (size %d, '%.20s'): already existing",
                 kvs->name, key, size, data);
        return false;
    }

    if (!e) {
        assert(kvs->used < kvs->size);
        e = &kvs->entry[kvs->used];
        kvs->used++;
        e->key = strdup(key);
        e->data = 0;
        e->updated = false;
    }

    laik_log(1, "in KVS '%s' set %s entry '%s' (size %d) to '%.20s'",
             kvs->name, (e->data == 0) ? "new" : "changed", key, size, data);

    if (e->updated && kvs->in_sync) {
        // update from other process and updated ourself differently
        laik_log(LAIK_LL_Panic,
                 "KVS '%s' at key '%s': update inconsistency\n",
                 kvs->name, key);
        exit(1);
    }

    free(e->data);
    e->data = (char*) malloc(size);
    assert(e->data);
    memcpy(e->data, data, size);
    e->dlen = size;

    if (kvs->in_sync) return true;
    e->updated = true;

    laik_kvs_changes_add(&(kvs->changes), key, size, data, true, false);

    return true;
}

// set a null-terminated string as value for key
bool laik_kvs_sets(Laik_KVStore* kvs, char* key, char* str)
{
    unsigned int len = strlen(str) + 1; // include null at end
    return laik_kvs_set(kvs, key, len, str);
}

// synchronize KV store
void laik_kvs_sync(Laik_KVStore* kvs)
{
    const Laik_Backend* b = kvs->inst->backend;
    assert(b && b->sync);

    laik_log(1, "sync KVS '%s' (progagating %d/%d entries) ...",
             kvs->name, kvs->changes.offUsed / 2, kvs->used);
    kvs->in_sync = true;
    (b->sync)(kvs);
    kvs->in_sync = false;

    // all queued entries sent, remove
    laik_kvs_changes_set_size(&(kvs->changes), 0, 0);

    for(unsigned int i = 0; i < kvs->used; i++)
        kvs->entry[i].updated = false;

    laik_log(1, "  sync done (now %d entries).", kvs->used);

    laik_kvs_sort(kvs);
}

Laik_KVS_Entry* laik_kvs_entry(Laik_KVStore* kvs, char* key)
{
    Laik_KVS_Entry* e;

    // do binary search in range [0 .. (sorted_upto-1)]
    int low = 0, high = ((int)kvs->sorted_upto) - 1, mid, res;
    //laik_log(1, "  binary search in KVS '%s' for '%s' in [%d,%d]",
    //         kvs->name, key, low, high);
    while(low <= high) {
        mid = (low + high) / 2;
        e = &(kvs->entry[mid]);
        res = strcmp(key, e->key);
        //laik_log(1, "    test [%d] '%s': %d", mid, e->key, res);
        if (res == 0)
            return e;
        if (res < 0)
            high = mid-1;
        else
            low = mid+1;
    }

    // linear search for unsorted items
    for(unsigned int i = kvs->sorted_upto; i < kvs->used; i++) {
        e = &(kvs->entry[i]);
        if (strcmp(e->key, key) == 0)
            return e;
    }

    return 0;
}

char* laik_kvs_data(Laik_KVS_Entry* e, unsigned int *psize)
{
    assert(e);
    if (psize) *psize = e->dlen;
    return e->data;
}

char* laik_kvs_get(Laik_KVStore* kvs, char* key, unsigned int* psize)
{
    Laik_KVS_Entry* e = laik_kvs_entry(kvs, key);
    if (!e) return 0;

    return laik_kvs_data(e, psize);
}

unsigned int laik_kvs_count(Laik_KVStore* kvs)
{
    assert(kvs);
    return kvs->used;
}

Laik_KVS_Entry* laik_kvs_getn(Laik_KVStore* kvs, unsigned int n)
{
    assert(kvs);
    if (n >= kvs->used) return 0;
    return &(kvs->entry[n]);
}

char* laik_kvs_key(Laik_KVS_Entry* e)
{
    assert(e);
    return e->key;
}

unsigned int laik_kvs_size(Laik_KVS_Entry* e)
{
    assert(e);
    return e->dlen;
}


unsigned int laik_kvs_copy(Laik_KVS_Entry* e, char* mem, unsigned int size)
{
    assert(e);
    if (e->dlen < size) size = e->dlen;
    memcpy(mem, e->data, size);
    return size;
}


void laik_kvs_sort(Laik_KVStore* kvs)
{
    qsort(kvs->entry, kvs->used, sizeof(Laik_KVS_Entry), entrycmp);
    kvs->sorted_upto = kvs->used;
}

