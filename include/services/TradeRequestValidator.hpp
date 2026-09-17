#pragma once
// Pure P2P trade-request validation (Wave 3.4).
//
// handleTradeRequestEvent checked six preconditions in a fixed order, each
// sending its own error code. The order is load-bearing (first match wins —
// e.g. a dead initiator reports cannot_trade_while_dead even if the target
// is also busy), so it lives here as a truth table, covered by unit tests.
// The handler only resolves the inputs (manager lookups) and sends the
// resulting error code; behavior is 1-1.
#include <string>

struct TradeRequestValidator
{
    struct Input
    {
        bool initiatorAlive = false;
        bool initiatorInSession = false;
        bool targetExists = false;
        bool inRange = false;
        bool targetBusy = false;
        bool targetOnline = false;
    };

    // "" = proceed; otherwise the exact error code for the client.
    static std::string validate(const Input &in)
    {
        if (!in.initiatorAlive)
            return "cannot_trade_while_dead";
        if (in.initiatorInSession)
            return "already_in_trade";
        if (!in.targetExists)
            return "target_not_found";
        if (!in.inRange)
            return "out_of_range";
        if (in.targetBusy)
            return "target_busy";
        if (!in.targetOnline)
            return "target_offline";
        return "";
    }

    TradeRequestValidator() = delete;
};
