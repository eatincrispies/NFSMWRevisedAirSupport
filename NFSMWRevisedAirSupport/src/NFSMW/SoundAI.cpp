#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_DEBUG)
#include <algorithm>
#include <string>
#include <vector>
#endif
#include "SoundAI.hpp"

#define HELI_FUEL_CRITICAL_TIME 8.0f

namespace Log {

#if defined(_DEBUG)
    namespace {

        FILE* gFile = nullptr;

    }

    void Open(void* module) {
        char path[MAX_PATH] = "";
        const DWORD length = GetModuleFileNameA(static_cast<HMODULE>(module), path, MAX_PATH);
        char* dot = length && length < MAX_PATH ? std::strrchr(path, '.') : nullptr;
        if (!dot || dot < std::strrchr(path, '\\')) return;
        std::strcpy(dot, ".log");
        gFile = std::fopen(path, "w");
    }

    void Close() {
        if (gFile) std::fclose(gFile);
        gFile = nullptr;
    }

    void Write(const char* format, ...) {
        if (!gFile) return;
        char line[512] = "";
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line) - 2, format, args);
        va_end(args);
        std::strcat(line, "\n");
        std::fputs(line, gFile);
        std::fflush(gFile);
    }
#else
    void Open(void*) {}
    void Close() {}
    void Write(const char* format, ...);
#endif

}

#if defined(_DEBUG)
#define LOG(...) Log::Write(__VA_ARGS__)
#else
#define LOG(...) ((void)sizeof((Log::Write(__VA_ARGS__), 0)))
#endif

namespace Memory {

    namespace {

        int SafeCopy(void* destination, const void* source, size_t length) {
            __try {
                std::memcpy(destination, source, length);
                return 1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }

    }

    bool Read(uintptr_t va, void* out, size_t length) {
        return SafeCopy(out, reinterpret_cast<const void*>(va), length) != 0;
    }

    bool Matches(uintptr_t va, const void* expected, size_t length) {
        uint8_t bytes[64] = {};
        return length <= sizeof(bytes) && Read(va, bytes, length) && std::memcmp(bytes, expected, length) == 0;
    }

    bool WriteCode(uintptr_t va, const void* bytes, size_t length) {
        void* target = reinterpret_cast<void*>(va);
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(target, bytes, length);
        FlushInstructionCache(GetCurrentProcess(), target, length);
        DWORD unused = 0;
        VirtualProtect(target, length, oldProtect, &unused);
        return true;
    }

}

namespace Patch {

    namespace {

        constexpr int     kMostPatches = 24;
        constexpr uint8_t kCall        = 0xE8;
        constexpr uint8_t kJump        = 0xE9;
        constexpr uint8_t kNop         = 0x90;

        struct Original {
            uintptr_t va;
            size_t    length;
            uint8_t   bytes[8];
        };

        Original gOriginals[kMostPatches] = {};
        int      gCount                   = 0;

        uint32_t CallOffset(uintptr_t call, uintptr_t target) {
            return static_cast<uint32_t>(target) - static_cast<uint32_t>(call + 5);
        }

    }

    bool Replace(const char* what, uintptr_t va, const void* expected, const void* replacement, size_t length) {
        if (gCount >= kMostPatches) return false;
        Original& original = gOriginals[gCount];
        if (length > sizeof(original.bytes) || !Memory::Read(va, original.bytes, length) || std::memcmp(original.bytes, expected, length) != 0) {
            LOG("%s is off: another mod already changed the game's code at 0x%08lX.", what, static_cast<unsigned long>(va));
            return false;
        }
        if (!Memory::WriteCode(va, replacement, length)) {
            LOG("%s is off: the game's code at 0x%08lX could not be changed.", what, static_cast<unsigned long>(va));
            return false;
        }
        original.va = va;
        original.length = length;
        ++gCount;
        return true;
    }

    bool Redirect(const char* what, uintptr_t va, uint32_t expected, uint32_t replacement) {
        return Replace(what, va, &expected, &replacement, sizeof(replacement));
    }

    bool CallsTo(uintptr_t call, uintptr_t function) {
        uint8_t code[5] = {};
        uint32_t offset = 0;
        if (!Memory::Read(call, code, sizeof(code)) || code[0] != kCall) return false;
        std::memcpy(&offset, code + 1, sizeof(offset));
        return offset == CallOffset(call, function);
    }

    bool RedirectCall(const char* what, uintptr_t call, uintptr_t function, const void* replacement) {
        uint8_t opcode = 0;
        if (!Memory::Read(call, &opcode, sizeof(opcode)) || opcode != kCall) {
            LOG("%s is off: another mod already changed the game's code at 0x%08lX.", what, static_cast<unsigned long>(call));
            return false;
        }
        return Redirect(what, call + 1, CallOffset(call, function), CallOffset(call, reinterpret_cast<uintptr_t>(replacement)));
    }

    bool ReplaceVirtual(const char* what, uintptr_t vtable, unsigned slot, uintptr_t function, const void* replacement) {
        return Redirect(what, vtable + slot, static_cast<uint32_t>(function), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement)));
    }

    bool Detour(const char* what, uintptr_t function, const uint8_t* code, size_t length, const void* replacement, uintptr_t& original) {
        auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline) return false;
        const uint32_t back = CallOffset(reinterpret_cast<uintptr_t>(trampoline) + length, function + length);
        std::memcpy(trampoline, code, length);
        trampoline[length] = kJump;
        std::memcpy(trampoline + length + 1, &back, sizeof(back));
        FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5);

        uint8_t jump[8] = { kJump };
        const uint32_t offset = CallOffset(function, reinterpret_cast<uintptr_t>(replacement));
        std::memcpy(jump + 1, &offset, sizeof(offset));
        for (size_t i = 5; i < length; ++i) jump[i] = kNop;
        const uintptr_t previous = original;
        original = reinterpret_cast<uintptr_t>(trampoline);
        if (Replace(what, function, code, jump, length)) return true;
        original = previous;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    void RestoreAll() {
        while (gCount > 0) {
            const Original& original = gOriginals[--gCount];
            Memory::WriteCode(original.va, original.bytes, original.length);
        }
    }

}

namespace Game {

    template <typename Return, typename... Args>
    Return ThisCall(uintptr_t function, Args... args) {
        return reinterpret_cast<Return (__thiscall*)(Args...)>(function)(args...);
    }

    template <typename Return, typename... Args>
    Return Call(uintptr_t function, Args... args) {
        return reinterpret_cast<Return (__cdecl*)(Args...)>(function)(args...);
    }

    template <typename Method>
    const void* MethodAddress(Method method) {
        static_assert(sizeof(method) == sizeof(void*), "only plain member functions have a single code address");
        const void* address = nullptr;
        std::memcpy(&address, &method, sizeof(address));
        return address;
    }

    template <typename T>
    T& Global(uintptr_t va) {
        return *reinterpret_cast<T*>(va);
    }

}

namespace {

    constexpr const char* kVersion = "V1.0.0";

    constexpr uintptr_t kSFXCTL_Pathfinder_m_curinteractive = 0x009121E8u;
    constexpr uintptr_t kSingleton_SoundAI_mInstance        = 0x00993CC8u;
    constexpr uintptr_t kManager_mGlobalHistory             = 0x00992718u;
    constexpr uintptr_t kPlayerViewPrecipitation            = 0x009196B8u;

    constexpr uintptr_t kObject_IList_Find                    = 0x005D59F0u;
    constexpr uintptr_t kISimable_FindInstance                = 0x0041AD40u;
    constexpr uintptr_t kIAIHelicopter_IHandle                = 0x00404060u;
    constexpr uintptr_t kAttrib_FindCollection                = 0x00455FD0u;
    constexpr uintptr_t kAttrib_Instance_GetAttributePointer  = 0x00454810u;
    constexpr uintptr_t kEventHistory_Find                    = 0x004CB3F0u;
    constexpr uintptr_t kManager_ScheduleSpeechPartII         = 0x00713B20u;
    constexpr uintptr_t kManager_NotifyEventCompletion        = 0x00712BF0u;
    constexpr uintptr_t kManager_IsCopSpeechBusy              = 0x007040C0u;
    constexpr uintptr_t kManager_IsQueued                     = 0x00709D00u;
    constexpr uintptr_t kManager_IsCopSpeechPlaying           = 0x006FF320u;
    constexpr uintptr_t kManager_CanPlayback                  = 0x00704490u;
    constexpr uintptr_t kStrategyFlow_MessageReqBackup        = 0x007048C0u;
    constexpr uintptr_t kSoundAI_GetRandomActiveCop           = 0x007153B0u;
    constexpr uintptr_t kSoundAI_UpdateStateMachines          = 0x00701090u;
    constexpr uintptr_t kSoundAI_FindClosestCop               = 0x00708390u;
    constexpr uintptr_t kSoundAI_AddNewHeli                   = 0x0070DA60u;
    constexpr uintptr_t kMiscSpeech_LostSuspect               = 0x0071D960u;
    constexpr uintptr_t kEAXCharacter_SetHandle               = 0x007001D0u;
    constexpr uintptr_t kEAXCharacter_DriverHistory           = 0x00717020u;
    constexpr uintptr_t kEAXCop_VehicleReport                 = 0x00718150u;
    constexpr uintptr_t kEAXCop_CallForBackup                 = 0x00718640u;
    constexpr uintptr_t kEAXCop_StrategyReset                 = 0x00718D50u;
    constexpr uintptr_t kEAXCop_SuspectBehavior               = 0x00718EE0u;
    constexpr uintptr_t kEAXCop_RegainVisual                  = 0x00718C50u;
    constexpr uintptr_t kEAXCop_PursuitUpdateReply            = 0x007194D0u;
    constexpr uintptr_t kEAXCop_IntentToRam                   = 0x00718A30u;
    constexpr uintptr_t kEAXCop_Reply911                      = 0x00718060u;
    constexpr uintptr_t kEAXCop_Spotted                       = 0x007199D0u;
    constexpr uintptr_t kEAXCop_WeatherReport                 = 0x00719B00u;
    constexpr uintptr_t kEAXCop_CallForRB                     = 0x007196C0u;
    constexpr uintptr_t kEAXDispatch_BackupReply              = 0x00717090u;
    constexpr uintptr_t kEAXDispatch_PursuitUpdate            = 0x00717140u;
    constexpr uintptr_t kEAXDispatch_PursuitEscalationGeneric = 0x00717190u;
    constexpr uintptr_t kEAXDispatch_RBUpdate                 = 0x007176D0u;
    constexpr uintptr_t kEAXAirSupport_Update                 = 0x00709F50u;
    constexpr uintptr_t kEAXAirSupport_Bailout                = 0x00717C00u;
    constexpr uintptr_t kEAXAirSupport_Swarming               = 0x00717C40u;
    constexpr uintptr_t kEAXAirSupport_HazardAlert            = 0x00717CA0u;
    constexpr uintptr_t kEAXAirSupport_GetCauseOfBailout      = 0x00707860u;

    constexpr uintptr_t kEAXCopVTable        = 0x008B1FA0u;
    constexpr uintptr_t kEAXAirSupportVTable = 0x008B2278u;

    constexpr unsigned kSetHandleSlot          = 0x2Cu;
    constexpr unsigned kUpdateSlot             = 0x54u;
    constexpr unsigned kVehicleReportSlot      = 0x84u;
    constexpr unsigned kCallForBackupSlot      = 0x98u;
    constexpr unsigned kStrategyResetSlot      = 0xC0u;
    constexpr unsigned kSuspectBehaviorSlot    = 0xC4u;
    constexpr unsigned kRegainVisualSlot       = 0xD8u;
    constexpr unsigned kPursuitUpdateReplySlot = 0xE4u;
    constexpr unsigned kIntentToRamSlot        = 0xF8u;
    constexpr unsigned kBailoutSlot            = 0x104u;
    constexpr unsigned kReply911Slot           = 0x120u;
    constexpr unsigned kSpottedSlot            = 0x140u;
    constexpr unsigned kWeatherReportSlot      = 0x154u;
    constexpr unsigned kSwarmingSlot           = 0x210u;
    constexpr unsigned kHazardAlertSlot        = 0x214u;

    constexpr uintptr_t kOnTaskUpdateStateMachinesCall        = 0x0072162Cu;
    constexpr uintptr_t kSyncCarsToActorsAddNewHeliCall       = 0x007213EBu;
    constexpr uintptr_t kDealWithDeadAirPursuitUpdateCall     = 0x0071C988u;
    constexpr uintptr_t kTerminatePursuitLostSuspectCall      = 0x0071F758u;
    constexpr uintptr_t kBailoutGetCauseOfBailoutCall         = 0x00717C0Du;
    constexpr uintptr_t kOnCollisionBailout                   = 0x0071BCF5u;
    constexpr uint8_t   kOnCollisionBailoutCode[6]            = { 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00 };
    constexpr uint8_t   kNotifyEventCompletionEntry[8]        = { 0x83, 0xEC, 0x14, 0x56, 0x8B, 0x74, 0x24, 0x1C };
    constexpr uint8_t   kMessageReqBackupEntry[7]             = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x50, 0x10 };
    constexpr uint8_t   kCanPlaybackEntry[6]                  = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr uint8_t   kReturnTrue[6]                        = { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 };
    constexpr uintptr_t kEAXAirSupportUpdateFuelCheck         = 0x00709F75u;
    constexpr uint8_t   kEAXAirSupportUpdateFuelCheckCode[40] = {
        0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x28, 0x50, 0xE8, 0xBE, 0x0D, 0xD1, 0xFF, 0x8B, 0x48, 0x04, 0x83, 0xC4, 0x04, 0x68,
        0x60, 0x40, 0x40, 0x00, 0xE8, 0x5E, 0xBA, 0xEC, 0xFF, 0x85, 0xC0, 0x74, 0x1E, 0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52, 0x34,
    };

    constexpr uintptr_t kSampleHeaders     = 0x009C2C10u;
    constexpr uintptr_t kSampleHeaderCount = 0x009C2C1Cu;
    constexpr unsigned  kSampleEntrySize   = 0x08u;
    constexpr unsigned  kSampleEntryHeader = 0x04u;
    constexpr uint32_t  kMostSampleHeaders = 4096u;
    constexpr unsigned  kHeaderDimensions  = 0x04u;
    constexpr unsigned  kHeaderTakeList    = 0x0Cu;
    constexpr uint8_t   kDimensionMask     = 0x7Fu;
    constexpr uint8_t   kUnusedTag         = 7u;
    constexpr unsigned  kMostHiddenTakes   = 4u;
    constexpr uintptr_t kTakeOffsetCall    = 0x00833EEBu;
    constexpr uintptr_t kTakeOffset        = 0x008349FAu;
    constexpr uintptr_t kTakeCheckCall     = 0x0083344Cu;
    constexpr uintptr_t kTakeCheck         = 0x00833087u;

    constexpr uint32_t kSpeechClassKey                = 0xC593DD47u;
    constexpr uint32_t kDepFollowKey                  = 0xC8C5D475u;
    constexpr uint32_t kReqLOSKey                     = 0xE0241FC1u;
    constexpr uint32_t kOnScreenOnlyKey               = 0x4B331604u;
    constexpr uint32_t kBackup_CallForBUKey           = 0xA4911F22u;
    constexpr uint32_t kAnytimeEvents_Unit911ReplyKey = 0xC6B1C631u;
    constexpr uint32_t kAnytimeEvents_RegainVisualKey = 0xFD58F23Du;
    constexpr uint32_t kStaticRoadblock_CallForRBKey  = 0x26EF7810u;
    constexpr uint32_t kHeliSpecific_HeliBailoutKey   = 0x602ACE63u;
    constexpr unsigned kCollectionLayout              = 0x18u;
    constexpr uint16_t kMostDepFollows                = 16u;
    constexpr uint16_t kDepFollowSize                 = 0x0Cu;
    constexpr uint16_t kWideArrayHeader               = 0x8000u;
    constexpr float    kLongestExpiry                 = 30.0f;
    constexpr float    kRelaxedExpiry                 = 20.0f;
    constexpr float    kLongestCullingRange           = 100000.0f;
    constexpr float    kRelaxedCullingRange           = 100000.0f;

    constexpr int     kAllEventQueues      = 4;
    constexpr int     kPrimaryCops         = 1;
    constexpr int     kRoadblockBackupType = 0x40;
    constexpr int     kStrategyBackupType  = 0x10;
    constexpr float   kFullHealth          = 1.0f;

    constexpr uint16_t kHeliCheckInSample       = 0xC4u;
    constexpr int      kBackupReplyTakes        = 4;
    constexpr int      kHistoryRequestFirstTake = 4;
    constexpr int      kHistoryRequestLastTake  = 7;
    constexpr uint16_t kDriverHistorySample     = 0xACu;
    constexpr unsigned kMinorRecordTakes        = 4u;
    constexpr uint8_t  kMinorRecordTag          = 0u;
    constexpr unsigned kRecordTiers             = 4u;
    constexpr uint16_t kDispPursEscGenSample    = 0x146u;
    constexpr unsigned kOddMultipleSuspectsTake = 11u;
    constexpr uint8_t  kMultipleSuspectsTag     = 1u;

    constexpr int          kDriverHistoryMinHeat = 5;
    constexpr int          kHeliMinHeat          = 4;
    constexpr unsigned     kBackupReplyChance    = 4u;
    constexpr int          kDrivingLineMinHeat   = 3;
    constexpr float        kDrivingLineMinSpeed  = 45.0f;
    constexpr float        kRainReportLevel      = 0.25f;

    enum AirSupportFlags : unsigned int {
        HELI_DOWN        = 1u << 0,
        BAILOUT_REQ      = 1u << 1,
        NEW_HELI_SPOT    = 1u << 2,
        AIR_SUPPORT_REQ  = 1u << 3,
        RB_CALLED        = 1u << 4,
        HISTORY_REQ      = 1u << 5,
        BACKUP_CALLED    = 1u << 6,
        BACKUP_REPLYING  = 1u << 7,
        DRIVING_LINE_REQ = 1u << 8,
        THEME_KNOWN      = 1u << 9,
        THEME_BROADCAST  = 1u << 10,
        RAIN_REPORTED    = 1u << 11,
    };

    enum class SpotLine { RegainVisual, Spotted };

    struct RoadblockUpdate {
        Csis::Type_yes_no         yes_no;
        Csis::Type_roadblock_type roadblock_type;
    };

    constexpr RoadblockUpdate kRoadblockUpdates[] = {
        { Csis::Type_yes_no_Yes_True, Csis::Type_roadblock_type_Roadblock_Generic_ },
        { Csis::Type_yes_no_No_False, Csis::Type_roadblock_type_Spikes },
    };

    constexpr unsigned kRoadblockUpdateCount = sizeof(kRoadblockUpdates) / sizeof(kRoadblockUpdates[0]);

    struct SpeechEvent {
        const char*        name;
        uintptr_t          interfaceId;
        uint32_t           interfaceCrcs;
        uintptr_t          functionHandle;
        uintptr_t          function;
        unsigned           pushOffset;
        SPCHType_1_EventID id;

        Csis::InterfaceId& Id() const {
            return Game::Global<Csis::InterfaceId>(interfaceId);
        }

        Csis::FunctionHandle& Handle() const {
            return Game::Global<Csis::FunctionHandle>(functionHandle);
        }

        bool Verify() const;
        bool VerifyVirtual(uintptr_t vtable, unsigned slot) const;
    };

    constexpr SpeechEvent kAnytimeEvents_IntentToRam      = { "AnytimeEvents_IntentToRam",      0x00901E5Cu, 0x6C5E5BA7u, 0x00992544u, kEAXCop_IntentToRam,                   0x43u,  kSPCH1_EventID_IntentToRam };
    constexpr SpeechEvent kOutcome_StrategyReset          = { "Outcome_StrategyReset",          0x00901D84u, 0x4EA05BA7u, 0x0099242Cu, kEAXCop_StrategyReset,                 0x42u,  kSPCH1_EventID_StrategyReset };
    constexpr SpeechEvent kAnytimeEvents_LostSuspect      = { "AnytimeEvents_LostSuspect",      0x00901E1Cu, 0x34A45BA7u, 0x009924ECu, kMiscSpeech_LostSuspect,               0x6Fu,  kSPCH1_EventID_LostSuspect };
    constexpr SpeechEvent kHeliSpecific_HeliBailout       = { "HeliSpecific_HeliBailout",       0x00901EDCu, 0x23945BA7u, 0x009924D4u, kEAXAirSupport_Bailout,                0x13u,  kSPCH1_EventID_HeliBailout };
    constexpr SpeechEvent kHeliSpecific_HeliHazardAlert   = { "HeliSpecific_HeliHazardAlert",   0x00901EF4u, 0x4AFB5BA7u, 0x0099235Cu, kEAXAirSupport_HazardAlert,            0x0Bu,  kSPCH1_EventID_HeliHazardAlert };
    constexpr SpeechEvent kAnytimeEvents_RegainVisual     = { "AnytimeEvents_RegainVisual",     0x00901E14u, 0x51D85BA7u, 0x0099240Cu, kEAXCop_RegainVisual,                  0x43u,  kSPCH1_EventID_RegainVisual };
    constexpr SpeechEvent kAnytimeEvents_Spotted          = { "AnytimeEvents_Spotted",          0x00901E94u, 0x0CB35BA7u, 0x00992524u, kEAXCop_Spotted,                       0x30u,  kSPCH1_EventID_Spotted };
    constexpr SpeechEvent kAnytimeEvents_Unit911Reply     = { "AnytimeEvents_Unit911Reply",     0x00901DF4u, 0x383D5BA7u, 0x009926CCu, kEAXCop_Reply911,                      0x05u,  kSPCH1_EventID_Unit911Reply };
    constexpr SpeechEvent kBackup_CallForBU               = { "Backup_CallForBU",               0x00901C84u, 0x398D5BA7u, 0x00992564u, kEAXCop_CallForBackup,                 0x1Fu,  kSPCH1_EventID_CallForBU };
    constexpr SpeechEvent kBackup_DispBackupReply         = { "Backup_DispBackupReply",         0x00901C94u, 0x5C2F5BA7u, 0x009926B0u, kEAXDispatch_BackupReply,              0x4Au,  kSPCH1_EventID_DispBackupReply };
    constexpr SpeechEvent kHeliSpecific_HeliSwarming      = { "HeliSpecific_HeliSwarming",      0x00901EE4u, 0x22745BA7u, 0x00992474u, kEAXAirSupport_Swarming,               0x05u,  kSPCH1_EventID_HeliSwarming };
    constexpr SpeechEvent kStaticRoadblock_CallForRB      = { "StaticRoadblock_CallForRB",      0x00901CD4u, 0x7D0E5BA7u, 0x00992594u, kEAXCop_CallForRB,                     0x32u,  kSPCH1_EventID_CallForRB };
    constexpr SpeechEvent kStaticRoadblock_DispRBUpdate   = { "StaticRoadblock_DispRBUpdate",   0x00901CF4u, 0x7B7F5BA7u, 0x009926F4u, kEAXDispatch_RBUpdate,                 0x2Eu,  kSPCH1_EventID_DispRBUpdate };
    constexpr SpeechEvent kAnytimeEvents_DriverHistory    = { "AnytimeEvents_DriverHistory",    0x00901E84u, 0x689E5BA7u, 0x0099231Cu, kEAXCharacter_DriverHistory,           0x07u,  kSPCH1_EventID_DriverHistory };
    constexpr SpeechEvent kAnytimeEvents_PursuitUpdateRep = { "AnytimeEvents_PursuitUpdateRep", 0x00901DD4u, 0x795D5BA7u, 0x009926D8u, kEAXCop_PursuitUpdateReply,            0x05u,  kSPCH1_EventID_PursuitUpdateRep };
    constexpr SpeechEvent kAnytimeEvents_SuspectBehaviour = { "AnytimeEvents_SuspectBehaviour", 0x00901E7Cu, 0x688B5BA7u, 0x00993C80u, kEAXCop_SuspectBehavior,               0x23u,  kSPCH1_EventID_SuspectBehaviour };
    constexpr SpeechEvent kAnytimeEvents_DispPursEscGen   = { "AnytimeEvents_DispPursEscGen",   0x00901E3Cu, 0x6BAC5BA7u, 0x0099237Cu, kEAXDispatch_PursuitEscalationGeneric, 0x23u,  kSPCH1_EventID_DispPursEscGen };
    constexpr SpeechEvent kSetup_VehicleReport            = { "Setup_VehicleReport",            0x00901C1Cu, 0x466D5BA7u, 0x0099246Cu, kEAXCop_VehicleReport,                 0x1C4u, kSPCH1_EventID_VehicleReport };
    constexpr SpeechEvent kAnytimeEvents_WeatherReport    = { "AnytimeEvents_WeatherReport",    0x00901EA4u, 0x7F435BA7u, 0x0099236Cu, kEAXCop_WeatherReport,                 0x05u,  kSPCH1_EventID_WeatherReport };

    struct AttribArray {
        uint16_t mAlloc;
        uint16_t mCount;
        uint16_t mSize;
        uint16_t mFlags;
    };

    struct speech_LayoutStruct {
        const char*        CollectionName;
        SPCHType_1_EventID SpeechID;
        float              BackTime;
        float              expiry;
        float              CullingRange;
    };

}

int&                  SFXCTL_Pathfinder::m_curinteractive = Game::Global<int>(kSFXCTL_Pathfinder_m_curinteractive);
Speech::EventHistory& Speech::Manager::mGlobalHistory = Game::Global<Speech::EventHistory>(kManager_mGlobalHistory);

static_assert(offsetof(EAXCharacter, mSpeakerID) == 0x0C, "EAXCharacter::mSpeakerID");
static_assert(offsetof(EAXCharacter, mCallsign) == 0x14, "EAXCharacter::mCallsign");
static_assert(offsetof(EAXCharacter, mHealth) == 0x30, "EAXCharacter::mHealth");
static_assert(offsetof(EAXCharacter, mDestroyed) == 0x34, "EAXCharacter::mDestroyed");
static_assert(offsetof(EAXCharacter, mActive) == 0x35, "EAXCharacter::mActive");
static_assert(offsetof(EAXCharacter, mSuspectLOS) == 0x36, "EAXCharacter::mSuspectLOS");
static_assert(offsetof(SoundAI, mFlags) == 0x54, "SoundAI::mFlags");
static_assert(offsetof(SoundAI, mActors) == 0x58, "SoundAI::mActors");
static_assert(offsetof(SoundAI, mDispatch) == 0xD8, "SoundAI::mDispatch");
static_assert(offsetof(SoundAI, mHeli) == 0xE0, "SoundAI::mHeli");
static_assert(offsetof(SoundAI, mPlayerHeat) == 0x104, "SoundAI::mPlayerHeat");
static_assert(offsetof(SoundAI, mPlayerSpeed) == 0x108, "SoundAI::mPlayerSpeed");
static_assert(offsetof(SoundAI, mPursuitDuration) == 0x140, "SoundAI::mPursuitDuration");
static_assert(offsetof(SoundAI, mPursuitState) == 0x1D4, "SoundAI::mPursuitState");
static_assert(sizeof(Speech::copMap) == 0x10, "Speech::copMap is 0x10 bytes");
static_assert(sizeof(Speech::ScheduledSpeechEvent) == 0x40, "Speech::ScheduledSpeechEvent is 0x40 bytes");
static_assert(offsetof(Speech::ScheduledSpeechEvent, ID) == 0x08, "Speech::ScheduledSpeechEvent::ID");
static_assert(offsetof(Speech::ScheduledSpeechEvent, actor) == 0x0C, "Speech::ScheduledSpeechEvent::actor");
static_assert(offsetof(Speech::ScheduledSpeechEvent, priority) == 0x3B, "Speech::ScheduledSpeechEvent::priority");
static_assert(offsetof(MReqBackup, fBackupType) == 0x10, "MReqBackup::fBackupType");
static_assert(offsetof(Rain, intensity) == 0x28C, "Rain::intensity");
static_assert(sizeof(Attrib::Instance) == 0x14, "Attrib::Instance is 0x14 bytes");
static_assert(offsetof(speech_LayoutStruct, expiry) == 0x0C, "Attrib::Gen::speech expiry");
static_assert(offsetof(speech_LayoutStruct, CullingRange) == 0x10, "Attrib::Gen::speech CullingRange");

UTL::COM::IUnknown* UTL::COM::IUnknown::FindInterface(void* handle) {
    return _mCOMObject != nullptr ? Game::ThisCall<IUnknown*>(kObject_IList_Find, _mCOMObject, handle) : nullptr;
}

ISimable* ISimable::FindInstance(HSIMABLE handle) {
    return handle != nullptr ? Game::Call<ISimable*>(kISimable_FindInstance, handle) : nullptr;
}

void* IAIHelicopter::_IHandle() {
    return reinterpret_cast<void*>(kIAIHelicopter_IHandle);
}

const Attrib::Collection* Attrib::FindCollection(unsigned int classkey, unsigned int collectionkey) {
    return Game::Call<const Collection*>(kAttrib_FindCollection, classkey, collectionkey);
}

const void* Attrib::Instance::GetAttributePointer(unsigned int attribkey, unsigned int index) const {
    return Game::ThisCall<const void*>(kAttrib_Instance_GetAttributePointer, this, attribkey, index);
}

Speech::History* Speech::EventHistory::Find(SPCHType_1_EventID id) {
    return Game::ThisCall<History*>(kEventHistory_Find, this, id);
}

Speech::ScheduledSpeechEvent* Speech::Manager::ScheduleSpeechPartII(unsigned int size, void* data, Csis::InterfaceId& iid, Csis::FunctionHandle& fh,
                                                                    EAXCharacter* actor) {
    return Game::Call<ScheduledSpeechEvent*, unsigned int, void*, Csis::InterfaceId&, Csis::FunctionHandle&, EAXCharacter*>(
        kManager_ScheduleSpeechPartII, size, data, iid, fh, actor);
}

bool Speech::Manager::IsCopSpeechBusy() {
    return Game::Call<bool>(kManager_IsCopSpeechBusy);
}

bool Speech::Manager::IsQueued(SPCHType_1_EventID evtID, int indices) {
    return Game::Call<bool>(kManager_IsQueued, evtID, indices);
}

bool Speech::Manager::IsCopSpeechPlaying(SPCHType_1_EventID event_id) {
    return Game::Call<bool>(kManager_IsCopSpeechPlaying, event_id);
}

SoundAI* SoundAI::Get() {
    return Game::Global<SoundAI*>(kSingleton_SoundAI_mInstance);
}

EAXCop* SoundAI::GetRandomActiveCop(int type, bool reqLOS) {
    return Game::ThisCall<EAXCop*>(kSoundAI_GetRandomActiveCop, this, type, reqLOS);
}

void SoundAI::UpdateStateMachines() {
    Game::ThisCall<void>(kSoundAI_UpdateStateMachines, this);
}

EAXCop* SoundAI::FindClosestCop(bool enforceLOS, bool includeHeli) {
    return Game::ThisCall<EAXCop*>(kSoundAI_FindClosestCop, this, enforceLOS, includeHeli);
}

void SoundAI::AddNewHeli(IVehicle* heli) {
    Game::ThisCall<void>(kSoundAI_AddNewHeli, this, heli);
}

int MiscSpeech::LostSuspect(int spkrID) {
    return Game::Call<int>(kMiscSpeech_LostSuspect, spkrID);
}

void EAXCharacter::SetHandle(HSIMABLE handle) {
    Game::ThisCall<void>(kEAXCharacter_SetHandle, this, handle);
}

void EAXCop::StrategyReset(bool new_strategy) {
    Game::ThisCall<void>(kEAXCop_StrategyReset, this, new_strategy);
}

void EAXCop::RegainVisual() {
    Game::ThisCall<void>(kEAXCop_RegainVisual, this);
}

void EAXCop::PursuitUpdateReply() {
    Game::ThisCall<void>(kEAXCop_PursuitUpdateReply, this);
}

void EAXCop::Reply911() {
    Game::ThisCall<void>(kEAXCop_Reply911, this);
}

void EAXCop::Spotted() {
    Game::ThisCall<void>(kEAXCop_Spotted, this);
}

void EAXCop::WeatherReport() {
    Game::ThisCall<void>(kEAXCop_WeatherReport, this);
}

void EAXDispatch::BackupReply(EAXCop* cop, int yes, int type) {
    Game::ThisCall<void>(kEAXDispatch_BackupReply, this, cop, yes, type);
}

void EAXDispatch::PursuitUpdate(EAXCop* cop) {
    Game::ThisCall<void>(kEAXDispatch_PursuitUpdate, this, cop);
}

void EAXAirSupport::Update() {
    Game::ThisCall<void>(kEAXAirSupport_Update, this);
}

void EAXAirSupport::Bailout() {
    Game::ThisCall<void>(kEAXAirSupport_Bailout, this);
}

void EAXAirSupport::Swarming() {
    Game::ThisCall<void>(kEAXAirSupport_Swarming, this);
}

void EAXAirSupport::HazardAlert(Csis::Type_heli_hazard_alert_type type) {
    Game::ThisCall<void>(kEAXAirSupport_HazardAlert, this, type);
}

Csis::Type_heli_bailout_type EAXAirSupport::GetCauseOfBailout() {
    return Game::ThisCall<Csis::Type_heli_bailout_type>(kEAXAirSupport_GetCauseOfBailout, this);
}

namespace {

    class SpeechRule {
      public:
        enum Kind { kDepFollow, kFlag, kExpiry, kCullingRange };

        void Relax();
        void Restore();

        SPCHType_1_EventID line;
        uint32_t           collection;
        Kind               kind;
        uint32_t           attribute;
        uint8_t*           value;
        uint32_t           saved;
        bool               relaxed;
    };

    uintptr_t gNotifyEventCompletionOriginal = kManager_NotifyEventCompletion;
    uintptr_t gMessageReqBackupOriginal      = kStrategyFlow_MessageReqBackup;
    uintptr_t gTakeCheckOriginal             = kTakeCheck;
    uintptr_t gOnCollisionReturn             = 0;

    bool gBailoutCauseOn       = false;
    bool gCheckInOn            = false;
    bool gAirSupportOn         = false;
    bool gRoadblockOn          = false;
    bool gDriverHistoryOn      = false;
    bool gBackupReplyOn        = false;
    bool gSwarmingOn           = false;
    bool gPursuitUpdateReplyOn = false;
    bool gThemeBroadcastOn     = false;
    bool gVehicleReportOn      = false;
    bool gWeatherReportOn      = false;

    unsigned int         gFlags                  = 0;
    EAXAirSupport*       gHeliInChase            = nullptr;
    int                  gHelisThisPursuit       = 0;
    bool                 gUnusedRegainVisualNext = true;
    int                  gPursuitTheme           = 0;
    unsigned             gRoadblockUpdate        = 0;
    Csis::Type_intensity gIntentToRamIntensity   = Csis::Type_intensity_High;
    Csis::Type_intensity gLostSuspectIntensity   = Csis::Type_intensity_High;
    bool                 gNewStrategy            = true;

    SpeechRule gBackup_CallForBU_DepFollow             = { kSPCH1_EventID_CallForBU, kBackup_CallForBUKey, SpeechRule::kDepFollow, kDepFollowKey };
    SpeechRule gAnytimeEvents_Unit911Reply_DepFollow   = { kSPCH1_EventID_Unit911Reply, kAnytimeEvents_Unit911ReplyKey, SpeechRule::kDepFollow, kDepFollowKey };
    SpeechRule gAnytimeEvents_RegainVisual_DepFollow   = { kSPCH1_EventID_RegainVisual, kAnytimeEvents_RegainVisualKey, SpeechRule::kDepFollow, kDepFollowKey };
    SpeechRule gAnytimeEvents_RegainVisual_expiry      = { kSPCH1_EventID_RegainVisual, kAnytimeEvents_RegainVisualKey, SpeechRule::kExpiry };
    SpeechRule gStaticRoadblock_CallForRB_DepFollow    = { kSPCH1_EventID_CallForRB, kStaticRoadblock_CallForRBKey, SpeechRule::kDepFollow, kDepFollowKey };
    SpeechRule gStaticRoadblock_CallForRB_reqLOS       = { kSPCH1_EventID_CallForRB, kStaticRoadblock_CallForRBKey, SpeechRule::kFlag, kReqLOSKey };
    SpeechRule gStaticRoadblock_CallForRB_OnScreenOnly = { kSPCH1_EventID_CallForRB, kStaticRoadblock_CallForRBKey, SpeechRule::kFlag, kOnScreenOnlyKey };
    SpeechRule gStaticRoadblock_CallForRB_expiry       = { kSPCH1_EventID_CallForRB, kStaticRoadblock_CallForRBKey, SpeechRule::kExpiry };
    SpeechRule gHeliSpecific_HeliBailout_expiry        = { kSPCH1_EventID_HeliBailout, kHeliSpecific_HeliBailoutKey, SpeechRule::kExpiry };
    SpeechRule gHeliSpecific_HeliBailout_CullingRange  = { kSPCH1_EventID_HeliBailout, kHeliSpecific_HeliBailoutKey, SpeechRule::kCullingRange };
    SpeechRule* const gRules[] = {
        &gBackup_CallForBU_DepFollow,
        &gAnytimeEvents_Unit911Reply_DepFollow,
        &gAnytimeEvents_RegainVisual_DepFollow,
        &gAnytimeEvents_RegainVisual_expiry,
        &gStaticRoadblock_CallForRB_DepFollow,
        &gStaticRoadblock_CallForRB_reqLOS,
        &gStaticRoadblock_CallForRB_OnScreenOnly,
        &gStaticRoadblock_CallForRB_expiry,
        &gHeliSpecific_HeliBailout_expiry,
        &gHeliSpecific_HeliBailout_CullingRange,
    };

    uint8_t*           gHiddenTags[kMostHiddenTakes]   = {};
    uint8_t            gHiddenValues[kMostHiddenTakes] = {};
    unsigned           gHiddenCount                    = 0;
    SPCHType_1_EventID gHiddenForLine                  = kSPCH1_EventID_DriverHistory;

    bool SpeechEvent::Verify() const {
        uint32_t gameInterfaceId[2] = {};
        char text[64] = "";
        const size_t length = std::strlen(name) + 1;
        if (length > sizeof(text) || !Memory::Read(interfaceId, gameInterfaceId, sizeof(gameInterfaceId)) || gameInterfaceId[1] != interfaceCrcs
            || !Memory::Read(gameInterfaceId[0], text, length) || std::memcmp(text, name, length) != 0) {
            LOG("%s is off: speed.exe has no Csis::%sId at 0x%08lX.", name, name, static_cast<unsigned long>(interfaceId));
            return false;
        }

        constexpr uint8_t kPush = 0x68;
        uint8_t code[24] = {};
        uint8_t pushHandle[5] = { kPush };
        uint8_t pushId[5] = { kPush };
        const uint32_t handleAddress = static_cast<uint32_t>(functionHandle);
        const uint32_t idAddress = static_cast<uint32_t>(interfaceId);
        std::memcpy(pushHandle + 1, &handleAddress, sizeof(handleAddress));
        std::memcpy(pushId + 1, &idAddress, sizeof(idAddress));
        const bool found = Memory::Read(function + pushOffset, code, sizeof(code)) && std::memcmp(code, pushHandle, sizeof(pushHandle)) == 0;
        bool pushed = false;
        for (size_t i = sizeof(pushHandle); found && !pushed && i + sizeof(pushId) <= sizeof(code); ++i)
            pushed = std::memcmp(code + i, pushId, sizeof(pushId)) == 0;
        if (!pushed) {
            LOG("%s is off: the game function at 0x%08lX does not schedule it the way this mod expects.", name, static_cast<unsigned long>(function));
            return false;
        }
        return true;
    }

    bool SpeechEvent::VerifyVirtual(uintptr_t vtable, unsigned slot) const {
        uintptr_t current = 0;
        if (Verify() && Memory::Read(vtable + slot, &current, sizeof(current)) && current == function) return true;
        LOG("%s is off: the police radio does not use the game's own function for it.", name);
        return false;
    }

    template <typename T> Speech::ScheduledSpeechEvent* ScheduleSpeech(T& data, const SpeechEvent& event, EAXCharacter* actor) {
        return Speech::Manager::ScheduleSpeechPartII(sizeof(T), &data, event.Id(), event.Handle(), actor);
    }

    void ResetPlayCount(const SpeechEvent& event) {
        if (Speech::History* history = Speech::Manager::GetHistory().Find(event.id)) history->count = 0;
    }

    bool IsQueuedOrPlaying(SPCHType_1_EventID id) {
        return Speech::Manager::IsQueued(id, kAllEventQueues) || Speech::Manager::IsCopSpeechPlaying(id);
    }

    uint8_t* FindLayoutValue(const Attrib::Collection* collection, size_t field, float longest) {
        speech_LayoutStruct* layout = nullptr;
        float number = 0.0f;
        if (!Memory::Read(reinterpret_cast<uintptr_t>(collection) + kCollectionLayout, &layout, sizeof(layout)) || !layout
            || !Memory::Read(reinterpret_cast<uintptr_t>(layout) + field, &number, sizeof(number)) || !(number > 0.0f) || number > longest)
            return nullptr;
        return reinterpret_cast<uint8_t*>(layout) + field;
    }

    uint8_t* FindRuleValue(const SpeechRule& rule) {
        const Attrib::Collection* collection = Attrib::FindCollection(kSpeechClassKey, rule.collection);
        if (!collection) return nullptr;
        if (rule.kind == SpeechRule::kExpiry) return FindLayoutValue(collection, offsetof(speech_LayoutStruct, expiry), kLongestExpiry);
        if (rule.kind == SpeechRule::kCullingRange) return FindLayoutValue(collection, offsetof(speech_LayoutStruct, CullingRange), kLongestCullingRange);

        const Attrib::Instance instance = { nullptr, collection, nullptr, 0u, 0u, 0u };
        auto* first = static_cast<uint8_t*>(const_cast<void*>(instance.GetAttributePointer(rule.attribute, 0u)));
        if (!first) return nullptr;
        if (rule.kind == SpeechRule::kFlag) {
            uint32_t flag = 0;
            return Memory::Read(reinterpret_cast<uintptr_t>(first), &flag, sizeof(flag)) && flag == 1u ? first : nullptr;
        }

        auto* array = reinterpret_cast<AttribArray*>(first - sizeof(AttribArray));
        AttribArray header = {};
        uint32_t eventClass = 0;
        if (!Memory::Read(reinterpret_cast<uintptr_t>(array), &header, sizeof(header))
            || !Memory::Read(reinterpret_cast<uintptr_t>(first), &eventClass, sizeof(eventClass)) || !header.mCount
            || header.mCount > kMostDepFollows || header.mSize != kDepFollowSize || (header.mFlags & kWideArrayHeader) || eventClass != kSpeechClassKey)
            return nullptr;
        return reinterpret_cast<uint8_t*>(&array->mCount);
    }

    void SpeechRule::Relax() {
        if (relaxed) return;
        if (!value) value = FindRuleValue(*this);
        if (!value) return;
        switch (kind) {
        case kDepFollow: {
            auto* count = reinterpret_cast<uint16_t*>(value);
            saved = *count;
            *count = 0;
            break;
        }
        case kFlag: {
            auto* flag = reinterpret_cast<uint32_t*>(value);
            saved = *flag;
            *flag = 0;
            break;
        }
        case kExpiry:
        case kCullingRange: {
            auto* number = reinterpret_cast<float*>(value);
            std::memcpy(&saved, number, sizeof(saved));
            *number = kind == kExpiry ? kRelaxedExpiry : kRelaxedCullingRange;
            break;
        }
        }
        relaxed = true;
    }

    void SpeechRule::Restore() {
        if (!relaxed) return;
        if (kind == kDepFollow)
            *reinterpret_cast<uint16_t*>(value) = static_cast<uint16_t>(saved);
        else
            std::memcpy(value, &saved, sizeof(saved));
        relaxed = false;
    }

    void RestoreRulesOfFinishedLines() {
        for (SpeechRule* rule : gRules)
            if (rule->relaxed && !IsQueuedOrPlaying(rule->line)) rule->Restore();
    }

    void RestoreAllRules() {
        for (SpeechRule* rule : gRules) rule->Restore();
    }

    struct SampleId {
        uint16_t sample;
        uint16_t speaker;
    };

    uint8_t* FindTakeTag(uint16_t sample, uint16_t speaker, unsigned take, uint8_t expected) {
        uint32_t table = 0;
        uint32_t count = 0;
        if (!Memory::Read(kSampleHeaders, &table, sizeof(table)) || !table || !Memory::Read(kSampleHeaderCount, &count, sizeof(count))
            || count > kMostSampleHeaders)
            return nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t header = 0;
            SampleId id = {};
            uint8_t shape[2] = {};
            if (!Memory::Read(table + i * kSampleEntrySize + kSampleEntryHeader, &header, sizeof(header)) || !header
                || !Memory::Read(header, &id, sizeof(id)) || id.sample != sample || id.speaker != speaker
                || !Memory::Read(header + kHeaderDimensions, shape, sizeof(shape)) || !(shape[0] & kDimensionMask) || take >= shape[1])
                continue;
            const uint32_t tag = header + kHeaderTakeList + (2u + (shape[0] & kDimensionMask)) * take + 2u;
            uint8_t value = 0;
            return Memory::Read(tag, &value, sizeof(value)) && value == expected ? reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(tag)) : nullptr;
        }
        return nullptr;
    }

    void ShowHiddenTakes() {
        while (gHiddenCount > 0) {
            --gHiddenCount;
            *gHiddenTags[gHiddenCount] = gHiddenValues[gHiddenCount];
        }
    }

    bool HideTakes(SPCHType_1_EventID line, uint16_t sample, uint16_t speaker, unsigned first, unsigned count, uint8_t expected) {
        if (gHiddenCount || count > kMostHiddenTakes) return false;
        for (unsigned take = first; take < first + count; ++take) {
            uint8_t* tag = FindTakeTag(sample, speaker, take, expected);
            if (!tag) {
                ShowHiddenTakes();
                return false;
            }
            gHiddenTags[gHiddenCount] = tag;
            gHiddenValues[gHiddenCount] = *tag;
            ++gHiddenCount;
            *tag = kUnusedTag;
        }
        gHiddenForLine = line;
        return true;
    }

    void ShowHiddenTakesOfFinishedLine() {
        if (gHiddenCount && !IsQueuedOrPlaying(gHiddenForLine)) ShowHiddenTakes();
    }

    bool ReadSampleId(const void* header, SampleId& id) {
        return Memory::Read(reinterpret_cast<uintptr_t>(header), &id, sizeof(id));
    }

    bool IsHeliCheckIn(const void* header) {
        SampleId id = {};
        return ReadSampleId(header, id) && id.sample == kHeliCheckInSample && id.speaker == static_cast<uint16_t>(Speech::Heli);
    }

#if defined(_DEBUG)
    struct SampleEntries {
        uint32_t              key;
        std::vector<uint32_t> entries;
    };

    struct TakeFile {
        uint32_t offset;
        uint16_t sample;
        uint16_t take;
    };

    std::vector<SampleEntries> gSampleEntries;

    uint32_t SampleKey(uint16_t sample, uint16_t speaker) {
        return (static_cast<uint32_t>(sample) << 16) | speaker;
    }

    std::vector<uint8_t> ReadSpeechIndex() {
        char path[MAX_PATH] = "";
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = length && length < MAX_PATH ? std::strrchr(path, '\\') : nullptr;
        std::vector<uint8_t> bytes;
        if (!slash) return bytes;
        slash[1] = 0;
        FILE* file = std::fopen((std::string(path) + "SOUND\\SPEECH\\copspeech.idx").c_str(), "rb");
        if (!file) return bytes;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (size > 0) {
            bytes.resize(static_cast<size_t>(size));
            if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
        }
        std::fclose(file);
        return bytes;
    }

    uint32_t Little32(const std::vector<uint8_t>& bytes, size_t at) {
        if (at + 4 > bytes.size()) return 0;
        return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
    }

    uint16_t Little16(const std::vector<uint8_t>& bytes, size_t at) {
        if (at + 2 > bytes.size()) return 0;
        return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
    }

    void LoadSpeechEntries() {
        const std::vector<uint8_t> idx = ReadSpeechIndex();
        const uint32_t groups = Little32(idx, 4);
        std::vector<TakeFile> files;
        for (uint32_t group = 0; group < groups && 0x58u + 16u * (group + 1u) <= idx.size(); ++group) {
            const size_t record = 0x58u + 16u * group;
            const uint32_t meta = Little32(idx, record + 8);
            const uint32_t big = Little32(idx, record + 12);
            if (meta + 12u > idx.size()) continue;
            const unsigned width = 2u + idx[meta + 4];
            const unsigned takes = idx[meta + 5];
            const uint16_t sample = static_cast<uint16_t>(gSampleEntries.size());
            gSampleEntries.push_back({ SampleKey(Little16(idx, meta), Little16(idx, meta + 2)), std::vector<uint32_t>(takes, 0u) });
            for (unsigned take = 0; take < takes && meta + 12u + width * (take + 1u) <= idx.size(); ++take) {
                const size_t at = meta + 12u + width * take;
                const uint32_t page = (static_cast<uint32_t>(idx[at]) << 8) | idx[at + 1];
                files.push_back({ big + page * 256u, sample, static_cast<uint16_t>(take) });
            }
        }
        std::sort(files.begin(), files.end(), [](const TakeFile& a, const TakeFile& b) { return a.offset < b.offset; });
        for (size_t i = 0; i < files.size(); ++i) gSampleEntries[files[i].sample].entries[files[i].take] = static_cast<uint32_t>(i);
        std::sort(gSampleEntries.begin(), gSampleEntries.end(), [](const SampleEntries& a, const SampleEntries& b) { return a.key < b.key; });
        LOG("Speech: %lu takes indexed from copspeech.idx.", static_cast<unsigned long>(files.size()));
    }

    void LogTake(const void* header, int take) {
        SampleId id = {};
        if (!ReadSampleId(header, id)) return;
        const uint32_t key = SampleKey(id.sample, id.speaker);
        const auto found = std::lower_bound(gSampleEntries.begin(), gSampleEntries.end(), key,
                                            [](const SampleEntries& item, uint32_t value) { return item.key < value; });
        if (found == gSampleEntries.end() || found->key != key || take < 0 || static_cast<size_t>(take) >= found->entries.size()) {
            LOG("Speech: ENTRY_????? (0x%04X)", id.sample);
            return;
        }
        LOG("Speech: ENTRY_%05lu (0x%04X)", static_cast<unsigned long>(found->entries[static_cast<size_t>(take)]), id.sample);
    }
#else
    void LoadSpeechEntries() {}
    void LogTake(const void*, int) {}
#endif

    Csis::Type_intensity NextIntensity(Csis::Type_intensity& last) {
        last = last == Csis::Type_intensity_Normal ? Csis::Type_intensity_High : Csis::Type_intensity_Normal;
        return last;
    }

    EAXAirSupport* ActiveHeli() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr ? ai->GetHeli() : nullptr;
    }

    EAXDispatch* Dispatch() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr ? ai->GetDispatch() : nullptr;
    }

    int PlayerHeat() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr ? ai->GetHeat() : 0;
    }

    float PlayerSpeed() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr ? ai->GetPlayerSpeed() : 0.0f;
    }

    float RainIntensity() {
        Rain* rain = Game::Global<Rain*>(kPlayerViewPrecipitation);
        return rain != nullptr ? rain->GetRainIntensity() : 0.0f;
    }

    void RelaxCallForRBRules() {
        gStaticRoadblock_CallForRB_DepFollow.Relax();
        gStaticRoadblock_CallForRB_reqLOS.Relax();
        gStaticRoadblock_CallForRB_OnScreenOnly.Relax();
        gStaticRoadblock_CallForRB_expiry.Relax();
    }

    void RelaxHeliBailoutRules() {
        gHeliSpecific_HeliBailout_expiry.Relax();
        gHeliSpecific_HeliBailout_CullingRange.Relax();
    }

    void CheckIn(EAXAirSupport* heli) {
        if (!gCheckInOn) return;
        ResetPlayCount(kAnytimeEvents_Unit911Reply);
        gAnytimeEvents_Unit911Reply_DepFollow.Relax();
        heli->EAXCop::Reply911();
        LOG("Check-in: the pilot checks in (%s).", IsQueuedOrPlaying(kSPCH1_EventID_Unit911Reply) ? "queued" : "turned down by the game");
    }

    void HelicopterArrived(EAXAirSupport* heli) {
        LOG("Helicopter: a helicopter joined the chase.");
        if (gFlags & AIR_SUPPORT_REQ) LOG("Air support: a helicopter showed up before anyone asked, so nobody asks.");
        gHeliInChase = heli;
        gFlags &= ~(HELI_DOWN | BAILOUT_REQ | NEW_HELI_SPOT | AIR_SUPPORT_REQ | RB_CALLED);
        if (gHelisThisPursuit++ > 0) {
            LOG("Helicopter: a new helicopter is in the chase, so its pilot uses the unused takes the first time he spots you.");
            gFlags |= NEW_HELI_SPOT;
            return;
        }
        CheckIn(heli);
    }

    void HelicopterLeft(bool inPursuit) {
        gHeliInChase = nullptr;
        if (!inPursuit || !gAirSupportOn) return;
        LOG("Helicopter: it left the chase, so a unit will ask for another one when it can see you.");
        gFlags |= AIR_SUPPORT_REQ;
    }

    void AskForAirSupport(SoundAI* ai) {
        EAXCop* caller = ai->GetRandomActiveCop(kPrimaryCops, true);
        EAXDispatch* dispatch = ai->GetDispatch();
        if (!caller || !dispatch) return;
        gFlags &= ~AIR_SUPPORT_REQ;
        LOG("Air support: unit with speaker ID %d can see you and asks for another helicopter.", caller->GetSpeakerID());
        gBackup_CallForBU_DepFollow.Relax();
        ResetPlayCount(kBackup_CallForBU);
        caller->CallForBackup(Csis::Type_disp_backup_type_Air_Support);
        ResetPlayCount(kBackup_DispBackupReply);
        dispatch->BackupReply(caller, 1, Csis::Type_disp_backup_type_Air_Support);
    }

    void PilotCallsForRB(EAXAirSupport* heli, EAXDispatch* dispatch) {
        RelaxCallForRBRules();
        ResetPlayCount(kStaticRoadblock_CallForRB);
        heli->CallForRB();
        gRoadblockUpdate = (gRoadblockUpdate + 1u) % kRoadblockUpdateCount;
        Csis::StaticRoadblock_DispRBUpdateStruct data = { dispatch->GetSpeakerID(), dispatch->GetRandomizedCode(), kRoadblockUpdates[gRoadblockUpdate].yes_no,
                                                          kRoadblockUpdates[gRoadblockUpdate].roadblock_type };
        ResetPlayCount(kStaticRoadblock_DispRBUpdate);
        Speech::ScheduledSpeechEvent* update = ScheduleSpeech(data, kStaticRoadblock_DispRBUpdate, dispatch);
        LOG("Roadblock: the pilot calls for a roadblock (%s) and dispatch answers (%s).",
            IsQueuedOrPlaying(kSPCH1_EventID_CallForRB) ? "queued" : "turned down by the game", update ? "queued" : "turned down by the game");
    }

    bool GetFuelTimeRemaining(EAXAirSupport* heli, float& seconds) {
        ISimable* simable = ISimable::FindInstance(heli->GetHandle());
        IAIHelicopter* ai = nullptr;
        if (!simable || !simable->QueryInterface(&ai)) return false;
        seconds = ai->GetFuelTimeRemaining();
        return true;
    }

    void SayDamageReport(EAXAirSupport* heli) {
        RelaxHeliBailoutRules();
        ResetPlayCount(kHeliSpecific_HeliBailout);
        Csis::HeliSpecific_HeliBailoutStruct data = { heli->GetSpeakerID(), Csis::Type_heli_bailout_type_damage_sustained };
        Speech::ScheduledSpeechEvent* event = ScheduleSpeech(data, kHeliSpecific_HeliBailout, heli);
        LOG("Going down: the pilot's damage report was %s.", event ? "queued" : "turned down by the game");
    }

    void GamesOwnBailout(EAXAirSupport* heli) {
        RelaxHeliBailoutRules();
        ResetPlayCount(kHeliSpecific_HeliBailout);
        heli->EAXAirSupport::Bailout();
    }

    void SaySearchPattern() {
        ResetPlayCount(kAnytimeEvents_LostSuspect);
        Csis::AnytimeEvents_LostSuspectStruct data = { Speech::Heli, NextIntensity(gLostSuspectIntensity) };
        Speech::ScheduledSpeechEvent* event = ScheduleSpeech(data, kAnytimeEvents_LostSuspect, nullptr);
        LOG("Losing you: the pilot's search pattern line was %s.", event ? "queued" : "turned down by the game");
    }

    void SayGoingDown(EAXAirSupport* heli) {
        if (heli->IsDead() || heli->GetHealth() < kFullHealth) {
            LOG("Going down: the helicopter was shot down, so the pilot reports damage.");
            SayDamageReport(heli);
            return;
        }
        float fuel = 0.0f;
        const bool fuelKnown = GetFuelTimeRemaining(heli, fuel);
        const Csis::Type_heli_bailout_type cause = heli->GetCauseOfBailout();
        if (cause != Csis::Type_heli_bailout_type_fuel_low || !fuelKnown || fuel <= HELI_FUEL_CRITICAL_TIME) {
            LOG("Going down: the game's own reason (cause %d, %d s of fuel left).", static_cast<int>(cause), fuelKnown ? static_cast<int>(fuel) : -1);
            GamesOwnBailout(heli);
            return;
        }
        LOG("Losing you: the helicopter lost sight of you with %d s of fuel left and no damage.", static_cast<int>(fuel));
        SaySearchPattern();
    }

    void SayUnusedRegainVisual(EAXAirSupport* heli) {
        gAnytimeEvents_RegainVisual_DepFollow.Relax();
        gAnytimeEvents_RegainVisual_expiry.Relax();
        ResetPlayCount(kAnytimeEvents_RegainVisual);
        Csis::AnytimeEvents_RegainVisualStruct data = { heli->GetSpeakerID(), Csis::Type_intensity_Normal };
        Speech::ScheduledSpeechEvent* event = ScheduleSpeech(data, kAnytimeEvents_RegainVisual, heli);
        LOG("Spotting you: the pilot's line was %s.", event ? "queued" : "turned down by the game");
    }

    void SayGamesOwnSpot(EAXAirSupport* heli, SpotLine line) {
        if (line == SpotLine::Spotted)
            heli->EAXCop::Spotted();
        else
            heli->EAXCop::RegainVisual();
    }

    void HeliSpots(EAXAirSupport* heli, SpotLine gameLine) {
        if (gFlags & NEW_HELI_SPOT) {
            gFlags &= ~NEW_HELI_SPOT;
            LOG("Spotting you: the new helicopter's pilot says he has you.");
            SayUnusedRegainVisual(heli);
            return;
        }
        const bool useUnusedTakes = gUnusedRegainVisualNext;
        gUnusedRegainVisualNext = !gUnusedRegainVisualNext;
        if (!useUnusedTakes) {
            LOG("Spotting you: this time the pilot uses the game's own takes.");
            SayGamesOwnSpot(heli, gameLine);
            return;
        }
        SayUnusedRegainVisual(heli);
    }

    void ReadDriverHistory(EAXDispatch* dispatch) {
        unsigned tier = static_cast<unsigned>(std::rand()) % kRecordTiers;
        if (tier == 0 && !HideTakes(kSPCH1_EventID_DriverHistory, kDriverHistorySample, Speech::Dispatch, 0, kMinorRecordTakes, kMinorRecordTag))
            tier = 1u + static_cast<unsigned>(std::rand()) % (kRecordTiers - 1u);
        ResetPlayCount(kAnytimeEvents_DriverHistory);
        Csis::AnytimeEvents_DriverHistoryStruct data = { dispatch->GetSpeakerID(), static_cast<Csis::Type_region>(1u << tier) };
        if (!ScheduleSpeech(data, kAnytimeEvents_DriverHistory, dispatch)) {
            LOG("Driver history: the game turned dispatch's line down.");
            ShowHiddenTakes();
            return;
        }
        LOG("Driver history: dispatch reads out record tier %u.", tier + 1u);
    }

    void AnswerHistoryRequest() {
        EAXDispatch* dispatch = Dispatch();
        if (!dispatch) return;
        if (PlayerHeat() < kDriverHistoryMinHeat) {
            LOG("Driver history: the pilot asked for your history below heat %d, so dispatch stays quiet.", kDriverHistoryMinHeat);
            return;
        }
        LOG("Driver history: the pilot asked for your history, so dispatch reads it out.");
        ReadDriverHistory(dispatch);
    }

    void PilotRepliesToBackup() {
        EAXAirSupport* heli = ActiveHeli();
        EAXDispatch* dispatch = Dispatch();
        const bool up = heli && !(gFlags & HELI_DOWN);
        if ((up && !gSwarmingOn) || (!up && (!dispatch || (PlayerHeat() < kHeliMinHeat && gHelisThisPursuit == 0)))) return;
        if (static_cast<unsigned>(std::rand()) % kBackupReplyChance != 0) {
            LOG("Backup reply: the pilot let this call for backup go.");
            return;
        }
        if (up) {
            ResetPlayCount(kHeliSpecific_HeliSwarming);
            heli->EAXAirSupport::Swarming();
            LOG("Backup reply: the pilot tells the unit he can see its cover closing in.");
            return;
        }
        ResetPlayCount(kAnytimeEvents_Unit911Reply);
        gAnytimeEvents_Unit911Reply_DepFollow.Relax();
        Csis::AnytimeEvents_Unit911ReplyStruct data = { Speech::Heli };
        if (!ScheduleSpeech(data, kAnytimeEvents_Unit911Reply, dispatch)) {
            LOG("Backup reply: the game turned the pilot's line down.");
            return;
        }
        gFlags |= BACKUP_REPLYING;
        LOG("Backup reply: no helicopter is up, so the pilot answers on his way in.");
    }

    void BroadcastThemeChange(EAXDispatch* dispatch) {
        if (!HideTakes(kSPCH1_EventID_DispPursEscGen, kDispPursEscGenSample, Speech::Dispatch, kOddMultipleSuspectsTake, 1, kMultipleSuspectsTag))
            LOG("Pursuit theme: ENTRY_05440 couldn't be hidden, so all four multiple-vehicles takes can play.");
        ResetPlayCount(kAnytimeEvents_DispPursEscGen);
        Csis::AnytimeEvents_DispPursEscGenStruct data = { dispatch->GetSpeakerID(), Csis::Type_num_suspects_multiple_suspects };
        if (!ScheduleSpeech(data, kAnytimeEvents_DispPursEscGen, dispatch)) {
            LOG("Pursuit theme: the game turned dispatch's broadcast down.");
            ShowHiddenTakes();
            return;
        }
        gFlags |= THEME_BROADCAST;
        LOG("Pursuit theme: dispatch makes her multiple-vehicles broadcast.");
    }

    void NoteThemeChange(SoundAI* ai) {
        const int theme = SFXCTL_Pathfinder::m_curinteractive;
        if (!(gFlags & THEME_KNOWN)) {
            gPursuitTheme = theme;
            gFlags |= THEME_KNOWN;
            return;
        }
        if (theme == gPursuitTheme) return;
        gPursuitTheme = theme;
        LOG("Pursuit theme: the music moved on to theme %d.", theme + 1);
        if (!ai->GetHeli() || (gFlags & HELI_DOWN)) {
            LOG("Pursuit theme: no helicopter is up, so the radio waits for the next theme change.");
            return;
        }
        EAXDispatch* dispatch = ai->GetDispatch();
        if (!dispatch || IsQueuedOrPlaying(kSPCH1_EventID_DispPursEscGen)) return;
        BroadcastThemeChange(dispatch);
    }

    void SayVehicleReport() {
        EAXAirSupport* heli = ActiveHeli();
        if (!gVehicleReportOn || !heli || (gFlags & HELI_DOWN) || !heli->IsActive()) return;
        heli->VehicleReport();
        LOG("Vehicle report: dispatch's theme-change broadcast is over, so the pilot describes your car (%s).",
            IsQueuedOrPlaying(kSPCH1_EventID_VehicleReport) ? "queued" : "turned down by the game");
    }

    void SayWeatherReport(EAXAirSupport* heli) {
        if ((gFlags & RAIN_REPORTED) || !(RainIntensity() > kRainReportLevel) || !heli->IsActive()) return;
        gFlags |= RAIN_REPORTED;
        ResetPlayCount(kAnytimeEvents_WeatherReport);
        heli->EAXCop::WeatherReport();
        LOG("Weather: it started raining, so the pilot reports the weather (%s).",
            IsQueuedOrPlaying(kSPCH1_EventID_WeatherReport) ? "queued" : "turned down by the game");
    }

    void EndPursuit() {
        gFlags &= RAIN_REPORTED;
        gHelisThisPursuit = 0;
        ShowHiddenTakes();
    }

    void OnEventComplete(SPCHType_1_EventID id, EAXCharacter* actor) {
        switch (id) {
        case kSPCH1_EventID_InitialCallForBU:
        case kSPCH1_EventID_CallForBU:
            if (actor && actor->GetSpeakerID() != Speech::Heli) gFlags |= BACKUP_CALLED;
            break;
        case kSPCH1_EventID_DispBackupReply:
        case kSPCH1_EventID_DispBUETA:
            if (!(gFlags & BACKUP_CALLED)) break;
            gFlags &= ~BACKUP_CALLED;
            if (gBackupReplyOn) PilotRepliesToBackup();
            break;
        case kSPCH1_EventID_Unit911Reply:
            gFlags &= ~BACKUP_REPLYING;
            if (!(gFlags & HISTORY_REQ)) break;
            gFlags &= ~HISTORY_REQ;
            AnswerHistoryRequest();
            break;
        case kSPCH1_EventID_DispPursEscGen:
            if (!(gFlags & THEME_BROADCAST)) break;
            gFlags &= ~THEME_BROADCAST;
            LOG("Pursuit theme: dispatch finished her broadcast.");
            SayVehicleReport();
            break;
        case kSPCH1_EventID_SuspectBehaviour:
            gFlags &= ~DRIVING_LINE_REQ;
            break;
        default:
            break;
        }
    }

    int __cdecl TakeOffsetHook(const void* header, int take, uint32_t* offset, uint32_t* size) {
        if (gDriverHistoryOn && IsHeliCheckIn(header) && take >= kHistoryRequestFirstTake && take <= kHistoryRequestLastTake) gFlags |= HISTORY_REQ;
        LogTake(header, take);
        return Game::Call<int>(kTakeOffset, header, take, offset, size);
    }

    bool __cdecl AllowTake(const void* header, int take) {
        if (!gBackupReplyOn || !IsHeliCheckIn(header)) return true;
        const bool backupTake = take >= 0 && take < kBackupReplyTakes;
        return (gFlags & BACKUP_REPLYING) ? backupTake : !backupTake;
    }

    __declspec(naked) void TakeCheckHook() {
        __asm {
            push eax
            push ecx
            push edx
            push ebx
            push esi
            call AllowTake
            add esp, 8
            test al, al
            pop edx
            pop ecx
            pop eax
            jz blocked
            jmp dword ptr [gTakeCheckOriginal]
        blocked:
            xor eax, eax
            ret
        }
    }

    template <typename Hook> bool ReplaceVirtual(const SpeechEvent& event, uintptr_t vtable, unsigned slot, Hook hook) {
        return event.VerifyVirtual(vtable, slot) && Patch::ReplaceVirtual(event.name, vtable, slot, event.function, Game::MethodAddress(hook));
    }

}

void Speech::Manager::NotifyEventCompletion(ScheduledSpeechEvent* evt, bool playback_complete) {
    Game::Call<void>(gNotifyEventCompletionOriginal, evt, playback_complete);
}

void Speech::Manager::NotifyEventCompletionHook(ScheduledSpeechEvent* evt, bool playback_complete) {
    if (evt == nullptr || !playback_complete) {
        NotifyEventCompletion(evt, playback_complete);
        return;
    }
    const SPCHType_1_EventID id = evt->ID;
    EAXCharacter* actor = evt->actor;
    NotifyEventCompletion(evt, playback_complete);
    OnEventComplete(id, actor);
}

void Speech::StrategyFlow::MessageReqBackup(const MReqBackup& message) {
    Game::ThisCall<void>(gMessageReqBackupOriginal, this, &message);
}

void Speech::StrategyFlow::MessageReqBackupHook(const MReqBackup& message) {
    MessageReqBackup(message);
    const int type = message.GetBackupType();
    if (type != kRoadblockBackupType && type != kStrategyBackupType) return;
    LOG("Roadblock: the police are setting up a roadblock.");
    SoundAI* ai = SoundAI::Get();
    EAXAirSupport* heli = ai != nullptr ? ai->GetHeli() : nullptr;
    EAXDispatch* dispatch = ai != nullptr ? ai->GetDispatch() : nullptr;
    if (!heli || !dispatch || (gFlags & (HELI_DOWN | RB_CALLED)) || !heli->IsActive()) {
        LOG("Roadblock: the pilot doesn't take this request (no helicopter up, already asked, or out of sight).");
        return;
    }
    gFlags |= RB_CALLED;
    PilotCallsForRB(heli, dispatch);
}

int MiscSpeech::LostSuspectHook(int spkrID) {
    if (ActiveHeli() == nullptr) {
        if (spkrID != Speech::Heli) return LostSuspect(spkrID);
        LOG("Losing you: the helicopter is gone, so a ground unit makes the call instead of its pilot.");
        return LostSuspect(0);
    }

    ResetPlayCount(kAnytimeEvents_LostSuspect);
    Csis::AnytimeEvents_LostSuspectStruct data = { Speech::Heli, NextIntensity(gLostSuspectIntensity) };
    Speech::ScheduledSpeechEvent* event = ScheduleSpeech(data, kAnytimeEvents_LostSuspect, nullptr);
    LOG("Losing you: the pilot's line was %s.", event ? "queued" : "turned down by the game");
    return Speech::Heli;
}

void SoundAI::UpdateStateMachinesHook() {
    UpdateStateMachines();
    RestoreRulesOfFinishedLines();
    ShowHiddenTakesOfFinishedLine();
    if (!(RainIntensity() > 0.0f)) gFlags &= ~RAIN_REPORTED;
    const bool inPursuit = GetPursuitDuration() >= 0.0f;
    if (gHeliInChase != nullptr && GetHeli() == nullptr) HelicopterLeft(inPursuit);
    if (!inPursuit) {
        EndPursuit();
        return;
    }
    if (gThemeBroadcastOn) NoteThemeChange(this);
    if ((gFlags & AIR_SUPPORT_REQ) && GetHeli() == nullptr && !Speech::Manager::IsCopSpeechBusy()) AskForAirSupport(this);
}

void SoundAI::AddNewHeliHook(IVehicle* heli) {
    AddNewHeli(heli);
    if (EAXAirSupport* chopper = GetHeli()) HelicopterArrived(chopper);
}

void EAXDispatch::PursuitUpdateHook(EAXCop* cop) {
    PursuitUpdate(cop);
    EAXAirSupport* heli = ActiveHeli();
    if (!gPursuitUpdateReplyOn || !heli || cop != heli || (gFlags & HELI_DOWN)) return;
    LOG("Pursuit update: dispatch is asking the helicopter for an update.");
    if (PlayerHeat() < kDrivingLineMinHeat || PlayerSpeed() < kDrivingLineMinSpeed) {
        LOG("Pursuit update: too slow or too little heat for the driving lines, so the pilot gives his usual update.");
        return;
    }
    ResetPlayCount(kAnytimeEvents_SuspectBehaviour);
    Csis::AnytimeEvents_SuspectBehaviourStruct data = { heli->GetSpeakerID(), Csis::Type_num_suspects_one_suspect };
    if (!ScheduleSpeech(data, kAnytimeEvents_SuspectBehaviour, heli)) {
        LOG("Pursuit update: the game turned the driving lines down, so the pilot gives his usual update.");
        return;
    }
    gFlags |= DRIVING_LINE_REQ;
    LOG("Pursuit update: the pilot answers by describing your driving.");
}

void EAXAirSupport::UpdateHook() {
    EAXAirSupport::Update();
    if (gFlags & BAILOUT_REQ) {
        gFlags &= ~BAILOUT_REQ;
        SayGoingDown(this);
    } else if (!(gFlags & HELI_DOWN) && IsDead()) {
        gFlags |= HELI_DOWN;
        LOG("Going down: the helicopter was destroyed.");
        SayDamageReport(this);
    }
    if (!(gFlags & HELI_DOWN) && gWeatherReportOn) SayWeatherReport(this);
}

void EAXAirSupport::SetHandleHook(HSIMABLE handle) {
    const HSIMABLE previous = GetHandle();
    EAXCharacter::SetHandle(handle);
    if (handle != nullptr && previous != nullptr && handle != previous) HelicopterArrived(this);
}

void EAXAirSupport::IntentToRamHook() {
    Csis::AnytimeEvents_IntentToRamStruct data = { GetSpeakerID(), NextIntensity(gIntentToRamIntensity) };
    ScheduleSpeech(data, kAnytimeEvents_IntentToRam, this);
}

void EAXAirSupport::StrategyResetHook(bool) {
    gNewStrategy = !gNewStrategy;
    EAXCop::StrategyReset(gNewStrategy);
}

void EAXAirSupport::BailoutHook() {
    const bool hit = gOnCollisionReturn && reinterpret_cast<uintptr_t>(_ReturnAddress()) == gOnCollisionReturn;
    if (gFlags & HELI_DOWN) {
        LOG("Going down: skipped the game's second going-down call.");
        return;
    }
    gFlags |= HELI_DOWN;
    if (hit) {
        LOG("Going down: the helicopter was hit hard, so the pilot reports damage.");
        SayDamageReport(this);
        return;
    }
    if (!gBailoutCauseOn) {
        GamesOwnBailout(this);
        return;
    }
    gFlags |= BAILOUT_REQ;
}

void EAXAirSupport::HazardAlertHook(Csis::Type_heli_hazard_alert_type type) {
    if ((gFlags & HELI_DOWN) || IsDead()) {
        LOG("Hazard warning: dropped, the helicopter is leaving the chase.");
        return;
    }
    EAXAirSupport::HazardAlert(type);
}

void EAXAirSupport::RegainVisualHook() {
    HeliSpots(this, SpotLine::RegainVisual);
}

void EAXAirSupport::SpottedHook() {
    HeliSpots(this, SpotLine::Spotted);
}

void EAXAirSupport::SwarmingHook() {
    if (ActiveHeli() && !(gFlags & HELI_DOWN)) {
        EAXAirSupport::Swarming();
        return;
    }
    LOG("Swarming: the helicopter is down or gone, so the pilot doesn't report cover units closing in.");
}

void EAXAirSupport::PursuitUpdateReplyHook() {
    if ((gFlags & DRIVING_LINE_REQ) && IsQueuedOrPlaying(kSPCH1_EventID_SuspectBehaviour)) return;
    gFlags &= ~DRIVING_LINE_REQ;
    EAXCop::PursuitUpdateReply();
}

void EAXAirSupport::SuspectBehaviorHook() {
    SoundAI* ai = SoundAI::Get();
    EAXCop* unit = ai != nullptr ? ai->FindClosestCop(true, false) : nullptr;
    if (!unit) {
        LOG("Driving lines: the game picked the pilot and no ground unit is close, so nobody says one.");
        return;
    }
    LOG("Driving lines: the game picked the pilot, so the closest ground unit describes your driving instead.");
    unit->SuspectBehavior();
}

bool SoundAI::Init(void* module) {
    Log::Open(module);
    LOG("NFSMWRevisedAirSupport %s starting.", kVersion);
    std::srand(GetTickCount());
    LoadSpeechEntries();

    const bool intentToRam = ReplaceVirtual(kAnytimeEvents_IntentToRam, kEAXAirSupportVTable, kIntentToRamSlot, &EAXAirSupport::IntentToRamHook);
    if (intentToRam)
        LOG("%s restored: the pilot switches between the unused normal takes and the game's intense ones when he goes in to ram you.",
            kAnytimeEvents_IntentToRam.name);

    const bool strategyReset = ReplaceVirtual(kOutcome_StrategyReset, kEAXAirSupportVTable, kStrategyResetSlot, &EAXAirSupport::StrategyResetHook);
    if (strategyReset)
        LOG("%s restored: the pilot alternates the 'go again' and 'hold on station' takes when he calls a reset.", kOutcome_StrategyReset.name);

    const bool lostSuspect = kAnytimeEvents_LostSuspect.Verify()
                          && Patch::RedirectCall("SoundAI::TerminatePursuit", kTerminatePursuitLostSuspectCall, kMiscSpeech_LostSuspect,
                                                 reinterpret_cast<const void*>(&MiscSpeech::LostSuspectHook));
    if (lostSuspect)
        LOG("%s restored: the pilot calls it when the police lose you while a helicopter is out.", kAnytimeEvents_LostSuspect.name);

    const bool heliUpdate = Patch::ReplaceVirtual("EAXAirSupport::Update", kEAXAirSupportVTable, kUpdateSlot, kEAXAirSupport_Update,
                                                  Game::MethodAddress(&EAXAirSupport::UpdateHook));

    if (Memory::Matches(kOnCollisionBailout, kOnCollisionBailoutCode, sizeof(kOnCollisionBailoutCode)))
        gOnCollisionReturn = kOnCollisionBailout + sizeof(kOnCollisionBailoutCode);
    gBailoutCauseOn = heliUpdate && Memory::Matches(kEAXAirSupportUpdateFuelCheck, kEAXAirSupportUpdateFuelCheckCode, sizeof(kEAXAirSupportUpdateFuelCheckCode))
                   && Patch::CallsTo(kBailoutGetCauseOfBailoutCall, kEAXAirSupport_GetCauseOfBailout);
    const bool bailout = ReplaceVirtual(kHeliSpecific_HeliBailout, kEAXAirSupportVTable, kBailoutSlot, &EAXAirSupport::BailoutHook);
    if (bailout && gBailoutCauseOn)
        LOG("%s fixed: a helicopter that leaves with fuel left reports damage or a search pattern, not low fuel.", kHeliSpecific_HeliBailout.name);
    const bool hazardAlert = bailout && ReplaceVirtual(kHeliSpecific_HeliHazardAlert, kEAXAirSupportVTable, kHazardAlertSlot, &EAXAirSupport::HazardAlertHook);
    if (hazardAlert)
        LOG("%s fixed: the pilot gives no hazard warnings after his going-down call.", kHeliSpecific_HeliHazardAlert.name);

    const bool regainVisual = ReplaceVirtual(kAnytimeEvents_RegainVisual, kEAXAirSupportVTable, kRegainVisualSlot, &EAXAirSupport::RegainVisualHook);
    if (regainVisual)
        LOG("%s restored: when the pilot finds you again, he switches between his unused takes and the game's own.", kAnytimeEvents_RegainVisual.name);
    const bool spotted = regainVisual && ReplaceVirtual(kAnytimeEvents_Spotted, kEAXAirSupportVTable, kSpottedSlot, &EAXAirSupport::SpottedHook);
    if (spotted)
        LOG("%s: when the pilot first spots you, he also switches between those unused takes and the game's own.", kAnytimeEvents_Spotted.name);

    const bool canPlayback = Patch::Replace("Speech::Manager::CanPlayback", kManager_CanPlayback, kCanPlaybackEntry, kReturnTrue, sizeof(kReturnTrue));
    if (canPlayback)
        LOG("Police radio: lines that already played can keep playing later in long pursuits.");

    const bool stateMachines = Patch::CallsTo(kOnTaskUpdateStateMachinesCall, kSoundAI_UpdateStateMachines)
                            && Patch::RedirectCall("SoundAI::UpdateStateMachines", kOnTaskUpdateStateMachinesCall, kSoundAI_UpdateStateMachines,
                                                   Game::MethodAddress(&SoundAI::UpdateStateMachinesHook));
    const bool eventComplete = Patch::Detour("Speech::Manager::NotifyEventCompletion", kManager_NotifyEventCompletion, kNotifyEventCompletionEntry,
                                             sizeof(kNotifyEventCompletionEntry), reinterpret_cast<const void*>(&Speech::Manager::NotifyEventCompletionHook),
                                             gNotifyEventCompletionOriginal);
    const bool heliArrival = Patch::CallsTo(kSyncCarsToActorsAddNewHeliCall, kSoundAI_AddNewHeli)
                          && Patch::RedirectCall("SoundAI::AddNewHeli", kSyncCarsToActorsAddNewHeliCall, kSoundAI_AddNewHeli,
                                                 Game::MethodAddress(&SoundAI::AddNewHeliHook))
                          && Patch::ReplaceVirtual("EAXCharacter::SetHandle", kEAXAirSupportVTable, kSetHandleSlot, kEAXCharacter_SetHandle,
                                                   Game::MethodAddress(&EAXAirSupport::SetHandleHook));
    if (!stateMachines || !eventComplete || !heliArrival)
        LOG("SoundAI: another mod changed the speech flows this mod hooks, so the radio exchanges below are off.");

    gAirSupportOn = stateMachines && kBackup_CallForBU.VerifyVirtual(kEAXCopVTable, kCallForBackupSlot) && kBackup_DispBackupReply.Verify();
    if (gAirSupportOn)
        LOG("%s restored: when a helicopter goes down, a unit that can see you asks for another one on the radio.", kBackup_CallForBU.name);

    gCheckInOn = heliArrival && eventComplete && kAnytimeEvents_Unit911Reply.VerifyVirtual(kEAXAirSupportVTable, kReply911Slot);
    if (gCheckInOn)
        LOG("%s restored: the pilot checks in when the first helicopter joins.", kAnytimeEvents_Unit911Reply.name);

    gRoadblockOn = heliArrival && kStaticRoadblock_DispRBUpdate.Verify() && kStaticRoadblock_CallForRB.Verify()
                && Patch::Detour("Speech::StrategyFlow::MessageReqBackup", kStrategyFlow_MessageReqBackup, kMessageReqBackupEntry,
                                 sizeof(kMessageReqBackupEntry), Game::MethodAddress(&Speech::StrategyFlow::MessageReqBackupHook),
                                 gMessageReqBackupOriginal);
    if (gRoadblockOn)
        LOG("%s restored: when the police set up a roadblock and a helicopter has been out long enough, the pilot calls it and dispatch answers.",
            kStaticRoadblock_CallForRB.name);

    const bool takesHooked = Patch::CallsTo(kTakeOffsetCall, kTakeOffset)
                          && Patch::RedirectCall("Speech player take lookup", kTakeOffsetCall, kTakeOffset, reinterpret_cast<const void*>(&TakeOffsetHook));
    if (!takesHooked)
        LOG("Speech player: another mod changed the game's take lookup, so take-based calls are off.");
    gDriverHistoryOn = takesHooked && eventComplete && kAnytimeEvents_DriverHistory.Verify();
    if (gDriverHistoryOn)
        LOG("%s restored: when the pilot asks for your history at heat 5 and up, dispatch reads out your record.", kAnytimeEvents_DriverHistory.name);

    gSwarmingOn = ReplaceVirtual(kHeliSpecific_HeliSwarming, kEAXAirSupportVTable, kSwarmingSlot, &EAXAirSupport::SwarmingHook);
    if (gSwarmingOn)
        LOG("%s fixed: the pilot only reports cover units closing in while his helicopter is up.", kHeliSpecific_HeliSwarming.name);

    const bool takeChoiceHooked = takesHooked && gCheckInOn && Patch::CallsTo(kTakeCheckCall, kTakeCheck)
                               && Patch::RedirectCall("Speech player take choice", kTakeCheckCall, kTakeCheck, reinterpret_cast<const void*>(&TakeCheckHook));
    if (takesHooked && gCheckInOn && !takeChoiceHooked)
        LOG("Speech player: another mod changed how the game picks takes, so the pilot's backup reply is off.");
    gBackupReplyOn = takeChoiceHooked;
    if (gBackupReplyOn)
        LOG("%s: after a unit calls for backup and dispatch answers, the pilot now and then answers too.", kBackup_CallForBU.name);

    gPursuitUpdateReplyOn = kAnytimeEvents_SuspectBehaviour.VerifyVirtual(kEAXAirSupportVTable, kSuspectBehaviorSlot)
                         && Patch::CallsTo(kDealWithDeadAirPursuitUpdateCall, kEAXDispatch_PursuitUpdate)
                         && ReplaceVirtual(kAnytimeEvents_PursuitUpdateRep, kEAXAirSupportVTable, kPursuitUpdateReplySlot, &EAXAirSupport::PursuitUpdateReplyHook)
                         && Patch::RedirectCall("EAXDispatch::PursuitUpdate", kDealWithDeadAirPursuitUpdateCall, kEAXDispatch_PursuitUpdate,
                                                Game::MethodAddress(&EAXDispatch::PursuitUpdateHook));
    if (gPursuitUpdateReplyOn)
        LOG("%s restored: when dispatch asks the helicopter for an update, the pilot describes your driving.", kAnytimeEvents_PursuitUpdateRep.name);
    const bool suspectBehavior = gPursuitUpdateReplyOn
                              && ReplaceVirtual(kAnytimeEvents_SuspectBehaviour, kEAXAirSupportVTable, kSuspectBehaviorSlot, &EAXAirSupport::SuspectBehaviorHook);
    if (suspectBehavior)
        LOG("%s fixed: the pilot only describes your driving when dispatch asks him; the game's own driving calls go to ground units.",
            kAnytimeEvents_SuspectBehaviour.name);

    gThemeBroadcastOn = stateMachines && eventComplete && kAnytimeEvents_DispPursEscGen.Verify();
    if (gThemeBroadcastOn)
        LOG("%s: when the pursuit music moves to the next theme and a helicopter is up, dispatch makes a broadcast.", kAnytimeEvents_DispPursEscGen.name);
    gVehicleReportOn = gThemeBroadcastOn && kSetup_VehicleReport.VerifyVirtual(kEAXAirSupportVTable, kVehicleReportSlot);
    if (gVehicleReportOn)
        LOG("%s restored: after dispatch's theme-change broadcast, the pilot describes your car.", kSetup_VehicleReport.name);

    gWeatherReportOn = heliUpdate && stateMachines && kAnytimeEvents_WeatherReport.VerifyVirtual(kEAXAirSupportVTable, kWeatherReportSlot);
    if (gWeatherReportOn)
        LOG("%s restored: when it starts raining, the pilot reports the weather.", kAnytimeEvents_WeatherReport.name);

    return intentToRam || strategyReset || lostSuspect || bailout || regainVisual || canPlayback || gAirSupportOn || gCheckInOn || gRoadblockOn
        || gDriverHistoryOn || gSwarmingOn || gBackupReplyOn || gPursuitUpdateReplyOn || gThemeBroadcastOn || gVehicleReportOn || gWeatherReportOn;
}

void SoundAI::Restore() {
    ShowHiddenTakes();
    RestoreAllRules();
    Patch::RestoreAll();
    Log::Close();
}
