#pragma once

struct HSIMABLE__;
typedef HSIMABLE__* HSIMABLE;

class IVehicle;
class IVehicleAI;
class IRigidBody;
class IAIHelicopter;
class IPursuitAI;
class IPursuit;
class IPlaceableScenery;
class EAXCharacter;
struct IPlayer;

namespace UMath {

    struct Vector3 {
        float x, y, z;
    };

}

namespace Sim {

    class IEntity;

    namespace Collision {

        struct Info {
            enum CollisionType {
                NONE   = 0,
                OBJECT = 1,
                WORLD  = 2,
                GROUND = 3,
            };

            UMath::Vector3 position;
            const void*    objAsurface;
            UMath::Vector3 normal;
            int            type : 3;
            int            objAImmobile : 1;
            int            objADetached : 1;
            int            objBImmobile : 1;
            int            objBDetached : 1;
            int            sliding : 1;
            int            unused : 24;
            UMath::Vector3 closingVel;
            float          force;
            UMath::Vector3 armA;
            HSIMABLE       objA;
            UMath::Vector3 armB;
            HSIMABLE       objB;
        };

        class IListener {
          public:
            void OnCollision(const Info& cinfo);

            void OnCollisionHook(const Info& cinfo);
        };

    }

}

class ISimable {
  public:
    static ISimable* FindInstance(HSIMABLE handle);

    virtual ~ISimable();
    virtual int GetSimableType() const;
    virtual void Kill();
    virtual bool Attach(void* object);
    virtual bool Detach(void* object);
    virtual const void* GetAttachments() const;
    virtual void AttachEntity(Sim::IEntity* e);
    virtual void DetachEntity();
    virtual IPlayer* GetPlayer() const;
    virtual bool IsPlayer() const;
};

class IRoadBlock {
  public:
    virtual ~IRoadBlock();
    virtual bool AddVehicle(IVehicle* vehicle) = 0;
    virtual void AddSmackable(IPlaceableScenery* smackable, bool isSpikeStrip) = 0;
    virtual bool RemoveVehicle(IVehicle* vehicle) = 0;
    virtual void ReleaseAllSmackables() = 0;
    virtual int GetNumCops() = 0;
    virtual void SetPursuit(IPursuit* pursuit) = 0;
    virtual IPursuit* GetPursuit() = 0;
    virtual void SetDodged(bool dodged) = 0;
    virtual bool GetDodged() = 0;
    virtual short GetNumCopsDamaged() = 0;
    virtual short GetNumCopsDestroyed() = 0;
    virtual void IncNumCopsDamaged() = 0;
    virtual void IncNumCopsDestroyed() = 0;
    virtual const UMath::Vector3& GetRoadBlockCentre() = 0;
    virtual const UMath::Vector3& GetRoadBlockDir() = 0;
    virtual void SetRoadBlockCentre(const UMath::Vector3& centre, const UMath::Vector3& dir) = 0;
    virtual float GetMinDistanceToTarget(float dT, float& distxz, IVehicle** minDistVehicle) = 0;
    virtual short GetNumSpikeStrips() = 0;
};

enum SPCHType_1_EventID {
    kSPCH1_EventID_CallForRB    = 76,
    kSPCH1_EventID_RegainVisual = 107,
    kSPCH1_EventID_LostSuspect  = 108,
    kSPCH1_EventID_IntentToRam  = 113,
    kSPCH1_EventID_VehicleReport = 155,
    kSPCH1_EventID_HeliBailout   = 176,
    kSPCH1_EventID_SpotterWanted = 216,
    kSPCH1_EventID_InterruptRamHigh = 198,
};

enum SPCHType_EventRuleResult {
    kSPCH_EventRuleResult_Ok = 0,
};

enum SpeechValRtnType {
    kKeepEvt     = 0,
    kIntEvt      = 1,
    kDitchEvt    = 2,
    kEvtNotFound = 3,
    kDeferEvt    = 4,
};

class Timer {
  public:
    int PackedTime;
};

class GameplaySettings {
  public:
    bool          AutoSaveOn;
    bool          RearviewOn;
    bool          Damage;
    unsigned char SpeedoUnits;
};

class UserProfile {
  public:
    unsigned char    mOptions[0x38];
    GameplaySettings TheGameplaySettings;
};

class cFrontendDatabase {
  public:
    GameplaySettings* GetGameplaySettings() {
        return &CurrentUserProfiles[0]->TheGameplaySettings;
    }

    unsigned char mFrontendDatabase[0x10];
    UserProfile*  CurrentUserProfiles[2];
};

extern cFrontendDatabase*& FEDatabase;

namespace Csis {

    enum Result : int {};

    struct InterfaceId {
        const char* pString;
        short       systemCrc;
        short       interfaceCrc;
    };

    class FunctionHandle {
      public:
        void* mpPrivate;
        int   mKey;
    };

    enum Type_code : int {};
    enum Type_heat_level : int {};

    enum Type_intensity {
        Type_intensity_Normal = 1,
        Type_intensity_High   = 2,
    };

    enum Type_heli_hazard_alert_type : int {};
    enum Type_car_color : int {};
    enum Type_car_type : int {};

    enum Type_speed {
        Type_speed_over_speed_limit = 1,
    };

    enum Type_measurement {
        Type_measurement_generic       = 1,
        Type_measurement_imperial_only = 2,
        Type_measurement_metric_only   = 4,
    };

    enum Type_roadblock_type {
        Type_roadblock_type_Roadblock_Generic_ = 1,
        Type_roadblock_type_Spikes             = 2,
    };

}

namespace Attrib {
namespace Gen {

    class pvehicle {
      public:
        struct _LayoutStruct {
            unsigned char       mBeforeVerbalType[0x40];
            Csis::Type_car_type VerbalType;
        };

        bool IsValid() const {
            return mLayoutPtr != nullptr;
        }

        Csis::Type_car_type VerbalType() const {
            return static_cast<const _LayoutStruct*>(mLayoutPtr)->VerbalType;
        }

        void*          mOwner;
        const void*    mCollection;
        const void*    mLayoutPtr;
        unsigned int   mMsgPort;
        unsigned short mFlags;
        unsigned short mLocks;
    };

}
}

namespace Csis {

    struct Setup_VehicleReportStruct {
        int              speaker_id;
        Type_car_color   car_color;
        Type_car_type    car_type;
        Type_speed       speed;
        Type_measurement measurement;
    };

    struct AnytimeEvents_IntentToRamStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct AnytimeEvents_LostSuspectStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct AnytimeEvents_RegainVisualStruct {
        int            speaker_id;
        Type_intensity intensity;
    };

    struct StaticRoadblock_CallForRBStruct {
        int                 speaker_id;
        Type_code           code;
        Type_roadblock_type roadblock_type;
    };

}

namespace Speech {

    enum SpeakerID {
        Dispatch = 1,
        Heli     = 2,
    };

    struct Battalion {
        int name;
        int number;
    };

    struct SPCHType_SampleRequestData;
    struct SpeechSampleData;

    struct ScheduledSpeechEvent {
        void* GetData(unsigned int* datasize);

        Csis::InterfaceId*    iid;
        Csis::FunctionHandle* fh;
        SPCHType_1_EventID    ID;
        EAXCharacter*         actor;
        Timer                 entry_time;
        Timer                 playback_time;
        Timer                 finish_time;
        SpeechSampleData*     assoc_samples[7];
        unsigned char         assoc_samples_count;
        unsigned char         assoc_samples_prep;
        unsigned char         curndx;
        unsigned char         priority;
        short                 frameindex;
        short                 flags;
    };

    struct History {
        Timer          time;
        unsigned short count;
        unsigned short speakers;
    };

    struct EventHistory {
        History* Find(SPCHType_1_EventID id);
        int      GetCount(SPCHType_1_EventID id);
    };

    class Module {
      public:
        virtual ~Module();
        virtual void Init(int channel) = 0;
        virtual void LoadBanks() = 0;
        virtual int TestSentenceRuleCallback(int eventID, int ruleID, int parmValue) = 0;
        virtual int SetSentenceRuleCallback(int eventID, int ruleID, int parmValue) = 0;
        virtual SPCHType_EventRuleResult EventRuleCallback(int eventID) = 0;
        virtual int GetNumBanks() = 0;
        virtual unsigned int GetBankOffset(int bnum) = 0;
        virtual void Update() = 0;
        virtual const char* GetFilename() = 0;
        virtual bool QueStream(int stream_type, void (*callback)(), bool trigger_play_after_callback) = 0;
        virtual unsigned int SampleRequestCallback(SPCHType_SampleRequestData* data) = 0;
        virtual bool IsStreamQueued() = 0;
        virtual char* GetCSIptr() = 0;
        virtual int GetChannel() = 0;
        virtual char* GetEventDat() = 0;
        virtual bool IsDataLoaded() = 0;
        virtual bool PlayStream(int stream_id) = 0;
        virtual void ReleaseResource() = 0;
    };

    struct Manager {
        static ScheduledSpeechEvent* ScheduleSpeechPartII(unsigned int size, void* data, Csis::InterfaceId& iid, Csis::FunctionHandle& fh,
                                                          EAXCharacter* actor);
        static bool IsQueued(SPCHType_1_EventID evtID, int indices);
        static bool IsCopSpeechPlaying(SPCHType_1_EventID event_id);
        static Csis::Result IndirectSpeechEvent(ScheduledSpeechEvent* evt, bool test_only);
        static SpeechValRtnType PostValidate(ScheduledSpeechEvent* evt, unsigned int mask);
        static void ClearPlayback();

        static EventHistory& GetHistory() {
            return mGlobalHistory;
        }

        static Csis::Result IndirectSpeechEventHook(ScheduledSpeechEvent* evt, bool test_only);
        static SpeechValRtnType PostValidateHook(ScheduledSpeechEvent* evt, unsigned int mask);

        static EventHistory& mGlobalHistory;

        static Module* (&m_SpeechModule)[2];
    };

    class RoadblockFlow {
      public:
        void Setup();
        void SetupHook();

        unsigned char mSpeechFlow[0x10];
        Timer         mT_setup;
    };

}

class MiscSpeech {
  public:
    static int LostSuspect(int spkrID);

    static int LostSuspectHook(int spkrID);
};

class EAXCharacter {
  public:
    virtual ~EAXCharacter();

    virtual void Ack();
    virtual void Deny();
    virtual void InterruptStatic();
    virtual void InterruptExpletive();
    virtual void InterruptViolent();
    virtual void InterruptComposedLow();
    virtual void InterruptComposedHigh();
    virtual void DriverHistory();
    virtual void HeatJump(Csis::Type_heat_level heat);
    virtual HSIMABLE GetHandle();
    virtual void SetHandle(HSIMABLE handle);
    virtual int GetSpeakerID();
    virtual int GetCallsign();
    virtual int GetUnitNumber();
    virtual void SetCallsign(int callsign);
    virtual void SetUnitNumber(int unitnum);
    virtual void SetSpeakerID(int spkrID);
    virtual void SetPosition(const UMath::Vector3& v);
    virtual const UMath::Vector3 GetPosition();
    virtual void SetSpeed(const float speed);
    virtual void Update();
    virtual float GetDistance();
    virtual float GetHealth();
    virtual bool IsActive();
    virtual void SetActive(bool active);
    virtual float GetSpeed();
    virtual bool IsDead();
    virtual bool HasLOS();
    virtual void SetLOS(bool yes);
    virtual Csis::Type_code GetRandomizedCode();
    virtual void Reset();

    int               mTimeLastSpoken;
    int               mLastEvent;
    int               mSpeakerID;
    HSIMABLE          mHandle;
    Speech::Battalion mCallsign;
    UMath::Vector3    mPos;
    float             mSpeed;
    float             mDistance;
    float             mHealth;
    bool              mDestroyed;
    bool              mActive;
    bool              mSuspectLOS;
};

class EAXCop : public EAXCharacter {
  public:
    virtual void AttemptVehicleStop();
    virtual void VehicleReport();
    virtual void InitiatePursuit();
    virtual void LocationReport();
    virtual void SelfStrategy(int type);
    virtual void InitialCallForBackup();
    virtual void CallForBackup(int type);
    virtual void UnitBackupReply();
    virtual void InitiateStrategy(int type);
    virtual void CallToPosition(EAXCop* cop);
    virtual void CallToPositionReminder();
    virtual void StrategyExecute();
    virtual void Collision(int collisionType, float force, EAXCop* spkr);
    virtual void AnticipateSuccess();
    virtual void AnticipateFail();
    virtual void OutcomeFail(short intensity);
    virtual void StrategyReset(bool new_strategy);
    virtual void SuspectBehavior();
    virtual void SuspectOutrun();
    virtual void SuspectUTurn();
    virtual void LostSuspect();
    virtual void LostVisual();
    virtual void RegainVisual();
    virtual void SwarmingReply();
    virtual void Arrest();
    virtual void PursuitUpdateReply();
    virtual void ReinitiatePursuit();
    virtual void NegativeBackupReply();
    virtual void BackupReminder(int type);
    virtual void BackupArrives();
    virtual void IntentToRam();
    virtual void CallforEV(unsigned int type);
    virtual void UnitDisabled(int other);
    virtual void Bailout();
    virtual void LoBailout();
    virtual void HiBailout();
    virtual void BailoutTraffic();
    virtual void BailoutBadRoad();
    virtual void Spotter();
    virtual void SpotterReply();
    virtual void Reply911();
    virtual void DenyBailout();
    virtual void PrimaryEngage();
    virtual void Bullhorn();
    virtual void PreBullhorn();
    virtual void BullhornArrest();
    virtual void SuspectConfirmed();
    virtual void FocusChange();
    virtual void Spotted();
    virtual void DirectionChange();
    virtual void CallForSwarming();
    virtual void SpotterWanted();
    virtual void Offroad(unsigned int id, bool subsequent);
    virtual void WeatherReport();
    virtual void CallForRB();
    virtual void RBReminder();
    virtual void NegRBReply();
    virtual void RBApproach();
    virtual void RBEngage(bool spikes_hit);
    virtual void PursuitApproaching();
    virtual void RBAverted();
    virtual void CallForSubRB();
    virtual void RearEnded(Csis::Type_intensity intensity);
    virtual void HeadOn(Csis::Type_intensity intensity);
    virtual void SideSwiped(Csis::Type_intensity intensity);
    virtual void TBoned(Csis::Type_intensity intensity);
    virtual void SuspectRollover(Csis::Type_intensity intensity);
    virtual void SuspectAirborne(Csis::Type_intensity intensity);
    virtual void SuspectSpunout(Csis::Type_intensity intensity);
    virtual void SuspectBrake();
    virtual void SwapVoices(EAXCop* cop);
    virtual bool IsPrimary();
    virtual bool IsHeli();
    virtual bool IsCross();
    virtual void SetInFormation(bool yes);
    virtual bool GetInFormation();
    virtual void SetInPosition(bool yes);
    virtual bool GetInPosition();
    virtual void SetTgtOffset(const UMath::Vector3& off);
    virtual const UMath::Vector3 GetTgtOffset();
    virtual bool SetRank(int newrank);
    virtual bool GetRank();
    virtual void JustHitTraffic();
    virtual void WasRammed();
    virtual int GetTimesRammed();
    virtual float GetTimeLastSeen();
    virtual float GetTimeAirborne();
    virtual float GetTimeLastRammed();
    virtual float GetTimeLastClosing();
    virtual float IsAhead();
    virtual void SetAhead(bool ahead);
    virtual void Impact_Suspect_World();
    virtual void Impact_Suspect_Semi();
    virtual void Impact_Suspect_Train();
    virtual void Impact_Suspect_Guardrail();
    virtual void Impact_Suspect_GasStation();
    virtual void Impact_Suspect_Spikebelt();
    virtual void Impact_Suspect_Traffic(Csis::Type_intensity intensity);
};

class EAXDispatch : public EAXCharacter {};

class EAXAirSupport : public EAXCop {
  public:
    virtual void JoinRB();
    virtual void IntentToBail();
    virtual void Swarming();
    virtual void HazardAlert(Csis::Type_heli_hazard_alert_type type);
    virtual void Quadrant();
    virtual void QuadrantMoving();

    void SetHandle(HSIMABLE handle) override;
    void Update() override;
    void SetLOS(bool yes) override;
    void IntentToRam() override;
    void Bailout() override;

    void SetHandleHook(HSIMABLE handle);
    void UpdateHook();
    void BailoutHook();
    void KillShot();
    void SetLOSHook(bool yes);
    void IntentToRamHook();
    void IntentToRamPullBack();
    void RegainVisualAfterRespawn();
    void VehicleReportOnFirstSighting();
    void SpotterWantedDuringCooldown();
    void SpotterWantedForEscapedCar();
    void CallForRBAhead(IRoadBlock* roadblock);
};

class AIActionHeliPursuit {
  public:
    enum kPursuitMode {
        kStraight_Line     = 0,
        kSearch_Pattern    = 1,
        kSkid_Hit_Approach = 2,
        kSkid_Hit_Strike   = 3,
    };

    void SkidHitPursuit();
    void SkidHitPursuitHook();

    unsigned char  mAIAction[0x4C];
    IVehicleAI*    mIVehicleAI;
    IVehicle*      mIVehicle;
    IRigidBody*    mIRigidBody;
    IAIHelicopter* mIAIHelicopter;
    IPursuitAI*    mIPursuitAI;
    float          mPursuitTime;
    float          mSkidKnockTimer;
    float          mPathTime;
    bool           mBuildingPath;
    float          mSearchPatternAngle;
    UMath::Vector3 mSearchDestPoint;
    IRigidBody*    mPlayerRigidBody;
    UMath::Vector3 mPlayerPosition;
    UMath::Vector3 mSkidHitOffset;
    int            mCollisionAbort;
    float          mPlayerSpeed;
    kPursuitMode   mPursuitMode;
};

class SoundAI {
  public:
    enum BailoutType {
        kOutrunBail = 0,
        kForcedBail = 1,
    };

    enum PursuitState {
        kActive      = 0,
        kSearching   = 1,
        kInactive    = 2,
        kOtherTarget = 3,
    };

    enum MachineState {
        kPursuitFlow  = 1,
        kStrategyFlow = 2,
        kLost         = 666,
        kTerminal     = 999,
    };

    enum SoundAIFlags {
        BUSTED = 1 << 7,
    };

    struct CarCustomizations {
        Csis::Type_car_color color;
        unsigned int         flags;
    };

    static SoundAI* Get();
    static bool     Init(void* module);
    static void     Restore();

    EAXAirSupport* GetHeli() {
        return mHeli;
    }

    int GetNumActiveCopCars() {
        return mNumActiveCopCars;
    }

    const float GetPlayerSpeed() {
        return mPlayerSpeed;
    }

    PursuitState GetPursuitState() {
        return mPursuitState;
    }

    const int GetFocus() {
        return mFocus;
    }

    const Attrib::Gen::pvehicle& GetPlayerSpecs() {
        return mPVehicle;
    }

    unsigned int GetPlayerCarColor() {
        return mPlayerCarCustom != nullptr ? mPlayerCarCustom->color : 0u;
    }

    IRoadBlock* GetRoadblock();
    void TerminatePursuit(BailoutType type);
    void ResetPursuit(bool including_music);
    void AddNewHeli(IVehicle* heli);

    void TerminatePursuitHook(BailoutType type);
    void ResetPursuitHook(bool including_music);
    void AddNewHeliHook(IVehicle* heli);

    unsigned char  mActivity[0x54];
    unsigned int   mFlags;
    unsigned char  mActorsAndUsage[0x80];
    EAXDispatch*   mDispatch;
    EAXCop*        mLeader;
    EAXAirSupport* mHeli;
    unsigned char  mCopsInFormation[0x14];
    float          mDeadAir;
    EAXCop*        mLastCopInFormation;
    EAXCop*        mLatestCop;
    int            mPlayerHeat;
    float          mPlayerSpeed;
    unsigned char  mPlayerDetails[0x2C];
    int            mFocus;
    unsigned char  mPursuitDetails[0x2C];
    Attrib::Gen::pvehicle mPVehicle;
    unsigned char  mTuneAndFlows[0x58];
    PursuitState   mPursuitState;
    unsigned char  mPursuitTimers[0x18];
    int            mNumActiveCopCars;
    unsigned char  mCopsInViewAndTimers[0x3C];
    CarCustomizations* mPlayerCarCustom;
};
