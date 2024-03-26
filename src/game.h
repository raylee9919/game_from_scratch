#ifndef GAME_H
#define GAME_H
 /* ―――――――――――――――――――――――――――――――――――◆――――――――――――――――――――――――――――――――――――
    $File: $
    $Date: $
    $Revision: $
    $Creator: Sung Woo Lee $
    $Notice: (C) Copyright 2024 by Sung Woo Lee. All Rights Reserved. $
    ―――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――――― */


#define Max(a, b) ( (a > b) ? a : b )
#define Min(a, b) ( (a < b) ? a : b )
#define ArrayCount(array) ( sizeof(array) / sizeof(array[0]) )
#define ZeroStruct(Struct) ClearToZero(sizeof(Struct), Struct)
internal void
ClearToZero(size_t size, void *data) {
    u8 *at = (u8 *)data;
    while (size--) {
        *at++ = 0;
    }
}

#include "math.h"
#include "platform.h"
#include "debug.h"
#include "asset.h"
#include "random.h"

//
// Memory ---------------------------------------------------------------------
//
struct MemoryArena {
    size_t size;
    size_t used;
    u8 *base;

    u32 tempCount;
};

// 
// Temporary Memory -----------------------------------------------------------
//
struct TemporaryMemory {
    MemoryArena *memoryArena;
    // Simply stores what was the used amount before.
    // Restoring without any precedence issues problem in multi-threading.
    size_t used;
};
struct WorkMemoryArena {
    b32 isUsed;
    MemoryArena memoryArena;
    TemporaryMemory flush;
};


struct Position {
    s32 chunkX;
    s32 chunkY;
    s32 chunkZ;
    vec3 offset;
};

struct Camera {
    Position pos;
};

enum EntityType {
    EntityType_Player,
    EntityType_Familiar,
    EntityType_Tree,
    EntityType_Golem
};

enum EntityFlag {
    EntityFlag_Collides = 1
};

struct Entity {
    EntityType type;
    vec3 dim;
    Position pos;
    vec3 velocity;
    vec3 accel;
    r32 u;
    Bitmap bmp;
    u32 flags;

    u32 face;

    Entity *next;
};

struct EntityList {
    Entity *head;
};

struct Chunk { 
    s32 chunkX;
    s32 chunkY;
    s32 chunkZ;
    EntityList entities;
    Chunk *next;
};

struct ChunkList {
    Chunk *head;
    u32 count;
};

struct ChunkHashmap { 
    ChunkList chunks[4096];
};

struct World {
    r32 ppm;

    ChunkHashmap chunkHashmap;
    vec3 chunkDim;

    Entity entities[1024];
    u32 entityCount;
};

struct Particle {
    vec3 P;
    vec3 V;
    vec3 A;
    r32 alpha;
    r32 dAlpha;
};

struct ParticleCel {
    r32 density;
    vec3 VSum;
    vec3 V;
};

struct Kerning {
    u32 first;
    u32 second;
    s32 value;
    Kerning *prev;
    Kerning *next;
};
struct Kerning_List {
    Kerning *first;
    Kerning *last;
    u32 count;
};
struct Kerning_Hashmap {
    Kerning_List entries[64];
};
inline u32
kerning_hash(Kerning_Hashmap *hashmap, u32 first, u32 second) {
    // todo: better hash function.
    u32 result = (first * 12 + second * 33) % ArrayCount(hashmap->entries);
    return result;
}
internal void
push_kerning(Kerning_Hashmap *hashmap, Kerning *kern, u32 entry_idx) {
    Assert(entry_idx < ArrayCount(hashmap->entries));
    Kerning_List *list = hashmap->entries + entry_idx;
    if (list->first) {
        list->last->next = kern;
        kern->prev = list->last;
        kern->next = 0;
        list->last = kern;
        ++list->count;
    } else {
        list->first = kern;
        list->last = kern;
        kern->prev = 0;
        kern->next = 0;
        ++list->count;
    }
}
internal s32
get_kerning(Kerning_Hashmap *hashmap, u32 first, u32 second) {
    s32 result = 0;
    u32 entry_idx = kerning_hash(hashmap, first, second);
    Assert(entry_idx < ArrayCount(hashmap->entries));
    for (Kerning *at = hashmap->entries[entry_idx].first;
            at;
            at = at->next) {
        if (at->first == first && at->second == second) {
            result = at->value;
        }
    }
    return result;
}

struct Game_Assets {
    Asset_State bitmapStates[GAI_Count];
    Bitmap *bitmaps[GAI_Count];

    Bitmap *playerBmp[2];
    Bitmap *familiarBmp[2];

    Kerning_Hashmap kern_hashmap;
    Asset_Glyph *glyphs[256];

    DEBUG_PLATFORM_READ_FILE_ *debug_platform_read_file;
};


struct GameState {
    b32 isInit;
    r32 time;

    RandomSeries particleRandomSeries;

    World *world;
    MemoryArena worldArena;

    MemoryArena renderArena;

    Bitmap drawBuffer;

    Camera camera;

    Entity *player;

    Particle particles[512];
    s32 particleNextIdx;
#define GRID_X 30
#define GRID_Y 20
    ParticleCel particleGrid[GRID_Y][GRID_X];
};

struct TransientState {
    b32 isInit;
    MemoryArena transientArena;
    PlatformWorkQueue *highPriorityQueue;
    PlatformWorkQueue *lowPriorityQueue;

    WorkMemoryArena workArena[4];

    MemoryArena assetArena;
    Game_Assets gameAssets;
};

internal void *
PushSize_(MemoryArena *arena, size_t size) {
    Assert((arena->used + size) <= arena->size);
    void *result = arena->base + arena->used;
    arena->used += size;

    return result;
}
#define PushSize(arena, size) PushSize_(arena, size)
#define PushStruct(arena, type) \
    (type *)PushSize_(arena, sizeof(type))
#define PushArray(arena, type, count) \
    (type *)PushSize_(arena, count * sizeof(type))

internal void
InitArena(MemoryArena *arena, size_t size, u8 *base) {
    arena->size = size;
    arena->base = base;
    arena->used = 0;
}

internal void
InitSubArena(MemoryArena *subArena, MemoryArena *motherArena, size_t size) {
    Assert(motherArena->size >= motherArena->used + size);
    InitArena(subArena, size, motherArena->base + motherArena->used);
    motherArena->used += size;
}

inline TemporaryMemory
BeginTemporaryMemory(MemoryArena *memoryArena) {
    memoryArena->tempCount++;

    TemporaryMemory result = {};
    result.memoryArena = memoryArena;
    result.used = memoryArena->used;

    return result;
}
inline void
EndTemporaryMemory(TemporaryMemory *temporaryMemory) {
    MemoryArena *arena = temporaryMemory->memoryArena;
    Assert(arena->used >= temporaryMemory->used);
    arena->used = temporaryMemory->used;
    Assert(arena->tempCount > 0);
    arena->tempCount--;
}

inline WorkMemoryArena *
BeginWorkMemory(TransientState *transState) {
    WorkMemoryArena *result = 0;
    for (s32 idx = 0;
            idx < ArrayCount(transState->workArena);
            ++idx) {
        WorkMemoryArena *workSlot = transState->workArena + idx;
        if (!workSlot->isUsed) {
            result = workSlot;
            result->isUsed = true;
            result->flush = BeginTemporaryMemory(&result->memoryArena);
            break;
        }
    }
    return result;
}
inline void
EndWorkMemory(WorkMemoryArena *workMemoryArena) {
    EndTemporaryMemory(&workMemoryArena->flush);
    __WRITE_BARRIER__
    workMemoryArena->isUsed = false;
}


#define GAME_MAIN(name) void name(GameMemory *gameMemory, GameState *gameState, \
        GameInput *gameInput, GameScreenBuffer *gameScreenBuffer)
typedef GAME_MAIN(GameMain_);


#endif
