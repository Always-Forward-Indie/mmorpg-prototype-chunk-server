#pragma once
// Admin-RPC gate helpers (DEV test hook, defense in depth layer 1+2 logic).
//
// Pure/header-only on purpose: unit-testable without servers, sockets or DB.
// The compiled handler (EventDispatcher::handleAdminCommand) adds layers 3+4:
//   3. `#ifdef ADMIN_RPC` — no such code in prod builds physically.
//   4. Audit — every served command logs at warn, every rejected attempt
//      at error (Watch SEAM pattern).
//
// Role model (A1): chunk has no DB access, so the GM allowlist lives in
// game_config `admin.gm_client_ids` (CSV of clientIds, DEV only, pushed via
// the boot handshake like every other knob). Bot accounts are never listed;
// a separate `gm_bot` (users.role=1) account is used by Tools/Bots/admin.py.
// A2 replaces the CSV with a real role push from game (users.role).
#include <string>

namespace admin_gate
{

inline std::string trimCsvToken(std::string s)
{
    const char *ws = " \t\r\n";
    const auto a = s.find_first_not_of(ws);
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

/// True when clientId appears in the CSV allowlist (exact integer match,
/// whitespace tolerant, empty list matches nobody).
inline bool isGmListed(int clientId, const std::string &csv)
{
    if (clientId <= 0 || csv.empty())
        return false;
    std::string cur;
    for (size_t i = 0; i <= csv.size(); ++i)
    {
        const char c = (i < csv.size()) ? csv[i] : ',';
        if (c == ',')
        {
            const std::string t = trimCsvToken(cur);
            if (!t.empty())
            {
                try
                {
                    if (std::stoi(t) == clientId)
                        return true;
                }
                catch (...)
                {
                    // Non-numeric tokens never match; they must not fail closed
                    // for the rest of the list (misconfig is visible in logs).
                }
            }
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    return false;
}

/// Full gate: config enabled AND caller allowlisted. No single layer suffices;
// callers log rejections at error level.
inline bool allows(bool adminEnabled, int clientId, const std::string &gmIdsCsv)
{
    return adminEnabled && isGmListed(clientId, gmIdsCsv);
}

/// Ops served by the A1 vertical slice. Unknown ops are rejected with an
/// error response (never ignored: the harness must fail loudly, not hang).
inline bool isKnownOp(const std::string &op)
{
    return op == "teleport" || op == "getState" || op == "grantXP" ||
           op == "grantLevel" || op == "grantItem" || op == "setHP" ||
           op == "skipTime" || op == "spawnMob" || op == "killMob" ||
           op == "resetWorld";
}

} // namespace admin_gate
