#pragma once
#include <string>
#include <vector>

/**
 * @brief Describes one effect that a skill applies when cast.
 *
 * For "damage" / "heal" skills the effects list is typically empty — the
 * CombatCalculator handles the value.  Use this struct for buff, debuff,
 * DoT, and HoT entries that must land as ActiveEffectStructs on the target.
 *
 * Populated by the game-server at startup via the skillsData JSON array and
 * parsed in JSONParser.
 */
struct SkillEffectDefinitionStruct
{
    std::string effectSlug;     ///< unique slug, e.g. "poison_dot", "str_buff"
    std::string effectTypeSlug; ///< "buff" | "debuff" | "dot" | "hot"
    std::string attributeSlug;  ///< target attribute, e.g. "hp", "strength"
    float value = 0.0f;         ///< stat modifier (buff/debuff) or per-tick amount (dot/hot)
    int durationSeconds = 0;    ///< 0 = permanent (passive only); >0 = timed
    int tickMs = 0;             ///< 0 = non-tick; >0 = DoT/HoT interval in ms
};

// Структура для скила персонажа/моба
struct SkillStruct
{
    std::string skillName = "";       // Название скила
    std::string skillSlug = "";       // Slug скила
    std::string scaleStat = "";       // От какого атрибута зависит
    std::string school = "";          // Школа магии (physical, fire, ice, etc.)
    std::string skillEffectType = ""; // Тип эффекта (damage, heal, buff, etc.)
    int skillLevel = 1;               // Уровень скила

    // Характеристики урона
    float coeff = 0.0f;   // Коэффициент масштабирования
    float flatAdd = 0.0f; // Плоская добавка

    // Характеристики скила
    int cooldownMs = 0;             // Время восстановления в мс
    int gcdMs = 0;                  // Глобальный КД в мс
    int castMs = 0;                 // Время каста в мс
    int costMp = 0;                 // Стоимость маны
    float maxRange = 0.0f;          // Максимальная дальность
    float areaRadius = 0.0f;        // Радиус AoE (0 = single target)
    int swingMs = 300;              // Длительность анимации свинга (после каста), мс
    std::string animationName = ""; // Название анимационного клипа кастера (Unity Animator state)

    // Passive skill flag: if true the skill is always-on and never cast actively.
    // Its modifiers arrive as ActiveEffectStruct entries (sourceType="skill_passive", expiresAt=0).
    bool isPassive = false;

    /**
     * @brief List of effects this skill applies on the target when executed.
     *
     * - "damage" / "heal" skills: typically empty — CombatCalculator handles value.
     * - "buff" / "debuff" skills: stat modifiers to land on caster or target.
     * - "dot" / "hot" skills: per-tick amounts with durationSeconds and tickMs.
     * - Passive skills: permanent stat modifiers (durationSeconds=0), applied
     *   automatically on character join and re-applied after respawn.
     *
     * Populated from the game-server skillsData["effects"] array.
     */
    std::vector<SkillEffectDefinitionStruct> effects;
};

// Структура для атрибута entity (персонажа или моба)
struct EntityAttributeStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

// Структура для расчета урона
struct DamageCalculationStruct
{
    int baseDamage = 0;
    int scaledDamage = 0;
    int totalDamage = 0;
    bool isCritical = false;
    bool isBlocked = false;
    bool isMissed = false;
    std::string damageType = ""; // physical, magical, true
};

// Структура для передачи скилов персонажа с ID
struct CharacterSkillStruct
{
    int characterId = 0;
    SkillStruct skill;
};

// Структура для результата использования скила
struct SkillUsageResult
{
    bool success = false;
    std::string errorMessage = "";
    DamageCalculationStruct damageResult;
    int healAmount = 0;
    std::vector<std::string> appliedEffects;
};
