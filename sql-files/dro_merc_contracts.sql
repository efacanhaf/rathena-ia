-- DimensionsRO Phase 6.2 — adventurer hire contract persistence.
--
-- Why: ai-server keeps merc state in process memory only (g_shells_local +
-- g_merc_by_owner). On ai-server crash/restart, every active merc vanishes
-- but the player's char_reg_num.ADV_HIRED_UNTIL is still set, leaving the
-- account stuck (NPC anti-stack rejects re-hire) and the zeny they paid
-- gone. This table records every hire so that:
--   - ai-server startup can re-spawn for online chars,
--   - char login on map-server can detect orphan (contract present but
--     ai-server has no merc) and refund the unused fraction,
--   - Treinador NPC can show real contract status.
--
-- One row per active contract. Deleted by:
--   - successful @dismiss (full cleanup),
--   - successful expiry (ai-server cron when expires_at <= now),
--   - refund flow on char login (after compensating the player).
--
-- char_id is the PK because anti-stack already enforces one merc per char.
-- account_id stored alongside for cross-char queries (e.g. account-level
-- leaderboards) without joining to char.

CREATE TABLE IF NOT EXISTS `dro_merc_contracts` (
  `char_id`        INT(11) UNSIGNED   NOT NULL,
  `account_id`     INT(11) UNSIGNED   NOT NULL,
  `role`           TINYINT(3) UNSIGNED NOT NULL,        -- 0=support, 1=tank
  `job`            SMALLINT(5) UNSIGNED NOT NULL,
  `tier`           TINYINT(3) UNSIGNED NOT NULL,
  `base_level`     SMALLINT(5) UNSIGNED NOT NULL,
  `job_level`      SMALLINT(5) UNSIGNED NOT NULL,
  `paid_zeny`      INT(11) UNSIGNED   NOT NULL DEFAULT 0,
  `paid_voucher`   TINYINT(1)         NOT NULL DEFAULT 0,
  `duration_min`   SMALLINT(5) UNSIGNED NOT NULL,
  `hired_at`       INT(11) UNSIGNED   NOT NULL,         -- unix ts
  `expires_at`     INT(11) UNSIGNED   NOT NULL,         -- unix ts; 0 = no expiry
  PRIMARY KEY (`char_id`),
  KEY `idx_account` (`account_id`),
  KEY `idx_expires` (`expires_at`)
) ENGINE=InnoDB;
