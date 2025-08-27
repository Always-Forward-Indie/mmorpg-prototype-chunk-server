#pragma once

#include "data/DataStructs.hpp"
#include "data/SkillStructs.hpp"
#include <random>

/**
 * @brief Класс для расчетов боевой системы
 */
class CombatCalculator
{
  public:
    CombatCalculator();
    ~CombatCalculator() = default;

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
     * @brief Проверить критический удар
     * @param attackerAttributes Атрибуты атакующего
     * @return true если критический удар
     */
    bool rollCriticalHit(const std::vector<CharacterAttributeStruct> &attackerAttributes);

    /**
     * @brief Проверить блокирование
     * @param targetAttributes Атрибуты цели
     * @return true если атака заблокирована
     */
    bool rollBlock(const std::vector<CharacterAttributeStruct> &targetAttributes);

    /**
     * @brief Проверить промах
     * @param attackerAttributes Атрибуты атакующего
     * @param targetAttributes Атрибуты цели
     * @return true если атака промахнулась
     */
    bool rollMiss(
        const std::vector<CharacterAttributeStruct> &attackerAttributes,
        const std::vector<CharacterAttributeStruct> &targetAttributes);

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
     * @param damage Изначальный урон
     * @param defenseValue Значение защиты
     * @param damageType Тип урона (physical/magical)
     * @return Уменьшенный урон
     */
    int applyDefense(int damage, int defenseValue, const std::string &damageType);

  private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<float> dis_;
};
