// =====================================================================================================================
//
//      _    __________  __      __           __
//     | |  / / ____/ / / /___  / /___  _____/ /__
//     | | / / /   / / / / __ \/ / __ \/ ___/ //_/        GRAND THEFT AUTO: VICE CITY
//     | |/ / /___/ /_/ / / / / / /_/ / /__/ ,<           Uncapped frame rate.
//     |___/\____/\____/_/ /_/_/\____/\___/_/|_|
//
// =====================================================================================================================
//
//   FILE      fixed.cpp
//   MODULE    fixed
//   ROLE      The fixed 30 Hz world and its interpolated presentation
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   The game simulates at 30 Hz exactly as it always did: every tick is a complete, unmodified CGame::Process with the
//   original time step. The frame rate is free. An accumulator schedules the ticks from real time, and every drawn
//   frame shows the world between its last two ticks.
//
//   What is drawn between ticks:
//
//   - entity roots: peds, vehicles and objects, blended from snapshots taken after every tick;
//   - skinned bones of peds and cutscene characters, vehicle parts and wheels (twist split from swing, so a wheel never
//     rolls backwards), cutscene props held on a bone;
//   - particles, the follow camera (relative to its target) and the mouse look, which turns at the full frame rate;
//   - world effects (fixed_fx.cpp) and the water waves, through a presentation clock.
//
//   Everything shifted for a frame is put back before the next tick, so the simulation only ever sees its own state.
//   Install is all or nothing: every core byte check passes before the first patch.
//
// ---------------------------------------------------------------------------------------------------------------------
//
//   LICENCE   MIT. Copyright (c) 2026 İlkay Dinç. The full text is in LICENSE.
//             SPDX-License-Identifier: MIT
//   NOTICE    Unofficial. Not affiliated with or endorsed by Rockstar Games or Take-Two
//             Interactive. Requires an original copy of the game.
//
// =====================================================================================================================

#include "fixed.h"
#include "fixed_math.h"
#include "fixed_fx.h"
#include "core.h"
#include "game.h"
#include "timer.h"
#include "fix.h"
#include <cstring>
#include <new>

namespace fixed {
namespace {
using namespace fixedmath;

// ---------------------------------------------------------------------------------------------------------------------
//  GAME FUNCTIONS
// ---------------------------------------------------------------------------------------------------------------------

constexpr uintptr_t kGameProcess = 0x4A42D0, kConstruct = 0x4CA120, kPreRender = 0x4CA0B0;
constexpr uintptr_t kCamera = 0x7E3690, kSceneCamera = 0x80F0C4;
constexpr uintptr_t kRwMatrixUpdate = 0x642760, kRwFrameUpdate = 0x643CB0;
constexpr uintptr_t kRwFrameGetLtm = 0x643D30, kRwCameraSync = 0x649BB0;
constexpr uintptr_t kSkinHierarchy = 0x57F080, kHierarchyUpdate = 0x645370;
constexpr uintptr_t kNumLights = 0x9741B8;  // CPointLights::NumLights
auto GameProcess = (void(__cdecl*)())kGameProcess;
auto Construct = (void(__cdecl*)())kConstruct;
auto PreRender = (void(__cdecl*)())kPreRender;
auto DerivedCamera = (void(__fastcall*)(void*, void*))0x46C296;
auto SetFov = (void(__cdecl*)(float))0x54A1D0;
auto RwMatrixUpdate = (void*(__cdecl*)(void*))kRwMatrixUpdate;
auto RwFrameUpdate = (void*(__cdecl*)(void*))kRwFrameUpdate;
auto RwFrameGetLtm = (void*(__cdecl*)(void*))kRwFrameGetLtm;
auto RwCameraSync = (void*(__cdecl*)(void*))kRwCameraSync;
auto SkinHierarchy = (void*(__cdecl*)(void*))kSkinHierarchy;
auto HierarchyUpdate = (void*(__cdecl*)(void*))kHierarchyUpdate;
void(__cdecl* OriginalIdle)(void*);

// ---------------------------------------------------------------------------------------------------------------------
//  ROOT SNAPSHOTS
// ---------------------------------------------------------------------------------------------------------------------

// A field of a game object, by byte offset.
template <class T> T& Field(void* p, size_t offset) { return *(T*)((uint8_t*)p + offset); }

struct Pool {
    uint8_t* entries;
    uint8_t* flags;
    int size, next;
};
static_assert(sizeof(Pool) == 16);

// One entity root: its last two tick poses, the drawn pose and what a drawn frame changed.
struct Slot {
    uint8_t* entity = nullptr;
    uint8_t id = 0;
    Matrix previous{}, current{};
    bool valid = false, applied = false, continuous = false, reposed = false;
    Matrix entityBackup{}, rwBackup{}, draw{}, drawVisual{};
    void* rwObject = nullptr;
    void* frame = nullptr;
};

Slot s_peds[140], s_vehicles[110], s_objects[460];

// ---------------------------------------------------------------------------------------------------------------------
//  SKINS
// ---------------------------------------------------------------------------------------------------------------------

constexpr int kMaxBones = 64;

struct SkinPose {
    void* clump = nullptr;
    void* hierarchy = nullptr;
    Matrix* matrices = nullptr;
    int count = 0;
    bool local = false, valid = false, applied = false, tree = false;
    int8_t parent[kMaxBones]{};  // bone hierarchy (-1 = the root)
    uint32_t adjustTick = 0;     // FrameCounter of the PreRender the adjustments came from
    Matrix built{};              // root PreRender built the palette at
    Matrix previous[kMaxBones]{}, current[kMaxBones]{}, backup[kMaxBones]{};
    Matrix adjust[kMaxBones]{};  // PreRender's change to each bone, relative to the tick pose
};

SkinPose s_skins[140];
SkinPose* s_objectSkins[460];  // cutscene actors are objects; allocated when a skinned one appears

// CCutsceneObject (vtable 0x690594): a prop attached to a bone of another object takes that bone's matrix
// (+0x198 = &palette[bone], +0x19C = the object) in PreRender.
constexpr uintptr_t kCutsceneVtable = 0x690594;
bool s_treeLayout = false, s_cutsceneLayout = false;

// ---------------------------------------------------------------------------------------------------------------------
//  VEHICLE PARTS AND CUTSCENE PROPS
// ---------------------------------------------------------------------------------------------------------------------

// Vehicle parts: RW child frames (doors, bonnet, boot, wheels, suspension, forks, rotors) and the RW
// root's pose relative to the entity (bike lean). Their poses are set from tick state by the world tick
// or by PreRender, so they are blended between the last two ticks like the root.
constexpr int kMaxParts = 48;

struct PartPose {
    void* clump = nullptr;
    uint8_t* entity = nullptr;
    uint8_t id = 0;
    void* frames[kMaxParts]{};
    int count = 0;
    uint32_t tick = 0;
    bool valid = false, applied = false;
    Matrix previous[kMaxParts]{}, current[kMaxParts]{};
    Spin spin[kMaxParts]{};
    // wheels: offset of the game's wheel angle that drives the roll (0 = not a wheel) and its sign
    uint16_t wheel[kMaxParts]{};
    int8_t side[kMaxParts]{};
    float anglePrevious[kMaxParts]{}, angleCurrent[kMaxParts]{};
    uint32_t animated[kMaxParts]{};  // 1 + last tick the part also moved between ticks
    Matrix rootPrevious{}, rootCurrent{}, rootPreRender{};
    Spin rootSpin{};
    bool rootApplied = false;
};

PartPose s_parts[110];

// Cutscene props (phone, cup, gun) are CCutsceneObjects with their own animation, which the world tick
// writes into the clump's frames like a vehicle's parts. Allocated when one appears.
PartPose* s_objectParts[460];

// ---------------------------------------------------------------------------------------------------------------------
//  WHEELS
// ---------------------------------------------------------------------------------------------------------------------

// Wheel frames and the per-tick wheel angles PreRender poses them with: Rz(steer) * Rx(side * angle).
struct WheelNode {
    uint16_t node, angle;
    int8_t side;
};

// CAutomobile (vtable 0x699D90): wheel frames RF/RM/RB/LF/LM/LB at +0x398..+0x3AC, angles FL/RL/FR/RR
// at +0x4D0..+0x4DC; left wheels use the negated angle (SetRotate(-angle, 0, pi + steer)).
constexpr uintptr_t kAutomobileVtable = 0x699D90, kBikeVtable = 0x6D6B04;
constexpr WheelNode kCarWheels[] = {{0x398, 0x4D8, 1},  {0x39C, 0x4DC, 1},  {0x3A0, 0x4DC, 1},
                                    {0x3A4, 0x4D0, -1}, {0x3A8, 0x4D4, -1}, {0x3AC, 0x4D4, -1}};

// CBike (vtable 0x6D6B04): front/rear wheel frames +0x2B0/+0x2B4, angles +0x410/+0x414.
constexpr WheelNode kBikeWheels[] = {{0x2B0, 0x410, 1}, {0x2B4, 0x414, 1}};
bool s_wheelLayout = false;

// Wheel contact points from the last suspension test (CColPoint, 0x28 bytes, point +0): CAutomobile +0x3E4,
// CBike +0x330. PreRender lays skidmarks and wheel particles at them, so they move with the drawn root.
struct ContactPoints {
    bool applied = false;
    Vec backup[4]{}, drawn[4]{};
};

ContactPoints s_contacts[110];
bool s_contactLayout = false;

// CBike lean matrix (CMatrix +0x2C4, valid flag +0x2C0): the entity matrix tilted by the lean angle, built once
// per tick and cached. PreRender aims the head/tail light quads and the headlight's ground light with it, so a
// drawn frame rebuilds it from the drawn root and the tick copy is put back after drawing.
struct LeanMatrix {
    bool applied = false, valid = false;
    Matrix backup{};
};

LeanMatrix s_lean[110];
bool s_leanLayout = false;

// ---------------------------------------------------------------------------------------------------------------------
//  POOLS
// ---------------------------------------------------------------------------------------------------------------------

// A game pool (peds, vehicles, objects) and the slots that track its entities.
struct PoolTrack {
    uintptr_t global;
    size_t stride;
    int capacity;
    Slot* slots;
    Pool* identity = nullptr;
    uint8_t* entries = nullptr;
    uint8_t* flags = nullptr;
};

PoolTrack s_pools[] = {
    {0x97E2B4, 0x6D8, 140, s_peds}, {0xA0EDEC, 0x5DC, 110, s_vehicles}, {0x94CBE8, 0x1A0, 460, s_objects}};

// ---------------------------------------------------------------------------------------------------------------------
//  CAMERA
// ---------------------------------------------------------------------------------------------------------------------

// The scene camera's last two tick poses; a follow camera also keeps its offset from the target.
struct CameraHistory {
    void* camera = nullptr;
    void* frame = nullptr;
    Matrix previous{}, current{};
    void* targetEntity = nullptr;
    Slot* target = nullptr;
    uint8_t targetId = 0, active = 255, transition = 0;
    int16_t mode = -1;
    Vec previousOffset{}, currentOffset{};
    bool following = false;
    float prevFov = 0, fov = 0;
    bool valid = false;
} s_camera;

// ---------------------------------------------------------------------------------------------------------------------
//  PARTICLES
// ---------------------------------------------------------------------------------------------------------------------

// Particles (gParticleArray 0x76D4EC, 750 x 0x58: position +0, velocity +0xC, next +0x54; free list 0x97F034)
// move only in the world tick. Each tick's positions are kept so the drawn frame can blend the last two.
constexpr uintptr_t kParticleArray = 0x76D4EC, kParticleFree = 0x97F034;
constexpr int kMaxParticles = 750, kParticleStride = 0x58;
struct ParticleTrack {
    uint32_t serial = 0;
    Vec previous{}, current{}, backup{};
    bool continuous = false, shifted = false;
};

ParticleTrack s_particles[kMaxParticles];
uint32_t s_particleSerial[kMaxParticles];  // set when AddParticle hands out the slot
uint32_t s_particleSerials = 0;
bool s_particleLayout = false, s_particlesShifted = false;

// ---------------------------------------------------------------------------------------------------------------------
//  WATER WAVES AND MOVE SPEED
// ---------------------------------------------------------------------------------------------------------------------

// Water waves read the game clock in the renderer; drawn frames get the clock at their interpolated time.
uint32_t s_waveMs = 0, s_tickMsPrevious = 0, s_tickMsCurrent = 0;

constexpr uintptr_t kMoveSpeed = 0x70;  // CPhysical::m_vecMoveSpeed, per 1/50 s
bool s_moveSpeedLayout = true;

// ---------------------------------------------------------------------------------------------------------------------
//  MOUSE LOOK
// ---------------------------------------------------------------------------------------------------------------------

// The world tick reads the mouse once (CPad::UpdateMouse -> _InputGetMouseState 0x5FEEF0, which
// returns the motion since the previous read). Drawn frames read it too and keep the sum for the next tick, so
// the simulation sees the same input; the camera is drawn turned by the mouse motion not yet simulated.
struct MouseState {  // DIMOUSESTATE2
    int32_t x, y, z;
    uint8_t buttons[8];
};
static_assert(sizeof(MouseState) == 20);

auto InputGetMouseState = (int32_t(__cdecl*)(MouseState*))0x5FEEF0;
auto IsForegroundApp = (bool(__cdecl*)())0x600D00;

// Mouse motion read by drawn frames since the last tick, and the clicks still to report to it.
struct MouseInput {
    int32_t x = 0, y = 0, z = 0;
    uint8_t latch[8]{};
    MouseState last{};
} s_mouse;

bool s_mouseLayout = false, s_inTick = false;

// Per tick: the active CCam's yaw (Beta +0xBC), pitch (Alpha +0xAC) and FOV (+0xB4), and the part of the
// latest tick's turn that was mouse input (checked against the game's own formula).
struct LookHistory {
    bool valid = false;
    void* cam = nullptr;
    void* target = nullptr;
    int16_t mode = -1;
    float alpha = 0, beta = 0, fov = 0;
    float yaw = 0, pitch = 0;
    bool yawBlocked = false;              // the game overrode the mouse yaw (transition beta, directly behind, train)
    bool lock = false, lockBoth = false;  // the target ped's heading follows the camera (mode 4)
} s_look;

Slot* s_lookPed = nullptr;
float s_lookPedYaw = 0;
uint32_t s_mouseTurns = 0;

// The camera state a drawn frame changes, restored before the next tick.
struct CameraBackup {
    void* camera = nullptr;
    void* frame = nullptr;
    Matrix world{}, inverse{}, view{}, rw{}, rwView{};
    Vec gamePos{};
    float front[2]{}, orientation = 0, fov = 0, planes[12]{};
    bool applied = false;
} s_cameraBackup;

// ---------------------------------------------------------------------------------------------------------------------
//  SCHEDULER STATE AND STATS
// ---------------------------------------------------------------------------------------------------------------------

bool s_started = false, s_drawing = false;
Accumulator s_acc;
double s_delta = 0;
uint32_t s_frameTicks = 0;
int32_t s_tickLights = 0;
uint32_t s_ticks = 0, s_frames = 0, s_skippedParticles = 0, s_blends = 0, s_skinBlends = 0, s_followBlends = 0,
         s_partBlends = 0, s_wheelBlends = 0;
uint32_t s_particleBlends = 0, s_attachments = 0, s_propBlends = 0;
double s_statStart = 0;
uint32_t s_clockStart = 0;

// ---------------------------------------------------------------------------------------------------------------------
//  SKIN CAPTURE AND BLENDING
// ---------------------------------------------------------------------------------------------------------------------

// Parent of each bone from the HAnim node flags (node info +0x10, 16 bytes each, flags at +8): 2 pushes
// the current parent, 1 pops it for the next bone; otherwise the next bone is this bone's child.
// The last leaf pops the (empty) root level.
bool BuildTree(void* hierarchy, int count, int8_t* parent) {
    uint8_t* info = s_treeLayout ? Field<uint8_t*>(hierarchy, 0x10) : nullptr;
    if (!info) return false;
    int stack[kMaxBones];
    int depth = 0, current = -1;
    for (int n = 0; n < count; n++) {
        parent[n] = (int8_t)current;
        uint32_t flags = Field<uint32_t>(info, n * 16 + 8);
        if (flags & 2) {
            if (depth == kMaxBones) return false;
            stack[depth++] = current;
        }
        if (flags & 1) {
            if (depth) current = stack[--depth];
            else if (n < count - 1) return false;
        } else current = n;
    }
    return true;
}

bool Skinned(uint8_t* entity) {
    void* clump = Field<void*>(entity, 0x4C);
    return clump && Field<uint8_t>(clump, 0) == 2 && SkinHierarchy(clump);
}

void CaptureSkin(Slot& root, SkinPose& s, bool continuous) {
    void* clump = Field<void*>(root.entity, 0x4C);
    // Only skinned clumps (peds, cutscene actors) are handled. Other model/animation systems keep their own path.
    void* hierarchy = clump && Field<uint8_t>(clump, 0) == 2 ? SkinHierarchy(clump) : nullptr;
    int count = hierarchy ? Field<int>(hierarchy, 4) : 0;
    Matrix* matrices = hierarchy ? Field<Matrix*>(hierarchy, 8) : nullptr;
    if (!matrices || count <= 0 || count > kMaxBones) {
        s.valid = false;
        return;
    }
    bool local = (Field<uint32_t>(hierarchy, 0) & 0x4000) != 0;
    continuous = continuous && s.valid && s.clump == clump && s.hierarchy == hierarchy && s.matrices == matrices &&
                 s.count == count && s.local == local;
    // Animation time advances inside the world tick; build its canonical skin palette now
    // instead of sampling a stale palette left by the preceding render's PreRender.
    HierarchyUpdate(hierarchy);
    for (int n = 0; n < count; n++)
        if (!Valid(matrices[n])) {
            s.valid = false;
            return;
        }
    for (int n = 0; n < count; n++) {
        Matrix pose = local ? matrices[n] : MoveVisual(root.current, Identity(), matrices[n]);
        s.previous[n] = continuous ? s.current[n] : pose;
        s.current[n] = pose;
    }
    if (!continuous) {
        s.tree = BuildTree(hierarchy, count, s.parent);
        s.adjustTick = game::FrameCounter - 1;
    }
    s.clump = clump;
    s.hierarchy = hierarchy;
    s.matrices = matrices;
    s.count = count;
    s.local = local;
    s.valid = true;
}

// built: the root PreRender built the palette at. drawRoot: the root the blended palette is drawn at.
void ApplySkin(Slot& root, SkinPose& s, const Matrix& built, const Matrix& drawRoot) {
    if (!s.valid || s.applied || s.clump != root.rwObject || SkinHierarchy(s.clump) != s.hierarchy ||
        Field<Matrix*>(s.hierarchy, 8) != s.matrices || Field<int>(s.hierarchy, 4) != s.count)
        return;
    memcpy(s.backup, s.matrices, s.count * sizeof(Matrix));
    // PreRender's changes to the palette (hidden limbs, the shirt flapping in the wind, Tommy's size) are
    // taken from the first drawn frame of each tick and held until the next, as at 30 fps; the wind is
    // re-randomised on every PreRender.
    bool fresh = s.adjustTick != game::FrameCounter;
    float alpha = s_acc.Alpha();
    Matrix blended[kMaxBones];
    for (int n = 0; n < s.count; n++) {
        // Bones blend relative to their parent, so a fast swing follows the joint's arc instead of
        // cutting across it (which shortens the arm mid-tick).
        int p = s.tree ? s.parent[n] : -1;
        blended[n] = p < 0 ? Blend(s.previous[n], s.current[n], alpha)
                           : MoveVisual(Identity(), blended[p],
                                        Blend(MoveVisual(s.previous[p], Identity(), s.previous[n]),
                                              MoveVisual(s.current[p], Identity(), s.current[n]), alpha));
        if (fresh) {
            Matrix visual = s.local ? s.backup[n] : MoveVisual(built, Identity(), s.backup[n]);
            s.adjust[n] = MoveVisual(s.current[n], Identity(), visual);
        }
        Matrix pose = MoveVisual(Identity(), blended[n], s.adjust[n]);
        s.matrices[n] = s.local ? pose : MoveVisual(Identity(), drawRoot, pose);
        RwMatrixUpdate(&s.matrices[n]);
    }
    s.adjustTick = game::FrameCounter;
    s.built = built;
    s.applied = true;
    ++s_skinBlends;
}

void RestoreSkin(Slot& root, SkinPose& s, bool alive) {
    if (!s.applied) return;
    if (alive && Field<void*>(root.entity, 0x4C) == s.clump && SkinHierarchy(s.clump) == s.hierarchy &&
        Field<Matrix*>(s.hierarchy, 8) == s.matrices && Field<int>(s.hierarchy, 4) == s.count) {
        // World-space bones go back to the tick root so the next tick's bone queries (weapon
        // origins, IK) match the simulation.
        if (s.local) memcpy(s.matrices, s.backup, s.count * sizeof(Matrix));
        else
            for (int n = 0; n < s.count; n++) s.matrices[n] = MoveVisual(s.built, root.entityBackup, s.backup[n]);
    }
    s.applied = false;
}

// ---------------------------------------------------------------------------------------------------------------------
//  POOL CAPTURE
// ---------------------------------------------------------------------------------------------------------------------

Pool* GetPool(PoolTrack& track) {
    auto pool = *(Pool**)track.global;
    if (!pool || !pool->entries || !pool->flags || pool->size <= 0 || pool->size > track.capacity) return nullptr;
    if (pool != track.identity || pool->entries != track.entries || pool->flags != track.flags) {
        for (int i = 0; i < track.capacity; i++) track.slots[i] = Slot{};
        track.identity = pool;
        track.entries = pool->entries;
        track.flags = pool->flags;
    }
    return pool;
}

SkinPose* SkinFor(PoolTrack& track, int i) {
    if (&track == &s_pools[0]) return &s_skins[i];
    if (&track == &s_pools[2] && s_cutsceneLayout) return s_objectSkins[i];
    return nullptr;
}

PartPose* PartsFor(PoolTrack& track, int i) {
    if (&track == &s_pools[1]) return &s_parts[i];
    if (&track == &s_pools[2] && s_cutsceneLayout) return s_objectParts[i];
    return nullptr;
}

void CapturePool(PoolTrack& track, bool reset) {
    Pool* pool = GetPool(track);
    for (int i = 0; i < track.capacity; i++) {
        Slot& s = track.slots[i];
        SkinPose* skin = SkinFor(track, i);
        if (!pool || i >= pool->size || (pool->flags[i] & 0x80)) {
            s.valid = false;
            if (skin) skin->valid = false;
            continue;
        }
        auto entity = pool->entries + i * track.stride;
        const Matrix& m = Field<Matrix>(entity, 4);
        if (!Valid(m)) {
            s.valid = false;
            if (skin) skin->valid = false;
            continue;
        }
        bool fresh = !reset && !(s.valid && s.entity == entity && s.id == pool->flags[i]);
        bool continuous = !reset && !fresh;
        // Large scripted moves and teleports snap instead of smearing across the world.
        if (continuous && Dot(m.pos - s.current.pos, m.pos - s.current.pos) > 100.0f) continuous = false;
        s.previous = continuous ? s.current : m;
        // Something that appears mid-game while moving (a thrown grenade, a rocket) is drawn where its speed
        // puts it a tick earlier, in step with the hand or launcher that is drawn a tick behind.
        Vec step = Field<Vec>(entity, kMoveSpeed) * timer::ts30;
        if (fresh && s_moveSpeedLayout && Finite(step) && Dot(step, step) < 25.0f) {
            s.previous.pos = m.pos - step;
            continuous = true;
        }
        s.current = m;
        s.entity = entity;
        s.id = pool->flags[i];
        s.valid = true;
        s.continuous = continuous;
        if (&track == &s_pools[2] && s_cutsceneLayout && !skin) {
            if (Skinned(entity)) skin = s_objectSkins[i] = new (std::nothrow) SkinPose;
            else if (!s_objectParts[i] && Field<uintptr_t>(entity, 0) == kCutsceneVtable)
                s_objectParts[i] = new (std::nothrow) PartPose;
        }
        if (skin) CaptureSkin(s, *skin, continuous && !fresh);
    }
}

Slot* FindTarget(void* entity, PoolTrack*& targetPool) {
    for (auto& track : s_pools) {
        Pool* pool = GetPool(track);
        if (!pool) continue;
        uintptr_t address = (uintptr_t)entity, base = (uintptr_t)pool->entries;
        if (address < base || address - base >= pool->size * track.stride || (address - base) % track.stride) continue;
        int index = (int)((address - base) / track.stride);
        Slot& s = track.slots[index];
        if (!s.valid || s.entity != entity || s.id != pool->flags[index] || (s.id & 0x80)) return nullptr;
        targetPool = &track;
        return &s;
    }
    return nullptr;
}

bool SameAxes(const Matrix& a, const Matrix& b) {
    // Ignores the RW flags word, which RwMatrixUpdate rewrites.
    return !memcmp(&a.right, &b.right, sizeof(Vec)) && !memcmp(&a.forward, &b.forward, sizeof(Vec)) &&
           !memcmp(&a.up, &b.up, sizeof(Vec)) && !memcmp(&a.pos, &b.pos, sizeof(Vec));
}

bool ReadVisual(Slot& s, Matrix& logical, Matrix& visual, void*& object, void*& frame) {
    object = Field<void*>(s.entity, 0x4C);
    frame = object ? Field<void*>(object, 4) : nullptr;
    if (!frame) return false;
    logical = Field<Matrix>(s.entity, 4);
    visual = Field<Matrix>(frame, 0x10);
    return Valid(logical) && Valid(visual);
}

// ---------------------------------------------------------------------------------------------------------------------
//  CAMERA CAPTURE
// ---------------------------------------------------------------------------------------------------------------------

void SyncCamera(void* camera, void* frame) {
    // RW's view includes projection-window scaling and offsets, so a plain rigid inverse
    // is not equivalent. Resolve the world frame and use the verified engine synchronizer.
    RwFrameGetLtm(frame);
    RwCameraSync(camera);
    Field<Matrix>((void*)kCamera, 0x870) = Field<Matrix>(camera, 0x20);
}

void CaptureCamera(bool reset) {
    auto camera = *(uint8_t**)kSceneCamera;
    void* frame = camera ? Field<void*>(camera, 4) : nullptr;
    if (!frame) {
        s_camera.valid = false;
        return;
    }
    Matrix m = Field<Matrix>(frame, 0x10);
    if (!Valid(m)) {
        s_camera.valid = false;
        return;
    }
    void* logicalCamera = (void*)kCamera;
    uint8_t active = Field<uint8_t>(logicalCamera, 0x76), transition = Field<uint8_t>(logicalCamera, 0x89);
    void* cam = active < 3 ? (uint8_t*)logicalCamera + 0x188 + active * 0x1CC : nullptr;
    int16_t mode = cam ? Field<int16_t>(cam, 0xC) : -1;
    void* targetEntity = cam ? Field<void*>(cam, 0x1B0) : nullptr;
    PoolTrack* targetPool = nullptr;
    Slot* target = FindTarget(targetEntity, targetPool);
    // Only normal ped/car follow modes use an orbit. Scripted/transition cameras retain
    // their world-space path, and an unknown target is never dereferenced.
    bool following = !transition && target && targetEntity == Field<void*>(logicalCamera, 0x80C) &&
                     ((mode == 4 && targetPool == &s_pools[0]) || (mode == 18 && targetPool == &s_pools[1]));
    bool continuous = !reset && s_camera.valid && camera == s_camera.camera && frame == s_camera.frame;
    continuous = continuous && active == s_camera.active && mode == s_camera.mode &&
                 transition == s_camera.transition && targetEntity == s_camera.targetEntity &&
                 following == s_camera.following;
    if (following && (target != s_camera.target || target->id != s_camera.targetId || !target->continuous))
        continuous = false;
    if (continuous) {
        Vec jump = m.pos - s_camera.current.pos;
        Quaternion a = Rotation(s_camera.current), b = Rotation(m);
        float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        if (Dot(jump, jump) > 400 || d < 0.5f) continuous = false;
    }
    s_camera.previous = continuous ? s_camera.current : m;
    s_camera.prevFov = continuous ? s_camera.fov : game::MsFov;
    s_camera.current = m;
    s_camera.fov = game::MsFov;
    if (following) {
        Vec offset = LocalVector(m, target->current.pos - m.pos);
        s_camera.previousOffset = continuous ? s_camera.currentOffset : offset;
        s_camera.currentOffset = offset;
    }
    s_camera.active = active;
    s_camera.mode = mode;
    s_camera.transition = transition;
    s_camera.targetEntity = targetEntity;
    s_camera.target = target;
    s_camera.targetId = target ? target->id : 0;
    s_camera.following = following;
    s_camera.camera = camera;
    s_camera.frame = frame;
    s_camera.valid = true;
    // Keep the authoritative view tied to the tick pose. Restoring it after presentation must
    // not leave the last interpolated view in the matrices used by gameplay/projection code.
    SyncCamera(camera, frame);
}

// ---------------------------------------------------------------------------------------------------------------------
//  MOUSE LOOK CAPTURE AND INPUT
// ---------------------------------------------------------------------------------------------------------------------

// Mouse-driven camera modes (CCam::Process jump table): Beta += kx*x*H*FOV/80, Alpha += 4*y*V*FOV/80, then
// Alpha is clamped to [lo,hi]. No timestep or smoothing, so the turn is the same however input splits into ticks.
struct LookMode {
    float kx = 0, fov = 0, lo = 0, hi = 0;
    bool valid = false;
};

constexpr uintptr_t kMouseX = 0x94C798, kMouseY = 0x94C79C;  // CPad::NewMouseControllerState x/y (signed)
constexpr uintptr_t kMouseH = 0x94CBD8, kMouseV = 0xA0C96C;  // CCamera::m_fMouseAccelHorzntl/Vertical
constexpr uintptr_t kPadDisable = 0x7DADA8;                  // CPad::Pads[0].DisablePlayerControls

uint8_t* ActiveCam() {
    uint8_t active = Field<uint8_t>((void*)kCamera, 0x76);
    return active < 3 ? (uint8_t*)kCamera + 0x188 + active * 0x1CC : nullptr;
}

// fov: the FOV the next tick's mouse code uses (sniper, camera and mode 39 keep their zoom).
LookMode MouseMode(void* cam, int16_t mode, float fov) {
    constexpr float kDown = -1.5620697f, kUp = 1.0471976f;
    switch (mode) {
        case 4:
            if (!*(uint8_t*)0xA0FB55) return {};
            return {-2.5f, 70, kDown, 0.7853982f, true};
        case 7:
        case 46:
        case 39: return {-3, fov, kDown, kUp, true};
        case 8:
        case 40:
        case 41:
        case 42:
        case 43: return {-3, 70, kDown, kUp, true};
        case 34:
        case 45: {
            // A gunner attached to a vehicle (ped +0x5A0) looks along the vehicle instead.
            PoolTrack* pool = nullptr;
            void* target = Field<void*>(cam, 0x1B0);
            Slot* s = target ? FindTarget(target, pool) : nullptr;
            if (!s || pool != &s_pools[0] || Field<void*>(s->entity, 0x5A0)) return {};
            return {-3, 70, kDown, kUp, true};
        }
    }
    return {};
}

// Mode 4 replaces the pitch input with a return step while fading or with player controls disabled.
bool AlphaReturn() {
    uint8_t fade = *(uint8_t*)0xA0FB1E;
    if (*(uint8_t*)0x7E3EFB && *(int16_t*)0x7E3FD0 == 1 && fade > *(uint16_t*)0x689CB0) return true;
    return fade > 200 || (*(uint16_t*)kPadDisable & 0x20);
}

float Heading(Vec v) { return std::atan2(-v.x, v.y); }

void CaptureLook(bool reset) {
    LookHistory& l = s_look;
    uint8_t* cam = ActiveCam();
    if (!s_mouseLayout || !cam) {
        l = LookHistory{};
        return;
    }
    int16_t mode = Field<int16_t>(cam, 0xC);
    float alpha = Field<float>(cam, 0xAC), beta = Field<float>(cam, 0xBC), fov = Field<float>(cam, 0xB4);
    void* target = Field<void*>(cam, 0x1B0);
    bool continuous = !reset && l.valid && l.cam == cam && l.mode == mode && l.target == target &&
                      !Field<uint8_t>((void*)kCamera, 0x89);
    if (!continuous) l.yawBlocked = false;
    l.yaw = l.pitch = 0;
    LookMode m = MouseMode(cam, mode, l.fov);
    if (continuous && m.valid) {
        float x = *(float*)kMouseX, y = *(float*)kMouseY, scale = m.fov * 0.0125f;
        if ((x != 0 || y != 0) && (mode != 4 || !*(uint16_t*)kPadDisable)) {
            float dB = WrapPi(beta - l.beta), predB = *(float*)kMouseH * m.kx * x * scale;
            bool match = std::fabs(dB - predB) < 1e-4f;
            if (match) l.yaw = dB;
            if (x != 0) l.yawBlocked = !match;
            float predA = *(float*)kMouseV * 4 * y * scale;
            if (std::fabs(alpha - std::clamp(l.alpha + predA, m.lo, m.hi)) < 1e-4f) l.pitch = alpha - l.alpha;
        }
    }
    // Mode 4 turns the player to the camera's heading at the end of each tick.
    bool lock = false;
    if (mode == 4 && s_camera.valid && s_camera.following && s_camera.target && s_camera.target->entity == target)
        lock = std::fabs(WrapPi(Heading(s_camera.target->current.forward) - Heading(Field<Vec>(cam, 0x168)))) < 1e-3f;
    l.lockBoth = continuous && l.lock && lock;
    l.lock = lock;
    l.cam = cam;
    l.target = target;
    l.mode = mode;
    l.alpha = alpha;
    l.beta = beta;
    l.fov = fov;
    l.valid = true;
}

// The turn the drawn camera still lacks: the rest of the latest tick's mouse turn plus the mouse motion read
// since that tick. False when the active camera does not take the mouse.
bool PresentedLook(float alpha, float& yaw, float& pitch, float& pedYaw, LookMode& m) {
    LookHistory& l = s_look;
    uint8_t* cam = ActiveCam();
    if (!l.valid || cam != l.cam || Field<int16_t>(cam, 0xC) != l.mode || Field<uint8_t>((void*)kCamera, 0x89) ||
        Field<uint8_t>(cam, 0xA))
        return false;
    m = MouseMode(cam, l.mode, Field<float>(cam, 0xB4));
    if (!m.valid) return false;
    float x = (*(uint8_t*)0xA0FA54 ? -1.0f : 1.0f) * (float)s_mouse.x;  // UpdateMouse's invert flags
    float y = (*(uint8_t*)0xA0FA55 ? -1.0f : 1.0f) * (float)s_mouse.y;
    bool use = (x != 0 || y != 0) && (l.mode != 4 || !*(uint16_t*)kPadDisable);
    float scale = m.fov * 0.0125f;
    // m_bUseTransitionBeta (+0x67) sets the yaw itself.
    float pb = use && !l.yawBlocked && !Field<uint8_t>((void*)kCamera, 0x67) ? *(float*)kMouseH * m.kx * x * scale : 0;
    float pa = use && !(l.mode == 4 && AlphaReturn()) ? *(float*)kMouseV * 4 * y * scale : 0;
    yaw = (1 - alpha) * l.yaw + pb;
    pitch = (1 - alpha) * l.pitch + pa;
    pedYaw = (l.lockBoth ? (1 - alpha) * l.yaw : 0) + (l.lock ? pb : 0);
    return true;
}

void ClearMouse() { s_mouse = MouseInput{}; }

int32_t ReadMouse() {
    MouseState m;
    int32_t hr = InputGetMouseState(&m);
    if (hr < 0) return hr;
    s_mouse.x += m.x;
    s_mouse.y += m.y;
    s_mouse.z += m.z;
    // A click that starts and ends between two ticks is still reported to the next tick.
    for (int i = 0; i < 8; i++)
        if ((m.buttons[i] & 0x80) && !(s_mouse.last.buttons[i] & 0x80)) s_mouse.latch[i] = 0x80;
    s_mouse.last = m;
    return hr;
}

void PollMouse() {
    auto ps = *(uint8_t**)0x9B38FC;  // RsGlobal.ps; the mouse device at +0x1C
    if (!s_mouseLayout) return;
    if (!IsForegroundApp() || !ps || !Field<void*>(ps, 0x1C)) {
        ClearMouse();
        return;
    }
    ReadMouse();
}

// CPad::UpdateMouse's read in the world tick: everything read since the previous tick.
int32_t __cdecl MouseStateHook(MouseState* out) {
    if (!s_started || !s_inTick) {
        ClearMouse();
        return InputGetMouseState(out);
    }
    int32_t hr = ReadMouse();
    bool held = s_mouse.x || s_mouse.y || s_mouse.z;
    for (uint8_t b : s_mouse.latch) held |= b != 0;
    if (hr < 0 && !held) {
        memset(out, 0, sizeof *out);
        return hr;
    }
    *out = s_mouse.last;
    out->x = s_mouse.x;
    out->y = s_mouse.y;
    out->z = s_mouse.z;
    for (int i = 0; i < 8; i++) out->buttons[i] |= s_mouse.latch[i];
    s_mouse.x = s_mouse.y = s_mouse.z = 0;
    memset(s_mouse.latch, 0, sizeof s_mouse.latch);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
//  CAPTURE AFTER EVERY TICK
// ---------------------------------------------------------------------------------------------------------------------

void CaptureParticles(bool reset);

void Capture(bool reset) {
    for (auto& track : s_pools) CapturePool(track, reset);
    CaptureCamera(reset);
    CaptureLook(reset);
    CaptureParticles(reset);
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENTATION: CAMERA
// ---------------------------------------------------------------------------------------------------------------------

void ApplyCamera() {
    if (!s_started || !s_camera.valid || s_cameraBackup.applied) return;
    if (*(void**)kSceneCamera != s_camera.camera || Field<void*>(s_camera.camera, 4) != s_camera.frame) return;
    void* camera = (void*)kCamera;
    auto& b = s_cameraBackup;
    b.camera = s_camera.camera;
    b.frame = s_camera.frame;
    b.world = Field<Matrix>(camera, 0);
    b.inverse = Field<Matrix>(camera, 0x820);
    b.view = Field<Matrix>(camera, 0x870);
    b.gamePos = Field<Vec>(camera, 0x7D8);
    b.front[0] = Field<float>(camera, 0xD4);
    b.front[1] = Field<float>(camera, 0xD8);
    b.orientation = Field<float>(camera, 0x160);
    b.fov = game::MsFov;
    memcpy(b.planes, (uint8_t*)camera + 0x8B8, sizeof b.planes);
    b.rw = Field<Matrix>(b.frame, 0x10);
    b.rwView = Field<Matrix>(b.camera, 0x20);
    Matrix draw = Blend(s_camera.previous, s_camera.current, s_acc.Alpha());
    bool drawsTarget = false;
    Vec drawTarget{};
    if (s_camera.following) {
        PoolTrack* targetPool = nullptr;
        Slot* target = FindTarget(s_camera.targetEntity, targetPool);
        if (target == s_camera.target && target && target->id == s_camera.targetId) {
            Matrix logical{}, visual{};
            void* object = nullptr;
            void* frame = nullptr;
            bool blends = ReadVisual(*target, logical, visual, object, frame);
            drawTarget = blends ? Lerp(target->previous.pos, target->current.pos, s_acc.Alpha()) : target->current.pos;
            draw = BlendFollow(s_camera.previous, s_camera.current, s_camera.previousOffset, s_camera.currentOffset,
                               drawTarget, s_acc.Alpha());
            drawsTarget = true;
            ++s_followBlends;
        }
    }
    // Mouse look is drawn up to date: first-person views turn in place, mode 4 orbits its pivot (the target
    // + 0.8 m, on the view ray) and turns the player with it.
    float yaw = 0, pitch = 0, pedYaw = 0;
    LookMode m;
    if (PresentedLook(s_acc.Alpha(), yaw, pitch, pedYaw, m) && (yaw != 0 || pitch != 0) &&
        (s_look.mode != 4 || drawsTarget)) {
        Vec look = draw.up * (1 / Length(draw.up)), pivot = drawTarget + Vec{0, 0, 0.8f};
        pivot = draw.pos + look * Dot(pivot - draw.pos, look);
        draw = TurnLook(draw, yaw, pitch, m.lo, m.hi, s_look.mode == 4 ? &pivot : nullptr);
        if (s_look.mode == 4 && pedYaw != 0) {
            s_lookPed = s_camera.target;
            s_lookPedYaw = pedYaw;
        }
        ++s_mouseTurns;
    }
    // Camera's logical basis is right/front/up; RW camera's basis is right/up/front.
    Matrix world = b.world;
    world.right = draw.right;
    world.forward = draw.up;
    world.up = draw.forward;
    world.pos = draw.pos;
    Field<Matrix>(camera, 0) = world;
    Field<Vec>(camera, 0x7D8) = draw.pos;
    SetFov(s_camera.prevFov + (s_camera.fov - s_camera.prevFov) * s_acc.Alpha());
    DerivedCamera(camera, nullptr);
    Field<Matrix>(b.frame, 0x10) = draw;
    RwMatrixUpdate((uint8_t*)b.frame + 0x10);
    RwFrameUpdate(b.frame);
    SyncCamera(b.camera, b.frame);
    b.applied = true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENTATION: ROOTS AND WHEEL CONTACTS
// ---------------------------------------------------------------------------------------------------------------------

size_t ContactOffset(uint8_t* entity) {
    uintptr_t vtable = Field<uintptr_t>(entity, 0);
    return !s_contactLayout ? 0 : vtable == kAutomobileVtable ? 0x3E4 : vtable == kBikeVtable ? 0x330 : 0;
}

void MoveContacts(Slot& s, ContactPoints& c, const Matrix& logical) {
    size_t offset = ContactOffset(s.entity);
    if (!offset) return;
    for (int k = 0; k < 4; k++) {
        Vec& p = Field<Vec>(s.entity, offset + 0x28 * k);
        c.backup[k] = p;
        if (Finite(p)) p = s.draw.pos + TransformVector(s.draw, LocalVector(logical, p - logical.pos));
        c.drawn[k] = p;
    }
    c.applied = true;
}

void RestoreContacts(Slot& s, ContactPoints& c, bool alive) {
    if (!c.applied) return;
    size_t offset = alive ? ContactOffset(s.entity) : 0;
    for (int k = 0; offset && k < 4; k++) {
        Vec& p = Field<Vec>(s.entity, offset + 0x28 * k);
        if (!memcmp(&p, &c.drawn[k], sizeof p)) p = c.backup[k];
    }
    c.applied = false;
}

void ResetLean(Slot& s, LeanMatrix& l) {
    if (!s_leanLayout || Field<uintptr_t>(s.entity, 0) != kBikeVtable || Field<void*>(s.entity, 0x2C4 + 0x40)) return;
    l.valid = Field<uint8_t>(s.entity, 0x2C0) != 0;
    l.backup = Field<Matrix>(s.entity, 0x2C4);
    Field<uint8_t>(s.entity, 0x2C0) = 0;
    l.applied = true;
}

void RestoreLean(Slot& s, LeanMatrix& l, bool alive) {
    if (!l.applied) return;
    if (alive && Field<uintptr_t>(s.entity, 0) == kBikeVtable) {
        Field<Matrix>(s.entity, 0x2C4) = l.backup;
        Field<uint8_t>(s.entity, 0x2C0) = l.valid;
    }
    l.applied = false;
}

// Runs before ConstructRenderList so culling and everything PreRender registers from the entity
// matrix (lights, coronas, shadows, skidmarks, particles) uses the drawn pose.
void ApplyRoots(PoolTrack& track) {
    Pool* pool = GetPool(track);
    if (!pool) return;
    for (int i = 0; i < pool->size; i++) {
        Slot& s = track.slots[i];
        if (!s.valid || s.applied || pool->flags[i] != s.id || s.entity != pool->entries + i * track.stride) continue;
        void* object = nullptr;
        void* frame = nullptr;
        Matrix logical{}, visual{};
        if (!ReadVisual(s, logical, visual, object, frame)) continue;
        s.draw = Blend(s.previous, s.current, s_acc.Alpha());
        if (&s == s_lookPed) {  // the player turned with the drawn mouse look
            Vec z{0, 0, 1};
            s.draw.right = RotateAround(s.draw.right, z, s_lookPedYaw);
            s.draw.forward = RotateAround(s.draw.forward, z, s_lookPedYaw);
            s.draw.up = RotateAround(s.draw.up, z, s_lookPedYaw);
        }
        s.entityBackup = logical;
        s.rwBackup = visual;
        s.rwObject = object;
        s.frame = frame;
        Field<Matrix>(s.entity, 4) = s.draw;
        if (&track == &s_pools[1]) {
            MoveContacts(s, s_contacts[i], logical);
            ResetLean(s, s_lean[i]);
        }
        // Keep any local offset of the RW root relative to the entity matrix.
        Field<Matrix>(frame, 0x10) = MoveVisual(logical, s.draw, visual);
        RwMatrixUpdate((uint8_t*)frame + 0x10);
        RwFrameUpdate(frame);
        s.drawVisual = Field<Matrix>(frame, 0x10);
        s.applied = true;
        ++s_blends;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENTATION: SKINS AND BONE PROPS
// ---------------------------------------------------------------------------------------------------------------------

bool BoneAttached(uint8_t* entity) {
    return s_cutsceneLayout && Field<uintptr_t>(entity, 0) == kCutsceneVtable && Field<void*>(entity, 0x198) &&
           Field<void*>(entity, 0x19C);
}

// A cutscene prop held in a hand: PreRender put it on the bone as it was before blending; move it to the
// blended bone. Returns the root the prop is drawn at.
Matrix FollowBone(Slot& s) {
    Matrix built = Field<Matrix>(s.entity, 4);
    auto bone = Field<Matrix*>(s.entity, 0x198);
    PoolTrack* track = nullptr;
    Slot* parent = FindTarget(Field<void*>(s.entity, 0x19C), track);
    SkinPose* skin = parent ? SkinFor(*track, (int)(parent - track->slots)) : nullptr;
    if (!skin || !skin->applied || skin->local || bone < skin->matrices || bone >= skin->matrices + skin->count ||
        ((uintptr_t)bone - (uintptr_t)skin->matrices) % sizeof(Matrix))
        return built;
    Matrix drawn = *bone;
    if (!Valid(drawn) || !Valid(built)) return built;
    Field<Matrix>(s.entity, 4) = drawn;
    Matrix& rw = Field<Matrix>(s.frame, 0x10);
    rw = MoveVisual(built, drawn, rw);
    RwMatrixUpdate(&rw);
    RwFrameUpdate(s.frame);
    s.reposed = true;
    ++s_attachments;
    return drawn;
}

// Runs after PreRender, which rebuilds the palettes from the current animation state.
void ApplySkins() {
    for (int pass = 0; pass < 2; pass++)
        for (int t = 0; t < 3; t += 2) {
            auto& track = s_pools[t];
            Pool* pool = GetPool(track);
            if (!pool || (pass && t == 0)) continue;
            for (int i = 0; i < pool->size; i++) {
                Slot& s = track.slots[i];
                SkinPose* skin = SkinFor(track, i);
                if (!s.applied || pool->flags[i] != s.id || s.entity != pool->entries + i * track.stride ||
                    Field<void*>(s.entity, 0x4C) != s.rwObject)
                    continue;
                // Props on a bone go second, after the skin they hang from.
                if (t == 2 && BoneAttached(s.entity) != (pass == 1)) continue;
                // PreRender normally leaves the root at the drawn pose; props attached to a frame were moved by it.
                Matrix built = Field<Matrix>(s.entity, 4);
                Matrix drawn = pass ? FollowBone(s) : built;
                if (skin) ApplySkin(s, *skin, built, drawn);
            }
        }
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENTATION: VEHICLE PARTS AND WHEELS
// ---------------------------------------------------------------------------------------------------------------------

// Depth-first list of the frames below root (RwFrame: child +0x98, next sibling +0x9C).
int CollectFrames(void* root, void** out) {
    void* stack[kMaxParts];
    int depth = 0, count = 0;
    void* f = Field<void*>(root, 0x98);
    while (f || depth) {
        if (!f) {
            f = stack[--depth];
            continue;
        }
        if (count == kMaxParts || depth == kMaxParts) return -1;
        out[count++] = f;
        stack[depth++] = Field<void*>(f, 0x9C);
        f = Field<void*>(f, 0x98);
    }
    return count;
}

void WriteFrame(void* frame, const Matrix& m) {
    Field<Matrix>(frame, 0x10) = m;
    RwMatrixUpdate((uint8_t*)frame + 0x10);
    RwFrameUpdate(frame);
}

void MapWheels(uint8_t* entity, void** frames, int count, PartPose& p) {
    memset(p.wheel, 0, sizeof p.wheel);
    memset(p.side, 0, sizeof p.side);
    if (!s_wheelLayout) return;
    uintptr_t vtable = Field<uintptr_t>(entity, 0);
    const WheelNode* nodes = vtable == kAutomobileVtable ? kCarWheels : vtable == kBikeVtable ? kBikeWheels : nullptr;
    int n = vtable == kAutomobileVtable ? 6 : 2;
    if (!nodes) return;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < n; k++)
            if (frames[i] && frames[i] == Field<void*>(entity, nodes[k].node)) {
                p.wheel[i] = nodes[k].angle;
                p.side[i] = nodes[k].side;
            }
}

// Wheels: the roll comes from the game's own wheel angles, so any change per tick (fast spin, a wheel
// landing or locking) turns the right way, and the steering blends separately from the roll.
bool BlendWheel(PartPose& p, int n, void* frame, float alpha) {
    if (!p.wheel[n]) return false;
    float expected = p.side[n] * (p.angleCurrent[n] - p.anglePrevious[n]);
    if (!std::isfinite(expected)) return false;
    Twist twist = SplitTwistX(p.previous[n], p.current[n]);
    if (!twist.valid || std::fabs(WrapPi(twist.angle - expected)) > 0.05f) return false;  // posed some other way
    if (NearlyEqual(p.previous[n], p.current[n]) && std::fabs(expected) < 1e-4f) return true;
    WriteFrame(frame, BlendTwist(p.previous[n], p.current[n], alpha, twist, NearestTurn(twist.angle, expected)));
    ++s_wheelBlends;
    return true;
}

// Runs after PreRender: by then the parts hold the pose for the current tick.
void ApplyParts() {
    uint32_t tick = game::FrameCounter;
    float alpha = s_acc.Alpha();
    for (int t = 1; t < 3; t++) {
        auto& track = s_pools[t];
        Pool* pool = GetPool(track);
        if (!pool) continue;
        for (int i = 0; i < pool->size; i++) {
            PartPose* part = PartsFor(track, i);
            if (!part) continue;
            Slot& s = track.slots[i];
            PartPose& p = *part;
            if (!s.applied || pool->flags[i] != s.id || s.entity != pool->entries + i * track.stride ||
                Field<void*>(s.entity, 0x4C) != s.rwObject || Field<void*>(s.rwObject, 4) != s.frame) {
                p.valid = false;
                continue;
            }
            // Objects: animated cutscene props only. A prop moved onto a blended bone is drawn as it is.
            if (t == 2 && (s.reposed || Field<uintptr_t>(s.entity, 0) != kCutsceneVtable || Skinned(s.entity))) {
                p.valid = false;
                continue;
            }
            void* frames[kMaxParts];
            int count = CollectFrames(s.frame, frames);
            if (count < 0) {
                p.valid = false;
                continue;
            }
            Matrix root = Field<Matrix>(s.frame, 0x10);
            if (!Valid(root)) {
                p.valid = false;
                continue;
            }
            Matrix rootLocal = MoveVisual(s.draw, Identity(), root);
            bool same = p.valid && p.clump == s.rwObject && p.entity == s.entity && p.id == s.id && p.count == count &&
                        !memcmp(p.frames, frames, count * sizeof(void*));
            if (!same) MapWheels(s.entity, frames, count, p);
            if (!same || p.tick != tick) {
                // A new tick: the last captured pose becomes the previous one. Gaps of a few ticks (catch-up
                // frames) still blend from the last seen pose; teleports and new vehicles snap.
                bool continuous = same && s.continuous && tick - p.tick <= 3;
                for (int n = 0; n < count; n++) {
                    Matrix m = Field<Matrix>(frames[n], 0x10);
                    p.previous[n] = continuous ? p.current[n] : m;
                    p.current[n] = m;
                    if (continuous) TrackSpin(p.previous[n], p.current[n], p.spin[n]);
                    else p.spin[n] = Spin{};
                    if (!continuous) p.animated[n] = 0;
                    if (p.wheel[n]) {
                        float angle = Field<float>(s.entity, p.wheel[n]);
                        p.anglePrevious[n] = continuous ? p.angleCurrent[n] : angle;
                        p.angleCurrent[n] = angle;
                    }
                }
                p.rootPrevious = continuous ? p.rootCurrent : rootLocal;
                p.rootCurrent = rootLocal;
                if (continuous) TrackSpin(p.rootPrevious, p.rootCurrent, p.rootSpin);
                else p.rootSpin = Spin{};
                p.tick = tick;
            } else {
                for (int n = 0; n < count; n++) {
                    // Parts PreRender animates on every drawn frame (helicopter rotors) are already smooth.
                    Matrix m = Field<Matrix>(frames[n], 0x10);
                    if (!NearlyEqual(m, p.current[n])) p.animated[n] = tick + 1;
                    p.current[n] = m;
                    if (p.wheel[n]) p.angleCurrent[n] = Field<float>(s.entity, p.wheel[n]);
                }
                p.rootCurrent = rootLocal;
            }
            memcpy(p.frames, frames, count * sizeof(void*));
            p.count = count;
            p.clump = s.rwObject;
            p.entity = s.entity;
            p.id = s.id;
            p.valid = true;
            p.applied = true;
            for (int n = 0; n < count; n++) {
                if (p.animated[n] && tick - (p.animated[n] - 1) <= 30) continue;
                if (!Valid(p.previous[n]) || !Valid(p.current[n])) continue;
                if (BlendWheel(p, n, frames[n], alpha)) continue;
                if (NearlyEqual(p.previous[n], p.current[n])) continue;
                WriteFrame(frames[n], BlendSpin(p.previous[n], p.current[n], alpha, p.spin[n]));
                ++(t == 1 ? s_partBlends : s_propBlends);
            }
            p.rootPreRender = root;
            p.rootApplied = false;
            if (!NearlyEqual(p.rootPrevious, p.rootCurrent, 1e-3f) && Valid(p.rootPrevious)) {
                WriteFrame(s.frame,
                           MoveVisual(Identity(), s.draw, BlendSpin(p.rootPrevious, p.rootCurrent, alpha, p.rootSpin)));
                p.rootApplied = true;
            }
        }
    }
}

void RestoreParts(Slot& s, PartPose& p, bool alive) {
    if (!p.applied) return;
    p.applied = false;
    if (!alive || Field<void*>(s.entity, 0x4C) != p.clump || Field<void*>(p.clump, 4) != s.frame) return;
    for (int n = 0; n < p.count; n++)
        if (!SameAxes(Field<Matrix>(p.frames[n], 0x10), p.current[n])) WriteFrame(p.frames[n], p.current[n]);
    if (p.rootApplied) Field<Matrix>(s.frame, 0x10) = p.rootPreRender;  // the root restore below finishes it
}

// ---------------------------------------------------------------------------------------------------------------------
//  PRESENTATION: PARTICLES
// ---------------------------------------------------------------------------------------------------------------------

uint8_t* Particle(int i) { return (uint8_t*)kParticleArray + i * kParticleStride; }

// Marks the slots on the free list; false if the list does not look like the verified layout.
bool FreeParticles(bool* freeSlots) {
    memset(freeSlots, 0, kMaxParticles);
    auto p = *(uint8_t**)kParticleFree;
    for (int n = 0; p; n++) {
        uintptr_t offset = (uintptr_t)p - kParticleArray;
        if (n == kMaxParticles || offset >= (uintptr_t)kMaxParticles * kParticleStride || offset % kParticleStride)
            return false;
        freeSlots[offset / kParticleStride] = true;
        p = Field<uint8_t*>(p, 0x54);
    }
    return true;
}

void CaptureParticles(bool reset) {
    bool freeSlots[kMaxParticles];
    if (!s_particleLayout || !FreeParticles(freeSlots)) {
        for (auto& t : s_particles) t = ParticleTrack{};
        return;
    }
    for (int i = 0; i < kMaxParticles; i++) {
        ParticleTrack& t = s_particles[i];
        uint32_t serial = s_particleSerial[i];
        Vec pos = Field<Vec>(Particle(i), 0);
        if (freeSlots[i] || !serial || !Finite(pos)) {
            t = ParticleTrack{};
            continue;
        }
        // The same particle as last tick (or since it was added by a drawn frame) unless it jumped.
        bool continuous = !reset && t.serial == serial && Dot(pos - t.current, pos - t.current) < 225.0f;
        t.previous = continuous ? t.current : pos;
        t.current = pos;
        t.serial = serial;
        t.continuous = continuous;
    }
}

// Around CParticle::Render: particles are drawn between their last two tick positions. A particle born
// in the latest tick is drawn where it was born until the next tick.
void ShiftParticles() {
    bool freeSlots[kMaxParticles];
    if (!s_started || !s_drawing || !s_particleLayout || s_particlesShifted || !FreeParticles(freeSlots)) return;
    float alpha = s_acc.Alpha();
    for (int i = 0; i < kMaxParticles; i++) {
        ParticleTrack& t = s_particles[i];
        if (freeSlots[i] || !s_particleSerial[i]) continue;
        Vec& pos = Field<Vec>(Particle(i), 0);
        if (t.serial != s_particleSerial[i]) {  // added by this drawn frame (PreRender): tracked from here
            t = ParticleTrack{};
            t.serial = s_particleSerial[i];
            t.previous = t.current = pos;
            continue;
        }
        if (!t.continuous || memcmp(&pos, &t.current, sizeof pos)) continue;
        t.backup = pos;
        t.shifted = true;
        pos = Lerp(t.previous, t.current, alpha);
        ++s_particleBlends;
    }
    s_particlesShifted = true;
}

void RestoreParticles() {
    if (!s_particlesShifted) return;
    for (int i = 0; i < kMaxParticles; i++) {
        ParticleTrack& t = s_particles[i];
        if (t.shifted) {
            Field<Vec>(Particle(i), 0) = t.backup;
            t.shifted = false;
        }
    }
    s_particlesShifted = false;
}

void __cdecl ParticleRenderHook() {
    ShiftParticles();
    ((void(__cdecl*)())0x5607B0)();
    RestoreParticles();
}

// AddParticle took a slot from the free list (ebp = the particle).
void ParticleAdded(fix::Regs* r, fix::Fpu*) {
    uintptr_t offset = r->ebp - kParticleArray;
    if (offset < (uintptr_t)kMaxParticles * kParticleStride && !(offset % kParticleStride))
        s_particleSerial[offset / kParticleStride] = ++s_particleSerials ? s_particleSerials : ++s_particleSerials;
}

// ---------------------------------------------------------------------------------------------------------------------
//  RESTORE
// ---------------------------------------------------------------------------------------------------------------------

// Puts back everything a drawn frame changed, so the next tick only ever sees its own state.
void Restore() {
    for (auto& track : s_pools) {
        auto pool = *(Pool**)track.global;
        for (int i = 0; i < track.capacity; i++) {
            Slot& s = track.slots[i];
            if (!s.applied) continue;
            // Never write a restored pose into a destroyed/recycled entity or RW object.
            bool alive = pool == track.identity && pool && pool->entries == track.entries &&
                         pool->flags == track.flags && i < pool->size && pool->flags[i] == s.id &&
                         s.entity == pool->entries + i * track.stride;
            if (SkinPose* skin = SkinFor(track, i)) RestoreSkin(s, *skin, alive);
            if (PartPose* part = PartsFor(track, i)) RestoreParts(s, *part, alive);
            if (&track == &s_pools[1]) {
                RestoreContacts(s, s_contacts[i], alive);
                RestoreLean(s, s_lean[i], alive);
            }
            if (alive) {
                Field<Matrix>(s.entity, 4) = s.entityBackup;
                if (Field<void*>(s.entity, 0x4C) == s.rwObject && Field<void*>(s.rwObject, 4) == s.frame) {
                    // PreRender normally leaves the root alone: restore it exactly. If it posed the
                    // root from the drawn matrix, carry that local pose back to the tick pose. A prop moved
                    // to a blended bone is re-attached by the next tick.
                    Matrix& rw = Field<Matrix>(s.frame, 0x10);
                    rw = s.reposed || SameAxes(rw, s.drawVisual) || !Valid(rw) ? s.rwBackup
                                                                               : MoveVisual(s.draw, s.entityBackup, rw);
                    RwMatrixUpdate((uint8_t*)s.frame + 0x10);
                    RwFrameUpdate(s.frame);
                }
            }
            s.applied = false;
            s.reposed = false;
        }
    }
    RestoreParticles();
    s_waveMs = game::TimeInMs;
    auto& b = s_cameraBackup;
    if (b.applied) {
        void* camera = (void*)kCamera;
        Field<Matrix>(camera, 0) = b.world;
        Field<Matrix>(camera, 0x820) = b.inverse;
        Field<Matrix>(camera, 0x870) = b.view;
        Field<Vec>(camera, 0x7D8) = b.gamePos;
        Field<float>(camera, 0xD4) = b.front[0];
        Field<float>(camera, 0xD8) = b.front[1];
        Field<float>(camera, 0x160) = b.orientation;
        memcpy((uint8_t*)camera + 0x8B8, b.planes, sizeof b.planes);
        SetFov(b.fov);
        if (*(void**)kSceneCamera == b.camera && Field<void*>(b.camera, 4) == b.frame) {
            Field<Matrix>(b.frame, 0x10) = b.rw;
            Field<Matrix>(b.camera, 0x20) = b.rwView;
            RwMatrixUpdate((uint8_t*)b.frame + 0x10);
            RwFrameUpdate(b.frame);
            SyncCamera(b.camera, b.frame);
            // Preserve the exact saved view bytes while recomputing RW frustum/LTM state.
            Field<Matrix>(camera, 0x870) = b.view;
            Field<Matrix>(b.camera, 0x20) = b.rwView;
        }
        b.applied = false;
    }
    fx::EndDraw();  // with the clock and the camera back at the tick
    s_drawing = false;
}

// ---------------------------------------------------------------------------------------------------------------------
//  FRAME LOOP HOOKS
// ---------------------------------------------------------------------------------------------------------------------

// Replaces the CGame::Process call in Idle: runs the ticks this drawn frame is owed.
void __cdecl Process() {
    s_drawing = false;
    s_frameTicks = 0;
    s_waveMs = game::TimeInMs;
    bool paused = game::UserPause || game::CodePause || game::MenuActive || game::MenuGameNotLoaded ||
                  game::GameState != 9;
    if (paused) {
        s_started = false;
        s_acc.Reset();
        s_statStart = 0;
        s_tickLights = 0;
        ClearMouse();
        // The original paused path still polls input, runs the frontend and services streaming.
        GameProcess();
        s_waveMs = game::TimeInMs;
        s_drawing = true;
        return;
    }
    if (!s_started) {
        s_acc.Reset();
        Capture(true);
        fx::Reset();
        s_started = true;
        s_tickLights = 0;
        s_tickMsPrevious = s_tickMsCurrent = game::TimeInMs;
        s_statStart = timer::Now();
        s_clockStart = game::TimeInMs;
        s_ticks = s_frames = s_skippedParticles = s_blends = s_skinBlends = s_followBlends = s_partBlends =
            s_wheelBlends = 0;
        s_particleBlends = s_attachments = s_propBlends = s_mouseTurns = 0;
        s_acc.dropped = 0;
        Log("fixed: world active, 30 Hz");
    }
    s_acc.lockstep = g_cfg.fpsLimit > 0 && g_cfg.fpsLimit <= 30;  // a blend would only add delay at these rates
    s_acc.Add(s_delta);
    while (s_acc.Take()) {
        timer::FixedTick();
        s_waveMs = game::TimeInMs;
        s_tickMsPrevious = s_tickMsCurrent;
        s_tickMsCurrent = game::TimeInMs;
        // Idle clears the point light list once per drawn frame (0x4A5C66); clear it per tick instead.
        *(int32_t*)kNumLights = 0;
        fx::BeforeTick(s_frameTicks);
        s_inTick = true;
        GameProcess();
        s_inTick = false;
        ++s_frameTicks;
        ++s_ticks;
        Capture(false);
        fx::AfterTick();
        // A tick can open the menu or initiate a load. Never continue a stale catch-up batch.
        if (game::UserPause || game::CodePause || game::MenuActive || *(uint8_t*)0x868671 || *(uint8_t*)0xA0FB78) {
            s_acc.Reset();
            s_started = false;
            break;
        }
    }
    // Lights registered by the last tick (fires, explosions, muzzle flashes) stay in the list for
    // every drawn frame until the next tick; render-path lights are appended after them.
    if (s_frameTicks) s_tickLights = *(int32_t*)kNumLights;
    else *(int32_t*)kNumLights = s_tickLights;
    bool nowPaused = game::UserPause || game::CodePause || game::MenuActive;
    timer::FixedRender(nowPaused ? 0 : s_frameTicks);
    s_drawing = true;
    ++s_frames;
    // The world is drawn between its last two ticks; the water waves follow the same clock.
    if (s_started && !nowPaused)
        s_waveMs = s_tickMsPrevious + (uint32_t)(s_acc.Alpha() * (float)(s_tickMsCurrent - s_tickMsPrevious) + 0.5f);
}

void __cdecl ConstructHook() {
    s_lookPed = nullptr;
    s_lookPedYaw = 0;
    if (s_started) {
        PollMouse();
        fx::BeginDraw(s_acc.Alpha(), s_frameTicks);
    }
    ApplyCamera();
    if (s_started) fx::AfterCamera(Field<Vec>((void*)kCamera, 0x30));
    if (s_started)
        for (auto& track : s_pools) ApplyRoots(track);
    Construct();
}

void __cdecl PreRenderHook() {
    PreRender();
    if (s_started) {
        ApplySkins();
        ApplyParts();
    }
}

void ParticleGate(fix::Regs* r, fix::Fpu*) {
    if (s_drawing && !s_frameTicks) {
        ++s_skippedParticles;
        r->cont = 0x563BCC;  // original null-return path after the already-executed prologue
    }
}

void __cdecl IdleHook(void* arg) {
    Restore();  // also protects against an interrupted/early-return previous draw
    OriginalIdle(arg);
    Restore();  // runs for every original Idle exit, including failed CameraBeginUpdate
    if (s_started && g_cfg.log && s_statStart && timer::Now() - s_statStart >= 5.0) {
        double elapsed = timer::Now() - s_statStart;
        Log("fixed: %.2f ticks/s, %.1f render fps, clock %.4fx, alpha %.3f, blends %u, skin palettes %u, follow camera "
            "blends %u, vehicle parts %u, wheel rolls %u, particles %u, bone props %u, prop parts %u, mouse turns %u, "
            "suppressed render particles %u, dropped %.2f ms",
            s_ticks / elapsed, s_frames / elapsed, (game::TimeInMs - s_clockStart) / (elapsed * 1000), s_acc.Alpha(),
            s_blends, s_skinBlends, s_followBlends, s_partBlends, s_wheelBlends, s_particleBlends, s_attachments,
            s_propBlends, s_mouseTurns, s_skippedParticles, s_acc.dropped);
        s_statStart = timer::Now();
        s_clockStart = game::TimeInMs;
        s_ticks = s_frames = s_skippedParticles = s_blends = s_skinBlends = s_followBlends = s_partBlends =
            s_wheelBlends = 0;
        s_particleBlends = s_attachments = s_propBlends = s_mouseTurns = 0;
        s_acc.dropped = 0;
    }
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
//  INTERFACE
// ---------------------------------------------------------------------------------------------------------------------

void FrameDelta(double scaledMs) { s_delta = std::isfinite(scaledMs) ? std::max(0.0, scaledMs) : 0.0; }

// ---------------------------------------------------------------------------------------------------------------------
//  TEST ENTRY POINTS
// ---------------------------------------------------------------------------------------------------------------------

#ifdef VCUNLOCK_FIXED_TEST
namespace testing {
void Reset() {
    Restore();
    s_started = false;
    s_acc = Accumulator{};
    s_delta = 0;
    s_camera = CameraHistory{};
    s_tickLights = 0;
    for (auto& track : s_pools) {
        track.identity = nullptr;
        track.entries = nullptr;
        track.flags = nullptr;
        for (int i = 0; i < track.capacity; i++) track.slots[i] = Slot{};
    }
    for (auto& skin : s_skins) skin = SkinPose{};
    for (auto& skin : s_objectSkins) {
        delete skin;
        skin = nullptr;
    }
    for (auto& part : s_parts) part = PartPose{};
    for (auto& part : s_objectParts) {
        delete part;
        part = nullptr;
    }
    for (auto& particle : s_particles) particle = ParticleTrack{};
    s_look = LookHistory{};
    ClearMouse();
    for (auto& c : s_contacts) c = ContactPoints{};
    for (auto& l : s_lean) l = LeanMatrix{};
    fx::Reset();
    memset(s_particleSerial, 0, sizeof s_particleSerial);
    s_particleSerials = 0;
}

void ParticleAdded(void* particle) {
    fix::Regs r{};
    r.ebp = (uint32_t)(uintptr_t)particle;
    fixed::ParticleAdded(&r, nullptr);
}

void RenderParticles() { ParticleRenderHook(); }
uint32_t WaveMs() { return s_waveMs; }

void ProcessFrame(double ms) {
    Restore();
    FrameDelta(ms);
    timer::FixedRender(0);
    *(int32_t*)kNumLights = 0;  // Idle's per-frame reset at 0x4A5C66
    Process();
}

void BeginDraw() {
    ConstructHook();
    PreRenderHook();
}

void EndDraw() { Restore(); }
uint32_t FrameTicks() { return s_frameTicks; }
float Alpha() { return s_acc.Alpha(); }
double DroppedMs() { return s_acc.dropped; }

bool ParticleSuppressed() {
    fix::Regs r{};
    ParticleGate(&r, nullptr);
    return r.cont == 0x563BCC;
}
}  // namespace testing
#endif

// ---------------------------------------------------------------------------------------------------------------------
//  INSTALL
// ---------------------------------------------------------------------------------------------------------------------

bool Install() {
    PatchGroup g("fixed: Steam update/render and matrix layouts");
    g.Expect(game::Idle, "53 56 57 55 83 EC 20 E8 94 B1 02 00")
        .Expect(0x4A5C66, "C7 05 B8 41 97 00 00 00 00 00")
        .Expect(0x4A5C70, "E8 5B E6 FF FF")
        .Expect(0x4A5D15, "E8 06 44 02 00")
        .Expect(0x4A5D1A, "E8 91 43 02 00")
        .Expect(kGameProcess, "53 55 E8 B9 72 00 00")
        .Expect(kConstruct, "53 56 57 55 83 EC 18")
        .Expect(kPreRender, "53 56 55 31 ED 83 3D EC C1 A0 00 00")
        .Expect(0x451B70, "69 C0 DC 05 00 00 03 06 5E 5B")
        .Expect(0x451BB0, "69 C0 D8 06 00 00 03 06")
        .Expect(0x4C0E11, "69 C0 A0 01 00 00")
        .Expect(0x4C0225, "A3 E8 CB 94 00")
        .Expect(0x489240, "8B 51 4C 85 D2 75 01 C3 80 3A 01 75 00 8B 42 04")
        .Expect(kRwMatrixUpdate, "8B 44 24 04 81 60 0C FC FF FD FF C3")
        .Expect(kRwFrameUpdate, "8B 44 24 04 33 C9 8B 90 A0 00 00 00")
        .Expect(0x643F27, "8B 90 98 00 00 00")                    // RwFrameRemoveChild: parent->child +0x98
        .Expect(0x643F31, "8B 91 9C 00 00 00 89 90 98 00 00 00")  //   next sibling +0x9C
        .Expect(kRwFrameGetLtm, "56 8B 74 24 08 8B 86 A0 00 00 00")
        .Expect(kRwCameraSync, "83 EC 0C 56 8B 74 24 14 83 7E 14 01")
        .Expect(kSkinHierarchy, "83 EC 08 C7 44 24 04 00 00 00 00")
        .Expect(kHierarchyUpdate, "81 EC 14 0A 00 00 53 55 8B AC 24 20 0A 00 00")
        .Expect(0x645469, "8B 45 04 8D 8C 24 64 02 00 00 89 4C 24 14")
        .Expect(0x6457D4, "8B 4D 18 C1 E0 06 03 41 08")
        .Expect(0x46C296, "53 89 CB 83 EC 50")
        .Expect(0x46C527, "0F B6 4B 76 69 C9 CC 01 00 00 83 BC 0B 38 03 00 00 00")
        .Expect(0x46C53B, "8B 83 0C 08 00 00 89 84 0B 38 03 00 00 0F B6 43 76 40 99")
        .Expect(0x4839F1, "0F BF 45 0C 48 83 F8 2D")
        .Expect(0x46C7DE, "80 BB 89 00 00 00 00")
        .Expect(0x46DBAD, "8B 83 08 08 00 00 59 8B 78 04")
        .Expect(0x46DBB7, "D9 43 30 8D 47 10 D9 58 30 D9 40 30 D9 9B D8 07 00 00")
        .Expect(0x563BBA, "80 3D 3F FB A0 00 00");
    // All or nothing: a partial install would leave the world without ticks. Every byte the core patches
    // below rely on is verified here, so past the code allocation nothing can fail.
    if (!g.Ok() || !timer::Verify()) {
        Log("fixed: verification failed; the game keeps its own 30 fps timing");
        return false;
    }

    PatchGroup w("fixed: vehicle wheel layouts");
    w.Expect(0x59E476, "C7 00 90 9D 69 00 8B 4C 24 08 81 C1 A0 02 00 00")           // CAutomobile ctor: vtable
        .Expect(0x61537C, "C7 00 04 6B 6D 00")                                      // CBike ctor: vtable
        // CAutomobile::PreRender, per wheel: the frame, then SetRotate(+-angle, 0, ...)
        .Expect(0x590096, "8B 85 98 03 00 00")                                      // RF frame
        .Expect(0x5900E6, "FF B5 D8 04 00 00")                                      //   +angle FR
        .Expect(0x58FAEA, "8B 85 9C 03 00 00 85 C0 0F 84 C0 02 00 00")              // RM frame
        .Expect(0x58FB42, "FF B5 DC 04 00 00")                                      //   +angle RR
        .Expect(0x58EF71, "8B 85 A0 03 00 00")                                      // RB frame
        .Expect(0x58EFBA, "FF B5 DC 04 00 00 E8 7B 4B E9 FF D9 9C 24 A4 01 00 00")  //   +angle RR
        .Expect(0x590D58, "8B 85 A4 03 00 00")                                      // LF frame
        .Expect(0x590E21, "D9 85 D0 04 00 00 FF 35 E4 95 69 00 50 D9 E0 D9 1C 24")  //   -angle FL
        .Expect(0x58FDB8, "8B 85 A8 03 00 00")                                      // LM frame
        .Expect(0x58FE10, "D9 85 D4 04 00 00 50 D9 E0")                             //   -angle RL
        .Expect(0x58F528, "8B 85 AC 03 00 00")                                      // LB frame
        .Expect(0x58F578, "D9 85 D4 04 00 00 50 D9 E0 D9 1C 24 E8 B7 45 E9 FF")     //   -angle RL
        // CBike::PreRender, per wheel: the frame, then SetRotate(angle, ...)
        .Expect(0x60C9C7, "8B 83 B0 02 00 00")                                      // front frame
        .Expect(0x60CA8E, "FF B3 10 04 00 00 DD D8 E8 65 26 ED FF")                 //   angle
        .Expect(0x60CB4D, "8B 83 B4 02 00 00")                                      // rear frame
        .Expect(0x60CBD0, "FF B3 14 04 00 00 DD D8 E8 23 25 ED FF");                //   angle
    s_wheelLayout = w.Ok();
    if (!s_wheelLayout) Log("fixed: wheel layout differs; wheels blend without the game's wheel angles");

    PatchGroup h("fixed: skin hierarchy layout");
    h.Expect(0x645477, "8B 4D 10")                        // HAnimHierarchy::pNodeInfo +0x10
        .Expect(0x645486, "83 C1 08")                     //   node flags +8, 16-byte records
        .Expect(0x64573A, "8B 54 24 18 8B 02 83 E0 03");  //   PUSH/POP bits
    s_treeLayout = h.Ok();

    PatchGroup c("fixed: cutscene object layout");
    c.Expect(0x4E0398, "C7 03 94 05 69 00 8A 43 50 24 07")  // CCutsceneObject ctor: vtable
        .Expect(0x4E002A, "8B 83 98 01 00 00")              //   attached bone matrix +0x198
        .Expect(0x4E0038, "8B 8B 9C 01 00 00 85 C9")        //   attached parent +0x19C
        .Expect(0x406640, "89 AB 9C 01 00 00")
        .Expect(0x406649, "89 BB 98 01 00 00");
    s_cutsceneLayout = c.Ok();

    PatchGroup p("fixed: particle layout");
    p.Expect(0x565850, "C7 05 34 F0 97 00 EC D4 76 00")  // free list head = particle array
        .Expect(0x565860, "81 FA ED 02 00 00 7C 0C")     //   750 particles
        .Expect(0x565877, "6B C0 58 05 EC D4 76 00")     //   0x58 bytes each
        .Expect(0x56587F, "89 81 40 D5 76 00")           //   next +0x54
        .Expect(0x564782, "8B 45 54 A3 34 F0 97 00")     // AddParticle pops the free list
        .Expect(0x561FF8, "D8 4B 0C")                    // render reads position +0
        .Expect(0x56202C, "8D 33");
    s_particleLayout = p.Ok();

    PatchGroup ml("fixed: mouse look layout");
    ml.Expect(0x4AD738, "8D 44 24 0C 50 E8 AE 17 15 00 85 C0 59 0F 8C")           // UpdateMouse: the only mouse read
        .Expect(0x4AD75E, "80 3D 55 FA A0 00 00")
        .Expect(0x4AD76A, "80 3D 54 FA A0 00 00")                                 //   invert y/x
        .Expect(0x5FEEF0, "A1 FC 38 9B 00 57 83 78 1C 00")                        // _InputGetMouseState: device ps+0x1C
        .Expect(0x600D00, "31 C9 31 C0 83 3D F4 49 6D 00 00")                     // IsForegroundApp
        .Expect(0x47BD8B, "C7 83 B4 00 00 00 00 00 8C 42")                        // mode 4: FOV 70
        .Expect(0x47BECB, "A1 9C C7 94 00 8B 15 98 C7 94 00")                     //   mouse y/x
        .Expect(0x47BF08, "66 83 B8 F0 00 00 00 00")                              //   controls disabled
        .Expect(0x47BF13, "D9 44 24 5C D9 E0 D8 0D 00 9E 68 00")                  //   -2.5 x
        .Expect(0x47BF28, "D9 05 E8 9D 68 00 D8 4C 24 60")                        //   4 y
        .Expect(0x47BF7F, "D9 83 B4 00 00 00 D8 0D 00 A0 68 00 DD D9")            //   FOV/80
        .Expect(0x47BF8D, "D9 05 D8 CB 94 00 D8 4C 24 14 D8 C9 D9 5C 24 20")
        .Expect(0x47BF9D, "D9 05 6C C9 A0 00")                                    // H, V
        .Expect(0x47C0FA, "D9 83 BC 00 00 00 D8 44 24 20 D9 9B BC 00 00 00 D9 83 AC 00 00 00 D8 44 24 1C")
        .Expect(0x47C175, "D8 1D 38 A0 68 00 DF E0 F6 C4 45 75 0C")               //   pitch up to 45 deg
        // sniper: mouse y/x
        .Expect(0x479512, "A1 9C C7 94 00 8B 15 98 C7 94 00 89 44 24 38 89 54 24 34")
        .Expect(0x47958E, "D8 0D F4 9D 68 00")                                    //   3 x
        .Expect(0x4795B5, "D9 85 B4 00 00 00 D8 0D 00 A0 68 00 DD DC")            //   its own FOV/80
        .Expect(0x479684, "D9 85 BC 00 00 00 D8 C5 D9 9D BC 00 00 00")
        .Expect(0x4796FB, "D8 1D 3C 9E 68 00")                                    //   pitch up to 60 deg
        .Expect(0x47AD64, "83 B8 A0 05 00 00 00 74 02 B3 01 D9 06")               // M16: attached gunner
        .Expect(0x4819C9, "66 83 7D 0C 27 74 0A C7 85 B4 00 00 00 00 00 8C 42");  // runabout: FOV 70 unless mode 39
    s_mouseLayout = ml.Ok();

    PatchGroup cp("fixed: wheel contact point layout");
    cp.Expect(0x58A420, "05 E4 03 00 00 FF 74 24 68 50")
        .Expect(0x58A474, "81 C1 E4 03 00 00 FF 70 04")  // CAutomobile::PreRender: colpoints +0x3E4
        .Expect(0x58A85D, "83 44 24 08 28")              //   0x28 each
        .Expect(0x60AA37, "8D B3 A8 03 00 00")
        .Expect(0x60AA7D, "8D 83 80 03 00 00");          // CBike::PreRender: +0x330 + 3/2 x 0x28
    s_contactLayout = cp.Ok();

    PatchGroup lm("fixed: bike lean matrix layout");
    lm.Expect(0x6098BB, "80 BB C0 02 00 00 00")                             // CBike::CalculateLeanMatrix: cached
        .Expect(0x609918, "8D 43 04 8D 8B C4 02 00 00 50 E8 59 5E ED FF")  //   until +0x2C0 is 0, +0x2C4 = matrix
        .Expect(0x6099FA, "C6 83 C0 02 00 00 01")
        .Expect(0x4DF793, "83 7B 40 00")                                    // CMatrix attachment +0x40
        .Expect(0x60E065, "C6 85 C0 02 00 00 00");                          // CBike::ProcessControl clears it
    s_leanLayout = lm.Ok();

    PatchGroup ms("fixed: move speed layout");
    ms.Expect(0x4BC104, "83 C0 70 C3 83 C1 70");  // CPhysical::GetMoveSpeed +0x70
    if (!ms.Ok()) s_moveSpeedLayout = false;

    uint8_t* trampoline = fix::AllocCode(16);  // the first allocation: the hook code below fits in the same block
    if (!trampoline) {
        Log("fixed: no memory for hook code; the game keeps its own 30 fps timing");
        return false;
    }
    timer::Install();
    memcpy(trampoline, (void*)game::Idle, 7);
    mem::Jmp((uintptr_t)trampoline + 7, (void*)(game::Idle + 7));
    OriginalIdle = (decltype(OriginalIdle))trampoline;
    fix::Hook(0x563BBA, 7, "80 3D 3F FB A0 00 00", ParticleGate, "fixed: render particle gate");
    fix::RetargetCall(0x4A5C70, kGameProcess, (void*)Process, "fixed: world ticks");
    fix::RetargetCall(0x4A5D15, kConstruct, (void*)ConstructHook, "fixed: camera presentation");
    fix::RetargetCall(0x4A5D1A, kPreRender, (void*)PreRenderHook, "fixed: entity presentation");
    if (s_mouseLayout && !fix::RetargetCall(0x4AD73D, 0x5FEEF0, (void*)MouseStateHook, "fixed: mouse look input"))
        s_mouseLayout = false;
    if (s_particleLayout &&
        !(fix::Hook(0x564785, 5, "A3 34 F0 97 00", ParticleAdded, "fixed: particle serials") &&
          fix::RetargetCall(0x4A6412, 0x5607B0, (void*)ParticleRenderHook, "fixed: particle presentation")))
        s_particleLayout = false;

    // Water waves read the millisecond clock directly; give them a presentation clock while drawing.
    static const struct {
        uintptr_t at;
        const char* pattern;
    } kWave[] = {{0x5BB9C7, "8B 35 34 3B 97 00"}, {0x5BBE07, "8B 15 34 3B 97 00"}, {0x5BD570, "8B 0D 34 3B 97 00"},
                 {0x5BEF12, "8B 35 34 3B 97 00"}, {0x5C168B, "8B 15 34 3B 97 00"}, {0x5C2867, "8B 15 34 3B 97 00"},
                 {0x5C2B54, "8B 0D 34 3B 97 00"}};
    for (auto& w : kWave) fix::Redirect(w.at, w.pattern, 0x973B34, &s_waveMs, "fixed: wave clock");
    s_waveMs = game::TimeInMs;
    fx::Install(&s_waveMs);

    // Idle goes through IdleHook last, once everything it relies on is in place.
    mem::Jmp(game::Idle, (void*)IdleHook, 7);
    Log("fixed: installed (30 Hz world; root poses, ped/cutscene skins, vehicle parts, particles, waves, world "
        "effects, target-relative follow camera and real-time mouse look)");
    return true;
}
}  // namespace fixed
