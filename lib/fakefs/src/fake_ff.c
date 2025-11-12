// Minimal in-memory fake filesystem for emulator mode
// Supports: mount, cwd, open/read/seek/close, opendir/readdir/closedir, gets/printf, stat
// and simple cluster helpers used by the kernel (contiguous mapping)

#include "../../fatfs/include/ff.h"
// Fallback for memory placement in emulator: map to EWRAM if available
#ifndef EWRAM_BSS
#define EWRAM_BSS __attribute__((section(".ewram")))
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

// -------------------- Simple FS model --------------------

typedef struct FsNode FsNode;
struct FsNode {
    char name[100];
    BYTE attr;            // AM_DIR for directory, AM_ARC for files
    FsNode* parent;
    FsNode* firstChild;   // for directories
    FsNode* nextSibling;  // for sibling list
    BYTE* data;           // for files
    FSIZE_t size;         // for files
    DWORD startCluster;   // fake contiguous cluster chain start
};

static FATFS* g_fs = NULL;           // provided by caller on mount
static FsNode* g_root = NULL;        // root directory
static FsNode* g_cwd = NULL;         // current working directory
static DWORD g_nextCluster = 2;      // next cluster to allocate (FAT uses 2-based clusters)
static int g_mounted = 0;

// Use a static pool for nodes to avoid heap fragmentation / failures in emulator
#define FS_MAX_NODES 64 // sufficient for emulator; smaller to save IWRAM
static FsNode g_node_pool[FS_MAX_NODES] EWRAM_BSS; // place in EWRAM, not IWRAM
static unsigned g_node_used EWRAM_BSS = 0;

static FsNode* fs_alloc_node(void) {
    // Simple reuse: find an unused slot with name[0]==0
    for (unsigned i = 0; i < g_node_used; ++i) {
        if (g_node_pool[i].name[0] == '\0' && g_node_pool[i].attr == 0) {
            memset(&g_node_pool[i], 0, sizeof(FsNode));
            return &g_node_pool[i];
        }
    }
    if (g_node_used >= FS_MAX_NODES) return NULL;
    FsNode* n = &g_node_pool[g_node_used++];
    memset(n, 0, sizeof(*n));
    return n;
}

// Helper: create directory/file nodes
static FsNode* fs_new_node(const char* name, BYTE attr) {
    FsNode* n = fs_alloc_node();
    if (!n) return NULL;
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1);
    n->attr = attr;
    n->parent = NULL;
    n->firstChild = NULL;
    n->nextSibling = NULL;
    n->data = NULL;
    n->size = 0;
    n->startCluster = 2; // default non-zero start cluster
    return n;
}

static void fs_add_child(FsNode* dir, FsNode* child) {
    if (!dir || !child) return;
    child->parent = dir;
    child->nextSibling = NULL;
    if (!dir->firstChild) {
        dir->firstChild = child;
    } else {
        FsNode* it = dir->firstChild;
        while (it->nextSibling) it = it->nextSibling;
        it->nextSibling = child;
    }
}

static FsNode* fs_find_child(FsNode* dir, const char* name) {
    if (!dir || !(dir->attr & AM_DIR)) return NULL;
    for (FsNode* it = dir->firstChild; it; it = it->nextSibling) {
        if (strcasecmp(it->name, name) == 0) return it;
    }
    return NULL;
}

static FsNode* fs_resolve(const char* path, int* is_dir_slash_end) {
    if (!g_root) return NULL;
    if (is_dir_slash_end) *is_dir_slash_end = 0;
    if (!path || !*path) return g_cwd ? g_cwd : g_root;
    FsNode* cur = (*path == '/') ? g_root : (g_cwd ? g_cwd : g_root);
    const char* p = path;
    if (*p == '/') p++;
    char seg[256];
    while (*p) {
        // extract segment until '/'
        size_t i = 0;
        while (*p && *p != '/' && i + 1 < sizeof(seg)) seg[i++] = *p++;
        seg[i] = '\0';
        if (i == 0) { // consecutive '/'
            if (*p == '/') p++;
            continue;
        }
        if (strcmp(seg, ".") == 0) {
            // stay
        } else if (strcmp(seg, "..") == 0) {
            if (cur->parent) cur = cur->parent;
        } else {
            FsNode* nxt = fs_find_child(cur, seg);
            if (!nxt) return NULL;
            cur = nxt;
        }
        if (*p == '/') {
            if (is_dir_slash_end && !*(p + 1)) *is_dir_slash_end = 1; // ends with '/'
            p++;
        }
    }
    return cur;
}

static FsNode* fs_ensure_dir(const char* path) {
    if (!g_root) return NULL;
    if (!path || !*path) return g_root;
    FsNode* cur = (*path == '/') ? g_root : (g_cwd ? g_cwd : g_root);
    const char* p = path;
    if (*p == '/') p++;
    char seg[256];
    while (*p) {
        size_t i = 0;
        while (*p && *p != '/' && i + 1 < sizeof(seg)) seg[i++] = *p++;
        seg[i] = '\0';
        if (i == 0) { if (*p == '/') p++; continue; }
        if (strcmp(seg, ".") == 0) {
        } else if (strcmp(seg, "..") == 0) {
            if (cur->parent) cur = cur->parent;
        } else {
            FsNode* nxt = fs_find_child(cur, seg);
            if (!nxt) {
                nxt = fs_new_node(seg, AM_DIR);
                fs_add_child(cur, nxt);
            }
            cur = nxt;
        }
        if (*p == '/') p++;
    }
    return cur;
}

static void fs_assign_clusters(FsNode* f) {
    if (!f || (f->attr & AM_DIR)) return;
    // assign contiguous clusters for the file content
    DWORD clusterSizeBytes = (DWORD)g_fs->csize * 512u;
    DWORD ncl = (f->size + clusterSizeBytes - 1) / clusterSizeBytes;
    if (ncl == 0) ncl = 1; // ensure non-zero chain for simplicity
    f->startCluster = g_nextCluster;
    g_nextCluster += ncl;
}

static void fs_populate_default(void) {
    // Root
    g_root = fs_new_node("", AM_DIR);
    g_cwd = g_root;

    // SYSTEM directory
    FsNode* sys = fs_new_node("SYSTEM", AM_DIR);
    fs_add_child(g_root, sys);

    // PATCH under SYSTEM
    FsNode* patch = fs_new_node("PATCH", AM_DIR);
    fs_add_child(sys, patch);

    // PLUG dir to mirror real layout
    FsNode* plug = fs_new_node("PLUG", AM_DIR);
    fs_add_child(sys, plug);

    // RECENT.TXT (empty initially)
    FsNode* recent = fs_new_node("RECENT.TXT", AM_ARC);
    recent->data = NULL;
    recent->size = 0;
    fs_assign_clusters(recent);
    fs_add_child(sys, recent);

    // Some demo files at root
    struct { const char* name; FSIZE_t size; } samples[] = {
        { "ALTT.gba", 8u * 1024u * 1024u },
        { "Metroid.gba", 16u * 1024u * 1024u },
        { "Sample.gb", 256u * 1024u },
        { "Readme.txt", 2048u },
    };
    for (unsigned i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
        FsNode* f = fs_new_node(samples[i].name, AM_ARC);
        if (!f) break;
        f->size = samples[i].size;
        // No backing data allocated to reduce memory usage; reads return zeros
        fs_assign_clusters(f);
        fs_add_child(g_root, f);
    }

    // GAMES dir with a couple files
    FsNode* games = fs_new_node("GAMES", AM_DIR);
    if (games) {
        fs_add_child(g_root, games);
        FsNode* f1 = fs_new_node("Pokemon.gba", AM_ARC);
        if (f1) { f1->size = 32u * 1024u * 1024u; fs_assign_clusters(f1); fs_add_child(games, f1); }
        FsNode* f2 = fs_new_node("MarioKart.gba", AM_ARC);
        if (f2) { f2->size = 16u * 1024u * 1024u; fs_assign_clusters(f2); fs_add_child(games, f2); }
    }
}

// -------------------- API Implementations --------------------

FRESULT f_open(FIL *fp, const TCHAR *path, BYTE mode) {
    if (!g_mounted || !fp) return FR_NOT_READY;
    memset(fp, 0, sizeof(*fp));
    // Resolve/possibly create target
    int endsWithSlash = 0;
    FsNode* node = fs_resolve(path, &endsWithSlash);
    if (!node) {
        // Create if requested
        if (mode & (FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW)) {
            // Create in parent
            // Split path to parent + name
            const char* lastSlash = strrchr(path, '/');
            char name[100];
            if (lastSlash) {
                strncpy(name, lastSlash + 1, sizeof(name) - 1); name[sizeof(name)-1] = '\0';
            } else {
                strncpy(name, path, sizeof(name) - 1); name[sizeof(name)-1] = '\0';
            }
            // Ensure parent directory exists
            char parentPath[512];
            if (lastSlash) {
                size_t plen = (size_t)(lastSlash - path);
                if (plen >= sizeof(parentPath)) plen = sizeof(parentPath)-1;
                memcpy(parentPath, path, plen); parentPath[plen] = '\0';
            } else {
                strcpy(parentPath, ".");
            }
            FsNode* parent = fs_ensure_dir(parentPath);
            if (!parent || !(parent->attr & AM_DIR)) return FR_NO_PATH;
            if (mode & FA_CREATE_NEW) {
                if (fs_find_child(parent, name)) return FR_EXIST;
            }
            node = fs_new_node(name, AM_ARC);
            if (!node) return FR_INT_ERR;
            node->size = 0;
            node->data = NULL;
            fs_assign_clusters(node);
            fs_add_child(parent, node);
        } else {
            return FR_NO_FILE;
        }
    }
    if (node->attr & AM_DIR) {
        // Open directory with f_open is invalid
        return FR_INVALID_OBJECT;
    }

    fp->obj.fs = g_fs;
    fp->obj.id = g_fs->id;
    fp->obj.attr = node->attr;
    fp->obj.sclust = node->startCluster;
    fp->obj.objsize = node->size;
    fp->flag = mode;
    fp->err = FR_OK;
    fp->fptr = 0;
    fp->clust = node->startCluster;
    fp->sect = 0;
    // Store back-pointer to node in dir_ptr (unused in exFAT)
    fp->dir_ptr = (BYTE*)node;
    return FR_OK;
}

FRESULT f_close(FIL *fp) {
    (void)fp;
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
    if (br) *br = 0;
    if (!g_mounted || !fp || !buff) return FR_INVALID_PARAMETER;
    FsNode* node = (FsNode*)fp->dir_ptr;
    if (!node) return FR_INVALID_OBJECT;
    if (fp->fptr >= node->size) { if (br) *br = 0; return FR_OK; }
    FSIZE_t remain = node->size - fp->fptr;
    UINT tocopy = (btr > remain) ? (UINT)remain : btr;
    if (node->data) {
        memcpy(buff, node->data + fp->fptr, tocopy);
    } else {
        // If no backing buffer, synthesize zeros
        memset(buff, 0, tocopy);
    }
    fp->fptr += tocopy;
    if (br) *br = tocopy;
    return FR_OK;
}

static int fs_file_ensure_capacity(FsNode* node, FSIZE_t needed) {
    if (!node) return 0;
    if (!node->data) {
        node->data = (BYTE*)malloc((size_t)needed);
        if (!node->data) return 0;
        if (node->size) memset(node->data, 0, (size_t)node->size);
        return 1;
    }
    if (needed <= node->size) return 1;
    BYTE* nd = (BYTE*)realloc(node->data, (size_t)needed);
    if (!nd) return 0;
    // zero the new region
    memset(nd + node->size, 0, (size_t)(needed - node->size));
    node->data = nd;
    return 1;
}

FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw) {
    if (bw) *bw = 0;
    if (!g_mounted || !fp || !buff) return FR_INVALID_PARAMETER;
    if (!(fp->flag & FA_WRITE)) return FR_DENIED;
    FsNode* node = (FsNode*)fp->dir_ptr;
    if (!node) return FR_INVALID_OBJECT;
    FSIZE_t endpos = fp->fptr + btw;
    if (!fs_file_ensure_capacity(node, endpos)) return FR_INT_ERR;
    memcpy(node->data + fp->fptr, buff, btw);
    fp->fptr = endpos;
    if (endpos > node->size) {
        node->size = endpos;
        fp->obj.objsize = node->size;
        // adjust cluster span if grew
        // simple approach: do nothing unless needed for cluster queries
    }
    if (bw) *bw = btw;
    return FR_OK;
}

FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
    if (!g_mounted || !fp) return FR_INVALID_PARAMETER;
    FsNode* node = (FsNode*)fp->dir_ptr;
    if (!node) return FR_INVALID_OBJECT;
    if (ofs > node->size && !(fp->flag & FA_WRITE)) {
        // In read-only mode, clip to file size
        ofs = node->size;
    }
    fp->fptr = ofs;
    return FR_OK;
}

// Global directory iteration state for emulator
static FsNode* g_iter_dir = NULL;
static FsNode* g_iter_next = NULL;

FRESULT f_opendir(DIR *dp, const TCHAR *path) {
    if (!g_mounted || !dp) return FR_INVALID_PARAMETER;
    FsNode* node;
    if (!path || !*path) node = g_cwd; else node = fs_resolve(path, NULL);
    if (!node) return FR_NO_PATH;
    if (!(node->attr & AM_DIR)) return FR_NO_PATH;
    memset(dp, 0, sizeof(*dp));
    g_iter_dir = node;
    g_iter_next = node->firstChild;
    dp->dir = (BYTE*)0x1; // non-null sentinel to indicate open
    return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno) {
    if (!g_mounted || !dp) return FR_INVALID_PARAMETER;
    if (!dp->dir) { if (fno) fno->fname[0] = 0; return FR_OK; }
    if (!fno) { g_iter_next = g_iter_dir ? g_iter_dir->firstChild : NULL; return FR_OK; }
    if (!g_iter_next) { fno->fname[0] = 0; return FR_OK; }
    FsNode* n = g_iter_next;
    g_iter_next = n->nextSibling;
    // Fill FILINFO
    strncpy(fno->fname, n->name, sizeof(fno->fname) - 1);
    fno->fname[sizeof(fno->fname) - 1] = '\0';
    fno->fattrib = n->attr;
    fno->fsize = (n->attr & AM_DIR) ? 0 : n->size;
    return FR_OK;
}

FRESULT f_stat(const TCHAR *path, FILINFO *fno) {
    if (!g_mounted || !fno) return FR_INVALID_PARAMETER;
    FsNode* n = fs_resolve(path, NULL);
    if (!n) return FR_NO_FILE;
    strncpy(fno->fname, n->name, sizeof(fno->fname) - 1);
    fno->fname[sizeof(fno->fname) - 1] = '\0';
    fno->fattrib = n->attr;
    fno->fsize = (n->attr & AM_DIR) ? 0 : n->size;
    return FR_OK;
}

TCHAR *f_gets(TCHAR *buff, int len, FIL *fp) {
    if (!g_mounted || !buff || len <= 0 || !fp) return NULL;
    FsNode* node = (FsNode*)fp->dir_ptr;
    if (!node) return NULL;
    if (fp->fptr >= node->size) return NULL;
    int i = 0;
    while (i < len - 1 && fp->fptr < node->size) {
        char c = node->data ? (char)node->data[fp->fptr] : '\0';
        fp->fptr++;
        buff[i++] = c;
        if (c == '\n') break;
    }
    buff[i] = '\0';
    return buff;
}

FRESULT f_unlink(const TCHAR *path) {
    FsNode* n = fs_resolve(path, NULL);
    if (!n) return FR_NO_FILE;
    if (n == g_root) return FR_DENIED;
    if ((n->attr & AM_DIR) && n->firstChild) return FR_DENIED; // non-empty
    // detach from parent list
    FsNode* parent = n->parent;
    if (!parent) return FR_INT_ERR;
    FsNode* prev = NULL; FsNode* it = parent->firstChild;
    while (it) { if (it == n) break; prev = it; it = it->nextSibling; }
    if (!it) return FR_INT_ERR;
    if (prev) prev->nextSibling = it->nextSibling; else parent->firstChild = it->nextSibling;
    if (n->data) { free(n->data); n->data = NULL; }
    // mark node as free for reuse
    n->name[0] = '\0';
    n->attr = 0;
    n->parent = NULL;
    n->firstChild = NULL;
    n->nextSibling = NULL;
    n->size = 0;
    n->startCluster = 0;
    return FR_OK;
}

FRESULT f_rename(const TCHAR *path_old, const TCHAR *path_new) {
    FsNode* n = fs_resolve(path_old, NULL);
    if (!n) return FR_NO_FILE;
    // Determine new parent and name
    const char* lastSlash = strrchr(path_new, '/');
    char name[100];
    if (lastSlash) { strncpy(name, lastSlash + 1, sizeof(name) - 1); name[sizeof(name)-1]='\0'; }
    else { strncpy(name, path_new, sizeof(name) - 1); name[sizeof(name)-1]='\0'; }
    FsNode* newParent;
    if (lastSlash) {
        char parentPath[512];
        size_t plen = (size_t)(lastSlash - path_new); if (plen >= sizeof(parentPath)) plen = sizeof(parentPath)-1;
        memcpy(parentPath, path_new, plen); parentPath[plen] = '\0';
        newParent = fs_ensure_dir(parentPath);
    } else {
        newParent = g_cwd;
    }
    if (!newParent || !(newParent->attr & AM_DIR)) return FR_NO_PATH;
    // Detach from current parent
    FsNode* oldParent = n->parent;
    if (oldParent) {
        FsNode* prev = NULL; FsNode* it = oldParent->firstChild;
        while (it) { if (it == n) break; prev = it; it = it->nextSibling; }
        if (it) { if (prev) prev->nextSibling = it->nextSibling; else oldParent->firstChild = it->nextSibling; }
    }
    // Update name and attach to new parent
    strncpy(n->name, name, sizeof(n->name) - 1); n->name[sizeof(n->name)-1] = '\0';
    n->nextSibling = NULL; n->parent = NULL;
    fs_add_child(newParent, n);
    return FR_OK;
}

FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) {
    (void)path; (void)opt;
    if (!fs) { // unmount
        g_mounted = 0; g_fs = NULL; return FR_OK;
    }
    memset(fs, 0, sizeof(*fs));
    fs->fs_type = FS_FAT16;   // simplest path
    fs->csize = 4;            // 4 sectors per cluster
    fs->id = 0x1234;
    fs->n_fatent = 0x10000;   // arbitrary
    fs->database = 2048;      // base sector for data area
    g_fs = fs;
    g_mounted = 1;

    // Initialize ram-disk layout
    g_root = NULL; g_cwd = NULL; g_nextCluster = 2; g_node_used = 0; g_iter_dir = NULL; g_iter_next = NULL;
    fs_populate_default();
    return FR_OK;
}

FRESULT f_mkdir (const TCHAR* path) {
    if (!g_mounted) return FR_NOT_READY;
    FsNode* n = fs_resolve(path, NULL);
    if (n) return FR_EXIST;
    FsNode* dir = fs_ensure_dir(path);
    return dir ? FR_OK : FR_INT_ERR;
}

FRESULT f_getcwd (TCHAR* buff, UINT len) {
    if (!g_mounted || !buff || len == 0) return FR_INVALID_PARAMETER;
    // build path by walking parents
    char tmp[512]; tmp[0] = '\0';
    FsNode* it = g_cwd ? g_cwd : g_root;
    if (it == g_root) {
        strncpy(buff, "/", len-1); buff[len-1] = '\0';
        return FR_OK;
    }
    // collect names backwards
    char* cursor = tmp + sizeof(tmp);
    *--cursor = '\0';
    while (it && it != g_root) {
        size_t nlen = strlen(it->name);
        if ((size_t)(cursor - tmp) < nlen + 2) break; // overflow guard
        for (size_t i = 0; i < nlen; ++i) *(--cursor) = it->name[nlen - 1 - i];
        *(--cursor) = '/';
        it = it->parent;
    }
    if (it == g_root && (cursor == tmp || *cursor != '/')) {
        *(--cursor) = '/';
    }
    strncpy(buff, cursor, len - 1); buff[len - 1] = '\0';
    return FR_OK;
}

FRESULT f_chdir (const TCHAR* path) {
    if (!g_mounted) return FR_NOT_READY;
    FsNode* n = fs_resolve(path, NULL);
    if (!n || !(n->attr & AM_DIR)) return FR_NO_PATH;
    g_cwd = n;
    return FR_OK;
}

FRESULT f_closedir (DIR *dp) {
    if (!dp) return FR_INVALID_PARAMETER;
    dp->dir = NULL;
    g_iter_dir = NULL;
    g_iter_next = NULL;
    return FR_OK;
}

int f_printf (FIL* fp, const TCHAR* fmt, ...) {
    if (!g_mounted || !fp || !(fp->flag & FA_WRITE)) return 0;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return 0;
    UINT bw = 0; f_write(fp, buf, (UINT)n, &bw);
    return (int)bw;
}

//------------------------------------------------------------------------------
// Provide basic cluster chain helpers expected by kernel code
DWORD Get_NextCluster(FFOBJID* obj, DWORD clst) {
    if (!obj || !g_fs) return 0xFFFF; // end
    DWORD clusterSizeBytes = (DWORD)g_fs->csize * 512u;
    if (clusterSizeBytes == 0) clusterSizeBytes = 2048;
    DWORD ncl = (obj->objsize + clusterSizeBytes - 1) / clusterSizeBytes;
    if (ncl == 0) ncl = 1; // ensure at least one
    DWORD base = obj->sclust ? obj->sclust : 2;
    if (clst < base) return base; // start
    DWORD idx = clst - base;
    if (idx + 1 >= ncl) return 0xFFFF; // FAT16 end marker used by kernel logic
    return base + idx + 1;
}

//------------------------------------------------------------------------------
DWORD ClustToSect(FATFS* fs, DWORD clst) {
    if (!fs || clst < 2) return 0;
    // Simple linear mapping: data area base + (clst - 2) * csize
    DWORD base = (DWORD)fs->database;
    DWORD csize = (DWORD)fs->csize;
    if (csize == 0) csize = 4;
    return base + (clst - 2) * csize;
}
