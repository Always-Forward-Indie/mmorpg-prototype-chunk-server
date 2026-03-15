#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Holds data-driven status effect templates loaded from the game server at startup.
 *
 * A "template" is the static definition of a named status effect: its category,
 * default duration, and the set of attribute modifiers it applies.
 *
 * The chunk server calls loadTemplates() once on SET_STATUS_EFFECT_TEMPLATES and
 * then uses getTemplate() at runtime (e.g. during player respawn) to build the
 * actual ActiveEffectStructs without any hardcoded values.
 */
class StatusEffectTemplateManager
{
  public:
    explicit StatusEffectTemplateManager(Logger &logger);

    /**
     * @brief Replace the full template map (called on SET_STATUS_EFFECT_TEMPLATES event).
     */
    void loadTemplates(const std::vector<StatusEffectTemplate> &templates);

    /**
     * @brief Look up a template by slug.
     * @return pointer to the template, or nullptr if not found.
     */
    const StatusEffectTemplate *getTemplate(const std::string &slug) const;

    /**
     * @brief Returns true if at least one template has been loaded.
     */
    bool isLoaded() const;

  private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, StatusEffectTemplate> templates_;
    Logger &logger_;
};
