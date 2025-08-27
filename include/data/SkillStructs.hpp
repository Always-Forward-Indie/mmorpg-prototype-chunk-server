#pragma once
#include <string>
#include <vector>

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
    int cooldownMs = 0;    // Время восстановления в мс
    int gcdMs = 0;         // Глобальный КД в мс
    int castMs = 0;        // Время каста в мс
    int costMp = 0;        // Стоимость маны
    float maxRange = 0.0f; // Максимальная дальность
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
