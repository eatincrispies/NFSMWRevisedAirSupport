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

#define TIMER_SHIFT_VALUE_INT 4000

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
        int      gCount = 0;

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

    constexpr uintptr_t kWorldTimer                        = 0x00925AE8u;
    constexpr uintptr_t kgHeliVehicle                      = 0x0090D61Cu;
    constexpr uintptr_t kSFXCTL_Pathfinder_m_curinteractive = 0x009121E8u;
    constexpr uintptr_t kSingleton_SoundAI_mInstance       = 0x00993CC8u;
    constexpr uintptr_t kManager_m_SpeechModule            = 0x0099222Cu;
    constexpr uintptr_t kManager_mGlobalHistory            = 0x00992718u;
    constexpr uintptr_t kManager_mEvents                   = 0x00993390u;
    constexpr uintptr_t kPlayerViewPrecipitation           = 0x009196B8u;

    constexpr uintptr_t kObject_IList_Find                    = 0x005D59F0u;
    constexpr uintptr_t kISimable_FindInstance                = 0x0041AD40u;
    constexpr uintptr_t kIAIHelicopter_IHandle                = 0x00404060u;
    constexpr uintptr_t kAttrib_FindCollection                = 0x00455FD0u;
    constexpr uintptr_t kAttrib_Instance_GetAttributePointer  = 0x00454810u;
    constexpr uintptr_t kEventHistory_Find                    = 0x004CB3F0u;
    constexpr uintptr_t kManager_ScheduleSpeechPartII         = 0x00713B20u;
    constexpr uintptr_t kManager_IsCopSpeechBusy              = 0x007040C0u;
    constexpr uintptr_t kManager_CanPlayback                  = 0x00704490u;
    constexpr uintptr_t kStrategyFlow_MessageReqBackup        = 0x007048C0u;
    constexpr uintptr_t kSoundAI_FindClosestCop               = 0x00708390u;
    constexpr uintptr_t kMiscSpeech_LostSuspect               = 0x0071D960u;
    constexpr uintptr_t kEAXCharacter_DriverHistory           = 0x00717020u;
    constexpr uintptr_t kEAXCop_Update                        = 0x00707BF0u;
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
    constexpr uintptr_t kEAXDispatch_BackupETA                = 0x00717860u;
    constexpr uintptr_t kEAXDispatch_PursuitEscalationGeneric = 0x00717190u;
    constexpr uintptr_t kEAXDispatch_RBUpdate                 = 0x007176D0u;
    constexpr uintptr_t kEAXAirSupport_Update                 = 0x00709F50u;
    constexpr uintptr_t kEAXAirSupport_Bailout                = 0x00717C00u;
    constexpr uintptr_t kEAXAirSupport_Swarming               = 0x00717C40u;
    constexpr uintptr_t kEAXAirSupport_HazardAlert            = 0x00717CA0u;
    constexpr uintptr_t kEAXAirSupport_GetCauseOfBailout      = 0x00707860u;

    constexpr uintptr_t kEAXCopVTable        = 0x008B1FA0u;
    constexpr uintptr_t kEAXAirSupportVTable = 0x008B2278u;

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

    constexpr uint8_t   kScheduleSpeechPartIIEntry[6]   = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr uint8_t   kCanPlaybackEntry[6]            = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    constexpr uint8_t   kReturnTrue[6]                  = { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 };
    constexpr uint8_t   kMessageReqBackupEntry[7]       = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x50, 0x10 };
    constexpr int       kRoadblockBackupType            = 0x40;
    constexpr int       kStrategyBackupType             = 0x10;
    constexpr uintptr_t kOnCollisionBailout             = 0x0071BCF5u;
    constexpr uint8_t   kOnCollisionBailoutCode[6]      = { 0xFF, 0x90, 0x04, 0x01, 0x00, 0x00 };
    constexpr uintptr_t kEAXAirSupportUpdateFuelCheck   = 0x00709F75u;
    constexpr uint8_t   kEAXAirSupportUpdateFuelCheckCode[40] = {
        0x8B, 0x16, 0x8B, 0xCE, 0xFF, 0x52, 0x28, 0x50, 0xE8, 0xBE, 0x0D, 0xD1, 0xFF, 0x8B, 0x48, 0x04, 0x83, 0xC4, 0x04, 0x68,
        0x60, 0x40, 0x40, 0x00, 0xE8, 0x5E, 0xBA, 0xEC, 0xFF, 0x85, 0xC0, 0x74, 0x1E, 0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52, 0x34,
    };
    constexpr uintptr_t kBailoutGetCauseOfBailoutCall   = 0x00717C0Du;
    constexpr uintptr_t kTerminatePursuitLostSuspectCall = 0x0071F758u;

    constexpr uintptr_t kSampleHeaders     = 0x009C2C10u;
    constexpr uintptr_t kSampleHeaderCount = 0x009C2C1Cu;
    constexpr unsigned  kSampleEntrySize   = 0x08u;
    constexpr unsigned  kSampleEntryHeader = 0x04u;
    constexpr uint32_t  kMostSampleHeaders = 4096u;
    constexpr unsigned  kHeaderDimensions  = 0x04u;
    constexpr unsigned  kHeaderTakeList    = 0x0Cu;
    constexpr uint8_t   kDimensionMask     = 0x7Fu;
    constexpr uint8_t   kUnusedTag         = 7u;
    constexpr uintptr_t kTakeOffsetCall    = 0x00833EEBu;
    constexpr uintptr_t kTakeOffset        = 0x008349FAu;
    constexpr uintptr_t kTakeCheckCall     = 0x0083344Cu;
    constexpr uintptr_t kTakeCheck         = 0x00833087u;

    constexpr uint32_t kspeech        = 0xC593DD47u;
    constexpr uint32_t kDepFollow     = 0xC8C5D475u;
    constexpr uint32_t kreqLOS        = 0xE0241FC1u;
    constexpr uint32_t kOnScreenOnly  = 0x4B331604u;
    constexpr uint32_t kBackup_CallForBUSpeech           = 0xA4911F22u;
    constexpr uint32_t kBackup_DispBackupReplySpeech     = 0x732FA60Au;
    constexpr uint32_t kAnytimeEvents_Unit911ReplySpeech = 0xC6B1C631u;
    constexpr uint32_t kAnytimeEvents_RegainVisualSpeech = 0xFD58F23Du;
    constexpr uint32_t kStaticRoadblock_CallForRBSpeech  = 0x26EF7810u;
    constexpr uint32_t kHeliSpecific_HeliBailoutSpeech   = 0x602ACE63u;
    constexpr unsigned kCollectionLayout    = 0x18u;
    constexpr uint16_t kMostDepFollows      = 16u;
    constexpr uint16_t kDepFollowSize       = 0x0Cu;
    constexpr uint16_t kWideArrayHeader     = 0x8000u;
    constexpr float    kLongestExpiry       = 30.0f;
    constexpr float    kRelaxedExpiry       = 20.0f;
    constexpr float    kLongestCullingRange = 100000.0f;
    constexpr float    kRelaxedCullingRange = 100000.0f;

    constexpr uint8_t  kLinePriority      = 100u;
    constexpr uint8_t  kInterruptPriority = 200u;
    constexpr float    HELI_FUEL_CRITICAL_TIME = 8.0f;
    constexpr float    kFullHealth        = 1.0f;

    constexpr int      kAirSupportCallers[] = { Speech::Primary1, Speech::Primary2, Speech::Primary3, Speech::Cross };
    constexpr unsigned kMostCallers         = 32u;
    constexpr unsigned kMostActors          = 64u;
    constexpr unsigned kMostQueuedEvents    = 256u;
    constexpr unsigned kMostHiddenTakes     = 4u;
    constexpr int      kCopSpeechModule     = 1;
    constexpr int      kEventQueues         = 4;

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

    constexpr unsigned int kRetry               = 1u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kLineTimeout         = 25u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRadioHoldTimeout    = 20u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRuleRelaxTime       = 25u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kBailoutSettle       = TIMER_SHIFT_VALUE_INT / 4u;
    constexpr unsigned int kTunnelAlertDelay    = 1u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRegainVisualRelax   = 10u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kSpotWindow          = 15u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kSpotSearchWindow    = 5u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kSpotQuiet           = 10u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kCheckInGiveUp       = 45u * TIMER_SHIFT_VALUE_INT;
    constexpr int          kAirSupportTries     = 3;
    constexpr unsigned int kAirSupportGiveUp    = 15u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRoadblockWait       = 120u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRoadblockRequestWindow = 10u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kRoadblockGiveUp     = 30u * TIMER_SHIFT_VALUE_INT;
    constexpr int          kRoadblockTries      = 3;
    constexpr unsigned int kHiddenTakeTimeout   = 30u * TIMER_SHIFT_VALUE_INT;
    constexpr int          kDriverHistoryMinHeat = 5;
    constexpr unsigned int kDriverHistoryWindow = 60u * TIMER_SHIFT_VALUE_INT;
    constexpr int          kHeliMinHeat         = 4;
    constexpr unsigned     kBackupReplyChance   = 4u;
    constexpr unsigned int kBackupWindow        = 15u * TIMER_SHIFT_VALUE_INT;
    constexpr int          kDrivingLineMinHeat  = 3;
    constexpr float        kDrivingLineMinSpeed = 45.0f;
    constexpr unsigned int kDrivingLineGap      = 60u * TIMER_SHIFT_VALUE_INT;
    constexpr unsigned int kUpdateReplyWindow   = 30u * TIMER_SHIFT_VALUE_INT;
    constexpr float        kThemeSettleSeconds  = 30.0f;
    constexpr unsigned int kVehicleReportWindow = 45u * TIMER_SHIFT_VALUE_INT;
    constexpr float        kRainReportLevel     = 0.25f;

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
        uintptr_t          iid;
        uint32_t           crcs;
        uintptr_t          fh;
        uintptr_t          function;
        unsigned           pushes;
        SPCHType_1_EventID id;

        Csis::InterfaceId& Id() const {
            return Game::Global<Csis::InterfaceId>(iid);
        }

        Csis::FunctionHandle& Handle() const {
            return Game::Global<Csis::FunctionHandle>(fh);
        }

        bool Verify() const;
        bool VerifyVirtual(uintptr_t vtable, unsigned slot) const;
    };

    constexpr SpeechEvent kAnytimeEvents_IntentToRam       = { "AnytimeEvents_IntentToRam",       0x00901E5Cu, 0x6C5E5BA7u, 0x00992544u, kEAXCop_IntentToRam,                   0x43u,  kSPCH1_EventID_IntentToRam };
    constexpr SpeechEvent kOutcome_StrategyReset           = { "Outcome_StrategyReset",           0x00901D84u, 0x4EA05BA7u, 0x0099242Cu, kEAXCop_StrategyReset,                 0x42u,  kSPCH1_EventID_StrategyReset };
    constexpr SpeechEvent kAnytimeEvents_LostSuspect       = { "AnytimeEvents_LostSuspect",       0x00901E1Cu, 0x34A45BA7u, 0x009924ECu, kMiscSpeech_LostSuspect,               0x6Fu,  kSPCH1_EventID_LostSuspect };
    constexpr SpeechEvent kHeliSpecific_HeliBailout        = { "HeliSpecific_HeliBailout",        0x00901EDCu, 0x23945BA7u, 0x009924D4u, kEAXAirSupport_Bailout,                0x13u,  kSPCH1_EventID_HeliBailout };
    constexpr SpeechEvent kHeliSpecific_HeliHazardAlert    = { "HeliSpecific_HeliHazardAlert",    0x00901EF4u, 0x4AFB5BA7u, 0x0099235Cu, kEAXAirSupport_HazardAlert,            0x0Bu,  kSPCH1_EventID_HeliHazardAlert };
    constexpr SpeechEvent kAnytimeEvents_RegainVisual      = { "AnytimeEvents_RegainVisual",      0x00901E14u, 0x51D85BA7u, 0x0099240Cu, kEAXCop_RegainVisual,                  0x43u,  kSPCH1_EventID_RegainVisual };
    constexpr SpeechEvent kAnytimeEvents_Spotted           = { "AnytimeEvents_Spotted",           0x00901E94u, 0x0CB35BA7u, 0x00992524u, kEAXCop_Spotted,                       0x30u,  kSPCH1_EventID_Spotted };
    constexpr SpeechEvent kAnytimeEvents_Unit911Reply      = { "AnytimeEvents_Unit911Reply",      0x00901DF4u, 0x383D5BA7u, 0x009926CCu, kEAXCop_Reply911,                      0x05u,  kSPCH1_EventID_Unit911Reply };
    constexpr SpeechEvent kBackup_CallForBU                = { "Backup_CallForBU",                0x00901C84u, 0x398D5BA7u, 0x00992564u, kEAXCop_CallForBackup,                 0x1Fu,  kSPCH1_EventID_CallForBU };
    constexpr SpeechEvent kBackup_DispBackupReply          = { "Backup_DispBackupReply",          0x00901C94u, 0x5C2F5BA7u, 0x009926B0u, kEAXDispatch_BackupReply,              0x4Au,  kSPCH1_EventID_DispBackupReply };
    constexpr SpeechEvent kBackup_DispBUETA                = { "Backup_DispBUETA",                0x00901CA4u, 0x3C9E5BA7u, 0x00992464u, kEAXDispatch_BackupETA,                0x12Cu, kSPCH1_EventID_DispBUETA };
    constexpr SpeechEvent kHeliSpecific_HeliSwarming       = { "HeliSpecific_HeliSwarming",       0x00901EE4u, 0x22745BA7u, 0x00992474u, kEAXAirSupport_Swarming,               0x05u,  kSPCH1_EventID_HeliSwarming };
    constexpr SpeechEvent kStaticRoadblock_CallForRB       = { "StaticRoadblock_CallForRB",       0x00901CD4u, 0x7D0E5BA7u, 0x00992594u, kEAXCop_CallForRB,                     0x32u,  kSPCH1_EventID_CallForRB };
    constexpr SpeechEvent kStaticRoadblock_DispRBUpdate    = { "StaticRoadblock_DispRBUpdate",    0x00901CF4u, 0x7B7F5BA7u, 0x009926F4u, kEAXDispatch_RBUpdate,                 0x2Eu,  kSPCH1_EventID_DispRBUpdate };
    constexpr SpeechEvent kAnytimeEvents_DriverHistory     = { "AnytimeEvents_DriverHistory",     0x00901E84u, 0x689E5BA7u, 0x0099231Cu, kEAXCharacter_DriverHistory,           0x07u,  kSPCH1_EventID_DriverHistory };
    constexpr SpeechEvent kAnytimeEvents_PursuitUpdateRep  = { "AnytimeEvents_PursuitUpdateRep",  0x00901DD4u, 0x795D5BA7u, 0x009926D8u, kEAXCop_PursuitUpdateReply,            0x05u,  kSPCH1_EventID_PursuitUpdateRep };
    constexpr SpeechEvent kAnytimeEvents_SuspectBehaviour  = { "AnytimeEvents_SuspectBehaviour",  0x00901E7Cu, 0x688B5BA7u, 0x00993C80u, kEAXCop_SuspectBehavior,               0x23u,  kSPCH1_EventID_SuspectBehaviour };
    constexpr SpeechEvent kAnytimeEvents_DispPursEscGen    = { "AnytimeEvents_DispPursEscGen",    0x00901E3Cu, 0x6BAC5BA7u, 0x0099237Cu, kEAXDispatch_PursuitEscalationGeneric, 0x23u,  kSPCH1_EventID_DispPursEscGen };
    constexpr SpeechEvent kSetup_VehicleReport             = { "Setup_VehicleReport",             0x00901C1Cu, 0x466D5BA7u, 0x0099246Cu, kEAXCop_VehicleReport,                 0x1C4u, kSPCH1_EventID_VehicleReport };
    constexpr SpeechEvent kAnytimeEvents_WeatherReport     = { "AnytimeEvents_WeatherReport",     0x00901EA4u, 0x7F435BA7u, 0x0099236Cu, kEAXCop_WeatherReport,                 0x05u,  kSPCH1_EventID_WeatherReport };
    constexpr SpeechEvent kSetup_InitialCallForBU          = { "Setup_InitialCallForBU",          0x00901C74u, 0u,          0u,          0u,                                    0u,     kSPCH1_EventID_InitialCallForBU };
    constexpr SpeechEvent kAnytimeEvents_DispPursuitUpdate = { "AnytimeEvents_DispPursuitUpdate", 0x00901DCCu, 0u,          0u,          0u,                                    0u,     kSPCH1_EventID_DispPursuitUpdate };

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

Timer&                                   WorldTimer = Game::Global<Timer>(kWorldTimer);
AIVehicleHelicopter*&                    gHeliVehicle = Game::Global<AIVehicleHelicopter*>(kgHeliVehicle);
int&                                     SFXCTL_Pathfinder::m_curinteractive = Game::Global<int>(kSFXCTL_Pathfinder_m_curinteractive);
Speech::Module* (&Speech::Manager::m_SpeechModule)[2] = Game::Global<Speech::Module* [2]>(kManager_m_SpeechModule);
Speech::EventHistory&                    Speech::Manager::mGlobalHistory = Game::Global<Speech::EventHistory>(kManager_mGlobalHistory);
Speech::SchedSpchEvents (&Speech::Manager::mEvents)[4] = Game::Global<Speech::SchedSpchEvents[4]>(kManager_mEvents);

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
static_assert(sizeof(Speech::copPair) == 0x08, "Speech::copPair is 0x8 bytes");
static_assert(sizeof(Speech::copMap) == 0x10, "Speech::copMap is 0x10 bytes");
static_assert(sizeof(Speech::ScheduledSpeechEvent) == 0x40, "Speech::ScheduledSpeechEvent is 0x40 bytes");
static_assert(offsetof(Speech::ScheduledSpeechEvent, actor) == 0x0C, "Speech::ScheduledSpeechEvent::actor");
static_assert(offsetof(Speech::ScheduledSpeechEvent, priority) == 0x3B, "Speech::ScheduledSpeechEvent::priority");
static_assert(sizeof(Speech::SchedSpchEvents) == 0x14, "Speech::SchedSpchEvents is 0x14 bytes");
static_assert(offsetof(Speech::GameSpeech, m_currEvent) == 0x88, "Speech::GameSpeech::m_currEvent");
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

SoundAI* SoundAI::Get() {
    return Game::Global<SoundAI*>(kSingleton_SoundAI_mInstance);
}

EAXCop* SoundAI::FindClosestCop(bool enforceLOS, bool includeHeli) {
    return Game::ThisCall<EAXCop*>(kSoundAI_FindClosestCop, this, enforceLOS, includeHeli);
}

int MiscSpeech::LostSuspect(int spkrID) {
    return Game::Call<int>(kMiscSpeech_LostSuspect, spkrID);
}

void EAXCop::Update() {
    Game::ThisCall<void>(kEAXCop_Update, this);
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

void EAXDispatch::BackupETA() {
    Game::ThisCall<void>(kEAXDispatch_BackupETA, this);
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

    enum class LineState { Waiting, Heard, Dropped };
    enum class CheckInStep { Idle, Waiting, Speaking };
    enum class AirSupportStep { Ready, Request, Asking, Reply, Answering };
    enum class RoadblockStep { Ready, Calling, Update, Updating };
    enum class BackupStep { Idle, Called, Dispatch, Pilot };
    enum class SpotLine { Ours, RegainVisual, Spotted };

    struct RadioLine {
        Speech::ScheduledSpeechEvent* event;
        unsigned int                  queued;
        bool                          heard;
    };

    class SpeechRule {
      public:
        enum Kind { kDepFollow, kFlag, kExpiry, kCullingRange };

        void Relax(unsigned int now, unsigned int duration = kRuleRelaxTime);
        void Restore();

        uint32_t     collection;
        Kind         kind;
        uint32_t     attribute;
        uint8_t*     value;
        uint32_t     saved;
        bool         relaxed;
        unsigned int until;
    };

    uintptr_t gScheduleSpeechPartIIOriginal = kManager_ScheduleSpeechPartII;
    uintptr_t gMessageReqBackupOriginal     = kStrategyFlow_MessageReqBackup;
    uintptr_t gTakeCheckOriginal            = kTakeCheck;

    bool gBailoutCauseOn = false;
    bool gCheckInOn = false;
    bool gAirSupportOn = false;
    bool gRoadblockOn = false;
    bool gDriverHistoryOn = false;
    bool gBackupReplyOn = false;
    bool gSwarmingOn = false;
    bool gPursuitUpdateReplyOn = false;
    bool gThemeBroadcastOn = false;
    bool gVehicleReportOn = false;
    bool gWeatherReportOn = false;

    bool                          gOwnLine = false;
    Speech::ScheduledSpeechEvent* gOwnScheduled = nullptr;
    bool                          gRadioHeld = false;
    unsigned int                  gRadioHeldUntil = 0;
    unsigned int                  gLastPursuitTick = 0;

    SpeechRule gBackup_CallForBU_DepFollow             = { kBackup_CallForBUSpeech, SpeechRule::kDepFollow, kDepFollow };
    SpeechRule gBackup_DispBackupReply_DepFollow       = { kBackup_DispBackupReplySpeech, SpeechRule::kDepFollow, kDepFollow };
    SpeechRule gAnytimeEvents_Unit911Reply_DepFollow   = { kAnytimeEvents_Unit911ReplySpeech, SpeechRule::kDepFollow, kDepFollow };
    SpeechRule gAnytimeEvents_RegainVisual_DepFollow   = { kAnytimeEvents_RegainVisualSpeech, SpeechRule::kDepFollow, kDepFollow };
    SpeechRule gAnytimeEvents_RegainVisual_expiry      = { kAnytimeEvents_RegainVisualSpeech, SpeechRule::kExpiry };
    SpeechRule gStaticRoadblock_CallForRB_DepFollow    = { kStaticRoadblock_CallForRBSpeech, SpeechRule::kDepFollow, kDepFollow };
    SpeechRule gStaticRoadblock_CallForRB_reqLOS       = { kStaticRoadblock_CallForRBSpeech, SpeechRule::kFlag, kreqLOS };
    SpeechRule gStaticRoadblock_CallForRB_OnScreenOnly = { kStaticRoadblock_CallForRBSpeech, SpeechRule::kFlag, kOnScreenOnly };
    SpeechRule gStaticRoadblock_CallForRB_expiry       = { kStaticRoadblock_CallForRBSpeech, SpeechRule::kExpiry };
    SpeechRule gHeliSpecific_HeliBailout_expiry        = { kHeliSpecific_HeliBailoutSpeech, SpeechRule::kExpiry };
    SpeechRule gHeliSpecific_HeliBailout_CullingRange  = { kHeliSpecific_HeliBailoutSpeech, SpeechRule::kCullingRange };
    SpeechRule* const gRules[] = {
        &gBackup_CallForBU_DepFollow,          &gBackup_DispBackupReply_DepFollow,     &gAnytimeEvents_Unit911Reply_DepFollow,
        &gAnytimeEvents_RegainVisual_DepFollow, &gAnytimeEvents_RegainVisual_expiry,    &gStaticRoadblock_CallForRB_DepFollow,
        &gStaticRoadblock_CallForRB_reqLOS,    &gStaticRoadblock_CallForRB_OnScreenOnly, &gStaticRoadblock_CallForRB_expiry,
        &gHeliSpecific_HeliBailout_expiry,     &gHeliSpecific_HeliBailout_CullingRange,
    };

    uint8_t*     gHiddenTags[kMostHiddenTakes] = {};
    uint8_t      gHiddenValues[kMostHiddenTakes] = {};
    unsigned     gHiddenCount = 0;
    unsigned int gHiddenAt = 0;

    bool                 gHeliOut = false;
    AIVehicleHelicopter* gHeliSeen = nullptr;
    bool                 gHeliAlive = false;
    bool                 gHeliDown = false;
    int                  gHelisThisPursuit = 0;
    unsigned int         gHeliJoinedAt = 0;

    Csis::Type_intensity gIntentToRamIntensity = Csis::Type_intensity_High;
    Csis::Type_intensity gLostSuspectIntensity = Csis::Type_intensity_High;
    bool                 gNewStrategy = true;

    uintptr_t                    gOnCollisionReturn = 0;
    bool                         gBailoutPending = false;
    unsigned int                 gBailoutAt = 0;
    EAXAirSupport*               gBailoutHeli = nullptr;
    Csis::Type_heli_bailout_type gBailoutCause = Csis::Type_heli_bailout_type_flight_conditions;
    float                        gBailoutFuel = 0.0f;
    bool                         gBailoutFuelKnown = false;
    RadioLine                    gGoingDownLine = {};
    bool                         gTunnelAlertPending = false;
    unsigned int                 gTunnelAlertAt = 0;

    bool         gSpotOursNext = true;
    bool         gSpotPending = false;
    bool         gSpotInterrupt = false;
    unsigned int gSpotAt = 0;
    SpotLine     gSpotGameLine = SpotLine::Ours;
    bool         gSpotSaid = false;
    unsigned int gSpotSaidAt = 0;
    RadioLine    gSpotLine = {};

    CheckInStep  gCheckInStep = CheckInStep::Idle;
    unsigned int gCheckInStarted = 0;
    unsigned int gCheckInNext = 0;
    RadioLine    gCheckInLine = {};

    AirSupportStep gAirSupportStep = AirSupportStep::Ready;
    bool           gAirSupportWanted = false;
    bool           gAirSupportAsked = false;
    int            gAirSupportFailures = 0;
    EAXCop*        gAirSupportCaller = nullptr;
    unsigned int   gAirSupportStarted = 0;
    unsigned int   gAirSupportNext = 0;
    int            gAirSupportTries = 0;
    RadioLine      gAirSupportLine = {};

    bool          gRoadblockRequested = false;
    unsigned int  gRoadblockRequestedAt = 0;
    bool          gRoadblockAsked = false;
    RoadblockStep gRoadblockStep = RoadblockStep::Ready;
    unsigned int  gRoadblockStarted = 0;
    unsigned int  gRoadblockNext = 0;
    int           gRoadblockTries = 0;
    unsigned      gRoadblockUpdate = 0;
    RadioLine     gRoadblockLine = {};

    bool         gHistoryAsked = false;
    bool         gHistoryHeard = false;
    unsigned int gHistoryAskedAt = 0;
    bool         gHistoryTracked = false;
    RadioLine    gHistoryLine = {};

    BackupStep   gBackupStep = BackupStep::Idle;
    unsigned int gBackupHeardAt = 0;
    bool         gBackupAnswered = false;
    bool         gBackupReplying = false;
    RadioLine    gBackupReplyLine = {};

    bool         gHeliAsked = false;
    RadioLine    gHeliQuestion = {};
    bool         gUpdateReplyTracked = false;
    RadioLine    gUpdateReplyLine = {};
    bool         gDrivingLineSaid = false;
    unsigned int gDrivingLineAt = 0;

    bool         gThemeKnown = false;
    int          gPursuitTheme = 0;
    bool         gBroadcastTracked = false;
    RadioLine    gBroadcastLine = {};
    bool         gVehicleReportDue = false;
    unsigned int gVehicleReportAt = 0;
    RadioLine    gVehicleLine = {};

    bool      gRainReported = false;
    RadioLine gWeatherLine = {};

    bool SpeechEvent::Verify() const {
        uint32_t interfaceId[2] = {};
        char text[64] = "";
        const size_t length = std::strlen(name) + 1;
        if (length > sizeof(text) || !Memory::Read(iid, interfaceId, sizeof(interfaceId)) || interfaceId[1] != crcs
            || !Memory::Read(interfaceId[0], text, length) || std::memcmp(text, name, length) != 0) {
            LOG("%s is off: speed.exe has no Csis::%sId at 0x%08lX.", name, name, static_cast<unsigned long>(iid));
            return false;
        }

        constexpr uint8_t kPush = 0x68;
        uint8_t code[24] = {};
        uint8_t pushHandle[5] = { kPush };
        uint8_t pushId[5] = { kPush };
        const uint32_t handleAddress = static_cast<uint32_t>(fh);
        const uint32_t idAddress = static_cast<uint32_t>(iid);
        std::memcpy(pushHandle + 1, &handleAddress, sizeof(handleAddress));
        std::memcpy(pushId + 1, &idAddress, sizeof(idAddress));
        const bool found = Memory::Read(function + pushes, code, sizeof(code)) && std::memcmp(code, pushHandle, sizeof(pushHandle)) == 0;
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

    unsigned int Now() {
        return static_cast<unsigned int>(WorldTimer.PackedTime);
    }

    template <typename T> Speech::ScheduledSpeechEvent* ScheduleSpeech(T& data, const SpeechEvent& event, EAXCharacter* actor) {
        return Speech::Manager::ScheduleSpeechPartII(sizeof(T), &data, event.Id(), event.Handle(), actor);
    }

    void ResetPlayCount(const SpeechEvent& event) {
        if (Speech::History* history = Speech::Manager::GetHistory().Find(event.id)) history->count = 0;
    }

    Speech::ScheduledSpeechEvent* GetCurrentEvent() {
        auto* copSpeech = static_cast<Speech::GameSpeech*>(Speech::Manager::m_SpeechModule[kCopSpeechModule]);
        return copSpeech != nullptr ? copSpeech->GetCurrentEvent() : nullptr;
    }

    bool IsScheduled(const Speech::ScheduledSpeechEvent* event) {
        for (int i = 0; i < kEventQueues; ++i) {
            const Speech::SchedSpchEvents& queue = Speech::Manager::mEvents[i];
            if (queue.mEnd < queue.mBegin || static_cast<unsigned>(queue.mEnd - queue.mBegin) > kMostQueuedEvents) continue;
            for (Speech::ScheduledSpeechEvent** scheduled = queue.mBegin; scheduled < queue.mEnd; ++scheduled)
                if (*scheduled == event) return true;
        }
        return false;
    }

    bool Listen(RadioLine& line, Speech::ScheduledSpeechEvent* event, unsigned int now) {
        if (!event) return false;
        if (event->priority < kLinePriority) event->priority = kLinePriority;
        line = { event, now, false };
        return true;
    }

    LineState Track(RadioLine& line, unsigned int now) {
        if (GetCurrentEvent() == line.event) {
            line.heard = true;
            return LineState::Waiting;
        }
        if (line.heard) return LineState::Heard;
        return IsScheduled(line.event) && now - line.queued < kLineTimeout ? LineState::Waiting : LineState::Dropped;
    }

    template <typename Speak> Speech::ScheduledSpeechEvent* OwnLine(Speak speak) {
        gOwnLine = true;
        gOwnScheduled = nullptr;
        Speech::ScheduledSpeechEvent* scheduled = speak();
        gOwnLine = false;
        return gOwnScheduled != nullptr ? gOwnScheduled : scheduled;
    }

    void HoldRadio(unsigned int now) {
        gRadioHeld = true;
        gRadioHeldUntil = now + kRadioHoldTimeout;
    }

    void ReleaseRadio() {
        gRadioHeld = false;
    }

    bool IsRadioHeld(unsigned int now) {
        return gRadioHeld && now < gRadioHeldUntil && gRadioHeldUntil - now <= kRadioHoldTimeout;
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
        const Attrib::Collection* collection = Attrib::FindCollection(kspeech, rule.collection);
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
            || header.mCount > kMostDepFollows || header.mSize != kDepFollowSize || (header.mFlags & kWideArrayHeader) || eventClass != kspeech)
            return nullptr;
        return reinterpret_cast<uint8_t*>(&array->mCount);
    }

    void SpeechRule::Relax(unsigned int now, unsigned int duration) {
        if (relaxed) {
            until = now + duration;
            return;
        }
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
        until = now + duration;
    }

    void SpeechRule::Restore() {
        if (!relaxed) return;
        if (kind == kDepFollow)
            *reinterpret_cast<uint16_t*>(value) = static_cast<uint16_t>(saved);
        else
            std::memcpy(value, &saved, sizeof(saved));
        relaxed = false;
    }

    void RestoreExpiredRules(unsigned int now) {
        for (SpeechRule* rule : gRules)
            if (rule->relaxed && (now >= rule->until || rule->until - now > kRuleRelaxTime)) rule->Restore();
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

    bool HideTakes(uint16_t sample, uint16_t speaker, unsigned first, unsigned count, uint8_t expected, unsigned int now) {
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
        gHiddenAt = now;
        return true;
    }

    void ExpireHiddenTakes(unsigned int now) {
        if (gHiddenCount && (now < gHiddenAt || now - gHiddenAt > kHiddenTakeTimeout)) ShowHiddenTakes();
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

    const char* LineName(AirSupportStep step) {
        return step == AirSupportStep::Asking || step == AirSupportStep::Request ? "unit's request" : "dispatch reply";
    }
#else
    void LoadSpeechEntries() {}
    void LogTake(const void*, int) {}
    const char* LineName(AirSupportStep step);
#endif

    Csis::Type_intensity NextIntensity(Csis::Type_intensity& last) {
        last = last == Csis::Type_intensity_Normal ? Csis::Type_intensity_High : Csis::Type_intensity_Normal;
        return last;
    }

    bool IsAirSupport(const EAXCharacter* character) {
        uintptr_t vtable = 0;
        return character && Memory::Read(reinterpret_cast<uintptr_t>(character), &vtable, sizeof(vtable)) && vtable == kEAXAirSupportVTable;
    }

    EAXAirSupport* ActiveHeli() {
        SoundAI* ai = SoundAI::Get();
        EAXAirSupport* heli = ai != nullptr ? ai->GetHeli() : nullptr;
        return gHeliVehicle != nullptr && IsAirSupport(heli) ? heli : nullptr;
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

    float PursuitDuration() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr ? ai->GetPursuitDuration() : -1.0f;
    }

    bool IsSearching() {
        SoundAI* ai = SoundAI::Get();
        return ai != nullptr && ai->GetPursuitState() == SoundAI::kSearching;
    }

    float RainIntensity() {
        Rain* rain = Game::Global<Rain*>(kPlayerViewPrecipitation);
        return rain != nullptr ? rain->GetRainIntensity() : 0.0f;
    }

    template <typename Visit> void ForEachCop(Visit visit) {
        SoundAI* ai = SoundAI::Get();
        if (!ai) return;
        const Speech::copMap& actors = ai->GetActors();
        if (actors.end() < actors.begin() || static_cast<unsigned>(actors.end() - actors.begin()) > kMostActors) return;
        for (const Speech::copPair* actor = actors.begin(); actor < actors.end(); ++actor)
            if (actor->cop) visit(actor->cop);
    }

    bool IsInChase(const EAXCop* unit) {
        bool found = false;
        ForEachCop([&](EAXCop* cop) { found = found || cop == unit; });
        return unit && found;
    }

    bool CanCallForAirSupport(EAXCop* cop) {
        const int speaker = cop->GetSpeakerID();
        for (int caller : kAirSupportCallers)
            if (speaker == caller) return cop->IsActive() && cop->HasLOS();
        return false;
    }

    EAXCop* PickAirSupportCaller() {
        EAXCop* callers[kMostCallers] = {};
        unsigned count = 0;
        ForEachCop([&](EAXCop* cop) {
            if (count < kMostCallers && CanCallForAirSupport(cop)) callers[count++] = cop;
        });
        return count ? callers[static_cast<unsigned>(std::rand()) % count] : nullptr;
    }

    void RelaxCallForRBRules(unsigned int now) {
        gStaticRoadblock_CallForRB_DepFollow.Relax(now);
        gStaticRoadblock_CallForRB_reqLOS.Relax(now);
        gStaticRoadblock_CallForRB_OnScreenOnly.Relax(now);
        gStaticRoadblock_CallForRB_expiry.Relax(now);
    }

    void RestoreCallForRBRules() {
        gStaticRoadblock_CallForRB_DepFollow.Restore();
        gStaticRoadblock_CallForRB_reqLOS.Restore();
        gStaticRoadblock_CallForRB_OnScreenOnly.Restore();
        gStaticRoadblock_CallForRB_expiry.Restore();
    }

    void FinishRoadblock() {
        gRoadblockStep = RoadblockStep::Ready;
        ReleaseRadio();
        RestoreCallForRBRules();
    }

    bool HeliMayCallRoadblock(unsigned int now) {
        return gRoadblockOn && !gRoadblockAsked && gHeliOut && now >= gHeliJoinedAt && now - gHeliJoinedAt >= kRoadblockWait;
    }

    bool RadioFreeForRoadblock() {
        return gAirSupportStep == AirSupportStep::Ready && gRoadblockStep == RoadblockStep::Ready && gCheckInStep == CheckInStep::Idle;
    }

    Speech::ScheduledSpeechEvent* PilotCallsForRB(EAXAirSupport* heli, unsigned int now) {
        RelaxCallForRBRules(now);
        ResetPlayCount(kStaticRoadblock_CallForRB);
        Speech::ScheduledSpeechEvent* event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
            heli->CallForRB();
            return nullptr;
        });
        if (!Listen(gRoadblockLine, event, now)) {
            RestoreCallForRBRules();
            return nullptr;
        }
        LOG("Roadblock: the pilot's call for a roadblock is queued.");
        gRoadblockAsked = true;
        gRoadblockStep = RoadblockStep::Calling;
        gRoadblockStarted = now;
        gRoadblockTries = 0;
        HoldRadio(now);
        return event;
    }

    void AnswerRoadblockRequest(EAXAirSupport* heli, unsigned int now) {
        if (!gRoadblockRequested) return;
        if (now < gRoadblockRequestedAt || now - gRoadblockRequestedAt > kRoadblockRequestWindow || !HeliMayCallRoadblock(now)) {
            LOG("Roadblock: the pilot can't take this request (too soon after joining, already asked, or out of time).");
            gRoadblockRequested = false;
            return;
        }
        if (!heli->IsActive() || !RadioFreeForRoadblock()) return;
        gRoadblockRequested = false;
        if (!PilotCallsForRB(heli, now)) LOG("Roadblock: the game turned the pilot's call down.");
    }

    Speech::ScheduledSpeechEvent* DispatchRBUpdate() {
        EAXDispatch* dispatch = Dispatch();
        if (!dispatch) return nullptr;
        gRoadblockUpdate = (gRoadblockUpdate + 1u) % kRoadblockUpdateCount;
        Csis::StaticRoadblock_DispRBUpdateStruct data = { dispatch->GetSpeakerID(), dispatch->GetRandomizedCode(), kRoadblockUpdates[gRoadblockUpdate].yes_no,
                                                          kRoadblockUpdates[gRoadblockUpdate].roadblock_type };
        ResetPlayCount(kStaticRoadblock_DispRBUpdate);
        return OwnLine([&data, dispatch] { return ScheduleSpeech(data, kStaticRoadblock_DispRBUpdate, dispatch); });
    }

    void AdvanceRoadblock(unsigned int now) {
        if (gRoadblockStep == RoadblockStep::Ready) return;
        if (now < gRoadblockStarted || now - gRoadblockStarted > kRoadblockGiveUp) {
            LOG("Roadblock: gave up on dispatch's answer.");
            FinishRoadblock();
            return;
        }

        switch (gRoadblockStep) {
        case RoadblockStep::Calling:
            switch (Track(gRoadblockLine, now)) {
            case LineState::Heard:
                LOG("Roadblock: the pilot's call played.");
                RestoreCallForRBRules();
                gRoadblockStep = RoadblockStep::Update;
                gRoadblockNext = now;
                break;
            case LineState::Dropped:
                LOG("Roadblock: the game dropped the pilot's call before it played; he can take the next request.");
                FinishRoadblock();
                gRoadblockAsked = false;
                break;
            default:
                break;
            }
            break;
        case RoadblockStep::Update:
            if (now < gRoadblockNext) return;
            if (!Listen(gRoadblockLine, DispatchRBUpdate(), now)) {
                LOG("Roadblock: the game turned dispatch's answer down, trying again in a second.");
                gRoadblockNext = now + kRetry;
                return;
            }
            LOG("Roadblock: dispatch's answer is queued.");
            gRoadblockStep = RoadblockStep::Updating;
            HoldRadio(now);
            break;
        case RoadblockStep::Updating:
            switch (Track(gRoadblockLine, now)) {
            case LineState::Heard:
                LOG("Roadblock: dispatch's answer played.");
                FinishRoadblock();
                break;
            case LineState::Dropped:
                LOG("Roadblock: the game dropped dispatch's answer before it played.");
                if (++gRoadblockTries >= kRoadblockTries) {
                    FinishRoadblock();
                    break;
                }
                gRoadblockStep = RoadblockStep::Update;
                gRoadblockNext = now + kRetry;
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    void StartCheckIn(unsigned int now) {
        if (!gCheckInOn) return;
        gCheckInStep = CheckInStep::Waiting;
        gCheckInStarted = now;
        gCheckInNext = now;
    }

    void CheckIn(EAXAirSupport* heli, unsigned int now) {
        if (gCheckInStep == CheckInStep::Idle) return;
        if (now < gCheckInStarted || now - gCheckInStarted > kCheckInGiveUp) {
            LOG("Check-in: gave up.");
            gAnytimeEvents_Unit911Reply_DepFollow.Restore();
            gCheckInStep = CheckInStep::Idle;
            return;
        }

        if (gCheckInStep == CheckInStep::Waiting) {
            if (now < gCheckInNext || Speech::Manager::IsCopSpeechBusy() || gAirSupportStep != AirSupportStep::Ready
                || gRoadblockStep != RoadblockStep::Ready)
                return;
            ResetPlayCount(kAnytimeEvents_Unit911Reply);
            gAnytimeEvents_Unit911Reply_DepFollow.Relax(now);
            Speech::ScheduledSpeechEvent* event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
                heli->EAXCop::Reply911();
                return nullptr;
            });
            if (!Listen(gCheckInLine, event, now)) {
                LOG("Check-in: the game turned the pilot's line down, trying again in a second.");
                gAnytimeEvents_Unit911Reply_DepFollow.Restore();
                gCheckInNext = now + kRetry;
                return;
            }
            LOG("Check-in: the pilot's line is queued.");
            gCheckInStep = CheckInStep::Speaking;
            return;
        }

        switch (Track(gCheckInLine, now)) {
        case LineState::Heard:
            LOG("Check-in: the pilot's line played.");
            gAnytimeEvents_Unit911Reply_DepFollow.Restore();
            gCheckInStep = CheckInStep::Idle;
            break;
        case LineState::Dropped:
            LOG("Check-in: the game dropped the pilot's line before it played.");
            gAnytimeEvents_Unit911Reply_DepFollow.Restore();
            gCheckInStep = CheckInStep::Waiting;
            gCheckInNext = now + kRetry;
            break;
        default:
            break;
        }
    }

    void FinishAirSupport(unsigned int now, bool checkIn) {
        gAirSupportStep = AirSupportStep::Ready;
        ReleaseRadio();
        gBackup_CallForBU_DepFollow.Restore();
        gBackup_DispBackupReply_DepFollow.Restore();
        if (!gAirSupportAsked && gAirSupportWanted && ++gAirSupportFailures >= kAirSupportTries) {
            LOG("Air support: nobody managed to ask for another helicopter, so the units stop trying.");
            gAirSupportWanted = false;
        }
        if (checkIn && ActiveHeli()) StartCheckIn(now);
    }

    bool StartAirSupport(unsigned int now) {
        if (!gAirSupportOn || gAirSupportStep != AirSupportStep::Ready || gRoadblockStep != RoadblockStep::Ready || PursuitDuration() < 0.0f)
            return false;
        LOG("Air support: the last helicopter is gone, so a unit that can see you asks for another one.");
        gAirSupportStep = AirSupportStep::Request;
        gAirSupportStarted = now;
        gAirSupportNext = now;
        gAirSupportTries = 0;
        return true;
    }

    Speech::ScheduledSpeechEvent* UnitCallsForAirSupport() {
        ResetPlayCount(kBackup_CallForBU);
        EAXCop* caller = gAirSupportCaller;
        return OwnLine([caller]() -> Speech::ScheduledSpeechEvent* {
            caller->CallForBackup(Csis::Type_disp_backup_type_Air_Support);
            return nullptr;
        });
    }

    Speech::ScheduledSpeechEvent* DispatchSendsAirSupport() {
        EAXDispatch* dispatch = Dispatch();
        if (!dispatch) return nullptr;
        ResetPlayCount(kBackup_DispBackupReply);
        EAXCop* caller = gAirSupportCaller;
        return OwnLine([dispatch, caller]() -> Speech::ScheduledSpeechEvent* {
            dispatch->BackupReply(caller, 1, Csis::Type_disp_backup_type_Air_Support);
            return nullptr;
        });
    }

    void SayAirSupportLine(AirSupportStep listening, SpeechRule& rule, Speech::ScheduledSpeechEvent* event, unsigned int now) {
        if (!Listen(gAirSupportLine, event, now)) {
            LOG("Air support: the game turned down the %s line, trying again in a second.", LineName(listening));
            rule.Restore();
            gAirSupportNext = now + kRetry;
            return;
        }
        LOG("Air support: the %s line is queued.", LineName(listening));
        gAirSupportStep = listening;
        HoldRadio(now);
    }

    void FollowAirSupportLine(AirSupportStep listening, SpeechRule& rule, AirSupportStep retry, unsigned int now) {
        switch (Track(gAirSupportLine, now)) {
        case LineState::Heard:
            LOG("Air support: the %s line played.", LineName(listening));
            rule.Restore();
            gAirSupportTries = 0;
            if (listening == AirSupportStep::Answering) {
                gAirSupportWanted = false;
                FinishAirSupport(now, true);
                return;
            }
            gAirSupportAsked = true;
            gAirSupportWanted = false;
            gAirSupportStep = AirSupportStep::Reply;
            gAirSupportStarted = now;
            gAirSupportNext = now;
            break;
        case LineState::Dropped:
            LOG("Air support: the game dropped the %s line before it played.", LineName(listening));
            rule.Restore();
            if (++gAirSupportTries >= kAirSupportTries) {
                FinishAirSupport(now, true);
                return;
            }
            gAirSupportStep = retry;
            gAirSupportNext = now + kRetry;
            break;
        default:
            break;
        }
    }

    void AdvanceAirSupport(unsigned int now) {
        if (gAirSupportStep == AirSupportStep::Ready) return;
        const bool waiting = gAirSupportStep == AirSupportStep::Request || gAirSupportStep == AirSupportStep::Reply;
        if (waiting && (now < gAirSupportStarted || now - gAirSupportStarted > kAirSupportGiveUp)) {
            LOG("Air support: the %s line couldn't be queued in time.", LineName(gAirSupportStep));
            FinishAirSupport(now, true);
            return;
        }

        switch (gAirSupportStep) {
        case AirSupportStep::Request:
            if (ActiveHeli()) {
                LOG("Air support: a helicopter is already up, so nobody asks.");
                FinishAirSupport(now, false);
                return;
            }
            if (now < gAirSupportNext || Speech::Manager::IsCopSpeechBusy()) return;
            gAirSupportCaller = PickAirSupportCaller();
            if (!gAirSupportCaller) {
                gAirSupportNext = now + kRetry;
                return;
            }
            LOG("Air support: unit with speaker ID %d can see you and asks for it.", gAirSupportCaller->GetSpeakerID());
            gBackup_CallForBU_DepFollow.Relax(now);
            SayAirSupportLine(AirSupportStep::Asking, gBackup_CallForBU_DepFollow, UnitCallsForAirSupport(), now);
            break;
        case AirSupportStep::Asking:
            FollowAirSupportLine(AirSupportStep::Asking, gBackup_CallForBU_DepFollow, AirSupportStep::Request, now);
            break;
        case AirSupportStep::Reply:
            if (now < gAirSupportNext) return;
            if (!IsInChase(gAirSupportCaller)) {
                LOG("Air support: the unit left the chase before dispatch could answer.");
                FinishAirSupport(now, true);
                return;
            }
            gBackup_DispBackupReply_DepFollow.Relax(now);
            SayAirSupportLine(AirSupportStep::Answering, gBackup_DispBackupReply_DepFollow, DispatchSendsAirSupport(), now);
            break;
        case AirSupportStep::Answering:
            FollowAirSupportLine(AirSupportStep::Answering, gBackup_DispBackupReply_DepFollow, AirSupportStep::Reply, now);
            break;
        default:
            break;
        }
    }

    bool GetFuelTimeRemaining(EAXAirSupport* heli, float& seconds) {
        ISimable* simable = gBailoutCauseOn ? ISimable::FindInstance(heli->GetHandle()) : nullptr;
        IAIHelicopter* ai = nullptr;
        if (!simable || !simable->QueryInterface(&ai)) return false;
        seconds = ai->GetFuelTimeRemaining();
        return true;
    }

    void ReportDamage(EAXAirSupport* heli, unsigned int now) {
        if (gRoadblockStep != RoadblockStep::Ready) FinishRoadblock();
        gHeliSpecific_HeliBailout_expiry.Relax(now);
        gHeliSpecific_HeliBailout_CullingRange.Relax(now);
        ResetPlayCount(kHeliSpecific_HeliBailout);
        Csis::HeliSpecific_HeliBailoutStruct data = { heli->GetSpeakerID(), Csis::Type_heli_bailout_type_damage_sustained };
        Speech::ScheduledSpeechEvent* event = OwnLine([&data, heli] { return ScheduleSpeech(data, kHeliSpecific_HeliBailout, heli); });
        if (!Listen(gGoingDownLine, event, now)) {
            LOG("Going down: the game turned the pilot's damage report down.");
            return;
        }
        LOG("Going down: the pilot's damage report is queued.");
    }

    void GamesOwnBailout(EAXAirSupport* heli, unsigned int now) {
        gHeliSpecific_HeliBailout_expiry.Relax(now);
        gHeliSpecific_HeliBailout_CullingRange.Relax(now);
        ResetPlayCount(kHeliSpecific_HeliBailout);
        Speech::ScheduledSpeechEvent* event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
            heli->EAXAirSupport::Bailout();
            return nullptr;
        });
        Listen(gGoingDownLine, event, now);
    }

    void SearchPattern(unsigned int now) {
        ResetPlayCount(kAnytimeEvents_LostSuspect);
        Csis::AnytimeEvents_LostSuspectStruct data = { Speech::Heli, NextIntensity(gLostSuspectIntensity) };
        Speech::ScheduledSpeechEvent* event = OwnLine([&data] { return ScheduleSpeech(data, kAnytimeEvents_LostSuspect, nullptr); });
        Listen(gGoingDownLine, event, now);
        LOG("Losing you: the pilot's search pattern line was %s.", event ? "queued" : "turned down by the game");
    }

    void ResolveBailout(unsigned int now) {
        if (!gBailoutPending || (now >= gBailoutAt && now - gBailoutAt < kBailoutSettle)) return;
        gBailoutPending = false;
        EAXAirSupport* heli = gBailoutHeli;
        if (!IsAirSupport(heli)) return;
        if (heli->IsDead() || heli->GetHealth() < kFullHealth) {
            LOG("Going down: the helicopter was shot down, so the pilot reports damage.");
            ReportDamage(heli, now);
            return;
        }
        if (gBailoutCause != Csis::Type_heli_bailout_type_fuel_low || !gBailoutFuelKnown || gBailoutFuel <= HELI_FUEL_CRITICAL_TIME) {
            LOG("Going down: the game's own reason (cause %d, %d s of fuel left).", static_cast<int>(gBailoutCause),
                gBailoutFuelKnown ? static_cast<int>(gBailoutFuel) : -1);
            GamesOwnBailout(heli, now);
            return;
        }
        LOG("Losing you: the helicopter lost sight of you with %d s of fuel left and no damage.", static_cast<int>(gBailoutFuel));
        SearchPattern(now);
    }

    void TunnelAlert(EAXAirSupport* heli, unsigned int now) {
        if (!gTunnelAlertPending || now < gTunnelAlertAt) return;
        gTunnelAlertPending = false;
        if (gHeliDown || heli->IsDead()) return;
        LOG("Tunnel warning: the pilot warns about the tunnel.");
        heli->EAXAirSupport::HazardAlert(Csis::Type_heli_hazard_alert_type_approaching_tunnel);
    }

    void SpotYou(EAXAirSupport* heli, unsigned int now, bool interrupt) {
        gAnytimeEvents_RegainVisual_DepFollow.Relax(now, kRegainVisualRelax);
        gAnytimeEvents_RegainVisual_expiry.Relax(now, kRegainVisualRelax);
        ResetPlayCount(kAnytimeEvents_RegainVisual);
        Csis::AnytimeEvents_RegainVisualStruct data = { heli->GetSpeakerID(), Csis::Type_intensity_Normal };
        Speech::ScheduledSpeechEvent* event = interrupt ? OwnLine([&data, heli] { return ScheduleSpeech(data, kAnytimeEvents_RegainVisual, heli); })
                                                        : ScheduleSpeech(data, kAnytimeEvents_RegainVisual, heli);
        if (Listen(gSpotLine, event, now) && interrupt) event->priority = kInterruptPriority;
        gSpotSaid = true;
        gSpotSaidAt = now;
        LOG("Spotting you: the pilot's line was %s.", !event ? "turned down by the game" : interrupt ? "queued to cut in" : "queued");
    }

    void SayGameSpot(EAXAirSupport* heli, SpotLine line) {
        if (line == SpotLine::Spotted)
            heli->EAXCop::Spotted();
        else
            heli->EAXCop::RegainVisual();
    }

    void HeliSpots(EAXAirSupport* heli, SpotLine gameLine) {
        const unsigned int now = Now();
        if (gSpotSaid && now >= gSpotSaidAt && now - gSpotSaidAt < kSpotQuiet) {
            LOG("Spotting you: the pilot just said it, so the game's own call is skipped.");
            return;
        }
        if (gSpotPending && gSpotInterrupt) {
            LOG("Spotting you: the pilot's arrival line is still waiting, so the game's own call is skipped.");
            return;
        }
        const bool ours = gSpotOursNext;
        gSpotOursNext = !gSpotOursNext;
        if (IsSearching()) {
            LOG("Spotting you: the search isn't over yet, so the pilot waits until it really is.");
            gSpotPending = true;
            gSpotInterrupt = false;
            gSpotGameLine = ours ? SpotLine::Ours : gameLine;
            gSpotAt = now;
            return;
        }
        if (!ours) {
            LOG("Spotting you: this time the pilot uses the game's own takes.");
            gSpotSaid = true;
            gSpotSaidAt = now;
            SayGameSpot(heli, gameLine);
            return;
        }
        if (!IsRadioHeld(now)) {
            SpotYou(heli, now, false);
            return;
        }
        LOG("Spotting you: the radio is busy with an exchange, so the pilot waits for it.");
        gSpotPending = true;
        gSpotGameLine = SpotLine::Ours;
        gSpotAt = now;
    }

    void PendingSpot(EAXAirSupport* heli, unsigned int now) {
        if (!gSpotPending) return;
        const bool expired = now < gSpotAt || now - gSpotAt > (gSpotInterrupt ? kSpotWindow : kSpotSearchWindow);
        if (IsSearching()) {
            if (gSpotInterrupt)
                gSpotAt = now;
            else if (expired) {
                LOG("Spotting you: the search went on, so the pilot never really spotted you.");
                gSpotPending = false;
            }
            return;
        }
        if (!gSpotInterrupt && IsRadioHeld(now)) return;
        if (!expired) {
            if (!heli->IsActive()) return;
            if (gSpotGameLine != SpotLine::Ours) {
                gSpotSaid = true;
                gSpotSaidAt = now;
                SayGameSpot(heli, gSpotGameLine);
            } else
                SpotYou(heli, now, gSpotInterrupt);
        }
        gSpotPending = false;
        gSpotInterrupt = false;
        gSpotGameLine = SpotLine::Ours;
    }

    void ReadDriverHistory(unsigned int now) {
        EAXDispatch* dispatch = Dispatch();
        if (!dispatch) return;
        unsigned tier = static_cast<unsigned>(std::rand()) % kRecordTiers;
        if (tier == 0 && !HideTakes(kDriverHistorySample, Speech::Dispatch, 0, kMinorRecordTakes, kMinorRecordTag, now))
            tier = 1u + static_cast<unsigned>(std::rand()) % (kRecordTiers - 1u);
        ResetPlayCount(kAnytimeEvents_DriverHistory);
        Csis::AnytimeEvents_DriverHistoryStruct data = { dispatch->GetSpeakerID(), static_cast<Csis::Type_region>(1u << tier) };
        Speech::ScheduledSpeechEvent* event = OwnLine([&data, dispatch] { return ScheduleSpeech(data, kAnytimeEvents_DriverHistory, dispatch); });
        if (!Listen(gHistoryLine, event, now)) {
            LOG("Driver history: the game turned dispatch's line down.");
            ShowHiddenTakes();
            return;
        }
        gHistoryTracked = true;
        LOG("Driver history: dispatch reads out record tier %u.", tier + 1u);
    }

    void NoteHistoryRequest(int take, unsigned int now) {
        if (!gDriverHistoryOn || take < kHistoryRequestFirstTake || take > kHistoryRequestLastTake) return;
        gHistoryAsked = true;
        gHistoryHeard = false;
        gHistoryAskedAt = now;
    }

    void AnswerHistoryRequest(unsigned int now) {
        if (now < gHistoryAskedAt || now - gHistoryAskedAt > kDriverHistoryWindow) {
            LOG("Driver history: the pilot's history request never played, so dispatch stays quiet.");
            gHistoryAsked = false;
            return;
        }
        Speech::ScheduledSpeechEvent* playing = GetCurrentEvent();
        if (playing != nullptr && playing->iid == &kAnytimeEvents_Unit911Reply.Id()) {
            gHistoryHeard = true;
            return;
        }
        if (!gHistoryHeard || IsRadioHeld(now)) return;
        gHistoryAsked = false;
        gHistoryHeard = false;
        if (PlayerHeat() < kDriverHistoryMinHeat) {
            LOG("Driver history: the pilot asked for your history below heat %d, so dispatch stays quiet.", kDriverHistoryMinHeat);
            return;
        }
        LOG("Driver history: the pilot asked for your history, so dispatch reads it out.");
        ReadDriverHistory(now);
    }

    void FollowHistoryLine(unsigned int now) {
        if (!gHistoryTracked || Track(gHistoryLine, now) == LineState::Waiting) return;
        gHistoryTracked = false;
        ShowHiddenTakes();
    }

    void FinishBackupReply() {
        if (gBackupReplying && gCheckInStep == CheckInStep::Idle) gAnytimeEvents_Unit911Reply_DepFollow.Restore();
        gBackupReplying = false;
        gBackupStep = BackupStep::Idle;
    }

    void PilotRepliesToBackup(EAXDispatch* dispatch, unsigned int now) {
        gBackupStep = BackupStep::Idle;
        EAXAirSupport* heli = ActiveHeli();
        const bool up = heli && !gHeliDown;
        if ((up && !gSwarmingOn) || (!up && (!dispatch || (PlayerHeat() < kHeliMinHeat && gHelisThisPursuit == 0)))) return;
        if (static_cast<unsigned>(std::rand()) % kBackupReplyChance != 0) {
            LOG("Backup reply: the pilot let this call for backup go.");
            return;
        }
        Speech::ScheduledSpeechEvent* event = nullptr;
        if (up) {
            ResetPlayCount(kHeliSpecific_HeliSwarming);
            event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
                heli->EAXAirSupport::Swarming();
                return nullptr;
            });
        } else {
            ResetPlayCount(kAnytimeEvents_Unit911Reply);
            gAnytimeEvents_Unit911Reply_DepFollow.Relax(now);
            gBackupReplying = true;
            Csis::AnytimeEvents_Unit911ReplyStruct data = { Speech::Heli };
            event = OwnLine([&data, dispatch] { return ScheduleSpeech(data, kAnytimeEvents_Unit911Reply, dispatch); });
        }
        if (!Listen(gBackupReplyLine, event, now)) {
            LOG("Backup reply: the game turned the pilot's line down.");
            FinishBackupReply();
            return;
        }
        gBackupStep = BackupStep::Pilot;
        if (up)
            LOG("Backup reply: the pilot tells the unit he can see its cover closing in.");
        else
            LOG("Backup reply: no helicopter is up, so the pilot answers on his way in.");
    }

    bool IsCallForBackup(const Speech::ScheduledSpeechEvent* event) {
        return event->iid == &kSetup_InitialCallForBU.Id() || event->iid == &kBackup_CallForBU.Id();
    }

    void FollowBackupCalls(unsigned int now) {
        EAXDispatch* dispatch = Dispatch();
        if (gBackupStep == BackupStep::Pilot) {
            if (Track(gBackupReplyLine, now) != LineState::Waiting) FinishBackupReply();
            return;
        }
        if (gBackupStep == BackupStep::Dispatch) {
            const LineState line = Track(gBackupReplyLine, now);
            if (line == LineState::Heard)
                PilotRepliesToBackup(dispatch, now);
            else if (line == LineState::Dropped)
                gBackupStep = BackupStep::Idle;
            return;
        }

        Speech::ScheduledSpeechEvent* playing = GetCurrentEvent();
        EAXCharacter* speaker = playing != nullptr ? playing->actor : nullptr;
        if (playing && IsCallForBackup(playing) && speaker && speaker->GetSpeakerID() != Speech::Heli) {
            gBackupStep = BackupStep::Called;
            gBackupAnswered = false;
            gBackupHeardAt = now;
            return;
        }
        if (gBackupStep != BackupStep::Called) return;
        if (playing && dispatch && speaker == dispatch) {
            gBackupAnswered = true;
            gBackupHeardAt = now;
            return;
        }
        if (now < gBackupHeardAt || now - gBackupHeardAt > kBackupWindow) {
            gBackupStep = BackupStep::Idle;
            return;
        }
        if (playing || Speech::Manager::IsCopSpeechBusy() || IsRadioHeld(now) || gCheckInStep != CheckInStep::Idle) return;
        if (gBackupAnswered || !dispatch) {
            PilotRepliesToBackup(dispatch, now);
            return;
        }
        ResetPlayCount(kBackup_DispBUETA);
        Speech::ScheduledSpeechEvent* event = OwnLine([dispatch]() -> Speech::ScheduledSpeechEvent* {
            dispatch->BackupETA();
            return nullptr;
        });
        if (!Listen(gBackupReplyLine, event, now)) {
            PilotRepliesToBackup(dispatch, now);
            return;
        }
        LOG("Backup reply: dispatch never answered the unit, so she tells it backup is on the way.");
        gBackupStep = BackupStep::Dispatch;
    }

    void NoteDispatchQuestion(unsigned int size, const void* data, const Csis::InterfaceId& iid, Speech::ScheduledSpeechEvent* event,
                              unsigned int now) {
        if (!gPursuitUpdateReplyOn || !event || &iid != &kAnytimeEvents_DispPursuitUpdate.Id()
            || size < sizeof(Csis::AnytimeEvents_DispPursuitUpdateStruct))
            return;
        EAXAirSupport* heli = ActiveHeli();
        const auto* asked = static_cast<const Csis::AnytimeEvents_DispPursuitUpdateStruct*>(data);
        if (!heli || asked->subject_battalion != heli->mCallsign.name || asked->subject_call_sign_id != heli->mCallsign.number) return;
        LOG("Pursuit update: dispatch is asking the helicopter (callsign %d/%d) for an update.", heli->mCallsign.name, heli->mCallsign.number);
        gHeliAsked = true;
        gHeliQuestion = { event, now, false };
    }

    void AnswerDispatchQuestion(unsigned int now) {
        const LineState question = Track(gHeliQuestion, now);
        if (question == LineState::Waiting && now >= gHeliQuestion.queued && now - gHeliQuestion.queued <= kUpdateReplyWindow) return;
        gHeliAsked = false;
        if (question != LineState::Heard) {
            LOG("Pursuit update: dispatch's question to the helicopter never played, so the pilot stays on his usual updates.");
            return;
        }
        EAXAirSupport* heli = ActiveHeli();
        if (!heli || gHeliDown) return;
        if (gDrivingLineSaid && now >= gDrivingLineAt && now - gDrivingLineAt < kDrivingLineGap) {
            LOG("Pursuit update: the pilot described your driving less than a minute ago, so he gives his usual update.");
            return;
        }
        if (PlayerHeat() < kDrivingLineMinHeat || PlayerSpeed() < kDrivingLineMinSpeed) {
            LOG("Pursuit update: too slow or too little heat for the driving lines, so the pilot gives his usual update.");
            return;
        }
        ResetPlayCount(kAnytimeEvents_SuspectBehaviour);
        Csis::AnytimeEvents_SuspectBehaviourStruct data = { heli->GetSpeakerID(), Csis::Type_num_suspects_one_suspect };
        Speech::ScheduledSpeechEvent* event = OwnLine([&data, heli] { return ScheduleSpeech(data, kAnytimeEvents_SuspectBehaviour, heli); });
        if (!Listen(gUpdateReplyLine, event, now)) {
            LOG("Pursuit update: the game turned the driving lines down, so the pilot gives his usual update.");
            return;
        }
        LOG("Pursuit update: dispatch asked the helicopter, so the pilot answers by describing your driving.");
        gUpdateReplyTracked = true;
        gDrivingLineSaid = true;
        gDrivingLineAt = now;
    }

    void BroadcastThemeChange(unsigned int now) {
        EAXDispatch* dispatch = Dispatch();
        if (!dispatch) return;
        if (!HideTakes(kDispPursEscGenSample, Speech::Dispatch, kOddMultipleSuspectsTake, 1, kMultipleSuspectsTag, now))
            LOG("Pursuit theme: ENTRY_05440 couldn't be hidden, so all four multiple-vehicles takes can play.");
        ResetPlayCount(kAnytimeEvents_DispPursEscGen);
        Csis::AnytimeEvents_DispPursEscGenStruct data = { dispatch->GetSpeakerID(), Csis::Type_num_suspects_multiple_suspects };
        Speech::ScheduledSpeechEvent* event = OwnLine([&data, dispatch] { return ScheduleSpeech(data, kAnytimeEvents_DispPursEscGen, dispatch); });
        if (!Listen(gBroadcastLine, event, now)) {
            LOG("Pursuit theme: the game turned dispatch's broadcast down.");
            ShowHiddenTakes();
            return;
        }
        gBroadcastTracked = true;
        LOG("Pursuit theme: dispatch makes her multiple-vehicles broadcast.");
    }

    void NoteThemeChange(float pursuitDuration, unsigned int now) {
        const int theme = SFXCTL_Pathfinder::m_curinteractive;
        if (!gThemeKnown || pursuitDuration < kThemeSettleSeconds) {
            gPursuitTheme = theme;
            gThemeKnown = true;
            return;
        }
        if (theme == gPursuitTheme) return;
        gPursuitTheme = theme;
        LOG("Pursuit theme: the music moved on to theme %d.", theme + 1);
        if (!ActiveHeli() || gHeliDown) {
            LOG("Pursuit theme: no helicopter is up, so the radio waits for the next theme change.");
            return;
        }
        if (gBroadcastTracked) return;
        BroadcastThemeChange(now);
    }

    void FollowBroadcast(unsigned int now) {
        if (!gBroadcastTracked) return;
        const LineState broadcast = Track(gBroadcastLine, now);
        if (broadcast != LineState::Waiting) {
            gBroadcastTracked = false;
            ShowHiddenTakes();
        }
        if (broadcast == LineState::Heard) {
            LOG("Pursuit theme: dispatch finished her broadcast.");
            gVehicleReportDue = gVehicleReportOn;
            gVehicleReportAt = now;
        }
    }

    void ReportVehicle(EAXAirSupport* heli, unsigned int now) {
        if (!gVehicleReportDue) return;
        if (now < gVehicleReportAt || now - gVehicleReportAt > kVehicleReportWindow) {
            gVehicleReportDue = false;
            return;
        }
        if (!heli->IsActive() || IsRadioHeld(now)) return;
        gVehicleReportDue = false;
        Speech::ScheduledSpeechEvent* event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
            heli->VehicleReport();
            return nullptr;
        });
        Listen(gVehicleLine, event, now);
        LOG("Vehicle report: dispatch's theme-change broadcast is over, so the pilot describes your car (%s).",
            event ? "queued" : "turned down by the game");
    }

    void ReportWeather(EAXAirSupport* heli, unsigned int now) {
        const float rain = RainIntensity();
        if (!(rain > 0.0f)) {
            gRainReported = false;
            return;
        }
        if (gRainReported || !(rain > kRainReportLevel) || !heli->IsActive()) return;
        gRainReported = true;
        ResetPlayCount(kAnytimeEvents_WeatherReport);
        Speech::ScheduledSpeechEvent* event = OwnLine([heli]() -> Speech::ScheduledSpeechEvent* {
            heli->EAXCop::WeatherReport();
            return nullptr;
        });
        if (Listen(gWeatherLine, event, now)) event->priority = kInterruptPriority;
        LOG("Weather: it started raining, so the pilot cuts in about the weather (%s).", event ? "queued" : "turned down by the game");
    }

    void HelicopterArrived(unsigned int now) {
        LOG("Helicopter: a helicopter joined the chase.");
        gHeliJoinedAt = now;
        gRoadblockAsked = false;
        gAirSupportWanted = false;
        gAirSupportAsked = false;
        const bool respawn = gHelisThisPursuit++ > 0;
        if (gAirSupportStep == AirSupportStep::Request) {
            LOG("Air support: a helicopter showed up before anyone asked, so nobody asks.");
            FinishAirSupport(now, false);
        }
        if (respawn) {
            if (gAirSupportStep != AirSupportStep::Ready) FinishAirSupport(now, false);
            LOG("Helicopter: a new helicopter is in the chase, so the pilot cuts in to say he has you.");
            gSpotPending = true;
            gSpotInterrupt = true;
            gSpotGameLine = SpotLine::Ours;
            gSpotAt = now;
            return;
        }
        StartCheckIn(now);
    }

    void WatchHelicopter(EAXAirSupport* heli, unsigned int now) {
        AIVehicleHelicopter* vehicle = gHeliVehicle;
        const bool heliOut = vehicle != nullptr;
        if (heliOut && (!gHeliOut || vehicle != gHeliSeen)) {
            gCheckInStep = CheckInStep::Idle;
            gTunnelAlertPending = false;
            gHeliDown = false;
            gHeliAlive = false;
            gBailoutPending = false;
            gAnytimeEvents_Unit911Reply_DepFollow.Restore();
            HelicopterArrived(now);
        }
        if (gHeliOut && !heliOut) {
            LOG("Helicopter: it left the chase, so a unit will ask for another one when it can see you.");
            gAirSupportWanted = true;
            gAirSupportAsked = false;
            gAirSupportFailures = 0;
        }
        gHeliOut = heliOut;
        gHeliSeen = vehicle;

        const bool dead = heli->IsDead();
        if (heliOut && !dead) gHeliAlive = true;
        if (dead && gHeliAlive && !gHeliDown) {
            gHeliAlive = false;
            gHeliDown = true;
            gTunnelAlertPending = false;
            LOG("Going down: the helicopter was destroyed.");
            ReportDamage(heli, now);
        }
    }

    void EndPursuit(unsigned int now) {
        if (gAirSupportStep != AirSupportStep::Ready) FinishAirSupport(now, false);
        if (gRoadblockStep != RoadblockStep::Ready) FinishRoadblock();
        gAirSupportWanted = false;
        gAirSupportAsked = false;
        gRoadblockRequested = false;
        gHelisThisPursuit = 0;
        gThemeKnown = false;
        gBroadcastTracked = false;
        gVehicleReportDue = false;
        gHistoryTracked = false;
        ShowHiddenTakes();
        if (gBackupStep != BackupStep::Idle) FinishBackupReply();
        gHeliAsked = false;
        gSpotPending = false;
        gSpotInterrupt = false;
        gSpotSaid = false;
    }

    void PursuitTick(unsigned int now) {
        ResolveBailout(now);
        const float pursuitDuration = PursuitDuration();
        if (pursuitDuration < 0.0f) {
            EndPursuit(now);
            return;
        }
        if (!(RainIntensity() > 0.0f)) gRainReported = false;
        if (gThemeBroadcastOn) NoteThemeChange(pursuitDuration, now);
        FollowBroadcast(now);
        if (gHistoryAsked && !gHistoryTracked) AnswerHistoryRequest(now);
        if (gBackupReplyOn) FollowBackupCalls(now);
        if (gHeliAsked) AnswerDispatchQuestion(now);
        FollowHistoryLine(now);
        ExpireHiddenTakes(now);
        if (gRoadblockRequested && !ActiveHeli()) {
            LOG("Roadblock: no helicopter is out, so the ground units handle it.");
            gRoadblockRequested = false;
        }
        if (gAirSupportWanted && gAirSupportStep == AirSupportStep::Ready && gRoadblockStep == RoadblockStep::Ready && !ActiveHeli()
            && !Speech::Manager::IsCopSpeechBusy() && PickAirSupportCaller())
            StartAirSupport(now);
        AdvanceAirSupport(now);
        if (gRoadblockOn) AdvanceRoadblock(now);
    }

    int __cdecl TakeOffsetHook(const void* header, int take, uint32_t* offset, uint32_t* size) {
        if (IsHeliCheckIn(header)) NoteHistoryRequest(take, Now());
        LogTake(header, take);
        return Game::Call<int>(kTakeOffset, header, take, offset, size);
    }

    bool __cdecl AllowTake(const void* header, int take) {
        if (!gBackupReplyOn || !IsHeliCheckIn(header)) return true;
        const bool backupTake = take >= 0 && take < kBackupReplyTakes;
        return gBackupReplying ? backupTake : !backupTake;
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

Speech::ScheduledSpeechEvent* Speech::Manager::ScheduleSpeechPartIIHook(unsigned int size, void* data, Csis::InterfaceId& iid, Csis::FunctionHandle& fh,
                                                                        EAXCharacter* actor) {
    const unsigned int now = Now();
    RestoreExpiredRules(now);
    if (!gOwnLine && IsRadioHeld(now)) return nullptr;
    ScheduledSpeechEvent* event = Game::Call<ScheduledSpeechEvent*, unsigned int, void*, Csis::InterfaceId&, Csis::FunctionHandle&, EAXCharacter*>(
        gScheduleSpeechPartIIOriginal, size, data, iid, fh, actor);
    if (gOwnLine)
        gOwnScheduled = event;
    else
        NoteDispatchQuestion(size, data, iid, event, now);
    return event;
}

void Speech::StrategyFlow::MessageReqBackup(const MReqBackup& message) {
    Game::ThisCall<void>(gMessageReqBackupOriginal, this, &message);
}

void Speech::StrategyFlow::MessageReqBackupHook(const MReqBackup& message) {
    MessageReqBackup(message);
    const int type = message.GetBackupType();
    if (type != kRoadblockBackupType && type != kStrategyBackupType) return;
    LOG("Roadblock: the police are setting up a roadblock.");
    gRoadblockRequested = true;
    gRoadblockRequestedAt = Now();
}

int MiscSpeech::LostSuspectHook(int spkrID) {
    if (SoundAI::Get() == nullptr || gHeliVehicle == nullptr) {
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

void EAXCop::UpdateHook() {
    EAXCop::Update();
    const unsigned int now = Now();
    if (now == gLastPursuitTick) return;
    gLastPursuitTick = now;
    PursuitTick(now);
}

void EAXAirSupport::UpdateHook() {
    EAXAirSupport::Update();
    const unsigned int now = Now();
    ResolveBailout(now);
    WatchHelicopter(this, now);
    if (!gHeliOut || gHeliDown) return;
    CheckIn(this, now);
    TunnelAlert(this, now);
    PendingSpot(this, now);
    if (gRoadblockOn) AnswerRoadblockRequest(this, now);
    if (gWeatherReportOn) ReportWeather(this, now);
    if (gVehicleReportOn) ReportVehicle(this, now);
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
    const unsigned int now = Now();
    if (gHeliDown) {
        LOG("Going down: skipped the game's second going-down call.");
        return;
    }
    if (gTunnelAlertPending) LOG("Tunnel warning: dropped, the helicopter is going down.");
    gTunnelAlertPending = false;
    gHeliDown = true;
    if (hit) {
        LOG("Going down: the helicopter was hit hard, so the pilot reports damage.");
        ReportDamage(this, now);
        return;
    }
    if (!gBailoutCauseOn) {
        GamesOwnBailout(this, now);
        return;
    }

    gBailoutPending = true;
    gBailoutAt = now;
    gBailoutHeli = this;
    gBailoutFuelKnown = GetFuelTimeRemaining(this, gBailoutFuel);
    gBailoutCause = GetCauseOfBailout();
}

void EAXAirSupport::HazardAlertHook(Csis::Type_heli_hazard_alert_type type) {
    if (gHeliDown || IsDead()) {
        LOG("Hazard warning: dropped, the helicopter is leaving the chase.");
        return;
    }
    if (type != Csis::Type_heli_hazard_alert_type_approaching_tunnel) {
        EAXAirSupport::HazardAlert(type);
        return;
    }
    gTunnelAlertPending = true;
    gTunnelAlertAt = Now() + kTunnelAlertDelay;
}

void EAXAirSupport::RegainVisualHook() {
    HeliSpots(this, SpotLine::RegainVisual);
}

void EAXAirSupport::SpottedHook() {
    HeliSpots(this, SpotLine::Spotted);
}

void EAXAirSupport::SwarmingHook() {
    if (ActiveHeli() && !gHeliDown) {
        EAXAirSupport::Swarming();
        return;
    }
    LOG("Swarming: the helicopter is down or gone, so the pilot doesn't report cover units closing in.");
}

void EAXAirSupport::PursuitUpdateReplyHook() {
    const unsigned int now = Now();
    if (gUpdateReplyTracked && Track(gUpdateReplyLine, now) == LineState::Waiting) return;
    if (gHeliAsked && now >= gHeliQuestion.queued && now - gHeliQuestion.queued <= kUpdateReplyWindow
        && Track(gHeliQuestion, now) != LineState::Dropped)
        return;
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
                          && Patch::RedirectCall(kAnytimeEvents_LostSuspect.name, kTerminatePursuitLostSuspectCall, kMiscSpeech_LostSuspect,
                                                 reinterpret_cast<const void*>(&MiscSpeech::LostSuspectHook));
    if (lostSuspect)
        LOG("%s restored: the pilot calls it when the police lose you while a helicopter is out.", kAnytimeEvents_LostSuspect.name);

    if (Memory::Matches(kOnCollisionBailout, kOnCollisionBailoutCode, sizeof(kOnCollisionBailoutCode)))
        gOnCollisionReturn = kOnCollisionBailout + sizeof(kOnCollisionBailoutCode);
    gBailoutCauseOn = Memory::Matches(kEAXAirSupportUpdateFuelCheck, kEAXAirSupportUpdateFuelCheckCode, sizeof(kEAXAirSupportUpdateFuelCheckCode))
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

    const bool heliUpdate = Patch::ReplaceVirtual("EAXAirSupport::Update", kEAXAirSupportVTable, kUpdateSlot, kEAXAirSupport_Update,
                                                  Game::MethodAddress(&EAXAirSupport::UpdateHook));
    const bool scheduleHooked = heliUpdate
                             && Patch::Detour("Speech::Manager::ScheduleSpeechPartII", kManager_ScheduleSpeechPartII, kScheduleSpeechPartIIEntry,
                                              sizeof(kScheduleSpeechPartIIEntry), reinterpret_cast<const void*>(&Speech::Manager::ScheduleSpeechPartIIHook),
                                              gScheduleSpeechPartIIOriginal);

    gAirSupportOn = scheduleHooked && kBackup_CallForBU.VerifyVirtual(kEAXCopVTable, kCallForBackupSlot) && kBackup_DispBackupReply.Verify()
                 && Patch::ReplaceVirtual("EAXCop::Update", kEAXCopVTable, kUpdateSlot, kEAXCop_Update, Game::MethodAddress(&EAXCop::UpdateHook));
    if (gAirSupportOn)
        LOG("%s restored: when a helicopter goes down, a unit that can see you asks for another one on the radio.", kBackup_CallForBU.name);

    gCheckInOn = scheduleHooked && kAnytimeEvents_Unit911Reply.VerifyVirtual(kEAXAirSupportVTable, kReply911Slot);
    if (gCheckInOn)
        LOG("%s restored: the pilot checks in when the first helicopter joins.", kAnytimeEvents_Unit911Reply.name);

    gRoadblockOn = gAirSupportOn && kStaticRoadblock_DispRBUpdate.Verify() && kStaticRoadblock_CallForRB.Verify()
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
    gDriverHistoryOn = takesHooked && scheduleHooked && kAnytimeEvents_DriverHistory.Verify();
    if (gDriverHistoryOn)
        LOG("%s restored: when the pilot asks for your history at heat 5 and up, dispatch reads out your record.", kAnytimeEvents_DriverHistory.name);

    gSwarmingOn = ReplaceVirtual(kHeliSpecific_HeliSwarming, kEAXAirSupportVTable, kSwarmingSlot, &EAXAirSupport::SwarmingHook);
    if (gSwarmingOn)
        LOG("%s fixed: the pilot only reports cover units closing in while his helicopter is up.", kHeliSpecific_HeliSwarming.name);

    const bool takeChoiceHooked = takesHooked && gCheckInOn && Patch::CallsTo(kTakeCheckCall, kTakeCheck)
                               && Patch::RedirectCall("Speech player take choice", kTakeCheckCall, kTakeCheck, reinterpret_cast<const void*>(&TakeCheckHook));
    if (takesHooked && gCheckInOn && !takeChoiceHooked)
        LOG("Speech player: another mod changed how the game picks takes, so the pilot's backup reply is off.");
    gBackupReplyOn = takeChoiceHooked && kBackup_DispBUETA.Verify();
    if (gBackupReplyOn)
        LOG("%s: after a unit calls for backup and dispatch answers, the pilot now and then answers too.", kBackup_CallForBU.name);

    gPursuitUpdateReplyOn = scheduleHooked && kAnytimeEvents_SuspectBehaviour.VerifyVirtual(kEAXAirSupportVTable, kSuspectBehaviorSlot)
                         && ReplaceVirtual(kAnytimeEvents_PursuitUpdateRep, kEAXAirSupportVTable, kPursuitUpdateReplySlot, &EAXAirSupport::PursuitUpdateReplyHook);
    if (gPursuitUpdateReplyOn)
        LOG("%s restored: when dispatch asks the helicopter for an update, the pilot describes your driving.", kAnytimeEvents_PursuitUpdateRep.name);
    const bool suspectBehavior = gPursuitUpdateReplyOn
                              && ReplaceVirtual(kAnytimeEvents_SuspectBehaviour, kEAXAirSupportVTable, kSuspectBehaviorSlot, &EAXAirSupport::SuspectBehaviorHook);
    if (suspectBehavior)
        LOG("%s fixed: the pilot only describes your driving when dispatch asks him; the game's own driving calls go to ground units.",
            kAnytimeEvents_SuspectBehaviour.name);

    gThemeBroadcastOn = scheduleHooked && kAnytimeEvents_DispPursEscGen.Verify();
    if (gThemeBroadcastOn)
        LOG("%s: when the pursuit music moves to the next theme and a helicopter is up, dispatch makes a broadcast.", kAnytimeEvents_DispPursEscGen.name);
    gVehicleReportOn = scheduleHooked && kSetup_VehicleReport.VerifyVirtual(kEAXAirSupportVTable, kVehicleReportSlot);
    if (gVehicleReportOn)
        LOG("%s restored: after dispatch's theme-change broadcast, the pilot describes your car.", kSetup_VehicleReport.name);

    gWeatherReportOn = heliUpdate && kAnytimeEvents_WeatherReport.VerifyVirtual(kEAXAirSupportVTable, kWeatherReportSlot);
    if (gWeatherReportOn)
        LOG("%s restored: when it starts raining, the pilot cuts in about the weather.", kAnytimeEvents_WeatherReport.name);

    return intentToRam || strategyReset || lostSuspect || bailout || regainVisual || canPlayback || gAirSupportOn || gCheckInOn || gRoadblockOn
        || gDriverHistoryOn || gSwarmingOn || gBackupReplyOn || gPursuitUpdateReplyOn || gThemeBroadcastOn || gVehicleReportOn || gWeatherReportOn;
}

void SoundAI::Restore() {
    ShowHiddenTakes();
    RestoreAllRules();
    Patch::RestoreAll();
    Log::Close();
}
