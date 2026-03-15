#pragma once

#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include "services/GameConfigService.hpp"
#include <chrono>
#include <random>

/**
 * @brief Класс для расчетов боевой системы
 *
 * Геймплейные константы (формулы защиты, разброс урона, шансы) читаются из GameConfigService если он доступен,
 * иначе используются хардкодные дефолты.
 */
class CombatCalculator
{
  public:
    /**
     * @param configService Необязательный GameConfigService (nullptr = использовать дефолты).
     */
    explicit CombatCalculator(GameConfigService *configService = nullptr);
    ~CombatCalculator() = default;

    /**
     * @brief Привязать GameConfigService. Вызывается из SkillManager после получения
     *        конфига от game-server (событие SET_GAME_CONFIG).
     */
    void setGameConfigService(GameConfigService *configService);

    /**
     * @brief Рассчитать урон от скила
     * @param skill Используемый скил
     * @param attacker Атакующий (персонаж или моб)
     * @param target Цель атаки
     * @return Результат расчета урона
     */
    DamageCalculationStruct calculateSkillDamage(
        const SkillStruct &skill,
        const CharacterDataStruct &attacker,
        const CharacterDataStruct &target);

    /**
     * @brief Рассчитать урон от скила моба
     * @param skill Используемый скил
     * @param attacker Атакующий моб
     * @param target Цель атаки
     * @return Результат расчета урона
     */
    DamageCalculationStruct calculateMobSkillDamage(
        const SkillStruct &skill,
        const MobDataStruct &attacker,
        const CharacterDataStruct &target);

    /**
     * @brief Рассчитать количество лечения скила (без редукции броней/сопротивлений)
     * @param skill Используемый скил
     * @param casterAttributes Атрибуты кастера
     * @return Количество лечения
     */
    int calculateHealAmount(
        const SkillStruct &skill,
        const std::vector<CharacterAttributeStruct> &casterAttributes);

    /**
     * @brief Рассчитать базовый урон скила
     * @param skill Скил
     * @param attackerAttributes Атрибуты атакующего
     * @return Базовый урон
     */
    int calculateBaseDamage(
        const SkillStruct &skill,
        const std::vector<CharacterAttributeStruct> &attackerAttributes);

    /**
     * @brief Рассчитать базовый урон скила моба
     * @param skill Скил
     * @param attackerAttributes Атрибуты атакующего моба
     * @return Базовый урон
     */
    int calculateBaseDamage(
        const SkillStruct &skill,
        const std::vector<MobAttributeStruct> &attackerAttributes);

    /**
     * @brief Проверить критический удар персонажа
     */
    bool rollCriticalHit(const std::vector<CharacterAttributeStruct> &attackerAttributes);

    /**
     * @brief Проверить критический удар моба
     */
    bool rollCriticalHit(const std::vector<MobAttributeStruct> &attackerAttributes);

    /**
     * @brief Проверить блокирование
     */
    bool rollBlock(const std::vector<CharacterAttributeStruct> &targetAttributes);

    /**
     * @brief Проверить промах персонажа по персонажу
     * @param hitModifier Дополнительный бонус/штраф к шансу попадания (level diff)
     */
    bool rollMiss(
        const std::vector<CharacterAttributeStruct> &attackerAttributes,
        const std::vector<CharacterAttributeStruct> &targetAttributes,
        float hitModifier = 0.0f);

    /**
     * @brief Проверить промах моба по персонажу
     * @param hitModifier Дополнительный бонус/штраф к шансу попадания (level diff)
     */
    bool rollMiss(
        const std::vector<MobAttributeStruct> &attackerAttributes,
        const std::vector<CharacterAttributeStruct> &targetAttributes,
        float hitModifier = 0.0f);

    /**
     * @brief Получить значение атрибута по slug
     * @param attributes Список атрибутов
     * @param slug Slug атрибута
     * @return Значение атрибута (0 если не найден)
     */
    int getAttributeValue(const std::vector<CharacterAttributeStruct> &attributes, const std::string &slug);

    /**
     * @brief Получить значение атрибута моба по slug
     * @param attributes Список атрибутов моба
     * @param slug Slug атрибута
     * @return Значение атрибута (0 если не найден)
     */
    int getAttributeValue(const std::vector<MobAttributeStruct> &attributes, const std::string &slug);

    /**
     * @brief Применить защиту к урону
     *
     * Формула убывающей доходности: reduction = armor / (armor + K * targetLevel)
     * Это гарантирует что защита никогда не будет бесполезной на низких
     * значениях и не уйдёт в кап на высоких.
     *
     * @param damage       Входящий урон после крита/блока
     * @param defenseValue Значение защиты цели (physical_defense / magical_defense)
     * @param damageType   Тип урона: "physical" | "magical"
     * @param targetLevel  Уровень цели (используется в знаменателе формулы DR)
     * @return Финальный урон после снижения
     */
    int applyDefense(int damage, int defenseValue, const std::string &damageType, int targetLevel);

    /**
     * @brief Применить неистёкшие stat-modifier эффекты к базовым атрибутам.
     *        DoT/HoT пропускаются (они обрабатываются отдельно через tickEffects).
     *        Используется перед любым расчётом урона/лечения.
     */
    std::vector<CharacterAttributeStruct> mergeEffects(const CharacterDataStruct &character) const;

  private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<float> dis_;
    GameConfigService *gameConfig_ = nullptr; ///< nullable, reads gameplay constants

    /// @brief Reads float constant from config if loaded, otherwise returns defaultValue.
    float cfg(const std::string &key, float defaultValue) const;
};
