#include "inventorymenu.h"

#include <Tempest/Painter>
#include <Tempest/Log>
#include <Tempest/SoundEffect>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "utils/string_frm.h"
#include "world/objects/npc.h"
#include "world/objects/interactive.h"
#include "world/objects/item.h"
#include "world/world.h"
#include "utils/gthfont.h"
#include "utils/keycodec.h"
#include "gothic.h"
#include "game/gamesession.h"
#include "game/mmoclientbridge.h"
#include "resources.h"

using namespace Tempest;

namespace {

[[nodiscard]] std::optional<Mmo::ClientEquipmentSlot> equipmentSlotFromHint(
    const std::uint8_t hint) noexcept {
  using Slot = Mmo::ClientEquipmentSlot;
  switch(hint) {
    case 3U: return Slot::MeleeWeapon;
    case 4U: return Slot::RangedWeapon;
    case 5U: return Slot::Armor;
    case 6U: return Slot::Amulet;
    case 7U: return Slot::RingLeft;
    case 8U: return Slot::RingRight;
    case 9U: return Slot::Belt;
    case 10U: return Slot::Spell;
    default: return std::nullopt;
  }
}

[[nodiscard]] constexpr std::string_view equipmentSlotName(
    const Mmo::ClientEquipmentSlot slot) noexcept {
  using Slot = Mmo::ClientEquipmentSlot;
  switch(slot) {
    case Slot::MeleeWeapon: return "Melee";
    case Slot::RangedWeapon: return "Ranged";
    case Slot::Armor: return "Armor";
    case Slot::Amulet: return "Amulet";
    case Slot::RingLeft: return "Ring L";
    case Slot::RingRight: return "Ring R";
    case Slot::Belt: return "Belt";
    case Slot::Spell: return "Spell";
  }
  return "Unknown";
}

} // namespace

struct InventoryMenu::Page {
  Page()=default;
  Page(const Page&)=delete;
  virtual ~Page()=default;

  size_t                      size() const {
    if(is(nullptr))
      return 0;
    size_t ret = 0;
    auto it = iterator();
    while(it.isValid()) {
      ret++;
      ++it;
      }
    return ret;
    }
  Inventory::Iterator         get(size_t id) const {
    auto it = iterator();
    for(size_t i=0; i<id && it.isValid(); ++i)
      ++it;
    return it;
    }

  virtual bool                is(const Inventory* i) const { return i==nullptr; }
  virtual Inventory::Iterator iterator() const { throw std::runtime_error("index out of range");  }
  };

struct InventoryMenu::InvPage : InventoryMenu::Page {
  InvPage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Inventory);
    }

  const Inventory& inv;
  };

struct InventoryMenu::TradePage : InventoryMenu::Page {
  TradePage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Trade);
    }

  const Inventory& inv;
  };

struct InventoryMenu::RansackPage : InventoryMenu::Page {
  RansackPage(const Inventory& i):inv(i){}

  bool                is(const Inventory* i) const override { return &inv==i; }
  Inventory::Iterator iterator() const override {
    return inv.iterator(Inventory::T_Ransack);
    }

  const Inventory& inv;
  };

InventoryMenu::InventoryMenu(const KeyCodec& key)
  :keycodec(key) {
  slot = Resources::loadTexture("INV_SLOT.TGA");
  selT = Resources::loadTexture("INV_SLOT_HIGHLIGHTED.TGA");
  selU = Resources::loadTexture("INV_SLOT_EQUIPPED.TGA");
  tex  = Resources::loadTexture("INV_BACK.TGA"); // INV_TITEL.TGA

  int invMaxColumns = Gothic::settingsGetI("GAME","invMaxColumns");
  if(invMaxColumns>0)
    columsCount = size_t(invMaxColumns); else
    columsCount = 5;

  setFocusPolicy(NoFocus);
  setCursorShape(CursorShape::Hidden);
  takeTimer.timeout.bind(this,&InventoryMenu::onTakeStuff);
  }

InventoryMenu::~InventoryMenu() {
  }

void InventoryMenu::close() {
  if(serverCorpseMode)
    submitServerCorpseClose();
  if(state!=State::Closed) {
    if(state==State::Trade)
      Gothic::inst().emitGlobalSound("TRADE_CLOSE"); else
      Gothic::inst().emitGlobalSound("INV_CLOSE");
    }
  renderer.reset(true);
  takeTimer.stop();
  serverInventoryMode = false;
  serverCorpseMode = false;
  serverInventoryResyncAttempted = false;
  serverInventoryResyncRequested = false;
  observedServerInventory = {};
  observedServerCorpse = {};
  serverCorpseHandle = {};
  serverCorpseTitle.clear();
  serverPreviewItems.clear();
  mergeSource.reset();
  state  = State::Closed;
  }

void InventoryMenu::open(Npc &pl) {
  if(Mmo::isServerBoundClientModeEnabled()) {
    const auto* inventory = serverInventoryState();
    if(inventory == nullptr || !inventory->ready()) {
      Tempest::Log::e("MMO inventory open skipped: server inventory is not ready");
      return;
    }
    state = State::Equip;
    player = &pl;
    trader = nullptr;
    chest = nullptr;
    page = 0;
    serverInventoryMode = true;
    serverInventoryResyncAttempted = false;
    serverInventoryResyncRequested = false;
    observedServerInventory =
        Mmo::ClientPresentation::ServerInventoryPageModel(*inventory)
            .fingerprint();
    serverPreviewItems.clear();
    mergeSource.reset();
    pagePl.reset();
    pageOth.reset();
    adjustScroll();
    update();
    Gothic::inst().emitGlobalSound("INV_OPEN");
    Tempest::Log::i("MMO inventory opened: revision=",
                    inventory->inventory().revision(),
                    " stacks=", inventory->inventory().stacks().size());
    return;
  }
  if(pl.isDown() || pl.isMonster() || pl.isInAir() || pl.isSlide() || (pl.interactive()!=nullptr))
    return;
  if(pl.bodyStateMasked()==BS_UNCONSCIOUS || pl.bodyStateMasked()==BS_LIE)
    return;
  serverInventoryMode = false;
  if(pl.weaponState()!=WeaponState::NoWeapon) {
    pl.stopAnim("");
    pl.closeWeapon(false);
    }
  state  = State::Equip;
  player = &pl;
  trader = nullptr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage  (pl.inventory()));
  pageOth.reset();
  adjustScroll();
  update();

  Gothic::inst().emitGlobalSound("INV_OPEN");
  //Gothic::inst().emitGlobalSound("INV_CHANGE");
  }

void InventoryMenu::trade(Npc &pl, Npc &tr) {
  if(Mmo::isServerBoundClientModeEnabled())
    return;
  if(pl.isDown())
    return;
  state  = State::Trade;
  player = &pl;
  trader = &tr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage  (pl.inventory()));
  pageOth.reset(new TradePage(tr.inventory()));
  adjustScroll();
  update();
  Gothic::inst().emitGlobalSound("TRADE_OPEN");
  }

bool InventoryMenu::ransack(Npc &pl, Npc &tr) {
  if(Mmo::isServerBoundClientModeEnabled())
    return false;
  if(pl.isDown())
    return false;
  auto it = tr.inventory().iterator(Inventory::T_Ransack);
  if(!it.isValid())
    return false;
  state  = State::Ransack;
  player = &pl;
  trader = &tr;
  chest  = nullptr;
  page   = 0;
  pagePl .reset(new InvPage    (pl.inventory()));
  pageOth.reset(new RansackPage(tr.inventory()));
  adjustScroll();
  update();
  Gothic::inst().emitGlobalSound("INV_OPEN");
  return true;
  }

bool InventoryMenu::openServerCorpse(Npc& pl, Npc& corpse) {
  if(!Mmo::isServerBoundClientModeEnabled() || pl.isDown() ||
     pl.bodyStateMasked()==BS_UNCONSCIOUS)
    return false;
  auto* session = Gothic::inst().gameSession();
  if(session==nullptr)
    return false;
  const auto target = session->mmoServerEntityTarget(corpse);
  const auto& inventory = session->mmoServerInventoryPresentation().inventory();
  if(!target.has_value() || !inventory.ready())
    return false;
  const auto& loot = session->mmoServerCorpseLootPresentation();
  if(!loot.canOpen(target->handle))
    return false;

  serverCorpseHandle = target->handle;
  serverCorpseTitle = std::string(corpse.displayName());
  if(!submitServerCorpseOpen()) {
    serverCorpseHandle = {};
    serverCorpseTitle.clear();
    return false;
  }

  state = State::ServerCorpse;
  player = &pl;
  trader = nullptr;
  chest = nullptr;
  page = 0;
  serverInventoryMode = false;
  serverCorpseMode = true;
  observedServerCorpse = loot.fingerprint();
  serverPreviewItems.clear();
  pageLocal[1] = {};
  pagePl.reset();
  pageOth.reset();
  adjustScroll();
  update();
  Gothic::inst().emitGlobalSound("INV_OPEN");
  return true;
}

void InventoryMenu::open(Npc &pl, Interactive &ch) {
  if(Mmo::isServerBoundClientModeEnabled())
    return;
  if(pl.isDown())
    return;
  const bool needToPicklock = ch.needToLockpick(pl);
  if(!pl.setInteraction(&ch))
    return;

  if(needToPicklock && !ch.isCracked()) {
    state = State::LockPicking;
    } else {
    state = State::Chest;
    }

  player = &pl;
  trader = nullptr;
  chest  = &ch;
  page   = 0;
  pagePl .reset(new InvPage(pl.inventory()));
  pageOth.reset(new InvPage(ch.inventory()));
  adjustScroll();
  update();
  }

InventoryMenu::State InventoryMenu::isOpen() const {
  return state;
  }

bool InventoryMenu::isActive() const {
  return state!=State::Closed;
  }

void InventoryMenu::onWorldChanged() {
  close();
  player = nullptr;
  trader = nullptr;
  chest  = nullptr;
  }

void InventoryMenu::tick(uint64_t /*dt*/) {
  if(serverCorpseMode) {
    syncServerCorpseView();
    if(state==State::Closed)
      return;
  }
  if(serverInventoryMode) {
    syncServerInventoryView();
    if(state==State::Closed)
      return;
  }
  if(player!=nullptr && (player->isDown() || player->bodyStateMasked()==BS_UNCONSCIOUS)) {
    close();
    return;
    }

  if(state==State::LockPicking) {
    if(chest->isCracked()) {
      state = State::Chest;
      return;
      }
    }

  if(state==State::Ransack) {
    if(trader==nullptr){
      close();
      return;
      }

    if(!trader->isDown()) {
      close();
      return;
      }

    auto it = trader->inventory().iterator(Inventory::T_Ransack);
    if(!it.isValid())
      close();
    }

  if(state==State::Closed) {
    if(player!=nullptr){
      if(!player->setInteraction(nullptr))
         return;
      player = nullptr;
      chest  = nullptr;
      }

    page = 0;
    serverInventoryMode = false;
    serverCorpseMode = false;
    serverInventoryResyncAttempted = false;
    serverInventoryResyncRequested = false;
    observedServerInventory = {};
    observedServerCorpse = {};
    serverCorpseHandle = {};
    serverCorpseTitle.clear();
    mergeSource.reset();
    renderer.reset();
    pagePl .reset();
    pageOth.reset();
    update();
    }
  }

void InventoryMenu::processMove(KeyEvent& e) {
  auto key = keycodec.tr(e);
  if(key==KeyCodec::Forward)
    moveUp();
  else if(key==KeyCodec::Back)
    moveDown();
  else if(key==KeyCodec::Left || key==KeyCodec::RotateL)
    moveLeft(true);
  else if(key==KeyCodec::Right || key==KeyCodec::RotateR)
    moveRight(true);
  }

void InventoryMenu::moveLeft(bool usePage) {
  auto& sel = activePageSel();

  if(usePage && sel.sel%columsCount==0 && page>0)
    page--;
  else if(sel.sel>0)
    sel.sel--;
  }

void InventoryMenu::moveRight(bool usePage) {
  auto&        sel    = activePageSel();
  const size_t size   = activePageSize();
  const size_t pCount = pagesCount();

  if(usePage && ((sel.sel+1u)%columsCount==0 || sel.sel+1u==size || size==0) && page+1u<pCount)
    page++;
  else if(sel.sel+1<size)
    sel.sel++;
  }

void InventoryMenu::moveUp() {
  auto& sel = activePageSel();

  if(sel.sel>=columsCount)
    sel.sel -= columsCount;
  else
    moveLeft(false);
  }

void InventoryMenu::moveDown() {
  auto& sel = activePageSel();

  if(sel.sel+columsCount<activePageSize())
    sel.sel += columsCount;
  else
    moveRight(false);
  }

void InventoryMenu::keyDownEvent(KeyEvent &e) {
  if(state==State::Closed || state==State::LockPicking){
    e.ignore();
    return;
    }

  if(serverInventoryMode && e.key==KeyEvent::K_S) {
    onServerSplitStack();
    adjustScroll();
    update();
    return;
  }
  if(serverInventoryMode && e.key==KeyEvent::K_M) {
    onServerMergeStack();
    adjustScroll();
    update();
    return;
  }

  processMove(e);

  if(serverCorpseMode && keycodec.tr(e)==KeyCodec::ActionRight) {
    submitServerCorpseTakeAll();
    }
  else if(keycodec.tr(e)==KeyCodec::Jump) {
    lootMode = LootMode::Stack;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if (keycodec.tr(e)==KeyCodec::ActionGeneric || e.key==KeyEvent::K_Return) {
    onItemAction(Item::NSLOT);
    }
  else if((KeyEvent::K_3<=e.key && e.key<=KeyEvent::K_9) || e.key==KeyEvent::K_0) {
    uint8_t slot = 10;
    if((KeyEvent::K_3<=e.key && e.key<=KeyEvent::K_9))
      slot = uint8_t(e.key-KeyEvent::K_0);
    onItemAction(slot);
    }
  else if(e.key==KeyEvent::K_ESCAPE || keycodec.tr(e)==KeyCodec::Inventory){
    close();
    }
  else if(e.key==KeyEvent::K_Space) {
    lootMode = LootMode::Normal;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if(e.key==KeyEvent::K_Z) {
    lootMode = LootMode::Ten;
    takeTimer.start(200);
    onTakeStuff();
    }
  else if(e.key==KeyEvent::K_X) {
    lootMode = LootMode::Hundred;
    takeTimer.start(200);
    onTakeStuff();
    }

  adjustScroll();
  update();
  }

void InventoryMenu::keyRepeatEvent(KeyEvent& e) {
  if(state==State::LockPicking || state==State::Closed)
    return;
  processMove(e);
  adjustScroll();
  update();
  }

void InventoryMenu::keyUpEvent(KeyEvent&) {
  takeTimer.stop();
  lootMode = LootMode::Normal;
  }

void InventoryMenu::mouseDownEvent(MouseEvent &e) {
  if(player==nullptr || state==State::Closed) {
    e.ignore();
    return;
    }

  if(state==State::LockPicking)
    return;

  if(e.button==MouseEvent::ButtonLeft)
    onItemAction(Item::NSLOT);
  else if(e.button==MouseEvent::ButtonRight) {
    if(serverCorpseMode)
      submitServerCorpseTakeAll();
    else
      close();
  }

  adjustScroll();
}

void InventoryMenu::mouseUpEvent(MouseEvent&) {
  takeTimer.stop();
  takeCount=0;
  }

void InventoryMenu::mouseWheelEvent(MouseEvent &e) {
  if(state==State::Closed) {
    e.ignore();
    return;
    }

  if(state==State::LockPicking)
    return;

  scrollDelta += e.delta;
  if(scrollDelta>0) {
    for(int i=0;i<scrollDelta/120;++i)
      moveUp();
    scrollDelta %= 120;
    } else {
    for(int i=0;i<-scrollDelta/120;++i)
      moveDown();
    scrollDelta %= 120;
    }
  adjustScroll();
  }

const World *InventoryMenu::world() const {
  return Gothic::inst().world();
  }

size_t InventoryMenu::rowsCount() const {
  int iy=30+34+70;
  return size_t((h()-iy-infoHeight()-20)/slotSize().h);
  }

void InventoryMenu::paintEvent(PaintEvent &e) {
  if(player==nullptr || state==State::Closed)
    return;
  renderer.reset();

  Painter p(e);
  drawAll(p,*player,DrawPass::Back);
  }

void InventoryMenu::paintNumOverlay(PaintEvent& e) {
  if(player==nullptr || state==State::Closed)
    return;

  Painter p(e);
  drawAll(p,*player,DrawPass::Front);
  }

Size InventoryMenu::slotSize() const {
  const float scale = Gothic::interfaceScale(this);
  const float cell  = float(Gothic::options().inventoryCellSize);
  return Size(int(cell*scale),int(cell*scale));
  }

int InventoryMenu::infoHeight() const {
  const float scale = Gothic::interfaceScale(this);
  const int rows = (serverInventoryMode || serverCorpseMode)
                       ? 12
                       : Item::MAX_UI_ROWS+2;
  return rows*int(float(Resources::font(scale).pixelSize()))+
         int(scale*10)/*padding bottom*/;
  }

size_t InventoryMenu::pagesCount() const {
  if(serverInventoryMode || serverCorpseMode)
    return 1;
  if(state==State::Chest || state==State::Trade)
    return 2;
  return 1;
  }

const InventoryMenu::Page &InventoryMenu::activePage() {
  if(pageOth!=nullptr)
    return *(page==0 ? pageOth : pagePl);
  if(pagePl)
    return *pagePl;

  static Page n;
  return n;
  }

InventoryMenu::PageLocal &InventoryMenu::activePageSel() {
  if(pageOth!=nullptr)
    return (page==0 ? pageLocal[0] : pageLocal[1]);
  return pageLocal[1];
  }
  
size_t InventoryMenu::activePageSize() const {
  if(serverCorpseMode) {
    const auto* corpse = serverCorpseState();
    return corpse != nullptr ? corpse->stacks().size() : 0U;
  }
  if(serverInventoryMode) {
    const auto* inventory = serverInventoryState();
    return inventory != nullptr
               ? Mmo::ClientPresentation::ServerInventoryPageModel(*inventory)
                     .stacks()
                     .size()
               : 0U;
  }
  if(pageOth!=nullptr)
    return (page==0 ? pageOth : pagePl)->size();
  return pagePl != nullptr ? pagePl->size() : 0U;
  }

const Mmo::ClientPresentation::ServerInventoryPresentationState*
InventoryMenu::serverInventoryState() const {
  const auto* session = Gothic::inst().gameSession();
  return session != nullptr ? &session->mmoServerInventoryPresentation() : nullptr;
  }

const Mmo::ClientPresentation::ServerCorpseLootPresentationState*
InventoryMenu::serverCorpseState() const {
  const auto* session = Gothic::inst().gameSession();
  return session != nullptr ? &session->mmoServerCorpseLootPresentation()
                            : nullptr;
}

const Mmo::ClientPresentation::ServerInventoryStack*
InventoryMenu::selectedServerStack() const {
  const auto* state = serverInventoryState();
  if(state == nullptr)
    return nullptr;
  return Mmo::ClientPresentation::ServerInventoryPageModel(*state).stackAt(
      pageLocal[1].sel);
  }

const Mmo::ClientPresentation::ServerInventoryStack*
InventoryMenu::selectedServerCorpseStack() const {
  const auto* state = serverCorpseState();
  return state != nullptr ? state->stackAt(pageLocal[1].sel) : nullptr;
}

bool InventoryMenu::serverInventoryActionsEnabled() const {
  const auto* state = serverInventoryState();
  return state != nullptr &&
         Mmo::ClientPresentation::ServerInventoryPageModel(*state)
             .actionsEnabled();
  }

bool InventoryMenu::serverCorpseActionsEnabled() const {
  const auto* state = serverCorpseState();
  return state != nullptr && state->actionsEnabled();
}

void InventoryMenu::onItemAction(uint8_t slotHint) {
  if(serverCorpseMode) {
    const auto* state = serverCorpseState();
    if(state==nullptr)
      return;
    if(!state->active()) {
      static_cast<void>(submitServerCorpseOpen());
      return;
    }
    if(serverCorpseActionsEnabled())
      submitServerCorpseTake(1U);
    return;
  }
  if(serverInventoryMode) {
    if(!serverInventoryActionsEnabled())
      return;
    if(slotHint==Item::NSLOT)
      submitServerUseOrUnequip();
    else if(const auto slot = equipmentSlotFromHint(slotHint))
      submitServerEquip(*slot);
    return;
  }

  auto& page = activePage();
  auto& sel  = activePageSel();

  auto it = page.get(sel.sel);
  if(!it.isValid())
    return;

  if(state==State::Equip) {
    const size_t clsId = it->clsId();
    if(it.isEquipped() && slotHint==Item::NSLOT) {
      player->unequipItem(clsId);
      } else {
      player->useItem(clsId,slotHint,false);
      auto it2 = page.get(sel.sel);
      if((!it2.isValid() || it2->clsId()!=clsId) && sel.sel>0)
        --sel.sel;
      }
    }
  else if(state==State::Chest || state==State::Trade || state==State::Ransack) {
    lootMode = LootMode::Normal;
    takeTimer.start(200);
    onTakeStuff();
    }
  }

void InventoryMenu::onTakeStuff() { 
  if(serverCorpseMode) {
    if(!serverCorpseActionsEnabled())
      return;
    const auto* stack = selectedServerCorpseStack();
    if(stack==nullptr)
      return;
    size_t amount = 1U;
    if(lootMode==LootMode::Stack)
      amount = stack->quantity;
    else if(lootMode==LootMode::Ten)
      amount = 10U;
    else if(lootMode==LootMode::Hundred)
      amount = 100U;
    submitServerCorpseTake(std::min<std::size_t>(amount,stack->quantity));
    return;
  }
  if(serverInventoryMode) {
    if(!serverInventoryActionsEnabled())
      return;
    const auto* stack = selectedServerStack();
    if(stack==nullptr)
      return;
    size_t itemCount = 0U;
    if(lootMode==LootMode::Normal) {
      ++takeCount;
      itemCount = size_t(std::pow(10,takeCount / 10));
      if(stack->quantity <= itemCount) {
        itemCount = stack->quantity;
        takeCount = 0;
      }
    } else if(lootMode==LootMode::Stack) {
      itemCount = stack->quantity;
    } else if(lootMode==LootMode::Ten) {
      itemCount = 10U;
    } else if(lootMode==LootMode::Hundred) {
      itemCount = 100U;
    }
    submitServerDrop(std::min<std::size_t>(itemCount, stack->quantity));
    return;
  }

  size_t itemCount = 0;
  auto& page = activePage();
  auto& sel  = activePageSel();
  if(sel.sel >= page.size())
    return;
  auto it = page.get(sel.sel);
  if(lootMode==LootMode::Normal) {
    ++takeCount;
    itemCount = uint32_t(std::pow(10,takeCount / 10));
    if(it.count() <= itemCount) {
      itemCount = uint32_t(it.count());
      takeCount = 0;
      }
    }
  else if(lootMode==LootMode::Stack) {
    itemCount = it.count();
    }
  else if(lootMode==LootMode::Ten) {
    itemCount = 10;
    }
  else if(lootMode==LootMode::Hundred) {
    itemCount = 100;
    }

  if(it.count() < itemCount) {
    itemCount = it.count();
    }

  if(state==State::Chest) {
    if(page.is(&player->inventory())) {
      player->moveItem(it->clsId(),*chest,itemCount);
      } else {
      player->addItem (it->clsId(),*chest,itemCount);
      }
    }
  else if(state==State::Trade) {
    if(page.is(&player->inventory())) {
      player->sellItem(it->clsId(),*trader,itemCount);
      } else {
      player->buyItem (it->clsId(),*trader,itemCount);
      }
    }
  else if(state==State::Ransack) {
    if(page.is(&trader->inventory())) {
      player->addItem(it->clsId(),*trader,itemCount);
      }
    }
  else if(state==State::Equip) {
    player->dropItem(it->clsId(),itemCount);
    }
  adjustScroll();
  }

void InventoryMenu::trackServerCorpseCommand(
    const Mmo::ClientMmoSubmitResult& result,
    Mmo::ClientPresentation::ServerCorpseLootPendingCommand command) {
  auto* session = Gothic::inst().gameSession();
  const auto* state = serverCorpseState();
  if(session==nullptr || state==nullptr)
    return;
  if(!result.submitted()) {
    session->rejectMmoServerCorpseLootCommandSubmission(result.status);
    observedServerCorpse = state->fingerprint();
    update();
    return;
  }
  command.command = result.command;
  if(session->trackMmoServerCorpseLootCommand(std::move(command))) {
    observedServerCorpse = state->fingerprint();
    update();
  }
}

bool InventoryMenu::submitServerCorpseOpen() {
  auto* session = Gothic::inst().gameSession();
  const auto* state = serverCorpseState();
  const auto* inventory = serverInventoryState();
  if(session==nullptr || state==nullptr || inventory==nullptr ||
     !inventory->inventory().ready() || !serverCorpseHandle.valid()) {
    return false;
  }
  const auto request = state->openRequest(
      serverCorpseHandle, inventory->inventory().revision());
  if(!request.has_value())
    return false;
  const auto result = Mmo::submitClientOpenCorpseLoot(*request);
  trackServerCorpseCommand(result, {
      .command = {},
      .kind = Mmo::ClientPresentation::ServerCorpseLootPendingKind::Open,
      .corpse = request->corpse,
      .stack = {},
      .amount = 0U,
      .expectedCorpseRevision = request->expectedCorpseRevision,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .phase = Mmo::ClientPresentation::ServerCorpseLootPendingPhase::Submitted,
  });
  return result.submitted();
}

void InventoryMenu::submitServerCorpseTake(const size_t amount) {
  const auto* state = serverCorpseState();
  const auto* stack = selectedServerCorpseStack();
  if(state==nullptr || stack==nullptr || amount==0U ||
     amount>stack->quantity ||
     amount>std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  const auto request = state->takeRequest(
      stack->handle, static_cast<std::uint32_t>(amount));
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientTakeCorpseLootStack(*request);
  trackServerCorpseCommand(result, {
      .command = {},
      .kind = Mmo::ClientPresentation::ServerCorpseLootPendingKind::TakeStack,
      .corpse = request->corpse,
      .stack = request->stack,
      .amount = request->amount,
      .expectedCorpseRevision = request->expectedCorpseRevision,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .phase = Mmo::ClientPresentation::ServerCorpseLootPendingPhase::Submitted,
  });
}

void InventoryMenu::submitServerCorpseTakeAll() {
  const auto* state = serverCorpseState();
  if(state==nullptr)
    return;
  const auto request = state->takeAllRequest();
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientTakeAllCorpseLoot(*request);
  trackServerCorpseCommand(result, {
      .command = {},
      .kind = Mmo::ClientPresentation::ServerCorpseLootPendingKind::TakeAll,
      .corpse = request->corpse,
      .stack = {},
      .amount = 0U,
      .expectedCorpseRevision = request->expectedCorpseRevision,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .phase = Mmo::ClientPresentation::ServerCorpseLootPendingPhase::Submitted,
  });
}

void InventoryMenu::submitServerCorpseClose() {
  const auto* state = serverCorpseState();
  if(state==nullptr)
    return;
  const auto request = state->closeRequest();
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientCloseCorpseLoot(*request);
  trackServerCorpseCommand(result, {
      .command = {},
      .kind = Mmo::ClientPresentation::ServerCorpseLootPendingKind::Close,
      .corpse = request->corpse,
      .stack = {},
      .amount = 0U,
      .expectedCorpseRevision = request->expectedCorpseRevision,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .phase = Mmo::ClientPresentation::ServerCorpseLootPendingPhase::Submitted,
  });
}

void InventoryMenu::trackServerCommand(
    const Mmo::ClientMmoSubmitResult& result,
    Mmo::ClientPresentation::ServerInventoryPendingCommand command) {
  auto* session = Gothic::inst().gameSession();
  const auto* state = serverInventoryState();
  if(session==nullptr || state==nullptr)
    return;
  if(!result.submitted()) {
    session->rejectMmoServerInventoryCommandSubmission(result.status);
    observedServerInventory =
        Mmo::ClientPresentation::ServerInventoryPageModel(*state)
            .fingerprint();
    update();
    return;
  }

  command.command = result.command;
  if(session->trackMmoServerInventoryCommand(std::move(command))) {
    observedServerInventory =
        Mmo::ClientPresentation::ServerInventoryPageModel(*state)
            .fingerprint();
    update();
  }
}

void InventoryMenu::submitServerUseOrUnequip() {
  const auto* state = serverInventoryState();
  const auto* stack = selectedServerStack();
  if(state==nullptr || stack==nullptr)
    return;
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  if(const auto equipped = pageModel.equipment().slotOf(stack->handle)) {
    const auto request = pageModel.unequipRequest(stack->handle);
    if(!request.has_value())
      return;
    const auto result = Mmo::submitClientUnequipItem(*request);
    trackServerCommand(result, {
        .command = {},
        .primary = stack->handle,
        .secondary = {},
        .slot = equipped,
        .expectedInventoryRevision = request->expectedInventoryRevision,
        .expectedEquipmentRevision = request->expectedEquipmentRevision,
        .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
    });
    return;
  }

  const auto request = pageModel.useRequest(stack->handle);
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientUseItem(*request);
  trackServerCommand(result, {
      .command = {},
      .primary = stack->handle,
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .expectedEquipmentRevision = 0U,
      .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
  });
}

void InventoryMenu::submitServerEquip(const Mmo::ClientEquipmentSlot slot) {
  const auto* state = serverInventoryState();
  const auto* stack = selectedServerStack();
  if(state==nullptr || stack==nullptr)
    return;
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  const auto request = pageModel.equipRequest(stack->handle, slot);
  if(!request.has_value())
    return;

  Mmo::ClientItemStackHandle replaced{};
  if(const auto* current = pageModel.equipment().at(slot);
     current != nullptr && current->item != stack->handle) {
    replaced = current->item;
  }
  const auto result = Mmo::submitClientEquipItem(*request);
  trackServerCommand(result, {
      .command = {},
      .primary = stack->handle,
      .secondary = replaced,
      .slot = slot,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .expectedEquipmentRevision = request->expectedEquipmentRevision,
      .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
  });
}

void InventoryMenu::submitServerDrop(const size_t amount) {
  const auto* state = serverInventoryState();
  const auto* stack = selectedServerStack();
  if(state==nullptr || stack==nullptr || player==nullptr || amount==0U ||
     amount>stack->quantity)
    return;
  const auto position = player->position();
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  const auto request = pageModel.dropRequest(
      stack->handle, static_cast<std::uint32_t>(amount),
      {.x = position.x, .y = position.y, .z = position.z});
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientDropItem(*request);
  trackServerCommand(result, {
      .command = {},
      .primary = stack->handle,
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .expectedEquipmentRevision = 0U,
      .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
  });
}

void InventoryMenu::onServerSplitStack() {
  const auto* state = serverInventoryState();
  const auto* stack = selectedServerStack();
  if(state==nullptr || stack==nullptr || stack->quantity<2U)
    return;
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  const auto request = pageModel.splitRequest(
      stack->handle, stack->quantity / 2U);
  if(!request.has_value())
    return;
  const auto result = Mmo::submitClientSplitStack(*request);
  trackServerCommand(result, {
      .command = {},
      .primary = stack->handle,
      .secondary = {},
      .slot = std::nullopt,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .expectedEquipmentRevision = 0U,
      .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
  });
}

void InventoryMenu::onServerMergeStack() {
  const auto* state = serverInventoryState();
  const auto* destination = selectedServerStack();
  if(state==nullptr || destination==nullptr)
    return;
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  if(!pageModel.actionsEnabled() ||
     pageModel.pendingPhase(destination->handle).has_value()) {
    return;
  }
  if(!mergeSource.has_value()) {
    mergeSource = destination->handle;
    return;
  }
  if(*mergeSource==destination->handle) {
    mergeSource.reset();
    return;
  }
  const auto* source = state->inventory().find(*mergeSource);
  if(source==nullptr) {
    mergeSource.reset();
    return;
  }
  const auto request = pageModel.mergeRequest(
      source->handle, destination->handle, source->quantity);
  if(!request.has_value()) {
    mergeSource.reset();
    return;
  }
  const auto result = Mmo::submitClientMergeStack(*request);
  trackServerCommand(result, {
      .command = {},
      .primary = source->handle,
      .secondary = destination->handle,
      .slot = std::nullopt,
      .expectedInventoryRevision = request->expectedInventoryRevision,
      .expectedEquipmentRevision = 0U,
      .phase = Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted,
  });
  if(result.submitted())
    mergeSource.reset();
}

void InventoryMenu::syncServerInventoryView() {
  const auto* inventory = serverInventoryState();
  if(inventory==nullptr || !inventory->ready()) {
    close();
    return;
  }

  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*inventory);
  const auto fingerprint = pageModel.fingerprint();
  if(fingerprint==observedServerInventory)
    return;

  if(fingerprint.replacementRevision !=
     observedServerInventory.replacementRevision) {
    serverPreviewItems.clear();
    mergeSource.reset();
    pageLocal[1] = {};
  } else if(fingerprint.inventoryRevision !=
            observedServerInventory.inventoryRevision) {
    serverPreviewItems.clear();
  }
  observedServerInventory = fingerprint;
  if(pageModel.feedbackStatus() ==
         Mmo::ClientPresentation::ServerInventoryFeedbackStatus::ResyncRequired) {
    if(!serverInventoryResyncAttempted) {
      serverInventoryResyncAttempted = true;
      serverInventoryResyncRequested =
          Mmo::requestClientMmoInventoryResync();
    }
  } else {
    serverInventoryResyncAttempted = false;
    serverInventoryResyncRequested = false;
  }
  if(mergeSource.has_value() &&
     inventory->inventory().find(*mergeSource)==nullptr) {
    mergeSource.reset();
  }
  adjustScroll();
  update();
}

void InventoryMenu::showServerCorpseFeedback(
    const Mmo::ClientPresentation::ServerCorpseLootFeedback feedback) {
  const auto* state = serverCorpseState();
  if(state==nullptr || state->feedback()!=feedback ||
     state->feedbackMessage().empty()) {
    return;
  }
  Gothic::inst().onPrintScreen(
      state->feedbackMessage(),2,4,2,
      Resources::font(Gothic::interfaceScale(this)));
}

void InventoryMenu::syncServerCorpseView() {
  const auto* state = serverCorpseState();
  if(state==nullptr) {
    close();
    return;
  }
  const auto fingerprint = state->fingerprint();
  if(fingerprint==observedServerCorpse)
    return;

  if(fingerprint.replacementRevision !=
         observedServerCorpse.replacementRevision ||
     fingerprint.corpseRevision != observedServerCorpse.corpseRevision) {
    serverPreviewItems.clear();
  }
  observedServerCorpse = fingerprint;

  if(state->activeCorpse().has_value() &&
     *state->activeCorpse()!=serverCorpseHandle) {
    close();
    return;
  }
  switch(state->feedback()) {
    case Mmo::ClientPresentation::ServerCorpseLootFeedback::OutOfRange:
    case Mmo::ClientPresentation::ServerCorpseLootFeedback::Empty:
    case Mmo::ClientPresentation::ServerCorpseLootFeedback::Decayed:
    case Mmo::ClientPresentation::ServerCorpseLootFeedback::Disconnected:
    case Mmo::ClientPresentation::ServerCorpseLootFeedback::ResyncRequired:
      showServerCorpseFeedback(state->feedback());
      close();
      return;
    default:
      break;
  }
  if(!state->active() && !state->pending() &&
     state->feedback()==
         Mmo::ClientPresentation::ServerCorpseLootFeedback::Ready) {
    close();
    return;
  }
  adjustScroll();
  update();
}

void InventoryMenu::adjustScroll() {
  auto& sel  = activePageSel();
  const auto size = activePageSize();
  sel.sel = std::min(sel.sel, std::max<size_t>(size,1)-1);
  while(sel.sel<sel.scroll*columsCount) {
    if(sel.scroll<=1){
      sel.scroll=0;
      return;
      }
    sel.scroll-=1;
    }

  const size_t hcount=rowsCount();
  while(sel.sel>=(sel.scroll+hcount)*columsCount) {
    sel.scroll+=1;
    }
  }

void InventoryMenu::drawAll(Painter &p, Npc &player, DrawPass pass) {
  const int padd = 43;

  int iy=30+34+70;

  if(state==State::LockPicking)
    return;

  const int wcount = int(columsCount);
  const int hcount = int(rowsCount());

  if(serverCorpseMode) {
    if(pass==DrawPass::Back)
      drawHeader(p,serverCorpseTitle.empty() ? "Corpse Loot" : serverCorpseTitle,
                 padd,70);
    drawServerItems(p,pass,pageLocal[1],
                    w()-padd-wcount*slotSize().w,iy,wcount,hcount);
    if(pass==DrawPass::Back)
      drawServerCorpseInfo(p);
    return;
  }

  if(serverInventoryMode) {
    if(pass==DrawPass::Back)
      drawHeader(p,"Server Inventory",padd,70);
    drawServerItems(p,pass,pageLocal[1],
                    w()-padd-wcount*slotSize().w,iy,wcount,hcount);
    if(pass==DrawPass::Back)
      drawServerInfo(p);
    return;
  }

  if(chest!=nullptr){
    if(pass==DrawPass::Back)
      drawHeader(p,chest->displayName(),padd,70);
    drawItems(p,pass,*pageOth,pageLocal[0],padd,iy,wcount,hcount);
    }

  if(trader!=nullptr) {
    if(pass==DrawPass::Back)
      drawHeader(p,trader->displayName(),padd,70);
    drawItems(p,pass,*pageOth,pageLocal[0],padd,iy,wcount,hcount);
    }

  if(state!=State::Ransack) {
    if(pass==DrawPass::Back)
      drawGold (p,player,w()-padd-2*slotSize().w,70);
    drawItems(p,pass,*pagePl,pageLocal[1],w()-padd-wcount*slotSize().w,iy,wcount,hcount);
    }

  if(pass==DrawPass::Back)
    drawInfo(p);
  }

void InventoryMenu::drawItems(Painter &p, DrawPass pass,
                              const Page &inv, const PageLocal& sel, int x0, int y, int wcount, int hcount) {
  if(state==State::LockPicking)
    return;

  if(tex!=nullptr && pass==DrawPass::Back) {
    p.setBrush(*tex);
    p.drawRect(x0,y,slotSize().w*wcount,slotSize().h*hcount,
               0,0,tex->w(),tex->h());
    }

  auto   it = inv.iterator();
  size_t id = 0;
  for(size_t i=0; it.isValid() && i<sel.scroll*size_t(wcount); ++i) {
    ++it;
    ++id;
    }
  for(int i=0;i<hcount;++i) {
    for(int r=0;r<wcount;++r) {
      const int x = x0 + r*slotSize().w;
      if(pass==DrawPass::Back) {
        p.setBrush(*slot);
        p.drawRect(x,y,slotSize().w,slotSize().h,
                   0,0,slot->w(),slot->h());
        }
      if(it.isValid()) {
        drawSlot(p,pass, it,inv,sel, x,y, id);
        ++it;
        ++id;
        }
      }
    y += slotSize().h;
    }
  }

void InventoryMenu::drawSlot(Painter &p, DrawPass pass, const Inventory::Iterator &it,
                             const Page& page, const PageLocal &sel,
                             int x, int y, size_t id) {
  if(!slot)
    return;

  auto& active = activePage();
  const float scale = Gothic::interfaceScale(this);

  if(pass==DrawPass::Back) {
    if((!it.isValid() && id==0) || (id==sel.sel && &page==&active)){
      p.setBrush(*selT);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selT->w(),selT->h());
      }

    if(it.isEquipped() && selU!=nullptr) {
      p.setBrush(*selU);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selU->w(),selU->h());
      }

    const int dsz = (id==sel.sel ? 5 : 0);
    renderer.drawItem(x-dsz, y-dsz, slotSize().w+2*dsz, slotSize().h+2*dsz, *it);
    } else {
    auto fnt = Resources::font(scale);

    if(it.count()>1) {
      string_frm vint(int(it.count()));
      auto sz = fnt.textSize(vint);
      fnt.drawText(p,x+slotSize().w-sz.w-10,
                   y+slotSize().h-10,
                   vint);
      }

    if(it.slot()!=Item::NSLOT) {
      fnt = Resources::font(Resources::FontType::Red, scale);

      string_frm vint(int(it.slot()));
      auto sz = fnt.textSize(vint);
      fnt.drawText(p,x+10,
                   y+slotSize().h/2+sz.h/2,
                   vint);
      }
    }
  }

std::string InventoryMenu::serverItemDisplayName(
    const Mmo::ClientPresentation::ServerInventoryStack& stack) const {
  const auto* session = Gothic::inst().gameSession();
  if(session != nullptr) {
    const auto name = session->mmoItemDisplayName(
        stack.archetypeId, stack.presentationId);
    if(name.has_value())
      return std::string(*name);
  }
  return std::string("Item ") + std::to_string(stack.archetypeId);
}

Item* InventoryMenu::serverPreviewItem(
    const Mmo::ClientPresentation::ServerInventoryStack& stack) {
  const auto cached = std::find_if(
      serverPreviewItems.begin(), serverPreviewItems.end(),
      [&stack](const auto& value) noexcept {
        return value.handle == stack.handle &&
               value.presentationId == stack.presentationId;
      });
  if(cached != serverPreviewItems.end())
    return cached->item.get();

  auto* w = Gothic::inst().world();
  auto* session = Gothic::inst().gameSession();
  if(w == nullptr || session == nullptr)
    return nullptr;
  const auto instance = session->mmoItemInstanceName(
      stack.archetypeId, stack.presentationId);
  if(!instance.has_value())
    return nullptr;
  const auto symbol = w->script().findSymbolIndex(*instance);
  if(symbol == size_t(-1))
    return nullptr;
  try {
    ServerPreviewItem preview{
        .handle = stack.handle,
        .presentationId = stack.presentationId,
        .item = std::make_unique<Item>(*w, symbol, Item::T_Inventory),
    };
    preview.item->setCount(stack.quantity);
    serverPreviewItems.push_back(std::move(preview));
    return serverPreviewItems.back().item.get();
  } catch(...) {
    return nullptr;
  }
}

void InventoryMenu::drawServerItems(Painter &p, DrawPass pass,
                                    const PageLocal& sel, int x0, int y,
                                    int wcount, int hcount) {
  std::span<const Mmo::ClientPresentation::ServerInventoryStack> stacks;
  if(serverCorpseMode) {
    const auto* state = serverCorpseState();
    if(state==nullptr)
      return;
    stacks = state->stacks();
  } else {
    const auto* state = serverInventoryState();
    if(state==nullptr)
      return;
    stacks = state->inventory().stacks();
  }
  if(tex!=nullptr && pass==DrawPass::Back) {
    p.setBrush(*tex);
    p.drawRect(x0,y,slotSize().w*wcount,slotSize().h*hcount,
               0,0,tex->w(),tex->h());
  }

  size_t id = sel.scroll*size_t(wcount);
  for(int row=0; row<hcount; ++row) {
    for(int column=0; column<wcount; ++column) {
      const int x = x0 + column*slotSize().w;
      if(pass==DrawPass::Back) {
        p.setBrush(*slot);
        p.drawRect(x,y,slotSize().w,slotSize().h,
                   0,0,slot->w(),slot->h());
      }
      if(id<stacks.size())
        drawServerSlot(p,pass,stacks[id],sel,x,y,id);
      ++id;
    }
    y += slotSize().h;
  }
}

void InventoryMenu::drawServerSlot(
    Painter &p, DrawPass pass,
    const Mmo::ClientPresentation::ServerInventoryStack& stack,
    const PageLocal& sel, int x, int y, size_t id) {
  const auto* inventoryState = serverInventoryState();
  const auto* corpseState = serverCorpseState();
  if(slot==nullptr ||
     (!serverCorpseMode && inventoryState==nullptr) ||
     (serverCorpseMode && corpseState==nullptr))
    return;
  const float scale = Gothic::interfaceScale(this);
  const auto equipped = !serverCorpseMode && inventoryState!=nullptr
                            ? inventoryState->equipment().slotOf(stack.handle)
                            : std::optional<Mmo::ClientEquipmentSlot>{};

  if(pass==DrawPass::Back) {
    if(id==sel.sel && selT!=nullptr) {
      p.setBrush(*selT);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selT->w(),selT->h());
    }
    if(equipped.has_value() && selU!=nullptr) {
      p.setBrush(*selU);
      p.drawRect(x,y,slotSize().w,slotSize().h,
                 0,0,selU->w(),selU->h());
    }
    if(auto* preview = serverPreviewItem(stack); preview != nullptr) {
      const int dsz = id==sel.sel ? 5 : 0;
      renderer.drawItem(
          x-dsz, y-dsz, slotSize().w+2*dsz, slotSize().h+2*dsz, *preview);
    } else {
      auto& fnt = Resources::font(scale);
      const auto label = serverItemDisplayName(stack);
      const auto size = fnt.textSize(label);
      fnt.drawText(p,x+(slotSize().w-size.w)/2,
                   y+slotSize().h/2+size.h/2,label);
    }
    return;
  }

  auto& fnt = Resources::font(scale);
  if(stack.quantity>1U) {
    const auto quantity = std::to_string(stack.quantity);
    const auto size = fnt.textSize(quantity);
    fnt.drawText(p,x+slotSize().w-size.w-10,
                 y+slotSize().h-10,quantity);
  }
  if(equipped.has_value()) {
    auto& equippedFont = Resources::font(Resources::FontType::Red,scale);
    const auto label = std::string(equipmentSlotName(*equipped));
    equippedFont.drawText(p,x+6,y+int(equippedFont.pixelSize()),label);
  }
  std::optional<std::string_view> pendingLabel;
  if(serverCorpseMode && corpseState!=nullptr) {
    if(const auto phase = corpseState->pendingPhase(stack.handle))
      pendingLabel = *phase==Mmo::ClientPresentation::ServerCorpseLootPendingPhase::Submitted
                         ? "..." : "OK";
  } else if(inventoryState!=nullptr) {
    const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*inventoryState);
    if(const auto phase = pageModel.pendingPhase(stack.handle))
      pendingLabel = *phase==Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted
                         ? "..." : "OK";
  }
  if(pendingLabel.has_value()) {
    auto& pendingFont = Resources::font(Resources::FontType::Red,scale);
    pendingFont.drawText(p,x+6,y+slotSize().h-8,*pendingLabel);
  } else if(!serverCorpseMode && mergeSource.has_value() &&
            *mergeSource==stack.handle) {
    auto& mergeFont = Resources::font(Resources::FontType::Red,scale);
    mergeFont.drawText(p,x+6,y+slotSize().h-8,"M");
  }
}

void InventoryMenu::drawServerInfo(Painter &p) {
  const auto* state = serverInventoryState();
  const auto* stack = selectedServerStack();
  if(state==nullptr)
    return;

  const float scale = Gothic::interfaceScale(this);
  const int dw = std::min(w(),int(720*scale));
  const int dh = infoHeight();
  const int x = (w()-dw)/2;
  const int y = h()-dh-20;
  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh,0,0,tex->w(),tex->h());
  }

  auto& fnt = Resources::font(scale);
  const auto title = stack != nullptr ? serverItemDisplayName(*stack)
                                      : std::string("Server inventory");
  const auto titleSize = fnt.textSize(title);
  fnt.drawText(p,x+(dw-titleSize.w)/2,y+int(fnt.pixelSize()),title);

  std::vector<std::string> lines;
  lines.reserve(10U);
  if(stack != nullptr) {
    lines.push_back("presentation: "+std::to_string(stack->presentationId));
    lines.push_back("stack: "+std::to_string(stack->handle.instanceId)+":"+
                    std::to_string(stack->handle.generation));
    lines.push_back("quantity: "+std::to_string(stack->quantity));
    lines.push_back("item revision: "+std::to_string(stack->itemRevision));
  }
  lines.push_back("inventory/equipment revision: "+
                  std::to_string(state->inventory().revision())+"/"+
                  std::to_string(state->equipment().revision()));
  if(stack != nullptr) {
    if(const auto equipped = state->equipment().slotOf(stack->handle))
      lines.push_back("equipped: "+std::string(equipmentSlotName(*equipped)));
  }
  const Mmo::ClientPresentation::ServerInventoryPageModel pageModel(*state);
  const auto selectedPhase = stack != nullptr
                                 ? pageModel.pendingPhase(stack->handle)
                                 : std::nullopt;
  if(selectedPhase.has_value()) {
    lines.push_back(
        *selectedPhase==
                Mmo::ClientPresentation::ServerInventoryPendingPhase::Submitted
            ? "pending: awaiting server receipt"
            : "accepted: awaiting authoritative inventory update");
  }
  switch(pageModel.feedbackStatus()) {
    case Mmo::ClientPresentation::ServerInventoryFeedbackStatus::Pending:
      if(!selectedPhase.has_value())
        lines.push_back("pending: awaiting server receipt");
      break;
    case Mmo::ClientPresentation::ServerInventoryFeedbackStatus::Accepted:
      if(!selectedPhase.has_value())
        lines.push_back("accepted: awaiting authoritative inventory update");
      break;
    case Mmo::ClientPresentation::ServerInventoryFeedbackStatus::Rejected:
      if(!pageModel.rejectionMessage().empty())
        lines.push_back(pageModel.rejectionMessage());
      break;
    case Mmo::ClientPresentation::ServerInventoryFeedbackStatus::ResyncRequired:
      lines.push_back("resync required: inventory actions are temporarily disabled");
      lines.push_back(serverInventoryResyncRequested
                          ? "authoritative inventory resync requested"
                          : serverInventoryResyncAttempted
                                ? "authoritative inventory resync request is unavailable"
                                : "authoritative inventory resync is pending");
      if(!pageModel.rejectionMessage().empty())
        lines.push_back(pageModel.rejectionMessage());
      break;
    case Mmo::ClientPresentation::ServerInventoryFeedbackStatus::Ready:
      break;
  }
  lines.push_back(pageModel.actionsEnabled()
                      ? "Enter use/unequip | 3-0 equip | Space drop | S split | M merge"
                      : "Waiting for authoritative inventory resynchronization");

  for(size_t index=0; index<lines.size(); ++index)
    fnt.drawText(p,x+20,y+int(index+2U)*fnt.pixelSize(),lines[index]);
}

void InventoryMenu::drawServerCorpseInfo(Painter& p) {
  const auto* state = serverCorpseState();
  const auto* stack = selectedServerCorpseStack();
  if(state==nullptr)
    return;

  const float scale = Gothic::interfaceScale(this);
  const int dw = std::min(w(),int(720*scale));
  const int dh = infoHeight();
  const int x = (w()-dw)/2;
  const int y = h()-dh-20;
  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh,0,0,tex->w(),tex->h());
  }

  auto& fnt = Resources::font(scale);
  const auto title = stack != nullptr ? serverItemDisplayName(*stack)
                                      : std::string("Server corpse loot");
  const auto titleSize = fnt.textSize(title);
  fnt.drawText(p,x+(dw-titleSize.w)/2,y+int(fnt.pixelSize()),title);

  std::vector<std::string> lines;
  lines.reserve(10U);
  if(stack != nullptr) {
    lines.push_back("stack: "+std::to_string(stack->handle.instanceId)+":"+
                    std::to_string(stack->handle.generation));
    lines.push_back("quantity: "+std::to_string(stack->quantity));
    lines.push_back("item revision: "+std::to_string(stack->itemRevision));
  }
  lines.push_back("corpse/inventory revision: "+
                  std::to_string(state->corpseRevision())+"/"+
                  std::to_string(state->inventoryRevision()));
  lines.push_back("snapshot: "+std::to_string(state->snapshotId()));
  if(!state->feedbackMessage().empty())
    lines.emplace_back(state->feedbackMessage());
  lines.push_back(state->actionsEnabled()
                      ? "Action/Enter/left click: take one"
                      : "Loot actions wait for authoritative state");
  lines.push_back("Jump: take stack | Z/X: take 10/100");
  lines.push_back("Right action/right click: take all | Inventory/Esc: close");

  for(size_t index=0; index<lines.size(); ++index)
    fnt.drawText(p,x+20,y+int(index+2U)*fnt.pixelSize(),lines[index]);
}

void InventoryMenu::drawGold(Painter &p, Npc &player, int x, int y) {
  if(!slot)
    return;
  auto           w    = world();
  auto           txt  = w ? w->script().currencyName() : "";
  const size_t   gold = player.inventory().goldCount();
  if(txt.empty())
    txt="Gold";

  string_frm vint(txt," : ",int(gold));
  drawHeader(p,vint,x,y);
  }

void InventoryMenu::drawHeader(Painter &p, std::string_view title, int x, int y) {
  const float scale = Gothic::interfaceScale(this);
  auto&       fnt   = Resources::font(scale);

  const int   tw    = fnt.textSize(title).w;
  const int   th    = fnt.textSize(title).h;
  const int   padd  = int(8*scale);
  const int   dw    = std::max(slotSize().w*2, tw+padd*2);
  const int   dh    = int(34*scale);

  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh, 0,0,tex->w(),tex->h());
    }
  if(slot) {
    p.setBrush(*slot);
    p.drawRect(x,y,dw,dh, 0,0,slot->w(),slot->h());
    }

  fnt.drawText(p,x+(dw-tw)/2,y+dh/2+th/2,title);
  }

void InventoryMenu::drawInfo(Painter &p) {
  if(serverCorpseMode) {
    drawServerCorpseInfo(p);
    return;
  }
  if(serverInventoryMode) {
    drawServerInfo(p);
    return;
  }
  const float scale = Gothic::interfaceScale(this);
  const int   dw    = std::min(w(), int(720*scale));
  const int   dh    = infoHeight();
  const int   x     = (w()-dw)/2;
  const int   y     = h()-dh-20;

  auto& pg  = activePage();
  auto& sel = activePageSel();

  auto it = pg.get(sel.sel);
  if(!it.isValid())
    return;

  auto& r = *pg.get(sel.sel);
  if(tex) {
    p.setBrush(*tex);
    p.drawRect(x,y,dw,dh,
               0,0,tex->w(),tex->h());
    }

  auto& fnt = Resources::font(scale);
  auto  desc = r.description();
  int   tw   = fnt.textSize(desc).w;

  fnt.drawText(p,x+(dw-tw)/2,y+int(fnt.pixelSize()),desc);

  for(size_t i=0;i<Item::MAX_UI_ROWS;++i){
    auto    txt = r.uiText(i);
    int32_t val = r.uiValue(i);

    if(txt.empty())
      continue;

    if(i+1==Item::MAX_UI_ROWS && state==State::Trade && player!=nullptr && pg.is(&player->inventory())){
      val = r.sellCost();
      }

    string_frm vint(val);
    int tw = fnt.textSize(vint).w;

    fnt.drawText(p, x+20,  y+int(i+2)*fnt.pixelSize(), txt);
    if(val!=0)
      fnt.drawText(p,x+dw-tw-20,y+int(i+2)*fnt.pixelSize(),vint);
    }

  const int sz = dh;
  renderer.drawItem(x+dw-sz-sz/2,y,sz,sz,r);
  }

void InventoryMenu::draw(Tempest::Encoder<CommandBuffer>& cmd) {
  renderer.draw(cmd);
  }
