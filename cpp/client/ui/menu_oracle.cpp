// The oracle.
//
// What the craft key opens while the flower stands at an oracle NPC (see
// shared/game/npc.h). The forge gambles five petals on a roll; the oracle
// takes a fixed price -- oracleCraftCost(), a table that runs from 7 commons
// to 1012 uniques -- and the upgrade is certain.
//
// The card is laid out against a reference shot (oracle_screenshot_menu.png)
// and measured off it: a slate card; one slot and the Craft button beside it;
// one line of text; and the grid, where every cell carries "owned/price" and a
// stack that cannot yet pay its column's price sits on a grey plate. The
// reference grid stops at super; this one keeps the unique column, because
// unique -> apex has a price here, and grows the card by that one column
// rather than shrinking every cell to fit it.
//
// One upgrade per craft, and one craft per half hour (kOracleCooldownMillis):
// while the account waits, the line of text turns red and says how long, and
// every stack in the grid sits on grey with its plain count, as the reference's
// second shot (oracle_screenshot_cooldown.png) has it.
//
// No spin. A roll is something to watch resolve; a purchase is not. The staged
// petal breathes the way a drop lying on the ground does, the breath swells
// while the oracle works, and the upgrade LANDS in the slot exactly as loot
// lands on the ground -- sliding in, unwinding, throwing a burst of its own
// rarity's grains.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "client/ui/item_tile.h"
#include "client/ui/menu_style.h"
#include "client/ui/menu_theme.h"
#include "client/ui/menus.h"
#include "client/ui/text.h"
#include "shared/game/config.h"

namespace flix {

using namespace flix::ui;

namespace {

// -- the card, off the reference ----------------------------------------------
//
// Every figure here is the reference shot's own, in design units (the shot is
// at one design unit to the pixel), measured from the card's OUTER top edge
// and from its centre line. The cells are exactly its cells, so its labels
// and greys land at its sizes.

/// The grid: 60-unit cells on a 70-unit pitch, 25 in from the card's left
/// edge and 42 clear of its right, where the scroll thumb runs.
constexpr double kGridCell = 60.0;
constexpr double kGridGap = 10.0;
constexpr double kGridLeft = 25.0;
constexpr double kGridRight = 42.0;
constexpr std::size_t kTierColumns = static_cast<std::size_t>(Rarity::Unique) + 1;
/// Where the scroll view begins, and the gap above its first row.
constexpr double kGridTop = 339.0;
constexpr double kGridPadding = 10.0;
/// The card's border, which the view stops short of at the bottom.
constexpr double kCardBorder = 6.0;
/// Its height: the reference's card is this tall and stands on the same bottom
/// edge the forge's does, which gives the grid its five visible rows.
constexpr double kCardHeight = 707.0;
constexpr double kWheelStep = 100.0;
/// The thumb: 8 wide, 20 in from the card's right edge, rounded, in the
/// border's slate. A hint at how far down the list is, not a control.
constexpr double kThumbWidth = 8.0;
constexpr double kThumbRight = 20.0;
constexpr double kThumbMinHeight = 20.0;

/// The slot and the Craft button share a line, either side of the centre.
constexpr double kSlotSide = 70.0;
constexpr double kRowCentreY = 181.0;
constexpr double kSlotOffsetX = -101.0;
constexpr double kCraftOffsetX = 129.0;
constexpr double kCraftWidth = 64.0;
constexpr double kCraftHeight = 35.0;
constexpr double kCraftRim = 4.0;
constexpr double kCraftRadius = 8.0;
/// The one line of text, centred.
constexpr double kLineY = 318.0;
constexpr const char* kLine = "The Oracle will guarantee a craft... for the right price.";

/// The idle Craft button's greys -- the same pair a grey cell wears.
constexpr std::uint32_t kCraftIdleFill = 0x777777u;
constexpr std::uint32_t kCraftIdleBorder = 0x606060u;
constexpr std::uint32_t kRefusalColor = 0xFF6B6Bu;
/// The waiting line's red, off the reference's cooldown shot.
constexpr std::uint32_t kCooldownColor = 0xED706Bu;
/// The forge's 60% label outline, which the oracle wears too.
constexpr double kSoftStroke = 0.6;

// -- the pulse ------------------------------------------------------------------

/// How long the oracle works before the upgrade may land. Long enough for the
/// swell to read; a server that answers sooner is held until it has run.
constexpr double kPulseSeconds = 1.4;
/// The breath swells from a drop's own 3% to this, and quickens from a drop's
/// 10 rad/s to this many times that, both over the pulse. An ease-in, so it
/// starts as the drop it is and builds.
constexpr double kPulsePeakAmount = 0.16;
constexpr double kPulsePeakRateScale = 2.4;
/// A pulse that has run out holds at its peak waiting on the server. One that
/// never hears back drops to idle after this: the craft resolved server-side
/// either way, and the profile will say how.
constexpr double kCraftTimeoutSeconds = 8.0;
/// How long a refusal stays under the slot.
constexpr double kRefusalSeconds = 3.0;

/// Grains a second the slot throws at the height of the pulse -- a drop's own
/// shimmer rate, arriving evenly. The shimmer ramps with the swell, so the
/// first moments of the pulse are as quiet as a drop at rest.
constexpr double kPulseGrainRate = 36.0;
constexpr double kPulseGrainSpeed = 1.2;
constexpr double kPulseGrainSpeedSpread = 1.2;
constexpr double kPulseGrainLifeMs = 700.0;
constexpr double kPulseGrainLifeSpreadMs = 500.0;
constexpr double kPulseGrainSize = 5.0;
constexpr double kPulseGrainSizeSpread = 10.0;
/// The landing throws twice a drop's burst: it is one tile doing what a whole
/// kill's worth of loot does, and a single drop's seven grains vanish behind
/// a tile this size.
constexpr int kLandingBurstGrains = kDropBurstCount * 2;
constexpr std::size_t kMaxGrains = 192;
/// A drop's grains fade from 60%, like the ground's.
constexpr double kGrainAlpha = 0.6;
/// The world's per-frame speeds, and its grain sizes, are stated against a
/// 60-unit drop; the slot is bigger, so they grow with it.
constexpr double kFramesPerSecond = 60.0;
constexpr double kGrainScale = kSlotSide / kItemTileDesign;


struct GridCell {
    Rect rect;
    std::uint16_t petalIndex = kNoPetal;
    Rarity rarity = Rarity::Common;
    std::uint32_t count = 0;
};

bool knownPetal(std::uint16_t petalIndex) {
    return petalIndex != kNoPetal && petalIndex < content().petalCount();
}

/// Particle jitter. Not reproducible and not meant to be, exactly as the world
/// renderer's is: nobody else sees this slot.
double jitter() {
    static std::uint32_t state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double>(state % 100000u) / 100000.0;
}

/// Draws `tile` into a square of `side` centred on `centre`, turned by
/// `rotation`. The transform is set before the tile opens a path and restored
/// after it closes the last, so both backends see the same geometry.
void drawTileAt(Canvas& canvas, const SpriteCache& sprites, Vec2 centre, double side,
                double rotation, const ItemTile& tile) {
    canvas.save();
    canvas.translate(static_cast<float>(centre.x), static_cast<float>(centre.y));
    if (rotation != 0.0) canvas.rotate(static_cast<float>(rotation));
    drawItemTile(canvas, sprites, {-side * 0.5, -side * 0.5, side, side}, tile);
    canvas.restore();
}

} // namespace

double OraclePanel::preferredHeight() { return kCardHeight; }

double OraclePanel::preferredWidth() {
    return kGridLeft + static_cast<double>(kTierColumns) * (kGridCell + kGridGap) - kGridGap +
           kGridRight;
}

void OraclePanel::reset() {
    scroll_ = {};
    stagedPetal_ = kNoPetal;
    crafts_ = 0;
    phase_ = Phase::Idle;
    resultPending_ = false;
    refusal_.clear();
    grains_.clear();
    grainCredit_ = 0;
}

void OraclePanel::stage(const Profile& profile, std::uint16_t petalIndex, Rarity rarity) {
    const int cost = oracleCraftCost(rarity);
    if (cost <= 0) return;
    if (profile.stackCount(petalIndex, rarity) < static_cast<std::uint32_t>(cost)) return;
    // One upgrade, so a click REPLACES what was staged rather than adding to
    // it: there is only ever one price in the slot.
    stagedPetal_ = petalIndex;
    stagedRarity_ = rarity;
    crafts_ = 1;
    // Whatever was refused, the player has moved on from it.
    refusal_.clear();
}

void OraclePanel::throwGrains(Vec2 at, Rarity rarity, int count, double speed,
                              double speedSpread, double lifeMs, double lifeSpreadMs,
                              double size, double sizeSpread) {
    for (int i = 0; i < count && grains_.size() < kMaxGrains; ++i) {
        // Each grain its own direction and facing: a drop's scatter, not a
        // petal's spokes.
        const double angle = jitter() * kTau;
        const double pace = (speed + jitter() * speedSpread) * kFramesPerSecond * kGrainScale;
        Grain grain;
        grain.position = {at.x + (jitter() - 0.5) * 4.0, at.y + (jitter() - 0.5) * 4.0};
        grain.velocity = Vec2::fromAngle(angle, pace);
        grain.lifeSeconds = grain.maxLifeSeconds = (lifeMs + jitter() * lifeSpreadMs) / 1000.0;
        grain.size = (size + jitter() * sizeSpread) * kGrainScale;
        grain.rotation = jitter() * kTau;
        grain.color = rarityColor(rarity);
        grains_.push_back(grain);
    }
}

bool OraclePanel::render(MenuContext& ctx) {
    Canvas& canvas = ctx.canvas;
    const Profile& profile = ctx.net.profile();
    const Rect panel = ctx.bounds;
    const Vec2 mouse = ctx.mouse();
    const double now = ctx.timeSeconds;
    const double dt = std::max(0.0, ctx.dt);

    const auto removeCraft = [this]() {
        crafts_ = 0;
        stagedPetal_ = kNoPetal;
    };
    // The account's wait for its next craft, counted down on this client's
    // clock from what the last profile said.
    const double cooldown = ctx.net.oracleCooldownRemainingMillis();
    const bool waiting = cooldown > 0.0;
    // The upgrade arrives: it slides in from a drop's distance, unwinding a
    // drop's spin, and throws a burst of its own colour from the slot. The
    // grains are placed relative to the slot's centre, so a card still sliding
    // up carries them with it.
    const auto land = [this, now]() {
        phase_ = Phase::Result;
        phaseStarted_ = now;
        resultPending_ = false;
        landFrom_ = Vec2::fromAngle(jitter() * kTau,
                                    (kDropLandNear + jitter() * kDropLandSpread) * kGrainScale);
        landSpin_ = (jitter() - 0.5) * kPi;
        throwGrains({}, resultRarity_, kLandingBurstGrains, kDropBurstSpeed, kDropBurstSpeedSpread,
                    kDropBurstLifeMs, kDropBurstLifeSpreadMs, kDropBurstSize,
                    kDropBurstSizeSpread);
    };

    // A result the server sent while this face of the menu was not showing
    // still has to land, so it is read here rather than in the click.
    OracleOutcome& outcome = ctx.net.oracleOutcome();
    if (outcome.pending) {
        outcome.pending = false;
        if (outcome.success) {
            resultPetal_ = outcome.petalIndex;
            resultRarity_ = outcome.rarity;
            resultCount_ = outcome.crafted;
            // Mid-pulse it is only recorded -- the pulse owns when it lands.
            if (phase_ == Phase::Pulsing && now - phaseStarted_ < kPulseSeconds) {
                resultPending_ = true;
            } else {
                land();
            }
        } else {
            // Refused: nothing left the inventory, so what was offered goes
            // straight back into the slot, with the reason under it.
            if (phase_ == Phase::Pulsing) {
                stagedPetal_ = offeredPetal_;
                stagedRarity_ = offeredRarity_;
                crafts_ = offeredCrafts_;
                phase_ = Phase::Idle;
            }
            refusal_ = outcome.reason.empty() ? "The oracle refused." : outcome.reason;
            refusalUntil_ = now + kRefusalSeconds;
        }
    }

    // Nothing can be staged while the oracle is not taking crafts.
    if (waiting && phase_ == Phase::Idle) removeCraft();

    // The staging area cannot outlive the petals behind it -- but never while
    // the pulse runs: those are already the server's, and a profile landing a
    // frame ahead of the result would empty the slot under its own animation.
    if (phase_ != Phase::Pulsing && knownPetal(stagedPetal_)) {
        const int cost = oracleCraftCost(stagedRarity_);
        const int affordable =
            cost > 0 ? static_cast<int>(profile.stackCount(stagedPetal_, stagedRarity_) /
                                        static_cast<std::uint32_t>(cost))
                     : 0;
        if (affordable < crafts_) {
            crafts_ = affordable;
            if (crafts_ <= 0) stagedPetal_ = kNoPetal;
        }
    }

    panelCard(canvas, panel, kOracleSkin, kCardBorder);
    panelTitle(canvas, panel, "Oracle");
    const Rect closeRect = closeButtonRect(panel);
    panelClose(canvas, closeRect, closeRect.contains(mouse));

    const double centreX = panel.x + panel.w * 0.5;
    const double rowY = panel.y + kRowCentreY;
    const Vec2 slotCentre{centreX + kSlotOffsetX, rowY};
    const Rect slotRect{slotCentre.x - kSlotSide * 0.5, slotCentre.y - kSlotSide * 0.5, kSlotSide,
                        kSlotSide};

    // --- the pulse ---------------------------------------------------------
    // Idle and on a result the slot breathes exactly as a drop on the ground
    // does. While the oracle works the breath swells and quickens; its PHASE is
    // the integral of the quickening rate, so the beat speeds up smoothly
    // rather than jumping every time the rate is re-read.
    double breath = dropPulse(now);
    if (phase_ == Phase::Pulsing) {
        const double elapsed = now - phaseStarted_;
        const double u = clamp(elapsed / kPulseSeconds, 0.0, 1.0);
        const double eased = u * u;
        const double rateGain = kDropPulseRate * (kPulsePeakRateScale - 1.0);
        const double ramp = std::min(elapsed, kPulseSeconds);
        const double phase = kDropPulseRate * elapsed +
                             rateGain * (ramp * ramp / (2.0 * kPulseSeconds) +
                                         std::max(0.0, elapsed - kPulseSeconds));
        const double amount = kDropPulseAmount + (kPulsePeakAmount - kDropPulseAmount) * eased;
        breath = 1.0 + std::sin(phase) * amount;

        // The shimmer, building with the swell.
        grainCredit_ += kPulseGrainRate * eased * dt;
        const int owed = static_cast<int>(grainCredit_);
        grainCredit_ -= owed;
        throwGrains({}, offeredRarity_, owed, kPulseGrainSpeed, kPulseGrainSpeedSpread,
                    kPulseGrainLifeMs, kPulseGrainLifeSpreadMs, kPulseGrainSize,
                    kPulseGrainSizeSpread);

        if (elapsed >= kPulseSeconds && resultPending_) {
            land();
        } else if (elapsed >= kCraftTimeoutSeconds) {
            phase_ = Phase::Idle;
        }
    }

    // --- grains -----------------------------------------------------------
    // Under the tile, as a drop's glitter lies under the drop.
    for (Grain& grain : grains_) {
        grain.position += grain.velocity * dt;
        grain.lifeSeconds -= dt;
    }
    grains_.erase(std::remove_if(grains_.begin(), grains_.end(),
                                 [](const Grain& g) { return g.lifeSeconds <= 0.0; }),
                  grains_.end());
    // Clipped to the card: a burst is the card's, and grains carried out over
    // the world would read as something happening out there.
    canvas.save();
    roundPath(canvas, panel, kMenuRadius);
    canvas.clip();
    for (const Grain& grain : grains_) {
        const double left = grain.lifeSeconds / grain.maxLifeSeconds;
        const double r = grain.size * left;
        if (r <= 0.0) continue;
        const Vec2 at = slotCentre + grain.position;
        const double c = std::cos(grain.rotation) * r;
        const double s = std::sin(grain.rotation) * r;
        setFill(canvas, grain.color);
        canvas.setGlobalAlpha(static_cast<float>(left * kGrainAlpha));
        canvas.beginPath();
        canvas.moveTo(static_cast<float>(at.x - c + s), static_cast<float>(at.y - s - c));
        canvas.lineTo(static_cast<float>(at.x + c + s), static_cast<float>(at.y + s - c));
        canvas.lineTo(static_cast<float>(at.x + c - s), static_cast<float>(at.y + s + c));
        canvas.lineTo(static_cast<float>(at.x - c - s), static_cast<float>(at.y - s + c));
        canvas.closePath();
        canvas.fill();
    }
    canvas.setGlobalAlpha(1.0f);
    canvas.restore();

    // --- the slot ----------------------------------------------------------
    // Its plate first, flat in the card's slate: an empty slot reads as a
    // place to put something rather than as a hole in the card.
    {
        ItemTile plate;
        plate.empty = true;
        plate.emptyFill = kOracleSkin.border;
        plate.emptyBorder = kOracleSkin.border;
        drawItemTile(canvas, ctx.sprites, slotRect, plate);
    }
    if (phase_ == Phase::Result && knownPetal(resultPetal_)) {
        // The landing: in from its offset and unwinding its spin, eased out
        // over a drop's 400 ms, settling into the breath it then keeps.
        const double t = clamp((now - phaseStarted_) / kDropLandSeconds, 0.0, 1.0);
        const double eased = 1.0 - (1.0 - t) * (1.0 - t);
        ItemTile tile;
        tile.petalIndex = resultPetal_;
        tile.rarity = resultRarity_;
        if (resultCount_ > 1) tile.badge = "x" + std::to_string(resultCount_);
        tile.timeSeconds = now;
        drawTileAt(canvas, ctx.sprites, slotCentre + landFrom_ * (1.0 - eased),
                   kSlotSide * breath, landSpin_ * (1.0 - eased), tile);
    } else {
        const std::uint16_t shown = phase_ == Phase::Pulsing ? offeredPetal_ : stagedPetal_;
        const Rarity shownRarity = phase_ == Phase::Pulsing ? offeredRarity_ : stagedRarity_;
        const int shownCrafts = phase_ == Phase::Pulsing ? offeredCrafts_ : crafts_;
        if (knownPetal(shown)) {
            ItemTile tile;
            tile.petalIndex = shown;
            tile.rarity = shownRarity;
            // The badge counts PETALS, the way a stack is counted everywhere
            // else: this is how many of them the oracle is being handed.
            tile.badge = "x" + std::to_string(shownCrafts * oracleCraftCost(shownRarity));
            tile.timeSeconds = now;
            drawTileAt(canvas, ctx.sprites, slotCentre, kSlotSide * breath, 0.0, tile);
        }
    }

    // A refusal, under the slot, for as long as it lasts. The reference has no
    // line here; the only thing that ever puts one there is the oracle saying
    // no, which is worth saying where the player is looking.
    if (!refusal_.empty() && now < refusalUntil_) {
        TextStyle style = panelLabel(12.0, Align::Centre, Baseline::Top);
        style.fill = kRefusalColor;
        outlinedText(canvas, refusal_, slotCentre.x, slotRect.bottom() + 10.0, style, kSoftStroke);
    } else {
        refusal_.clear();
    }

    // --- craft button ------------------------------------------------------
    // The reference's grey pill at rest, and the colour of the tier being
    // bought once there is something to buy -- the forge's rule, so the two
    // faces of the menu answer "is anything staged" the same way.
    const Rect craftRect{centreX + kCraftOffsetX - kCraftWidth * 0.5, rowY - kCraftHeight * 0.5,
                         kCraftWidth, kCraftHeight};
    const bool canCraft =
        !waiting && phase_ == Phase::Idle && knownPetal(stagedPetal_) && crafts_ > 0;
    const Rarity nextRarity = upgradeRarity(stagedRarity_);
    const bool tinted = knownPetal(stagedPetal_) && nextRarity != stagedRarity_;
    const std::uint32_t craftFill = tinted ? rarityColor(nextRarity) : kCraftIdleFill;
    const std::uint32_t craftBorder = tinted ? darken(craftFill, 0.25) : kCraftIdleBorder;
    inlaid(canvas, craftRect,
           craftRect.contains(mouse) ? lighten(craftFill, 0.15) : craftFill, craftBorder, kCraftRim,
           kCraftRadius);
    outlinedText(canvas, "Craft", craftRect.x + craftRect.w * 0.5, craftRect.y + craftRect.h * 0.5,
                 panelLabel(16.0, Align::Centre, Baseline::Middle), kSoftStroke);

    // The line says how long to wait while there is a wait, in red.
    if (waiting) {
        TextStyle style = panelLabel(16.0, Align::Centre, Baseline::Middle);
        style.fill = kCooldownColor;
        outlinedText(canvas, oracleCooldownText(cooldown), centreX, panel.y + kLineY, style,
                     kSoftStroke);
    } else {
        outlinedText(canvas, kLine, centreX, panel.y + kLineY,
                     panelLabel(16.0, Align::Centre, Baseline::Middle), kSoftStroke);
    }

    // --- the grid ----------------------------------------------------------
    // Rows are the petal types the account owns, from the UNDEDUCTED profile so
    // a stack staged down to nothing keeps its row; columns are every craftable
    // tier. Apex is not a column: nothing crafts out of it.
    std::vector<std::uint16_t> types;
    for (const Profile::Stack& stack : profile.inventory) {
        if (stack.count == 0 || stack.rarity == Rarity::Apex) continue;
        if (std::find(types.begin(), types.end(), stack.petalIndex) == types.end()) {
            types.push_back(stack.petalIndex);
        }
    }
    std::sort(types.begin(), types.end());

    const double inventoryTop = panel.y + kGridTop;
    const Rect view{panel.x + kCardBorder, inventoryTop, panel.w - kCardBorder * 2,
                    std::max(0.0, panel.bottom() - kCardBorder - inventoryTop)};
    const double startX = panel.x + kGridLeft;

    const std::uint32_t held =
        knownPetal(stagedPetal_)
            ? static_cast<std::uint32_t>(std::max(0, crafts_)) *
                  static_cast<std::uint32_t>(oracleCraftCost(stagedRarity_))
            : 0u;
    std::vector<GridCell> cells;
    double y = kGridPadding;
    for (const std::uint16_t petalIndex : types) {
        for (std::size_t column = 0; column < kTierColumns; ++column) {
            const Rarity rarity = static_cast<Rarity>(column);
            std::uint32_t count = profile.stackCount(petalIndex, rarity);
            if (petalIndex == stagedPetal_ && rarity == stagedRarity_) {
                count = count > held ? count - held : 0;
            }
            cells.push_back({Rect{startX + static_cast<double>(column) * (kGridCell + kGridGap), y,
                                  kGridCell, kGridCell},
                             petalIndex, rarity, count});
        }
        y += kGridCell + kGridGap;
    }
    const double contentHeight = y - kGridGap + kGridPadding;

    scroll_.contentHeight = contentHeight;
    scroll_.viewHeight = view.h;
    if (panel.contains(mouse) && mouse.y >= inventoryTop) {
        scroll_.offset -= static_cast<double>(ctx.wheel()) * kWheelStep;
    }
    scroll_.offset -= touchScroll(ctx.window, view, scroll_.maxOffset() > 0);
    scroll_.offset = clamp(scroll_.offset, 0.0, scroll_.maxOffset());

    canvas.save();
    canvas.beginPath();
    canvas.rect(static_cast<float>(view.x), static_cast<float>(view.y), static_cast<float>(view.w),
                static_cast<float>(view.h));
    canvas.clip();

    // Each row RIGHT TO LEFT: a cell's "owned/price" label hangs past its right
    // edge in the reference, over the next cell, and painting that cell after
    // it would cut the label off at the join.
    int hovered = -1;
    for (std::size_t k = 0; k < cells.size(); ++k) {
        const std::size_t rowStart = k - k % kTierColumns;
        const std::size_t i = rowStart + (kTierColumns - 1 - k % kTierColumns);
        const GridCell& cell = cells[i];
        const Rect rect{cell.rect.x, view.y - scroll_.offset + cell.rect.y, cell.rect.w,
                        cell.rect.h};
        if (rect.bottom() < view.y || rect.y > view.bottom()) continue;

        if (cell.count == 0) {
            // A tier the account holds none of: a flat slate square.
            ItemTile blank;
            blank.empty = true;
            blank.emptyFill = kOracleSkin.border;
            blank.emptyBorder = kOracleSkin.border;
            drawItemTile(canvas, ctx.sprites, rect, blank);
            continue;
        }
        // Held but short of the column's price: on a grey plate, and not
        // clickable. The petal keeps its colours, and the label still says how
        // far short -- "4/19" is the reason to go farm. While the oracle is
        // not taking crafts, EVERY stack is on grey and says only how many
        // there are: there is no price to be short of until the wait is over.
        const std::uint32_t price =
            static_cast<std::uint32_t>(std::max(1, oracleCraftCost(cell.rarity)));
        const bool affordable = !waiting && cell.count >= price;
        if (affordable && rect.contains(mouse) && view.contains(mouse)) {
            hovered = static_cast<int>(i);
        }

        ItemTile tile;
        tile.petalIndex = cell.petalIndex;
        tile.rarity = cell.rarity;
        tile.hovered = hovered == static_cast<int>(i);
        tile.greyed = !affordable;
        tile.badge = waiting ? "x" + stackCountText(cell.count)
                             : stackCountText(cell.count) + "/" + std::to_string(price);
        tile.badgeCentred = true;
        tile.timeSeconds = now;
        drawItemTile(canvas, ctx.sprites, rect, tile);
    }
    canvas.restore();

    if (contentHeight > view.h && view.h > 0.0) {
        const double thumbHeight = std::max(kThumbMinHeight, view.h * view.h / contentHeight);
        const double travel = contentHeight - view.h;
        const double thumbY =
            view.y + clamp(scroll_.offset / travel, 0.0, 1.0) * (view.h - thumbHeight);
        fillRound(canvas,
                  {panel.right() - kThumbRight - kThumbWidth, thumbY, kThumbWidth, thumbHeight},
                  kThumbWidth * 0.5, kOracleSkin.accent);
    }

    // --- input -------------------------------------------------------------
    // On press, as the forge answers: a press that starts on a cell and
    // drifts off must not still fire.
    const bool rightPressed = panel.contains(mouse) && ctx.window.mousePressed(MouseButton::Right);
    if (phase_ == Phase::Idle && rightPressed) {
        removeCraft();
        return true;
    }
    if (!ctx.pressed() && !rightPressed) return true;
    if (closeRect.contains(mouse) && ctx.pressed()) return false;

    // The upgrade sits there until it is dismissed, and the press that
    // dismisses it does nothing else.
    if (phase_ == Phase::Result && panel.contains(mouse)) {
        phase_ = Phase::Idle;
        return true;
    }

    if (craftRect.contains(mouse)) {
        if (canCraft) {
            phase_ = Phase::Pulsing;
            phaseStarted_ = now;
            resultPending_ = false;
            grainCredit_ = 0;
            offeredPetal_ = stagedPetal_;
            offeredRarity_ = stagedRarity_;
            offeredCrafts_ = crafts_;
            refusal_.clear();
            ctx.net.requestOracleCraft(stagedPetal_, stagedRarity_);
            // Handed over: the slot draws the offer's copy from here, and a
            // refusal is what puts it back.
            stagedPetal_ = kNoPetal;
            crafts_ = 0;
        }
        return true;
    }

    if (phase_ == Phase::Idle && slotRect.contains(mouse)) {
        removeCraft();
        return true;
    }

    if (hovered >= 0 && phase_ == Phase::Idle) {
        const GridCell& cell = cells[static_cast<std::size_t>(hovered)];
        stage(profile, cell.petalIndex, cell.rarity);
    }
    return true;
}

} // namespace flix
