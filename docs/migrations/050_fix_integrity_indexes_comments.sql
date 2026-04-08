-- ============================================================
-- Migration 050: Полный аудит целостности схемы
--   • Исправляем дефолты 0 → NULL в zone_event_templates
--   • Добавляем отсутствующие FK-ограничения
--   • Удаляем дублирующий индекс character_equipment
--   • Добавляем недостающие индексы
--   • Добавляем GIN-индекс на npc_dialogue.condition_group
--   • Добавляем комментарии ко всем таблицам и ключевым колонкам
-- ============================================================

BEGIN;

-- ===========================================================
-- 1. Очистка мусорных значений 0 → NULL до добавления FK
-- ===========================================================

-- zone_event_templates: 0 означало «не задано», должен быть NULL
UPDATE zone_event_templates SET game_zone_id               = NULL WHERE game_zone_id               = 0;
UPDATE zone_event_templates SET invasion_mob_template_id   = NULL WHERE invasion_mob_template_id   = 0;
UPDATE zone_event_templates SET invasion_champion_template_id = NULL WHERE invasion_champion_template_id = 0;

-- Меняем дефолты: 0 → NULL
ALTER TABLE zone_event_templates
    ALTER COLUMN game_zone_id                 SET DEFAULT NULL,
    ALTER COLUMN invasion_mob_template_id     SET DEFAULT NULL,
    ALTER COLUMN invasion_champion_template_id SET DEFAULT NULL;

-- ===========================================================
-- 2. Новые внешние ключи
-- ===========================================================

-- skills.scale_stat_id → skill_scale_type.id
ALTER TABLE skills
    ADD CONSTRAINT skills_scale_stat_id_fkey
    FOREIGN KEY (scale_stat_id) REFERENCES skill_scale_type(id);

-- skills.school_id → skill_school.id
ALTER TABLE skills
    ADD CONSTRAINT skills_school_id_fkey
    FOREIGN KEY (school_id) REFERENCES skill_school(id);

-- character_reputation.faction_slug → factions.slug
ALTER TABLE character_reputation
    ADD CONSTRAINT character_reputation_faction_slug_fkey
    FOREIGN KEY (faction_slug) REFERENCES factions(slug);

-- character_skill_mastery.mastery_slug → mastery_definitions.slug
ALTER TABLE character_skill_mastery
    ADD CONSTRAINT character_skill_mastery_mastery_slug_fkey
    FOREIGN KEY (mastery_slug) REFERENCES mastery_definitions(slug);

-- mob.faction_slug → factions.slug  (nullable)
ALTER TABLE mob
    ADD CONSTRAINT mob_faction_slug_fkey
    FOREIGN KEY (faction_slug) REFERENCES factions(slug);

-- npc.faction_slug → factions.slug  (nullable)
ALTER TABLE npc
    ADD CONSTRAINT npc_faction_slug_fkey
    FOREIGN KEY (faction_slug) REFERENCES factions(slug);

-- items.mastery_slug → mastery_definitions.slug  (nullable)
ALTER TABLE items
    ADD CONSTRAINT items_mastery_slug_fkey
    FOREIGN KEY (mastery_slug) REFERENCES mastery_definitions(slug);

-- respawn_zones.zone_id → zones.id
ALTER TABLE respawn_zones
    ADD CONSTRAINT respawn_zones_zone_id_fkey
    FOREIGN KEY (zone_id) REFERENCES zones(id);

-- timed_champion_templates.mob_template_id → mob.id
ALTER TABLE timed_champion_templates
    ADD CONSTRAINT timed_champion_templates_mob_template_id_fkey
    FOREIGN KEY (mob_template_id) REFERENCES mob(id);

-- zone_event_templates.game_zone_id → zones.id  (nullable)
ALTER TABLE zone_event_templates
    ADD CONSTRAINT zone_event_templates_game_zone_id_fkey
    FOREIGN KEY (game_zone_id) REFERENCES zones(id);

-- zone_event_templates.invasion_mob_template_id → mob.id  (nullable)
ALTER TABLE zone_event_templates
    ADD CONSTRAINT zone_event_templates_invasion_mob_id_fkey
    FOREIGN KEY (invasion_mob_template_id) REFERENCES mob(id);

-- zone_event_templates.invasion_champion_template_id → timed_champion_templates.id  (nullable)
ALTER TABLE zone_event_templates
    ADD CONSTRAINT zone_event_templates_invasion_champion_id_fkey
    FOREIGN KEY (invasion_champion_template_id) REFERENCES timed_champion_templates(id);

-- passive_skill_modifiers.attribute_slug → entity_attributes.slug
ALTER TABLE passive_skill_modifiers
    ADD CONSTRAINT passive_skill_modifiers_attribute_slug_fkey
    FOREIGN KEY (attribute_slug) REFERENCES entity_attributes(slug);

-- mob_active_effect.source_player_id → characters.id  (nullable, ON DELETE SET NULL)
ALTER TABLE mob_active_effect
    ADD CONSTRAINT mob_active_effect_source_player_id_fkey
    FOREIGN KEY (source_player_id) REFERENCES characters(id) ON DELETE SET NULL;

-- ===========================================================
-- 3. Удаление дублирующего индекса на character_equipment
-- ===========================================================
-- idx_character_equipment_char дублирует ix_character_equipment_char
DROP INDEX IF EXISTS idx_character_equipment_char;

-- ===========================================================
-- 4. Новые индексы
-- ===========================================================

-- FK-колонки, которые теперь стали FK-ограничениями
CREATE INDEX ix_mob_faction_slug            ON mob                    (faction_slug)            WHERE faction_slug IS NOT NULL;
CREATE INDEX ix_npc_faction_slug            ON npc                    (faction_slug)            WHERE faction_slug IS NOT NULL;
CREATE INDEX ix_items_mastery_slug          ON items                  (mastery_slug)            WHERE mastery_slug IS NOT NULL;
CREATE INDEX ix_respawn_zones_zone_id       ON respawn_zones           (zone_id);
CREATE INDEX ix_timed_champion_mob_tpl      ON timed_champion_templates (mob_template_id);
CREATE INDEX ix_zone_event_game_zone        ON zone_event_templates    (game_zone_id)            WHERE game_zone_id IS NOT NULL;
CREATE INDEX ix_zone_event_invasion_mob     ON zone_event_templates    (invasion_mob_template_id) WHERE invasion_mob_template_id IS NOT NULL;
CREATE INDEX ix_mob_active_effect_src_player ON mob_active_effect      (source_player_id)        WHERE source_player_id IS NOT NULL;

-- Дополнительные операционные индексы
CREATE INDEX ix_respawn_zones_default       ON respawn_zones           (zone_id, is_default)     WHERE is_default = true;
CREATE INDEX ix_zone_event_trigger_type     ON zone_event_templates    (trigger_type);
CREATE INDEX ix_npc_dialogue_cond_gin       ON npc_dialogue            USING GIN (condition_group)
    WHERE condition_group IS NOT NULL;

-- ===========================================================
-- 5. Комментарии к таблицам
-- ===========================================================

COMMENT ON TABLE character_bestiary IS
    'Бестиарий персонажа: сколько раз игрок убил каждый шаблон моба. Используется для разблокировки записей бестиария и potential pity-механик.';

COMMENT ON TABLE character_pity IS
    'Pity-счётчики редких дропов. Хранит количество убийств без выпадения конкретного предмета, чтобы гарантировать дроп при превышении порога (гарантированный лут).';

COMMENT ON TABLE character_reputation IS
    'Репутация персонажа у каждой фракции. Положительные значения = союзник, отрицательные = враг. Используется для диалоговых условий и доступа к контенту.';

COMMENT ON TABLE character_skill_mastery IS
    'Накопленные очки мастерства персонажа по типу оружия/школы. Например, sword_mastery растёт при ударах мечом и влияет на бонусы к урону.';

COMMENT ON TABLE character_titles IS
    'Титулы, заработанные персонажем. equipped=true означает, что этот титул отображается над именем в мире. Только один может быть активным одновременно.';

COMMENT ON TABLE damage_elements IS
    'Справочник элементов урона: fire, ice, physical, shadow, holy и т.д. PK — slug. Используется в mob_resistances и mob_weaknesses.';

COMMENT ON TABLE factions IS
    'Справочник фракций игрового мира. Мобы и NPC принадлежат фракции (faction_slug). Репутация персонажа ко фракции хранится в character_reputation.';

COMMENT ON TABLE item_class_restrictions IS
    'Ограничения предмета по классу персонажа. Если для предмета есть хотя бы одна запись — предмет может использовать только указанный класс.';

COMMENT ON TABLE item_set_bonuses IS
    'Сетовые бонусы: бонус к атрибуту, который даётся при надевании pieces_required предметов из одного сета. Несколько строк на сет для разных порогов.';

COMMENT ON TABLE item_set_members IS
    'Состав сетов: какие предметы входят в набор. Один предмет может быть только в одном сете.';

COMMENT ON TABLE item_sets IS
    'Именованные наборы предметов (сеты). Бонусы за сборку набора хранятся в item_set_bonuses.';

COMMENT ON TABLE item_use_effects IS
    'Эффекты, применяемые при использовании предмета (зелье, еда). is_instant=true → разовое мгновенное применение; false → эффект с длительностью и тиками.';

COMMENT ON TABLE mastery_definitions IS
    'Справочник типов мастерства оружия/магии (sword, bow, fire_magic и т.д.). PK — slug. max_value задаёт капу накопления.';

COMMENT ON TABLE mob_resistances IS
    'Сопротивления моба к элементам урона. Значение сопротивления задаётся логикой combat_calculator в chunk-server согласно записи в этой таблице.';

COMMENT ON TABLE mob_weaknesses IS
    'Уязвимости моба к элементам урона. При попадании атакой уязвимого элемента chunk-server применяет множитель урона.';

COMMENT ON TABLE respawn_zones IS
    'Точки возрождения персонажей в зонах. is_default=true — используется при первом входе или смерти без выбранной точки. Несколько точек на зону допустимо.';

COMMENT ON TABLE timed_champion_templates IS
    'Шаблоны мировых чемпионов с таймером спавна. Чемпион — усиленный моб, появляется с заданным интервалом в указанной зоне. next_spawn_at — unix-timestamp следующего спавна.';

COMMENT ON TABLE title_definitions IS
    'Каталог титулов. earn_condition — строковый ключ для логики выдачи на game-server. bonuses — JSON-массив модификаторов атрибутов.';

COMMENT ON TABLE zone_event_templates IS
    'Шаблоны мировых событий (вторжения, праздники, осады). При срабатывании trigger_type chunk-server клонирует шаблон в активное событие. invasion_* поля задают волну мобов.';

-- ===========================================================
-- 6. Комментарии к колонкам
-- ===========================================================

-- character_bestiary
COMMENT ON COLUMN character_bestiary.character_id     IS 'FK → characters.id. Персонаж-владелец записи бестиария.';
COMMENT ON COLUMN character_bestiary.mob_template_id  IS 'FK → mob.id. Шаблон моба (не runtime-инстанс).';
COMMENT ON COLUMN character_bestiary.kill_count       IS 'Суммарное количество убийств данного моба персонажем.';

-- character_pity
COMMENT ON COLUMN character_pity.character_id  IS 'FK → characters.id. Персонаж.';
COMMENT ON COLUMN character_pity.item_id       IS 'FK → items.id. Предмет с pity-механикой (редкий дроп).';
COMMENT ON COLUMN character_pity.kill_count    IS 'Счётчик убийств без выпадения данного предмета. Сбрасывается в 0 после получения предмета.';

-- character_reputation
COMMENT ON COLUMN character_reputation.character_id  IS 'FK → characters.id. Персонаж.';
COMMENT ON COLUMN character_reputation.faction_slug  IS 'FK → factions.slug. Фракция.';
COMMENT ON COLUMN character_reputation.value         IS 'Очки репутации. > 0 = союзник, < 0 = враг. Диапазон определяется дизайном.';

-- character_skill_mastery
COMMENT ON COLUMN character_skill_mastery.character_id  IS 'FK → characters.id. Персонаж.';
COMMENT ON COLUMN character_skill_mastery.mastery_slug  IS 'FK → mastery_definitions.slug. Тип мастерства.';
COMMENT ON COLUMN character_skill_mastery.value         IS 'Текущие накопленные очки мастерства. Ограничены mastery_definitions.max_value.';

-- character_titles
COMMENT ON COLUMN character_titles.character_id  IS 'FK → characters.id. Персонаж.';
COMMENT ON COLUMN character_titles.title_slug    IS 'FK → title_definitions.slug. Полученный титул.';
COMMENT ON COLUMN character_titles.equipped      IS 'TRUE = этот титул отображается над именем персонажа в игровом мире.';
COMMENT ON COLUMN character_titles.earned_at     IS 'Временная метка получения титула.';

-- damage_elements
COMMENT ON COLUMN damage_elements.slug  IS 'PK. Уникальный код элемента урона: physical, fire, ice, shadow, holy, arcane и т.д.';

-- factions
COMMENT ON COLUMN factions.id    IS 'Суррогатный PK.';
COMMENT ON COLUMN factions.slug  IS 'Уникальный код фракции. Используется как FK в mob, npc, character_reputation.';
COMMENT ON COLUMN factions.name  IS 'Отображаемое имя фракции.';

-- item_class_restrictions
COMMENT ON COLUMN item_class_restrictions.item_id   IS 'FK → items.id. Предмет с ограничением по классу.';
COMMENT ON COLUMN item_class_restrictions.class_id  IS 'FK → character_class.id. Класс, которому разрешён данный предмет.';

-- item_set_bonuses
COMMENT ON COLUMN item_set_bonuses.set_id          IS 'FK → item_sets.id. Набор, к которому относится бонус.';
COMMENT ON COLUMN item_set_bonuses.pieces_required IS 'Минимальное количество предметов набора для активации этого бонуса.';
COMMENT ON COLUMN item_set_bonuses.attribute_id    IS 'FK → entity_attributes.id. Атрибут, к которому прибавляется бонус.';
COMMENT ON COLUMN item_set_bonuses.bonus_value     IS 'Величина прибавки к атрибуту при активации бонуса.';

-- item_set_members
COMMENT ON COLUMN item_set_members.set_id   IS 'FK → item_sets.id. Набор.';
COMMENT ON COLUMN item_set_members.item_id  IS 'FK → items.id. Предмет, входящий в набор.';

-- item_sets
COMMENT ON COLUMN item_sets.id    IS 'Суррогатный PK.';
COMMENT ON COLUMN item_sets.slug  IS 'Уникальный код набора: используется в game-server и клиентском UI.';
COMMENT ON COLUMN item_sets.name  IS 'Отображаемое имя набора.';

-- item_use_effects
COMMENT ON COLUMN item_use_effects.item_id           IS 'FK → items.id. Предмет, за которым закреплён эффект.';
COMMENT ON COLUMN item_use_effects.effect_slug       IS 'Идентификатор эффекта (произвольный slug или ссылка на status_effects.slug).';
COMMENT ON COLUMN item_use_effects.attribute_slug    IS 'Атрибут-цель эффекта (ссылается на entity_attributes.slug).';
COMMENT ON COLUMN item_use_effects.value             IS 'Числовое значение изменения атрибута.';
COMMENT ON COLUMN item_use_effects.is_instant        IS 'TRUE = мгновенное применение (зелье). FALSE = длительный эффект.';
COMMENT ON COLUMN item_use_effects.duration_seconds  IS 'Продолжительность эффекта в секундах (0 для мгновенных).';
COMMENT ON COLUMN item_use_effects.tick_ms           IS 'Интервал тика в мс для периодических эффектов (0 для мгновенных).';
COMMENT ON COLUMN item_use_effects.cooldown_seconds  IS 'Кулдаун предмета после использования в секундах.';

-- mastery_definitions
COMMENT ON COLUMN mastery_definitions.slug             IS 'PK. Уникальный код типа мастерства: sword, bow, fire_magic и т.д.';
COMMENT ON COLUMN mastery_definitions.name             IS 'Отображаемое имя мастерства.';
COMMENT ON COLUMN mastery_definitions.weapon_type_slug IS 'NULL = общая мастерства; иначе — привязана к конкретному типу оружия.';
COMMENT ON COLUMN mastery_definitions.max_value        IS 'Максимальный уровень накопления очков мастерства (капа).';

-- mob_resistances
COMMENT ON COLUMN mob_resistances.mob_id        IS 'FK → mob.id. Шаблон моба.';
COMMENT ON COLUMN mob_resistances.element_slug  IS 'FK → damage_elements.slug. Элемент, к которому у моба есть сопротивление.';

-- mob_weaknesses
COMMENT ON COLUMN mob_weaknesses.mob_id        IS 'FK → mob.id. Шаблон моба.';
COMMENT ON COLUMN mob_weaknesses.element_slug  IS 'FK → damage_elements.slug. Элемент, к которому у моба есть уязвимость.';

-- mob_active_effect новая FK-колонка
COMMENT ON COLUMN mob_active_effect.source_player_id IS 'FK → characters.id. Персонаж, наложивший эффект на моба. NULL = эффект от зоны, квеста или системы.';

-- respawn_zones
COMMENT ON COLUMN respawn_zones.id          IS 'Суррогатный PK.';
COMMENT ON COLUMN respawn_zones.name        IS 'Отображаемое название точки возрождения.';
COMMENT ON COLUMN respawn_zones.x           IS 'X-координата точки возрождения.';
COMMENT ON COLUMN respawn_zones.y           IS 'Y-координата точки возрождения.';
COMMENT ON COLUMN respawn_zones.z           IS 'Z-координата точки возрождения.';
COMMENT ON COLUMN respawn_zones.zone_id     IS 'FK → zones.id. Зона, к которой принадлежит точка возрождения.';
COMMENT ON COLUMN respawn_zones.is_default  IS 'TRUE = эта точка используется по умолчанию при первом входе или смерти без явно выбранной точки.';

-- timed_champion_templates
COMMENT ON COLUMN timed_champion_templates.id                  IS 'Суррогатный PK.';
COMMENT ON COLUMN timed_champion_templates.slug                IS 'Уникальный код шаблона чемпиона.';
COMMENT ON COLUMN timed_champion_templates.zone_id             IS 'FK → zones.id. Зона, в которой появляется чемпион.';
COMMENT ON COLUMN timed_champion_templates.mob_template_id     IS 'FK → mob.id. Шаблон моба, на основе которого создаётся чемпион.';
COMMENT ON COLUMN timed_champion_templates.interval_hours      IS 'Интервал между спавнами чемпиона в часах.';
COMMENT ON COLUMN timed_champion_templates.window_minutes      IS 'Временное окно (в минутах) в котором чемпион может появиться после истечения интервала.';
COMMENT ON COLUMN timed_champion_templates.next_spawn_at       IS 'Unix timestamp (секунды) ближайшего возможного спавна. NULL = ещё не рассчитан.';
COMMENT ON COLUMN timed_champion_templates.last_killed_at      IS 'Временная метка последнего убийства чемпиона.';
COMMENT ON COLUMN timed_champion_templates.announcement_key    IS 'Ключ строки анонса для клиентского UI при появлении чемпиона. NULL = без анонса.';

-- title_definitions
COMMENT ON COLUMN title_definitions.id             IS 'Суррогатный PK.';
COMMENT ON COLUMN title_definitions.slug           IS 'Уникальный код титула. Используется как FK в character_titles.';
COMMENT ON COLUMN title_definitions.display_name   IS 'Отображаемое имя титула в UI.';
COMMENT ON COLUMN title_definitions.description    IS 'Описание способа получения/значения титула.';
COMMENT ON COLUMN title_definitions.earn_condition IS 'Строковый ключ условия получения. Обрабатывается логикой game-server (achievement_manager и т.п.).';
COMMENT ON COLUMN title_definitions.bonuses        IS 'JSON-массив бонусов: [{\"attribute\":\"slug\",\"value\":N}]. Применяются при активации титула.';

-- zone_event_templates
COMMENT ON COLUMN zone_event_templates.id                            IS 'Суррогатный PK.';
COMMENT ON COLUMN zone_event_templates.slug                          IS 'Уникальный код шаблона события.';
COMMENT ON COLUMN zone_event_templates.game_zone_id                  IS 'FK → zones.id. Зона, в которой происходит событие. NULL = глобальное событие.';
COMMENT ON COLUMN zone_event_templates.trigger_type                  IS 'Способ запуска: manual (GM-команда), timed (по расписанию), random (случайный по вероятности).';
COMMENT ON COLUMN zone_event_templates.duration_sec                  IS 'Продолжительность активного события в секундах.';
COMMENT ON COLUMN zone_event_templates.loot_multiplier               IS 'Множитель вероятности дропа во время события (1.0 = норма).';
COMMENT ON COLUMN zone_event_templates.spawn_rate_multiplier         IS 'Множитель скорости спавна мобов (1.0 = норма).';
COMMENT ON COLUMN zone_event_templates.mob_speed_multiplier          IS 'Множитель скорости движения мобов (1.0 = норма).';
COMMENT ON COLUMN zone_event_templates.announce_key                  IS 'Ключ строки анонса для клиентского UI при старте события. NULL = без анонса.';
COMMENT ON COLUMN zone_event_templates.interval_hours                IS 'Интервал повторения события в часах (0 = не повторяется). Применяется при trigger_type=timed.';
COMMENT ON COLUMN zone_event_templates.random_chance_per_hour        IS 'Вероятность случайного запуска в час (0.0–1.0). Применяется при trigger_type=random.';
COMMENT ON COLUMN zone_event_templates.has_invasion_wave             IS 'TRUE = событие сопровождается волной вторжения мобов.';
COMMENT ON COLUMN zone_event_templates.invasion_mob_template_id      IS 'FK → mob.id. Шаблон моба-захватчика. NULL = нет вторжения.';
COMMENT ON COLUMN zone_event_templates.invasion_wave_count           IS 'Количество волн вторжения.';
COMMENT ON COLUMN zone_event_templates.invasion_champion_template_id IS 'FK → timed_champion_templates.id. Чемпион, появляющийся в финальной волне. NULL = без чемпиона.';
COMMENT ON COLUMN zone_event_templates.invasion_champion_slug        IS 'Slug чемпиона (дублирует FK для runtime без JOIN). NULL = без чемпиона.';

-- items
COMMENT ON COLUMN items.mastery_slug IS 'FK → mastery_definitions.slug. Требуемый тип мастерства для использования/экипировки предмета. NULL = без требований к мастерству.';

COMMIT;
