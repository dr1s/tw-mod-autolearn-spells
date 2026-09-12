# tw-mod-autolearn-spells

Automatically teaches class spells on level-up and catches up missing spells when a player logs in.

## Configuration

Copy `conf/tw-mod-autolearn-spells.conf.dist` to your module config directory as `tw-mod-autolearn-spells.conf` and adjust:

```ini
[AutoLearnSpells]
AutoLearnSpells.Enable = 1
AutoLearnSpells.CatchUpOnLogin = 1
AutoLearnSpells.MaxLevel = 60
```

- `Enable`: master toggle. Set to `0` to disable the module.
- `CatchUpOnLogin`: learn all missing spells for the player's current level on login.
- `MaxLevel`: do not learn spells beyond this level.

## How it works

The module registers a `PlayerScript` that hooks `OnLogin` and `OnLevelChanged`.

For each level in the requested range:

1. A small hardcoded table applies spells that are not correctly level-tagged in the database (`Dual Wield`, `Parry`, `Pick Lock`, `Mail`/`Plate Mail`).
2. The spell store is scanned. A spell is learned when all of the following are true:
   - It is listed in `skill_line_ability` for the player's class and race.
   - `skill_line_ability.learn_on_get_skill` is `0` (trainer spell) or `2` (level/skill spell).
   - `spell_template.baseLevel` matches the level being processed.
   - It uses the player's spell family, or it is `SPELLFAMILY_GENERIC` and uses one of the class's own skill lines.
   - It is not a talent and the previous rank (if any) is already known.
3. Hunter pet abilities and other non-player spells are rejected because they use non-class skill lines.
4. On login, shamans are given the four totem items if they are missing.

This bypasses normal trainer and quest requirements (e.g. warrior stance quests, paladin/warlock mount quests, shaman totem quests), which is the intended behavior for a Cataclysm-style auto-learn module.

## Build

```sh
cmake -S . -B build -DMODULES=static
cmake -S . -B build -DMODULES=dynamic
# or per-module:
cmake -S . -B build -DMODULE_TW_MOD_AUTOLEARN_SPELLS=static
```

## Attribution

Based on the AzerothCore module [mod-learn-spells](https://github.com/azerothcore/mod-learn-spells).

Spell levels and class/race availability were verified against the live Turtle WoW world database (`tw_world.skill_line_ability` and `tw_world.spell_template`).

## License

This module is licensed under the [GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html) (AGPL-3.0), the same license as the AzerothCore `mod-learn-spells` module it is derived from and the Tortoise-WoW project as a whole.

The full license text is available in the project's top-level `LICENSE` file.
