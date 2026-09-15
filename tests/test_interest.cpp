// Unit tests for InterestManager (spatial subscriptions, interest v2).
//
// See README.md in this directory for build & run. Exit code 0 = green.
// Keep tests dependency-free (no gtest): one file per manager, asserts with
// messages, deterministic, milliseconds.
#include "services/InterestManager.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(cond))                                                         \
        {                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));      \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool hasCell(const std::vector<InterestManager::CellKey> &v, int cx, int cy)
{
    for (const auto &c : v)
        if (c.cx == cx && c.cy == cy)
            return true;
    return false;
}

int main()
{
    Logger logger("test");
    InterestManager im(logger);
    im.configure(1500.0f, 1, 0.15f, true);

    // 1. First sighting subscribes 3x3.
    {
        auto r = im.onPlayerMoved(5, 10, 0.0f, 0.0f);
        CHECK(r.entered.size() == 9, "first sighting subscribes 3x3");
        CHECK(r.left.empty(), "first sighting leaves nothing");
        auto snap = im.snapshot();
        CHECK(snap.tracked.count(5) == 1, "client tracked");
        CHECK(snap.members.size() == 9, "9 live cells");
        auto cell = InterestManager::cellFor(-434.0f, -973.0f, 1500.0f);
        CHECK(cell.cx == -1 && cell.cy == -1, "negative coords floor correctly");
        CHECK(snap.members.find(cell) != snap.members.end(), "glade cell subscribed");
    }

    // 2. Hysteresis: small step across the edge does NOT rehome.
    {
        // Anchor cell is (0,0): [0,1500)x[0,1500), margin 225.
        auto r = im.onPlayerMoved(5, 10, 1600.0f, 100.0f); // 100 past edge < margin
        CHECK(r.entered.empty() && r.left.empty(), "margin absorbs edge jitter");
        r = im.onPlayerMoved(5, 10, 2000.0f, 100.0f); // 500 past edge > margin
        CHECK(r.entered.size() == 3 && r.left.size() == 3, "rehome swaps one column");
        auto cells = im.getClientCells(5);
        CHECK(hasCell(cells, 2, -1) && !hasCell(cells, -1, -1), "anchor moved to (1,0)");
    }

    // 3. Teleport resubscribes fully.
    {
        auto r = im.onPlayerMoved(5, 10, 20000.0f, 20000.0f);
        CHECK(r.entered.size() == 9 && r.left.size() == 9, "teleport swaps all");
    }

    // 4. Watchlist with TTL semantics.
    {
        im.watch(5, 1000007);
        im.watch(5, 0);   // invalid uid ignored
        im.watch(0, 100); // invalid client ignored
        auto wi = im.watchIndex();
        CHECK(wi.size() == 1 && wi[1000007].size() == 1, "watch registered");
        im.unwatch(5, 1000007);
        CHECK(im.watchIndex().empty(), "unwatch removes");
    }

    // 5. Second client shares cells (refcounted members).
    {
        im.onPlayerMoved(6, 11, 20100.0f, 20100.0f);
        auto snap = im.snapshot();
        auto cell = InterestManager::cellFor(20000.0f, 20000.0f, 1500.0f);
        auto it = snap.members.find(cell);
        CHECK(it != snap.members.end() && it->second.size() == 2, "cell shared");
        im.removeClient(5);
        snap = im.snapshot();
        it = snap.members.find(cell);
        CHECK(it != snap.members.end() && it->second.size() == 1, "remove keeps other");
        CHECK(im.snapshot().tracked.count(5) == 0, "removed untracked");
        im.removeClient(5); // idempotent
    }

    // 6. Disabled => empty snapshot, no tracking.
    {
        im.configure(1500.0f, 1, 0.15f, false);
        CHECK(im.snapshot().members.empty(), "disabled snapshot empty");
        auto r = im.onPlayerMoved(7, 12, 0.0f, 0.0f);
        CHECK(r.entered.empty(), "disabled tracks nothing");
        im.configure(1500.0f, 1, 0.15f, true);
    }

    // 7. recipientsFor: subscribers + fail-open (unready/untracked), exclude.
    {
        InterestManager im2(logger);
        im2.configure(1500.0f, 1, 0.15f, true);
        im2.onPlayerMoved(1, 1, 0.0f, 0.0f);   // subscribes around origin
        std::vector<std::pair<int, bool>> viewers = {{1, true}, {2, true}, {3, false}};
        auto ids = im2.recipientsFor(100.0f, 100.0f, viewers, -1);
        bool has1 = false, has2 = false, has3 = false;
        for (int id : ids)
        {
            has1 = has1 || id == 1;
            has2 = has2 || id == 2;
            has3 = has3 || id == 3;
        }
        CHECK(has1, "subscriber included");
        CHECK(has2, "untracked fail-open included");
        CHECK(has3, "not-ready fail-open included");
        auto ids2 = im2.recipientsFor(100.0f, 100.0f, viewers, 1);
        for (int id : ids2)
            CHECK(id != 1, "excluded id absent");
    }

    if (failures == 0)
        std::printf("ALL OK\n");
    else
        std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
