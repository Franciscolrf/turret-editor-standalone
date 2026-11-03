#pragma once
// ============================================================================
// Turret Editor - Misc. Bulk-edit the settings of your tribe's turrets inside
// a radius measured in foundations: Range (Low/Medium/High) and Targeting (the
// six AI modes).
//
// HOW THE GAME DOES IT
//   ARK's turret wheel does not write the replicated fields directly. It sends
//   PlayerController.ServerProcessNetExecCommand(ForActor = turret, Command,
//   FNetExecParams) and the server's turret handler cycles/sets the setting,
//   which then replicates back:
//       RangeSetting   (byte, 0 Low / 1 Medium / 2 High)
//       AISetting      (byte, 0..5 targeting mode)
//       TargetingRanges (three floats: the distances for Low/Medium/High)
//
//   The command NAMES are Blueprint FNames inside ShooterGame's turret graph,
//   so they are not known ahead of time (and differ between builds). This
//   module therefore LEARNS them: change a turret setting once with ARK's own
//   wheel while Learn is on, and captures the ServerMultiUse action and the
//   resulting property value. Since each action selects one concrete option,
//   a separate action is retained for each learned setting value.
//
// FOUNDATIONS
//   ARK's own ranges (Low 20 m ~ 6 foundations, Medium 53 m ~ 14, High 81 m ~
//   21) line up with 1 foundation = 400 cm, which is what is used here.
//
// THREADING
//   Tick()/OnPreEvent() run on the game thread (DrawHUD / ProcessEvent hook).
//   DrawUI() runs on the render thread and only reads settings + snapshots.
// ============================================================================
#include <Global.h>
#include <CheatSettings.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>
#include "imgui.h"
#include "Client Manager/Modules/Engine/EngineRefs.h"

namespace TurretEditor
{
    using S = Settings::Turret;

    inline constexpr float     kFoundationCm = 400.f;   // 1 foundation = 4 m
    inline constexpr uint64_t  kScanMs       = 500;
    inline constexpr uint64_t  kSendGapMs    = 1200;    // wait for the server replication result before giving up
    inline constexpr uint64_t  kGlobalGapMs  = 450;     // ServerMultiUse is throttled at about one call per 350 ms
    inline constexpr uint64_t  kMenuStepGapMs = 350;    // allow the radial menu to process a stage on a later game tick
    inline constexpr uint64_t  kApplyMaxMs   = 300000;   // allow larger radii at the safe request rate
    inline constexpr uint64_t  kLearnWaitMs  = 250;
    inline constexpr uint64_t  kLearnDropMs  = 3000;

    inline bool Valid(const void* p)
    {
        const uintptr_t v = reinterpret_cast<uintptr_t>(p);
        return v > 0x10000 && v < 0x7FFFFFFFFFFF;
    }
    template <typename T> inline T& At(const void* b, uintptr_t o)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(b) + o);
    }

    // Fallbacks for this Store build; resolved through reflection first so a
    // different build keeps working (same approach as InfoHud).
    inline constexpr uintptr_t OFF_TEAM    = 0x0230;
    inline constexpr uintptr_t OFF_ROOT    = 0x0268;
    inline constexpr uintptr_t OFF_C2W_POS = 0x00F0;

    inline uint32_t g_offTeam = 0;
    inline uint32_t g_offRoot = 0;

    // Keep this editor independent from QuickPickup and the rest of the menu
    // modules. The property walk is the same reflection used by that module.
    inline uint32_t PropOffset(void* cls, const char* name)
    {
        int guard = 0;
        for (UStruct* st = reinterpret_cast<UStruct*>(cls); st && guard++ < 48; st = st->SuperField)
        {
            int fguard = 0;
            for (UField* f = st->Children; f && fguard++ < 8192; f = f->Next)
            {
                if (f->GetName() != name) continue;
                const uint32_t off = static_cast<uint32_t>(reinterpret_cast<UProperty*>(f)->Offset);
                if (off >= 0x20 && off < 0x8000) return off;
            }
        }
        return 0;
    }

    // Set by the host's input poller. This keeps the editor independent from
    // InputBridge and works with ARK's UWP CoreWindow input-thread handling.
    inline volatile bool g_f7Down = false;
    inline void SetF7Down(bool down) { g_f7Down = down; }

    inline uint32_t TeamOffset(const UObject* o)
    {
        if (!g_offTeam && Valid(o) && Valid(o->Class))
            g_offTeam = PropOffset(o->Class, "TargetingTeam");
        return g_offTeam ? g_offTeam : static_cast<uint32_t>(OFF_TEAM);
    }
    inline uint32_t RootOffset(const UObject* o)
    {
        if (!g_offRoot && Valid(o) && Valid(o->Class))
            g_offRoot = PropOffset(o->Class, "RootComponent");
        return g_offRoot ? g_offRoot : static_cast<uint32_t>(OFF_ROOT);
    }

    struct V3 { float x, y, z; };
    inline bool PosOf(const void* actor, V3& out)
    {
        const void* root = At<void*>(actor, RootOffset(reinterpret_cast<const UObject*>(actor)));
        if (!Valid(root)) return false;
        out = At<V3>(root, OFF_C2W_POS);
        return true;
    }
    inline float Dist2(const V3& a, const V3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    __declspec(noinline) inline bool Call(void* obj, UFunction* fn, void* parms)
    {
        if (!fn || !Valid(obj)) return false;
        __try { UObject::ProcessEvent(obj, fn, parms); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    inline std::string NameOf(const UObject* o)
    {
        if (!Valid(o) || !Valid(o->Class)) return std::string();
        return o->Class->GetName();
    }
    inline std::string ClassNameOf(const UObject* o)
    {
        if (!Valid(o) || !Valid(o->Class)) return std::string();
        return o->Class->GetName();
    }
    inline bool Has(const std::string& s, const char* sub)
    {
        return s.find(sub) != std::string::npos;
    }

    // ---- turret identity ----------------------------------------------------
    inline bool IsTurretActor(const UObject* o)
    {
        const UClass* turret = EngineRefs::g_refs.clsTurret;
        if (!Valid(o) || !Valid(o->Class) || !turret) return false;
        int depth = 0;
        for (UStruct* s = o->Class; Valid(s) && depth < 32; s = s->SuperField, depth++)
            if (s == reinterpret_cast<const UStruct*>(turret)) return true;
        return false;
    }

    enum Kind { KNone = 0, KAuto = 1, KHeavy = 2, KTek = 3, KPlant = 4 };
    inline int KindOf(const UObject* o)
    {
        static std::unordered_map<const void*, int> s_kind;
        if (!Valid(o) || !Valid(o->Class)) return KNone;
        auto it = s_kind.find(o->Class);
        if (it != s_kind.end()) return it->second;
        int k = KNone;
        if (IsTurretActor(o))
        {
            const std::string c = ClassNameOf(o);
            if (Has(c, "Plant"))      k = KPlant;
            else if (Has(c, "Tek"))   k = KTek;
            else if (Has(c, "Heavy")) k = KHeavy;
            else                      k = KAuto;
        }
        s_kind.emplace(o->Class, k);
        return k;
    }
    inline bool KindWanted(int k)
    {
        switch (k)
        {
        case KAuto:  return S::FilterAuto;
        case KHeavy: return S::FilterHeavy;
        case KTek:   return S::FilterTek;
        case KPlant: return S::FilterPlant;
        default:     return false;
        }
    }

    // ---- reflected offsets (per blueprint class) ----------------------------
    struct Props { uint32_t range = 0, ai = 0, ranges = 0; };
    inline std::unordered_map<const void*, Props> g_propsByClass;

    inline const Props& PropsOf(const UObject* o)
    {
        static Props s_empty;
        if (!Valid(o) || !Valid(o->Class)) return s_empty;
        auto it = g_propsByClass.find(o->Class);
        if (it != g_propsByClass.end()) return it->second;
        Props p;
        void* cls = reinterpret_cast<void*>(o->Class);
        p.range  = PropOffset(cls, "RangeSetting");
        p.ai     = PropOffset(cls, "AISetting");
        p.ranges = PropOffset(cls, "TargetingRanges");
        Props& slot = g_propsByClass[o->Class];
        slot = p;
        Logger::Log("[TurretEditor] %s: RangeSetting=0x%X AISetting=0x%X TargetingRanges=0x%X",
                    ClassNameOf(o).c_str(), p.range, p.ai, p.ranges);
        return slot;
    }

    // -1 = property not found.
    inline int ReadByte(const UObject* o, uint32_t off)
    {
        if (!off) return -1;
        __try
        {
            const int v = At<uint8_t>(o, off);
            return (v >= 0 && v < 64) ? v : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    inline int ReadRange(const UObject* o)
    {
        if (!Valid(o)) return -1;
        return ReadByte(o, PropsOf(o).range);
    }
    inline int ReadAI(const UObject* o)
    {
        if (!Valid(o)) return -1;
        return ReadByte(o, PropsOf(o).ai);
    }
    inline std::string FNameText(const FName& n)
    {
        if (n.ComparisonIndex <= 0) return std::string();
        return n.GetName();
    }

    // ---- learned wheel actions ---------------------------------------------
    // ARK can route turret-wheel actions through ServerMultiUse, or through
    // ServerProcessNetExecCommand on builds that use Blueprint net-exec.
    struct Cmd
    {
        bool    have = false;
        bool    multiUse = false;
        int32_t useIndex = -1;
        FName   name{};
        uint8_t block[0x50] = {};
        void*   learnedActor = nullptr;   // turret the sample came from (its pointer may be in the params)
        int     learnedValue = -1;        // property value right after the change
    };
    // ServerMultiUse indices select concrete menu options (for example,
    // "High"). Keep one action per turret kind and resulting enum value.
    // Slot 0 is unused; Kind values are KAuto..KPlant (1..4).
    inline std::array<std::array<Cmd, 3>, 5> g_cmdRange{}; // 0..2
    inline std::array<std::array<Cmd, 6>, 5> g_cmdAI{};    // 0..5
    struct MenuRoute { int count = 0; int32_t indices[8] = {}; };
    inline MenuRoute g_rangeRoute{}, g_targetRoute{};
    inline UFunction* g_pcClientDoMultiUse = nullptr; // captured from the live PC event
    inline UFunction* g_pcBPActivatedMultiUseSelection = nullptr;
    inline void* g_menuPathActor = nullptr;
    inline MenuRoute g_menuPath{};
    inline void* g_lastMultiUseActor = nullptr;
    inline int32_t g_lastMultiUseIndex = -1;
    inline uint64_t g_lastMultiUseAt = 0;
    inline bool s_sending = false;   // our own sends must not be re-learned
    inline void Save();
    inline const char* KindName(int k);
    inline void LogCandidateSignature(UFunction* fn);

    template <size_t N> inline int LearnedCount(const std::array<Cmd, N>& a)
    {
        int n = 0;
        for (const Cmd& c : a) if (c.have) ++n;
        return n;
    }
    template <size_t N, size_t M> inline int LearnedCount(const std::array<std::array<Cmd, N>, M>& a)
    {
        int n = 0;
        for (const auto& row : a) n += LearnedCount(row);
        return n;
    }

    inline void Forget()
    {
        g_cmdRange = {};
        g_cmdAI = {};
        g_rangeRoute = {}; g_targetRoute = {}; g_menuPath = {};
        Save();
        Logger::Log("[TurretEditor] forgot every learned turret command");
    }

    // ---- learning from the player's own wheel changes -----------------------
    struct Pending
    {
        void*     actor = nullptr;
        int       kind = KNone;
        bool      multiUse = false;
        int32_t   useIndex = -1;
        FName     name{};
        uint64_t  at = 0;
        int       preRange = -1, preAI = -1;
        MenuRoute route{};
        uint8_t   block[0x50] = {};
    };
    inline std::vector<Pending> g_pending;
    inline std::vector<int32_t> g_seenCmdIdx;
    inline std::vector<int32_t> g_seenCandidateFnIdx;
    inline std::vector<int32_t> g_seenRpcCmdIdx;
    inline std::vector<int32_t> g_seenMultiUseIdx;
    inline std::vector<int32_t> g_seenFlowFnIdx;
    inline int g_invalidRpcParamLogs = 0;
    inline int g_flowTraceLogs = 0;
    struct FlowFrame { UFunction* fn = nullptr; void* object = nullptr; uint64_t id = 0; };
    // Do not use C++ thread_local here: the isolated manual mapper does not
    // initialize the DLL's PE TLS directory. ProcessEvent is observed on the
    // game thread; a regular module-global stack safely tracks nested calls.
    inline std::vector<FlowFrame> g_flowStack;
    inline volatile LONG64 g_flowEventSequence = 0;

    inline bool SeenCmd(int32_t idx)
    {
        for (int32_t i : g_seenCmdIdx) if (i == idx) return true;
        g_seenCmdIdx.push_back(idx);
        return false;
    }

    inline bool RelevantCandidateFunction(const std::string& n)
    {
        return Has(n, "NetExec") || Has(n, "ExecCommand") || Has(n, "Turret") ||
               Has(n, "Targeting") || Has(n, "RangeSetting") ||
               Has(n, "Setting") || Has(n, "Handle") || Has(n, "MultiUse") || Has(n, "CopyCustom");
    }

    inline void LogCandidateSignature(UFunction* fn)
    {
        if (!Valid(fn)) return;
        Logger::Log("[TurretEditor] candidate signature: fn=%p flags=0x%08X parms=%d size=%u rpc=%u",
                    fn, fn->FunctionFlags, (int)fn->NumParms, (unsigned)fn->ParmsSize, (unsigned)fn->RPCId);
        int count = 0;
        for (UField* f = fn->Children; Valid(f) && count < 24; f = f->Next, ++count)
        {
            UObject* prop = reinterpret_cast<UObject*>(f);
            const std::string name = prop->GetName();
            const std::string type = ClassNameOf(prop);
            const uint32_t off = static_cast<uint32_t>(reinterpret_cast<UProperty*>(f)->Offset);
            const int32_t size = reinterpret_cast<UProperty*>(f)->ElementSize;
            Logger::Log("[TurretEditor]   param %s type=%s offset=0x%X elem=%d",
                        name.c_str(), type.c_str(), off, size);
        }
    }

    inline bool SeenCandidateFunction(int32_t idx)
    {
        for (int32_t i : g_seenCandidateFnIdx) if (i == idx) return true;
        if (g_seenCandidateFnIdx.size() >= 64) return true;
        g_seenCandidateFnIdx.push_back(idx);
        return false;
    }

    inline bool SeenRpcCommand(int32_t idx)
    {
        for (int32_t i : g_seenRpcCmdIdx) if (i == idx) return true;
        if (g_seenRpcCmdIdx.size() >= 64) return true;
        g_seenRpcCmdIdx.push_back(idx);
        return false;
    }

    inline bool SeenMultiUseIndex(int32_t idx)
    {
        for (int32_t i : g_seenMultiUseIdx) if (i == idx) return true;
        if (g_seenMultiUseIdx.size() >= 64) return true;
        g_seenMultiUseIdx.push_back(idx);
        return false;
    }

    inline bool SeenFlowFunction(int32_t idx)
    {
        for (int32_t i : g_seenFlowFnIdx) if (i == idx) return true;
        g_seenFlowFnIdx.push_back(idx);
        return false;
    }

    inline void AddPending(void* actor, const FName& name, bool multiUse, int32_t useIndex,
                           const void* parms)
    {
        for (size_t i = g_pending.size(); i-- > 0;)
        {
            const Pending& old = g_pending[i];
            const bool same = old.multiUse == multiUse &&
                (multiUse ? old.useIndex == useIndex : old.name.ComparisonIndex == name.ComparisonIndex);
            if (same) g_pending.erase(g_pending.begin() + i);
        }
        if (g_pending.size() > 64) g_pending.clear();

        UObject* a = reinterpret_cast<UObject*>(actor);
        Pending p;
        p.actor = actor;
        p.kind = KindOf(a);
        p.multiUse = multiUse;
        p.useIndex = useIndex;
        p.name = name;
        p.at = GetTickCount64();
        p.preRange = ReadRange(a);
        p.preAI = ReadAI(a);
        if (parms) memcpy(p.block, parms, multiUse ? 0x0C : sizeof(p.block));
        if (multiUse) memset(p.block + 0x0C, 0, sizeof(p.block) - 0x0C);
        else memset(p.block + 0x10 + 0x30, 0, 0x10); // StringParam1 isn't ours to keep
        g_pending.push_back(p);
    }

    // Called from the ProcessEvent hook, before the original. Never skips the
    // event: the player's own change must still reach the server.
    inline void OnPreEvent(void* obj, UFunction* fn, void* parms)
    {
        if (!fn) return;
        const std::string fnName = fn->GetName();
        const bool traceFlow = fnName == "BPActivatedMultiUseSelection" ||
            fnName == "ClientDoMultiUse" || fnName == "BPClientDoMultiUse" ||
            fnName == "ServerMultiUse" || fnName == "BPTryMultiUse";
        if (traceFlow)
        {
            if (!SeenFlowFunction(fn->InternalIndex)) LogCandidateSignature(fn);
            if (S::Learn || s_sending)
            {
                void* arg0 = parms && fn->ParmsSize >= 8 ? *reinterpret_cast<void**>(parms) : nullptr;
                const int32_t arg1 = parms && fn->ParmsSize >= 12
                    ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(parms) + 8) : -1;
                void* target = (fnName == "BPTryMultiUse" || fnName == "BPClientDoMultiUse") ? obj : arg0;
                const bool turretTarget = Valid(target) && IsTurretActor(reinterpret_cast<UObject*>(target));
                const int range = turretTarget ? ReadRange(reinterpret_cast<UObject*>(target)) : -1;
                const int ai = turretTarget ? ReadAI(reinterpret_cast<UObject*>(target)) : -1;
                const uint64_t id = static_cast<uint64_t>(InterlockedIncrement64(&g_flowEventSequence));
                g_flowStack.push_back({ fn, obj, id });
                char bytes[257] = {};
                const unsigned byteCount = parms ? (std::min)(static_cast<unsigned>(fn->ParmsSize), 96u) : 0u;
                for (unsigned i = 0; i < byteCount; ++i)
                    sprintf_s(bytes + i * 2, sizeof(bytes) - i * 2, "%02X", reinterpret_cast<const uint8_t*>(parms)[i]);
                Logger::Log("[TurretEditor] FLOW #%llu PRE ms=%llu %s obj=%p class=%s flags=0x%08X parms=%u retOff=%u arg0=%p(%s) useIndex=%d target=%p(%s) range=%d ai=%d bytes=%s learn=%d applying=%d",
                            id, GetTickCount64(), fnName.c_str(), obj,
                            ClassNameOf(reinterpret_cast<UObject*>(obj)).c_str(), fn->FunctionFlags,
                            (unsigned)fn->ParmsSize, (unsigned)fn->ReturnValueOffset, arg0,
                            Valid(arg0) ? ClassNameOf(reinterpret_cast<UObject*>(arg0)).c_str() : "null",
                            arg1, target, turretTarget ? ClassNameOf(reinterpret_cast<UObject*>(target)).c_str() : "n/a",
                            range, ai, bytes, S::Learn ? 1 : 0, s_sending ? 1 : 0);
            }
        }
        if (!EngineRefs::Ready()) return;
        if (fnName == "BPActivatedMultiUseSelection")
            g_pcBPActivatedMultiUseSelection = fn;
        // The manual radial-menu flow issues ClientDoMultiUse navigation
        // calls before its ServerMultiUse action. Keep that short prefix so
        // Apply can reproduce the same route instead of replaying only the
        // terminal action index.
        if (fnName == "ClientDoMultiUse" && !s_sending && parms && fn->ParmsSize >= 12)
        {
            g_pcClientDoMultiUse = fn;
            void* target = *reinterpret_cast<void**>(parms);
            const int32_t idx = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(parms) + 8);
            const uint64_t now = GetTickCount64();
            const bool isPostAction = target == g_lastMultiUseActor && idx == g_lastMultiUseIndex &&
                                      now - g_lastMultiUseAt < 1500;
            if (Valid(target) && IsTurretActor(reinterpret_cast<UObject*>(target)) && !isPostAction)
            {
                if (g_menuPathActor != target) { g_menuPathActor = target; g_menuPath = {}; }
                if (g_menuPath.count < (int)_countof(g_menuPath.indices))
                    g_menuPath.indices[g_menuPath.count++] = idx;
                Logger::Log("[TurretEditor] captured menu step %d for %s (%d/8)", idx,
                            ClassNameOf(reinterpret_cast<UObject*>(target)).c_str(), g_menuPath.count);
            }
        }
        if (s_sending)
            return;
        if (!S::Learn) return;
        const bool multiUseCall = fn == EngineRefs::g_refs.pcServerMultiUse || fnName == "ServerMultiUse";
        if (multiUseCall)
        {
            // QuickPickup's reflected signature confirms { ForObject, UseIndex }.
            if (!parms) return;
            void* target = *reinterpret_cast<void**>(parms);
            const int32_t useIndex = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(parms) + 8);
            if (!Valid(target) || !IsTurretActor(reinterpret_cast<UObject*>(target)))
            {
                if (Valid(target) && !SeenCandidateFunction(fn->InternalIndex))
                    Logger::Log("[TurretEditor] ServerMultiUse index %d targeted non-turret class '%s'",
                                useIndex, ClassNameOf(reinterpret_cast<UObject*>(target)).c_str());
                return;
            }
            if (!SeenMultiUseIndex(useIndex))
                Logger::Log("[TurretEditor] saw turret ServerMultiUse index %d on %s; before range=%d ai=%d",
                            useIndex, ClassNameOf(reinterpret_cast<UObject*>(target)).c_str(),
                            ReadRange(reinterpret_cast<UObject*>(target)), ReadAI(reinterpret_cast<UObject*>(target)));
            AddPending(target, FName{}, true, useIndex, parms);
            if (!g_menuPath.count || g_menuPathActor != target)
                Logger::Log("[TurretEditor] manual action %d has no captured ClientDoMultiUse menu prefix", useIndex);
            else
            {
                g_pending.back().route = g_menuPath;
                Logger::Log("[TurretEditor] attached %d menu steps to ServerMultiUse %d", g_menuPath.count, useIndex);
            }
            g_menuPath = {}; g_menuPathActor = nullptr;
            g_lastMultiUseActor = target; g_lastMultiUseIndex = useIndex; g_lastMultiUseAt = GetTickCount64();
            return;
        }

        if (fn != EngineRefs::g_refs.pcNetExecCommand)
        {
            if (RelevantCandidateFunction(fnName) && !SeenCandidateFunction(fn->InternalIndex))
            {
                Logger::Log("[TurretEditor] candidate event '%s' on '%s' (index %d), expected ServerProcessNetExecCommand=%p",
                            fnName.c_str(), ClassNameOf(reinterpret_cast<UObject*>(obj)).c_str(),
                            fn->InternalIndex, EngineRefs::g_refs.pcNetExecCommand);
                LogCandidateSignature(fn);
            }
            return;
        }

        void* forActor = *reinterpret_cast<void**>(parms);
        if (!Valid(forActor))
        {
            if (g_invalidRpcParamLogs++ < 8)
                Logger::Log("[TurretEditor] ServerProcessNetExecCommand matched but ForActor pointer is invalid (%p)", forActor);
            return;
        }
        UObject* a = reinterpret_cast<UObject*>(forActor);

        const FName cmd = *reinterpret_cast<FName*>(reinterpret_cast<uint8_t*>(parms) + 8);
        if (cmd.ComparisonIndex <= 0)
        {
            if (g_invalidRpcParamLogs++ < 8)
                Logger::Log("[TurretEditor] ServerProcessNetExecCommand matched for '%s' but FName at +0x08 is invalid (%d)",
                            ClassNameOf(a).c_str(), cmd.ComparisonIndex);
            return;
        }
        const bool firstRpc = !SeenRpcCommand(cmd.ComparisonIndex);
        if (firstRpc)
            Logger::Log("[TurretEditor] ServerProcessNetExecCommand: ForActor=%s command='%s' index=%d",
                        ClassNameOf(a).c_str(), FNameText(cmd).c_str(), cmd.ComparisonIndex);

        if (!IsTurretActor(a))
        {
            // Diagnostic: some builds put the turret in a parameter and the
            // player controller in ForActor. Say so once, so the log explains
            // why nothing is being learned.
            bool foundTurretParam = false;
            for (int off = 0x10; off + 8 <= 0x50; off += 8)
            {
                UObject* p = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(parms) + off);
                if (Valid(p) && IsTurretActor(p))
                {
                    foundTurretParam = true;
                    if (firstRpc)
                        Logger::Log("[TurretEditor] command '%s' targets a turret through a parameter, not ForActor - please report this log",
                                    FNameText(cmd).c_str());
                    break;
                }
            }
            if (!foundTurretParam && firstRpc)
                Logger::Log("[TurretEditor] command '%s' ForActor class '%s' is not a turret; no turret actor in scanned params",
                            FNameText(cmd).c_str(), ClassNameOf(a).c_str());
            return;
        }

        AddPending(forActor, cmd, false, -1, parms);

        if (!SeenCmd(cmd.ComparisonIndex))
            Logger::Log("[TurretEditor] saw turret command '%s' (index %d) - watching which setting it changes",
                        FNameText(cmd).c_str(), cmd.ComparisonIndex);
    }

    // Log the exit side of every relevant ProcessEvent, preserving nested order
    // with the id assigned by OnPreEvent. This captures BPTry's return and the
    // property values after each Blueprint event.
    inline void OnPostEvent(void* obj, UFunction* fn, void* parms)
    {
        if (!fn) return;
        const std::string fnName = fn->GetName();
        const bool traceFlow = fnName == "BPActivatedMultiUseSelection" ||
            fnName == "ClientDoMultiUse" || fnName == "BPClientDoMultiUse" ||
            fnName == "ServerMultiUse" || fnName == "BPTryMultiUse";
        if (!traceFlow) return;
        uint64_t id = 0;
        for (size_t i = g_flowStack.size(); i-- > 0;)
            if (g_flowStack[i].fn == fn && g_flowStack[i].object == obj)
            { id = g_flowStack[i].id; g_flowStack.erase(g_flowStack.begin() + i); break; }
        if (!id && !S::Learn && !s_sending) return;
        void* arg0 = parms && fn->ParmsSize >= 8 ? *reinterpret_cast<void**>(parms) : nullptr;
        const int32_t useIndex = parms && fn->ParmsSize >= 12
            ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(parms) + 8) : -1;
        void* target = (fnName == "BPTryMultiUse" || fnName == "BPClientDoMultiUse") ? obj : arg0;
        const bool turretTarget = Valid(target) && IsTurretActor(reinterpret_cast<UObject*>(target));
        const int range = turretTarget ? ReadRange(reinterpret_cast<UObject*>(target)) : -1;
        const int ai = turretTarget ? ReadAI(reinterpret_cast<UObject*>(target)) : -1;
        int returnByte = -1;
        if (parms && fn->ReturnValueOffset < fn->ParmsSize)
            returnByte = reinterpret_cast<uint8_t*>(parms)[fn->ReturnValueOffset];
        char bytes[257] = {};
        const unsigned byteCount = parms ? (std::min)(static_cast<unsigned>(fn->ParmsSize), 96u) : 0u;
        for (unsigned i = 0; i < byteCount; ++i)
            sprintf_s(bytes + i * 2, sizeof(bytes) - i * 2, "%02X", reinterpret_cast<const uint8_t*>(parms)[i]);
        Logger::Log("[TurretEditor] FLOW #%llu POST ms=%llu %s obj=%p arg0=%p useIndex=%d target=%p(%s) range=%d ai=%d retOff=%u retByte=%d bytes=%s learn=%d applying=%d",
                    id, GetTickCount64(), fnName.c_str(), obj, arg0, useIndex, target,
                    turretTarget ? ClassNameOf(reinterpret_cast<UObject*>(target)).c_str() : "n/a",
                    range, ai, (unsigned)fn->ReturnValueOffset, returnByte, bytes,
                    S::Learn ? 1 : 0, s_sending ? 1 : 0);
    }

    inline void LearnCmd(Cmd& dst, const Pending& p, const char* what, int value)
    {
        dst.have = true;
        dst.multiUse = p.multiUse;
        dst.useIndex = p.useIndex;
        dst.name = p.name;
        memcpy(dst.block, p.block, sizeof(dst.block));
        dst.learnedActor = p.actor;
        dst.learnedValue = value;
        MenuRoute& route = strcmp(what, "range") == 0 ? g_rangeRoute : g_targetRoute;
        if (p.route.count > 0)
        {
            route = p.route;
            Logger::Log("[TurretEditor] learned %s menu route with %d ClientDoMultiUse steps",
                        what, route.count);
        }
        if (p.multiUse)
            Logger::Log("[TurretEditor] learned %s %s: ServerMultiUse index %d -> %d",
                        KindName(p.kind), what, p.useIndex, value);
        else
            Logger::Log("[TurretEditor] learned %s: net-exec '%s' -> %d", what, FNameText(p.name).c_str(), value);
        Save();
    }

    inline void ProcessPending(uint64_t now)
    {
        for (size_t i = g_pending.size(); i-- > 0;)
        {
            Pending& p = g_pending[i];
            if (now - p.at < kLearnWaitMs) continue;
            UObject* a = reinterpret_cast<UObject*>(p.actor);
            if (!Valid(a) || !IsTurretActor(a)) { g_pending.erase(g_pending.begin() + i); continue; }

            const int r = ReadRange(a), ai = ReadAI(a);
            const bool rCh = (r >= 0 && p.preRange >= 0 && r != p.preRange);
            const bool aCh = (ai >= 0 && p.preAI >= 0 && ai != p.preAI);
            const int changes = (rCh ? 1 : 0) + (aCh ? 1 : 0);

            if (changes == 1)
            {
                if (p.kind > KNone && p.kind <= KPlant && rCh && r >= 0 && r < 3)
                    LearnCmd(g_cmdRange[p.kind][r], p, "range", r);
                else if (p.kind > KNone && p.kind <= KPlant && aCh && ai >= 0 && ai < 6)
                    LearnCmd(g_cmdAI[p.kind][ai], p, "targeting", ai);
                g_pending.erase(g_pending.begin() + i);
                continue;
            }
            if (changes > 1)
            {
                // Ambiguous (two settings moved at once): ignore this sample.
                g_pending.erase(g_pending.begin() + i);
                continue;
            }
            if (now - p.at > kLearnDropMs)
            {
                if (p.multiUse)
                    Logger::Log("[TurretEditor] ServerMultiUse index %d did not change range/targeting",
                                p.useIndex);
                else
                    Logger::Log("[TurretEditor] net-exec '%s' did not change range/targeting",
                                FNameText(p.name).c_str());
                g_pending.erase(g_pending.begin() + i);
            }
        }
    }

    // ---- turret sweep -------------------------------------------------------
    struct Ent { int32_t idx, serial, kind; V3 pos; };
    inline std::vector<Ent> g_turrets;
    inline uint64_t g_lastSweep = 0;
    inline UClass* g_hudTargetClass = nullptr;
    inline uint32_t g_hudTargetOffset = 0;
    inline UObject* g_lastTargetedActor = nullptr;

    // Resolve the game's current use-prompt target by reflection (the same
    // weak pointer ARK uses for its normal multi-use interaction).
    inline UObject* ReadHudTarget(AHUD* hud)
    {
        UObject* hudObject = reinterpret_cast<UObject*>(hud);
        if (!Valid(hudObject) || !Valid(hudObject->Class)) return nullptr;
        if (g_hudTargetClass != hudObject->Class)
        {
            g_hudTargetClass = hudObject->Class;
            g_hudTargetOffset = 0;
            for (UStruct* st = hudObject->Class; Valid(st); st = st->SuperField)
            {
                int guard = 0;
                for (UField* f = st->Children; Valid(f) && guard++ < 8192; f = f->Next)
                    if (f->GetName() == "LastTargetedActor")
                    {
                        const int off = reinterpret_cast<UProperty*>(f)->Offset;
                        if (off >= 0x20 && off < 0x8000) g_hudTargetOffset = static_cast<uint32_t>(off);
                        break;
                    }
                if (g_hudTargetOffset) break;
            }
            Logger::Log("[TurretEditor] HUD %s LastTargetedActor offset=0x%X",
                        ClassNameOf(hudObject).c_str(), g_hudTargetOffset);
        }
        if (!g_hudTargetOffset) return nullptr;
        const int32_t* weak = reinterpret_cast<const int32_t*>(reinterpret_cast<uintptr_t>(hud) + g_hudTargetOffset);
        auto& objs = UObject::GetGlobalObjects();
        if (weak[0] <= 0 || weak[0] >= objs.Count()) return nullptr;
        FUObjectItem* item = objs.GetItemByIndex(weak[0]);
        if (!item || item->SerialNumber != weak[1] || !Valid(item->Object)) return nullptr;
        return item->Object;
    }

    inline void LogHudTarget(UObject* target)
    {
        const std::string name = Valid(target) ? ClassNameOf(target) : "none";
        Logger::Log("[TurretEditor] HUD target changed: %p (%s)", target, name.c_str());
    }

    inline UObject* Resolve(int32_t idx, int32_t serial)
    {
        auto& objs = UObject::GetGlobalObjects();
        if (idx <= 0 || idx >= objs.Count()) return nullptr;
        FUObjectItem* item = objs.GetItemByIndex(idx);
        if (!item || item->SerialNumber != serial || !Valid(item->Object)) return nullptr;
        return item->Object;
    }

    inline void Sweep(const V3& me, int32_t myTeam, float radiusCm)
    {
        g_turrets.clear();
        UWorld* world = UWorld::Get();
        if (!Valid(world) || !Valid(world->PersistentLevel) || !EngineRefs::g_refs.clsTurret) return;
        TArray<AActor*>& actors = world->PersistentLevel->Actors;
        const int count = actors.Count();
        if (count <= 0 || count > 400000) return;

        const float r2 = radiusCm * radiusCm;
        auto& objs = UObject::GetGlobalObjects();
        for (int i = 0; i < count; i++)
        {
            const UObject* a = reinterpret_cast<const UObject*>(actors[i]);
            if (!Valid(a) || !Valid(a->Class)) continue;
            const int k = KindOf(a);
            if (!k || !KindWanted(k)) continue;
            if (At<int32_t>(a, TeamOffset(a)) != myTeam) continue;
            V3 p;
            if (!PosOf(a, p) || Dist2(p, me) > r2) continue;
            const int32_t idx = a->InternalIndex;
            FUObjectItem* item = objs.GetItemByIndex(idx);
            if (!item || item->Object != a) continue;
            g_turrets.push_back({ idx, item->SerialNumber, k, p });
            if (g_turrets.size() >= 2048) break;
        }
    }

    struct ApplySequence
    {
        bool active = false;
        int32_t objectIndex = -1, serial = -1;
        Cmd command{};
        MenuRoute route{};
        int desired = -1, before = -1, kind = KNone;
        bool targeting = false;
        float distanceMeters = 0.f;
        int nextStep = 0;
        uint64_t nextAt = 0;
    };
    inline ApplySequence g_applySequence{};

    // ---- sending ------------------------------------------------------------
    inline bool SendCmd(void* pc, void* actor, const Cmd& c, int value, int current, int kind,
                        const char* setting, float distanceMeters)
    {
        if (!c.have || !Valid(pc) || !Valid(actor)) return false;
        if (c.multiUse)
        {
            if (!EngineRefs::g_refs.pcServerMultiUse)
            {
                Logger::Log("[TurretEditor] cannot send %s %s: ServerMultiUse ref is null",
                            KindName(kind), setting);
                return false;
            }
            const MenuRoute& route = strcmp(setting, "Range") == 0 ? g_rangeRoute : g_targetRoute;
            if (route.count <= 0 || !g_pcClientDoMultiUse || !g_pcBPActivatedMultiUseSelection)
            {
                Logger::Log("[TurretEditor] cannot apply %s: radial-menu route/functions not captured (steps=%d Activate=%p ClientDo=%p); enable Learn, manually change one %s option, then retry",
                            setting, route.count, g_pcBPActivatedMultiUseSelection, g_pcClientDoMultiUse, setting);
                return false;
            }
            const int32_t idx = reinterpret_cast<UObject*>(actor)->InternalIndex;
            auto& objs = UObject::GetGlobalObjects();
            FUObjectItem* item = idx > 0 && idx < objs.Count() ? objs.GetItemByIndex(idx) : nullptr;
            if (!item || item->Object != actor) return false;
            g_applySequence.active = true;
            g_applySequence.objectIndex = idx;
            g_applySequence.serial = item->SerialNumber;
            g_applySequence.command = c;
            g_applySequence.route = route;
            g_applySequence.desired = value;
            g_applySequence.before = current;
            g_applySequence.kind = kind;
            g_applySequence.targeting = strcmp(setting, "Range") != 0;
            g_applySequence.distanceMeters = distanceMeters;
            g_applySequence.nextStep = 0;
            g_applySequence.nextAt = 0;
            Logger::Log("[TurretEditor] queued radial route %s on %s: %d menu steps, action=%d; stages separated by %llu ms",
                        setting, KindName(kind), route.count, c.useIndex, kMenuStepGapMs);
            return true;
        }
        if (!EngineRefs::g_refs.pcNetExecCommand) return false;
        uint8_t block[0x50];
        memcpy(block, c.block, sizeof(block));
        *reinterpret_cast<void**>(block)   = actor;
        *reinterpret_cast<FName*>(block + 8) = c.name;
        // The captured params may reference the turret the sample came from,
        // and may carry the new value as an integer. Only whole 8-byte slots
        // that look like a plain number are patched, so heap pointers are safe.
        for (int off = 0x10; off + 8 <= 0x50; off += 8)
        {
            uint64_t& q = *reinterpret_cast<uint64_t*>(block + off);
            if (c.learnedActor && q == reinterpret_cast<uint64_t>(c.learnedActor))
                q = reinterpret_cast<uint64_t>(actor);
            else if (c.learnedValue > 0 && q == static_cast<uint64_t>(c.learnedValue))
                q = static_cast<uint64_t>(value);
        }
        s_sending = true;
        const bool called = Call(pc, EngineRefs::g_refs.pcNetExecCommand, block);
        s_sending = false;
        return called;
    }

    struct Sent
    {
        uint64_t at[2] = {};
        int lastValue[2] = { -1, -1 };
        uint8_t stalled[2] = {};
        bool giveUp[2] = {};
    };
    inline std::unordered_map<uint64_t, Sent> g_lastSent;
    inline uint64_t g_lastSend = 0;

    inline bool ApplyActive = false;
    inline uint64_t ApplyStart = 0;
    inline int ApplySent = 0, ApplySkipped = 0, ApplyMissing = 0;

    inline void StartApply()
    {
        ApplyActive = true;
        ApplyStart = GetTickCount64();
        ApplySent = ApplySkipped = ApplyMissing = 0;
        g_applySequence = {};
        g_lastSent.clear();
        g_lastSend = 0;
        Logger::Log("[TurretEditor] apply start: radius %.1f foundations, range %d, ai %d, learned %d/%d values",
                    S::RangeFoundations, S::RangeSetting, S::AISetting,
                    LearnedCount(g_cmdRange), LearnedCount(g_cmdAI));
    }

    inline void FinishApply(const char* why)
    {
        ApplyActive = false;
        g_applySequence = {};
        char b[192];
        sprintf_s(b, "Turret Editor: %s - %d sent, %d skipped", why, ApplySent, ApplySkipped);
        Logger::Log("[TurretEditor] %s (sent %d, skipped %d, missing %d)",
                    b, ApplySent, ApplySkipped, ApplyMissing);
    }

    inline void TickApply(uint64_t now)
    {
        if (!ApplyActive) return;

        // Run one menu stage per game tick. The manual trace shows the game
        // processes a radial selection before the subsequent server action;
        // dispatching all events synchronously only opens the wheel and loses
        // that state transition.
        if (g_applySequence.active)
        {
            ApplySequence& q = g_applySequence;
            if (now < q.nextAt) return;
            UObject* actor = Resolve(q.objectIndex, q.serial);
            ULocalPlayer* seqLp = ULocalPlayer::Get();
            APlayerController* seqPc = seqLp ? seqLp->PlayerController : nullptr;
            if (!Valid(actor) || !Valid(seqPc) || !EngineRefs::g_refs.pcServerMultiUse ||
                !g_pcClientDoMultiUse || !g_pcBPActivatedMultiUseSelection)
            {
                Logger::Log("[TurretEditor] radial route cancelled: turret/controller/function went invalid");
                q = {};
                return;
            }
            s_sending = true;
            if (q.nextStep < q.route.count)
            {
                const int step = q.nextStep;
                struct { void* ForObject; int32_t UseIndex; int32_t Pad; } nav{ actor, q.route.indices[step], 0 };
                const bool ok = Call(seqPc, g_pcBPActivatedMultiUseSelection, nullptr) &&
                                Call(seqPc, g_pcClientDoMultiUse, &nav);
                s_sending = false;
                if (!ok)
                {
                    Logger::Log("[TurretEditor] radial navigation failed at step %d/%d index=%d",
                                step + 1, q.route.count, q.route.indices[step]);
                    q = {};
                    return;
                }
                Logger::Log("[TurretEditor] radial navigation step %d/%d index=%d sent; waiting %llu ms",
                            step + 1, q.route.count, q.route.indices[step], kMenuStepGapMs);
                ++q.nextStep;
                q.nextAt = now + kMenuStepGapMs;
                return;
            }

            struct { void* ForObject; int32_t UseIndex; int32_t Pad; } action{ actor, q.command.useIndex, 0 };
            const bool activated = Call(seqPc, g_pcBPActivatedMultiUseSelection, nullptr);
            const bool serverCalled = activated && Call(seqPc, EngineRefs::g_refs.pcServerMultiUse, &action);
            const bool clientDone = Call(seqPc, g_pcClientDoMultiUse, &action);
            s_sending = false;
            Logger::Log("[TurretEditor] radial action %s on %s: steps=%d action=%d target=%d before=%d distance=%.1fm serverCalled=%d clientDone=%d",
                        q.targeting ? "Targeting" : "Range", KindName(q.kind), q.route.count,
                        q.command.useIndex, q.desired, q.before, q.distanceMeters,
                        serverCalled ? 1 : 0, clientDone ? 1 : 0);
            q = {};
            return;
        }

        ULocalPlayer* lp = ULocalPlayer::Get();
        APlayerController* pc = lp ? lp->PlayerController : nullptr;
        if (!Valid(pc)) { FinishApply("no controller"); return; }
        void* me = reinterpret_cast<void*>(ULocalPlayer::GetActor());
        if (!Valid(me)) { FinishApply("no character"); return; }
        V3 myPos{};
        if (!PosOf(reinterpret_cast<UObject*>(me), myPos)) { FinishApply("no player position"); return; }

        int mismatch = 0;
        for (const Ent& t : g_turrets)
        {
            UObject* a = Resolve(t.idx, t.serial);
            if (!a) continue;
            const uint64_t key = (uint64_t)(uint32_t)t.idx << 32 | (uint32_t)t.serial;
            Sent& s = g_lastSent[key];

            auto cmdFor = [](int kind, int setting, const auto& commands) -> const Cmd* {
                return kind > KNone && kind <= KPlant && setting >= 0 &&
                       setting < (int)commands[kind].size() && commands[kind][setting].have
                    ? &commands[kind][setting] : nullptr;
            };
            struct Want { const Cmd* cmd; int target, cur; };
            const Want wants[2] = {
                { cmdFor(t.kind, S::RangeSetting, g_cmdRange), S::RangeSetting, ReadRange(a) },
                { cmdFor(t.kind, S::AISetting, g_cmdAI),       S::AISetting,    ReadAI(a) },
            };
            for (int i = 0; i < 2; i++)
            {
                if (wants[i].target < 0) continue;
                if (!wants[i].cmd) { ApplyMissing++; continue; }
                if (wants[i].cur < 0) { ++ApplySkipped; continue; }
                if (wants[i].cur == wants[i].target) continue;
                if (s.giveUp[i]) continue;
                ++mismatch;
                if (now - s.at[i] < kSendGapMs) continue;
                if (now - g_lastSend < kGlobalGapMs) continue;
                if (s.at[i] != 0 && s.lastValue[i] == wants[i].cur)
                {
                    if (++s.stalled[i] >= 1)
                    {
                        s.giveUp[i] = true;
                        ++ApplySkipped;
                        Logger::Log("[TurretEditor] stopped repeating %s on turret %d: value stayed at %d after %u sends; learned action may select a fixed value",
                                    i == 0 ? "Range" : "Targeting", t.idx,
                                    wants[i].cur, (unsigned)s.stalled[i]);
                        continue;
                    }
                }
                else s.stalled[i] = 0;
                const char* settingName = i == 0 ? "Range" : "Targeting";
                const float distanceMeters = sqrtf(Dist2(myPos, t.pos)) / 100.f;
                if (!SendCmd(pc, a, *wants[i].cmd, wants[i].target, wants[i].cur,
                             t.kind, settingName, distanceMeters))
                {
                    s.giveUp[i] = true;
                    ++ApplySkipped;
                    continue;
                }
                s.at[i] = now;
                s.lastValue[i] = wants[i].cur;
                g_lastSend = now;
                ++ApplySent;
                if (g_applySequence.active) return; // serialize radial-menu transactions
            }
        }

        if (mismatch == 0)
        {
            if (ApplyMissing && !ApplySent)  FinishApply("requested option not learned yet");
            else if (ApplyMissing)           FinishApply("done, but some settings were not learned");
            else                             FinishApply("done");
            return;
        }
        if (now - ApplyStart > kApplyMaxMs)
            FinishApply("timed out - some turrets did not accept the change");
    }

    // ---- logging / diagnostics ---------------------------------------------
    __declspec(noinline) inline bool Interesting(const char* s)
    {
        const std::string n(s);
        if (n == "RangeSetting" || n == "AISetting" || n == "TargetingRanges") return true;
        if (Has(n, "Targeting") && (Has(n, "Range") || Has(n, "Setting") || Has(n, "Mode"))) return true;
        if (Has(n, "Turret") && (Has(n, "Range") || Has(n, "Setting") || Has(n, "Target"))) return true;
        if (Has(n, "Cycle") && Has(n, "Target")) return true;
        if (Has(n, "Exec")) return true;
        return false;
    }

    inline void DumpNames()
    {
        if (!FName::GNames) { Logger::Log("[TurretEditor] dump: GNames missing"); return; }
        const uintptr_t table = *reinterpret_cast<const uintptr_t*>(FName::GNames);
        if (table < 0x10000) { Logger::Log("[TurretEditor] dump: GNames table missing"); return; }
        int logged = 0;
        for (int32_t k = 0; k < 128; k++)
        {
            const uintptr_t chunk = reinterpret_cast<const uintptr_t*>(table)[k];
            if (chunk < 0x10000) break;
            __try
            {
                for (int32_t j = 0; j < 16384; j++)
                {
                    const uintptr_t e = reinterpret_cast<const uintptr_t*>(chunk)[j];
                    if (e < 0x10000) continue;
                    if (*reinterpret_cast<const int32_t*>(e) & 1) continue;   // wide name
                    const char* s = reinterpret_cast<const char*>(e + 0x10);
                    if (!Interesting(s)) continue;
                    Logger::Log("[TurretEditor] name %d: %s", k * 16384 + j, s);
                    if (++logged >= 160) return;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        Logger::Log("[TurretEditor] dump: %d interesting names", logged);
    }

    inline void DumpClass()
    {
        UObject* sample = nullptr;
        UWorld* world = UWorld::Get();
        if (Valid(world) && Valid(world->PersistentLevel))
        {
            TArray<AActor*>& actors = world->PersistentLevel->Actors;
            const int count = actors.Count();
            for (int i = 0; i < count && i < 400000; i++)
            {
                UObject* a = reinterpret_cast<UObject*>(actors[i]);
                if (Valid(a) && IsTurretActor(a)) { sample = a; break; }
            }
        }
        if (!sample)
        {
            Logger::Log("[TurretEditor] dump: no turret loaded/streamed - stand near one and dump again");
            DumpNames();
            return;
        }

        Logger::Log("[TurretEditor] dump: %s (%s)", ClassNameOf(sample).c_str(), sample->GetFullName().c_str());
        int depth = 0;
        for (UStruct* st = sample->Class; Valid(st) && depth < 32; st = st->SuperField, depth++)
        {
            Logger::Log("[TurretEditor] class chain %d: %s", depth, reinterpret_cast<UObject*>(st)->GetName().c_str());
            int n = 0;
            for (UField* f = st->Children; Valid(f) && n < 4000; f = f->Next, n++)
            {
                UObject* fo = reinterpret_cast<UObject*>(f);
                const std::string fn = fo->GetName();
                const std::string fc = ClassNameOf(fo);
                if (fc == "Function")
                {
                    if (Has(fn, "Target") || Has(fn, "Range") || Has(fn, "Setting") ||
                        Has(fn, "NetExec") || Has(fn, "Cycle"))
                        Logger::Log("[TurretEditor]   fn %s", fn.c_str());
                }
                else if (fc.find("Property") != std::string::npos)
                {
                    const uint32_t off = static_cast<uint32_t>(reinterpret_cast<UProperty*>(f)->Offset);
                    if (Has(fn, "Range") || Has(fn, "Target") || Has(fn, "Setting") ||
                        Has(fn, "AI") || Has(fn, "Bullet"))
                        Logger::Log("[TurretEditor]   prop %s +0x%X (%s)", fn.c_str(), off, fc.c_str());
                }
            }
        }

        const Props& p = PropsOf(sample);
        if (p.ranges)
        {
            const float* r = reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(sample) + p.ranges);
            Logger::Log("[TurretEditor] TargetingRanges: %.0f / %.0f / %.0f cm", r[0], r[1], r[2]);
        }
        Logger::Log("[TurretEditor] current: range=%d ai=%d",
                    ReadRange(sample), ReadAI(sample));
        DumpNames();
    }

    // ---- persistence --------------------------------------------------------
    inline bool g_loaded = false;
    inline void IniPath(wchar_t* out, size_t cap)
    {
        wchar_t* profile = nullptr;
        size_t len = 0;
        // Match Logger::Initialize(): packaged ARK can virtualize LOCALAPPDATA,
        // while USERPROFILE still resolves to the real per-user package tree.
        std::wstring base;
        if (_wdupenv_s(&profile, &len, L"USERPROFILE") == 0 && profile && *profile)
            base = profile;
        free(profile);
        if (base.empty())
        {
            wchar_t fallback[MAX_PATH] = {};
            const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", fallback, MAX_PATH);
            if (n && n < MAX_PATH) base = fallback;
        }
        if (base.empty()) base = L"C:\\Users\\Default";
        std::wstring p = base + L"\\AppData\\Local\\Packages\\StudioWildcard.4558480580BB9_1w2mm55455e38\\AC\\paidini\\turret_editor.ini";
        wcsncpy_s(out, cap, p.c_str(), _TRUNCATE);
    }
    inline void Save()
    {
        wchar_t path[MAX_PATH];
        IniPath(path, MAX_PATH);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            Logger::Log("[TurretEditor] cannot save settings: path=%ls error=%lu", path, GetLastError());
            return;
        }
        std::string b;
        char line[256];
        sprintf_s(line, "schema=3\nbuild=9622\nvisible=%d\nlearn=%d\nradius_milli=%d\nauto=%d\nheavy=%d\ntek=%d\nplant=%d\nrange=%d\nai=%d\n",
                  S::Visible ? 1 : 0, S::Learn ? 1 : 0, static_cast<int>(S::RangeFoundations * 1000.f),
                  S::FilterAuto ? 1 : 0, S::FilterHeavy ? 1 : 0, S::FilterTek ? 1 : 0, S::FilterPlant ? 1 : 0,
                  S::RangeSetting, S::AISetting);
        b += line;
        auto appendCommands = [&b](const char* setting, const auto& commands)
        {
            for (int kind = KAuto; kind <= KPlant; ++kind)
                for (size_t value = 0; value < commands[kind].size(); ++value)
                {
                    const Cmd& c = commands[kind][value];
                    const int index = c.have && c.multiUse ? c.useIndex : -1;
                    char entry[96];
                    sprintf_s(entry, "cmd_%s_%d_%zu=%d\n", setting, kind, value, index);
                    b += entry;
                }
        };
        appendCommands("range", g_cmdRange);
        appendCommands("target", g_cmdAI);
        auto appendRoute = [&b](const char* setting, const MenuRoute& route)
        {
            char entry[96];
            sprintf_s(entry, "%s_path_count=%d\n", setting, route.count);
            b += entry;
            for (int i = 0; i < route.count && i < (int)_countof(route.indices); ++i)
            {
                sprintf_s(entry, "%s_path_%d=%d\n", setting, i, route.indices[i]);
                b += entry;
            }
        };
        appendRoute("range", g_rangeRoute);
        appendRoute("target", g_targetRoute);
        DWORD w = 0;
        WriteFile(h, b.data(), static_cast<DWORD>(b.size()), &w, nullptr);
        CloseHandle(h);
    }

    inline bool IniReadInt(const char* data, const char* key, int& value)
    {
        const size_t keyLen = strlen(key);
        const char* line = data;
        while (line && *line)
        {
            const char* end = strpbrk(line, "\r\n");
            if (!end) end = line + strlen(line);
            if (static_cast<size_t>(end - line) > keyLen &&
                line[keyLen] == '=' && memcmp(line, key, keyLen) == 0)
            {
                value = atoi(line + keyLen + 1);
                return true;
            }
            line = *end ? end + 1 : nullptr;
            if (line && *(line - 1) == '\r' && *line == '\n') ++line;
        }
        return false;
    }

    inline void Load()
    {
        if (g_loaded) return;
        g_loaded = true;
        wchar_t path[MAX_PATH];
        IniPath(path, MAX_PATH);
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            Logger::Log("[TurretEditor] cannot open settings: path=%ls error=%lu", path, GetLastError());
            return;
        }
        char b[8192] = {};
        DWORD got = 0;
        ReadFile(h, b, sizeof(b) - 1, &got, nullptr);
        CloseHandle(h);
        b[got] = '\0';
        auto read = [&b](const char* key, int fallback)
        {
            int value = fallback;
            IniReadInt(b, key, value);
            return value;
        };
        const int vis = read("visible", S::Visible ? 1 : 0);
        const int learn = read("learn", S::Learn ? 1 : 0);
        const float rad = static_cast<float>(read("radius_milli", static_cast<int>(S::RangeFoundations * 1000.f))) / 1000.f;
        const int a = read("auto", S::FilterAuto), hv = read("heavy", S::FilterHeavy);
        const int tk = read("tek", S::FilterTek), pl = read("plant", S::FilterPlant);
        const int rs = read("range", S::RangeSetting), ai = read("ai", S::AISetting);
        S::Visible = vis != 0;
        S::Learn = learn != 0;
        if (rad >= 1.f && rad <= 60.f) S::RangeFoundations = rad;
        S::FilterAuto = a != 0; S::FilterHeavy = hv != 0; S::FilterTek = tk != 0; S::FilterPlant = pl != 0;
        if (rs >= -1 && rs <= 2) S::RangeSetting = rs;
        if (ai >= -1 && ai <= 5) S::AISetting = ai;
        const int schema = read("schema", 0);
        const bool compatible = (schema == 2 || schema == 3) && read("build", 0) == 9622;
        if (compatible)
        {
            auto loadCommands = [&b](const char* setting, auto& commands, int valueCount)
            {
                for (int kind = KAuto; kind <= KPlant; ++kind)
                    for (int value = 0; value < valueCount; ++value)
                    {
                        char key[64];
                        sprintf_s(key, "cmd_%s_%d_%d", setting, kind, value);
                        int index = -1;
                        if (IniReadInt(b, key, index) && index >= 0 && index <= 1000000)
                        {
                            Cmd& c = commands[kind][value];
                            c.have = true;
                            c.multiUse = true;
                            c.useIndex = index;
                            c.learnedValue = value;
                        }
                    }
            };
            loadCommands("range", g_cmdRange, 3);
            loadCommands("target", g_cmdAI, 6);
            if (schema >= 3)
            {
                auto loadRoute = [&b](const char* setting, MenuRoute& route)
                {
                    char key[64];
                    sprintf_s(key, "%s_path_count", setting);
                    int count = 0;
                    if (!IniReadInt(b, key, count) || count < 0 || count > (int)_countof(route.indices)) return;
                    route.count = count;
                    for (int i = 0; i < count; ++i)
                    {
                        sprintf_s(key, "%s_path_%d", setting, i);
                        int index = -1;
                        if (!IniReadInt(b, key, index) || index < 0 || index > 1000000)
                        { route = {}; return; }
                        route.indices[i] = index;
                    }
                };
                loadRoute("range", g_rangeRoute);
                loadRoute("target", g_targetRoute);
                Logger::Log("[TurretEditor] radial routes loaded: range=%d steps, targeting=%d steps",
                            g_rangeRoute.count, g_targetRoute.count);
            }
        }
        Logger::Log("[TurretEditor] loaded turret_editor.ini: learned commands %s (build UWP9622)",
                    compatible ? "restored" : "not found or incompatible; relearn required");
    }

    // ---- snapshots for the UI (game thread writes) --------------------------
    inline volatile LONG g_countAuto = 0, g_countHeavy = 0, g_countTek = 0, g_countPlant = 0;
    inline volatile LONG g_count = 0;

    inline void Reset()
    {
        g_turrets.clear();
        g_pending.clear();
        g_lastSent.clear();
        g_propsByClass.clear();
        g_seenCmdIdx.clear();
        g_offTeam = 0;
        g_offRoot = 0;
        g_lastSweep = 0;
        s_sending = false;
        ApplyActive = false;
        g_count = g_countAuto = g_countHeavy = g_countTek = g_countPlant = 0;
    }

    // ---- game thread --------------------------------------------------------
    inline void Tick(bool menuOpen, AHUD* hud = nullptr)
    {
        Load();
        if (Valid(hud))
        {
            UObject* target = ReadHudTarget(hud);
            if (target != g_lastTargetedActor)
            {
                g_lastTargetedActor = target;
                LogHudTarget(target);
            }
        }

        // Deferred reset requested from the render thread (world change), so
        // the vectors are only ever mutated here on the game thread.
        if (S::ResetRequested) { S::ResetRequested = false; Reset(); }

        // F7 toggles the window while the main menu is open.
        static bool s_f7 = false;
        const bool f7 = g_f7Down;
        if (menuOpen && f7 && !s_f7)
        {
            S::Visible = !S::Visible;
            Save();
        }
        s_f7 = f7;

        if (S::DumpRequested) { S::DumpRequested = false; DumpClass(); }
        if (S::ForgetRequested) { S::ForgetRequested = false; Forget(); }
        if (S::ApplyRequested && !ApplyActive) { S::ApplyRequested = false; StartApply(); }

        if (!EngineRefs::Ready()) return;
        const uint64_t now = GetTickCount64();
        ProcessPending(now);

        // No window and nothing to apply: don't sweep the world every frame.
        if (!S::Visible && !ApplyActive) return;

        ULocalPlayer* lp = ULocalPlayer::Get();
        APlayerController* pc = lp ? lp->PlayerController : nullptr;
        if (!Valid(pc)) return;
        void* me = nullptr;
        __try { me = reinterpret_cast<void*>(ULocalPlayer::GetActor()); }
        __except (EXCEPTION_EXECUTE_HANDLER) { me = nullptr; }
        if (!Valid(me)) return;
        V3 myPos;
        if (!PosOf(me, myPos)) return;
        const int32_t myTeam = At<int32_t>(me, TeamOffset(reinterpret_cast<const UObject*>(me)));
        if (myTeam <= 0) return;

        const float radiusCm = (std::max)(1.f, (std::min)(60.f, S::RangeFoundations)) * kFoundationCm;
        if (now - g_lastSweep >= kScanMs)
        {
            g_lastSweep = now;
            Sweep(myPos, myTeam, radiusCm);
            LONG a = 0, h = 0, t = 0, p = 0;
            for (const Ent& e : g_turrets)
            {
                if (e.kind == KAuto) ++a;
                else if (e.kind == KHeavy) ++h;
                else if (e.kind == KTek) ++t;
                else if (e.kind == KPlant) ++p;
            }
            g_countAuto = a; g_countHeavy = h; g_countTek = t; g_countPlant = p;
            g_count = static_cast<LONG>(g_turrets.size());
        }

        TickApply(now);
    }

    // ---- render thread ------------------------------------------------------
    inline const char* KindName(int k)
    {
        switch (k)
        {
        case KAuto:  return "Auto";
        case KHeavy: return "Heavy";
        case KTek:   return "Tek";
        case KPlant: return "Plant";
        default:     return "?";
        }
    }

    inline void DrawUI()
    {
        if (!S::Visible) return;
        ImGui::SetNextWindowSize(ImVec2(430.f, 500.f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Turret Editor (F7)", &S::Visible))
        {
            ImGui::End();
            return;
        }

        bool changed = false;
        ImGui::TextDisabled("Bulk-edit your tribe's turrets in a radius.");
            ImGui::TextDisabled("1 foundation = 4 m (ARK: High = 81 m ~ 21).");
            ImGui::TextDisabled("Radius finds candidate turrets; ARK still validates each multi-use action.");
        ImGui::Separator();

        changed |= ImGui::Checkbox("Learn commands from my turret changes", &S::Learn);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turn this on, close the editor with F7, and manually select each value you want to apply\n"
                              "using ARK's wheel. This captures the radial-menu path and the selected action.\n"
                              "Learned actions and paths are saved for this game build.");
        ImGui::TextDisabled("Radial menu routes: Range %d steps | Targeting %d steps",
                            g_rangeRoute.count, g_targetRoute.count);
        ImGui::TextDisabled("Learn a route once per setting; learn each value/type only if its count is incomplete.");
        ImGui::TextDisabled("Saved progress (Range / Targeting):");
        ImGui::Text("Auto:  %d/3 | %d/6", LearnedCount(g_cmdRange[KAuto]), LearnedCount(g_cmdAI[KAuto]));
        ImGui::Text("Heavy: %d/3 | %d/6", LearnedCount(g_cmdRange[KHeavy]), LearnedCount(g_cmdAI[KHeavy]));
        ImGui::Text("Tek:   %d/3 | %d/6", LearnedCount(g_cmdRange[KTek]), LearnedCount(g_cmdAI[KTek]));
        ImGui::Text("Plant: %d/3 | %d/6", LearnedCount(g_cmdRange[KPlant]), LearnedCount(g_cmdAI[KPlant]));
        if (ImGui::Button("Forget learned")) S::ForgetRequested = true;
        ImGui::SameLine();
        if (ImGui::Button("Dump turret info to log")) S::DumpRequested = true;

        ImGui::Separator();
        changed |= ImGui::SliderFloat("Radius (foundations)", &S::RangeFoundations, 1.f, 60.f, "%.0f");
        ImGui::Text("Turrets in range: %ld  (Auto %ld, Heavy %ld, Tek %ld, Plant %ld)",
                    g_count, g_countAuto, g_countHeavy, g_countTek, g_countPlant);

        changed |= ImGui::Checkbox("Auto", &S::FilterAuto);  ImGui::SameLine();
        changed |= ImGui::Checkbox("Heavy", &S::FilterHeavy); ImGui::SameLine();
        changed |= ImGui::Checkbox("Tek", &S::FilterTek);    ImGui::SameLine();
        changed |= ImGui::Checkbox("Plant", &S::FilterPlant);

        ImGui::Separator();
        ImGui::Text("New settings (leave on No change to keep)");

        static const char* kRange[] = { "No change", "Low (20 m)", "Medium (53 m)", "High (81 m)" };
        int rangeSel = S::RangeSetting + 1;   // -1 -> 0
        if (ImGui::Combo("Range", &rangeSel, kRange, IM_ARRAYSIZE(kRange)))
        {
            S::RangeSetting = rangeSel - 1;
            changed = true;
        }

        static const char* kAI[] = {
            "No change",
            "All targets",
            "Survivors + tames",
            "Survivors only",
            "Wild only",
            "Survivors + mounted",
            "Tamed only",
        };
        int aiSel = S::AISetting + 1;
        if (ImGui::Combo("Targeting", &aiSel, kAI, IM_ARRAYSIZE(kAI)))
        {
            S::AISetting = aiSel - 1;
            changed = true;
        }

        ImGui::Separator();
        const bool anyTarget = S::RangeSetting >= 0 || S::AISetting >= 0;
        if (!anyTarget) ImGui::BeginDisabled();
        if (ImGui::Button("Apply to all turrets in range", ImVec2(-1.f, 34.f)))
            S::ApplyRequested = true;
        if (!anyTarget) ImGui::EndDisabled();

        if (ApplyActive)
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.25f, 1.f), "Applying... %d sent", ApplySent);
        else
            ImGui::TextDisabled("Turrets ignore the change if the server rejects it (range/ownership).");

        ImGui::Separator();
        ImGui::TextDisabled("If nothing is learned: dump the log, then send it.");
        ImGui::TextDisabled("ARK's wheel only sends these commands when YOU change a setting.");

        bool vis = S::Visible;
        (void)vis;
        if (changed) Save();
        ImGui::End();
    }
}
