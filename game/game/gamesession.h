#pragma once

#include <Tempest/Sound>
#include <Tempest/SoundDevice>
#include <memory>
#include <cstdint>
#include <string>

#include "game/gamescript.h"
#include "camera.h"
#include "gametime.h"

class World;
class WorldView;
class Npc;
class Serialize;
class GSoundEffect;
class SoundFx;
class ParticleFx;
class VisualFx;
class WorldStateStorage;
class VersionInfo;
class GthFont;
class MmoRuntimeSqlite;

class GameSession final {
  public:
    GameSession()=delete;
    GameSession(const GameSession&)=delete;
    enum class StartupMode : uint8_t {
      NewGame,
      MmoDbContinue,
      };

    GameSession(std::string file);
    GameSession(std::string file, StartupMode startupMode);
    GameSession(Serialize&  fin, std::string sourceSlot = {});
    ~GameSession();

    void         save(Serialize& fout, std::string_view name, const Tempest::Pixmap &screen);
    void         recordMmoSaveSlot(std::string_view slotPath, std::string_view displayName);
    void         setupSettings();

    void         setWorld(std::unique_ptr<World> &&w);
    auto         clearWorld() -> std::unique_ptr<World>;

    void         changeWorld(std::string_view world, std::string_view wayPoint);
    void         exitSession();

    auto         version() const -> const VersionInfo&;

    const World* world() const { return wrld.get(); }
    World*       world()       { return wrld.get(); }

    WorldView*   view()   const;
    GameScript*  script() const { return vm.get(); }

    Camera&      camera()       { return     *cam; }

    auto         loadSound(const Tempest::Sound& raw) -> Tempest::SoundEffect;
    auto         loadSound(const SoundFx&        fx, bool& looped)  -> Tempest::SoundEffect;

    Npc*         player();
    void         updateListenerPos(const Camera::ListenerPos& lpos);

    gtime        time() const { return  wrldTime; }
    void         setTime(gtime t);
    void         tick(uint64_t dt);
    uint64_t     tickCount() const { return ticks; }

    void         setTimeMultiplyer(float t);

    void         updateAnimation(uint64_t dt);

    auto         updateDialog(const GameScript::DlgChoice &dlg, Npc &player, Npc &npc) -> std::vector<GameScript::DlgChoice>;
    void         dialogExec(const GameScript::DlgChoice &dlg, Npc &player, Npc &npc);
    void         recordDialogChoices(Npc &player, Npc &npc,
                                     const std::vector<GameScript::DlgChoice>& choices,
                                     std::string_view phase, bool includeImportant);
    void         recordMmoChapterIntro(std::string_view title, std::string_view subtitle,
                                       std::string_view image, std::string_view sound, int time);

    std::string_view         messageFromSvm(std::string_view id, int voice) const;
    std::string_view         messageByName (std::string_view id) const;
    uint32_t                 messageTime   (std::string_view id) const;

    AiOuputPipe* openDlgOuput(Npc &player, Npc &npc);
    bool         isNpcInDialog(const Npc& npc) const;
    bool         isInDialog() const;

  private:
    struct ChWorld {
      std::string zen, wp;
      };

    struct HeroStorage {
      void                 save(Npc& npc);
      void                 putToWorld(World &owner, std::string_view wayPoint) const;

      std::vector<uint8_t> storage;
      };

    bool         isWorldKnown(std::string_view name) const;
    void         initPerceptions();
    void         initScripts(bool firstTime);
    void         consumeMmoRestoreSnapshot(std::string_view reason) noexcept;
    auto         implChangeWorld(std::unique_ptr<GameSession> &&game, std::string_view world, std::string_view wayPoint) -> std::unique_ptr<GameSession>;
    auto         findStorage(std::string_view name) -> const WorldStateStorage&;

    Tempest::SoundDevice           sound;

    std::unique_ptr<Camera>        cam;
    std::unique_ptr<GameScript>    vm;
    std::unique_ptr<World>         wrld;
    std::unique_ptr<MmoRuntimeSqlite> mmoSqlite;

    struct MmoActionCheckpointState final {
      bool     initialized = false;
      uint64_t lastEmitTick = 0;
      float    posX = 0.f, posY = 0.f, posZ = 0.f;
      float    yaw = 0.f;
      int32_t  level = 0;
      int32_t  experience = 0;
      int32_t  experienceNext = 0;
      int32_t  learningPoints = 0;
      int32_t  healthCurrent = 0;
      int32_t  healthMax = 0;
      int32_t  manaCurrent = 0;
      int32_t  manaMax = 0;
      int32_t  strength = 0;
      int32_t  dexterity = 0;
      uint32_t guild = 0;
      int32_t  trueGuild = 0;
      int32_t  permanentAttitude = 0;
      int32_t  temporaryAttitude = 0;
      };

    const char* mmoActionCheckpointReason(const Npc& npc, uint64_t now) const noexcept;
    void        recordMmoActionCheckpointState(const Npc& npc, uint64_t now) noexcept;

    struct MmoActionMovementProposalState final {
      bool     initialized = false;
      uint64_t lastEmitTick = 0;
      float    posX = 0.f, posY = 0.f, posZ = 0.f;
      float    yaw = 0.f;
      int32_t  healthCurrent = 0;
      int32_t  healthMax = 0;
      int32_t  manaCurrent = 0;
      int32_t  manaMax = 0;
      bool     inAir = false;
      bool     falling = false;
      bool     fallingDeep = false;
      bool     slide = false;
      bool     jump = false;
      bool     jumpUp = false;
      bool     swim = false;
      bool     dive = false;
      bool     inWater = false;
      };

    const char* mmoActionMovementProposalReason(const Npc& npc, uint64_t now) const noexcept;
    void        recordMmoActionMovementProposalState(const Npc& npc, uint64_t now) noexcept;
    void        tickMmoMovementProposal(Npc& npc, uint64_t now) noexcept;

    struct MmoServerSnapshotRestoreState final {
      bool        requested = false;
      bool        completed = false;
      bool        waitingLogged = false;
      bool        storyDirtySinceRequest = false;
      uint64_t    requestedAtTick = 0;
      uint64_t    lastPollTick = 0;
      uint64_t    lastLiveRefreshPollTick = 0;
      uint32_t    lastAppliedSnapshotId = 0;
      std::string reason;
      };

    void        scheduleMmoServerSnapshotRestore(std::string_view reason, bool reuseExistingSnapshot = false) noexcept;
    bool        tryApplyMmoServerSnapshotRestore(bool forcePoll) noexcept;
    void        waitForMmoServerSnapshotRestoreDuringLoad() noexcept;
    void        pollMmoServerSnapshotRestore() noexcept;
    bool        tryApplyMmoServerWorldSnapshotRefresh() noexcept;
    void        markMmoServerSnapshotStoryDirty() noexcept;

    uint64_t                       ticks = 0, wrldTimePart = 0;
    MmoActionCheckpointState       lastMmoActionCheckpoint;
    MmoActionMovementProposalState lastMmoActionMovementProposal;
    MmoServerSnapshotRestoreState  mmoServerSnapshotRestore;
    uint64_t                       timeMul = 1000, timeMulFract = 0;
    gtime                          wrldTime;

    std::vector<WorldStateStorage> visitedWorlds;

    ChWorld                        chWorld;
    bool                           exitSessionFlg=false;

    static const uint64_t          multTime;
    static const uint64_t          divTime;
  };








