#include "services/StatusEffectTemplateManager.hpp"

StatusEffectTemplateManager::StatusEffectTemplateManager(Logger &logger)
    : logger_(logger)
{
}

void
StatusEffectTemplateManager::loadTemplates(const std::vector<StatusEffectTemplate> &templates)
{
    std::unique_lock lock(mutex_);
    templates_.clear();
    for (const auto &tmpl : templates)
        templates_[tmpl.slug] = tmpl;

    logger_.log("[StatusEffectTemplateManager] Loaded " + std::to_string(templates_.size()) + " status effect templates.");
}

const StatusEffectTemplate *
StatusEffectTemplateManager::getTemplate(const std::string &slug) const
{
    std::shared_lock lock(mutex_);
    auto it = templates_.find(slug);
    return it != templates_.end() ? &it->second : nullptr;
}

bool
StatusEffectTemplateManager::isLoaded() const
{
    std::shared_lock lock(mutex_);
    return !templates_.empty();
}
