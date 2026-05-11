# DimensionsRO — Changelog

All notable changes to the DimensionsRO server (rAthena fork) are tracked here.

Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---


## 2026-05-11

### Adicionado — Receita Lite da Runa Lux Anima

- **Nova receita alternativa** para a **Runa Lux Anima** do Rune Knight: agora pode ser forjada com **1 Gold + 3 Light Granule + 3 Runa Catalisadora** (em vez de 3 Gold + 3 Light Granule). A receita original continua valendo para quem prefere queimar Gold sobrando.
- **Runa Catalisadora** é um material novo, comprável no novo NPC **Damontiê** (prontera 144,284) a **3 Booster Coin cada**. Item ligado ao char (não pode trade, drop ou vender).
- **Por quê:** Gold continuou impraticável mesmo após o buff de 2026-05-07. Em vez de empilhar buff em cima de buff de drop, agora a Lux Anima ganha um caminho previsível via Booster Coin (das instâncias), preservando o prestígio da receita original.

### Adicionado — Damontiê (Prontera 144,284)

Novo NPC vende em troca de Booster Coin:

| Item | Custo | Efeito |
|---|---|---|
| Runa Catalisadora | 3 BC | Material da Lux Anima Lite |
| Battle Manual | 5 BC | +100% EXP por 30 min |
| Field Manual 25% | 1 BC | +25% EXP por 30 min |
| Bubble Gum | 5 BC | +100% drop por 30 min |


## 2026-05-11

### Adicionado — Booster Coin como recompensa de instances

- Toda instance (51 no total) agora dá **Booster Coin** ao derrotar o boss
  final, tieradas por dificuldade:
  - **Easy (2 BC)**: Bakonawa, Bangungot, Buwaya, Eclage, Hazy Forest, Isle of
    Bios, Malangdo Culvert, Octopus, Sealed Shrine, Orcs Memory
  - **Medium (5 BC)**: Airship Assault, Charleston, Deep Forest, Devil Tower,
    EDDA, Faceworms, Friday/Weekend Dungeon, Ghost Palace, Heart Hunter,
    Hidden Garden, Horror Toy, Infinite Space, Last Room, Lost Farm, Maze of
    Oz, Morse Cave, Nightmarish Jitterbug, Os Occupation, Ritual of Blessing,
    Room of Consciousness, Sara Memory, Sealed Os, Sky Fortress, Villa de
    Deception/HighPriest (Normal), Water Garden, Nydhogg, Endless Tower
  - **Hard (10 BC)**: Central Lab, Cor Operation, Regenschirm, Sarah and Fenrir,
    Temple of Demon God, Werner Lab, Wolves, OGH Normal e Hard
  - **Mega (20 BC)**: Villa of Deception Hard
- **Cap diário: 30 Booster Coin por conta.** Reseta na virada do dia. Se voce
  atingir o cap, o servidor avisa e os Coins extras nao dropam.
- **Compartilhado pela party humana** no mapa do boss (aventureiros IA nao
  recebem). Cada player tem o proprio cap diario.

### Alterado — Nyangvine de MVP virou estocastico com pity

- Matar um **MVP do mundo aberto** agora da **10% de chance** de dropar 1
  Nyangvine (antes era garantido toda vez).
- **Pity em 10 kills**: se voce matar 10 MVPs sem dropar, o 10o e
  garantido. Reseta no drop. Contador per-conta.
- **MVPs de instance NAO dropam Nyangvine** (ja pagam via Booster Coin).
  Os MVPs do MVP Tracker continuam sendo a referencia de "MVPs do mundo".

### Alterado — Aventureiros IA

- Varios crashes de comandos GM (`@kick`, `@recallall`, etc) em aventureiros
  foram corrigidos. Servidor nao cai mais quando voce usa esses comandos
  com aventureiros perto.
- **Kill credit**: quando o Tanker IA da o killing blow, o credito agora vai
  pro **dono humano** — quest progress, MVP drops, EXP, achievement. Antes o
  kill era perdido.
- **Auto-restore**: depois de qualquer reinicializacao do servidor, os
  aventureiros voltam sozinhos em ate 15 segundos. Nao precisa mais relogar
  pra traze-los.
- **Instances**: aventureiros entram corretamente em instances com o dono.
  Antes ficavam pra tras.
- **Follow apertado**: aparecem em 3x3 ao redor do dono ao warpar (era 9x9).
- **Portais NPC**: aventureiros agora atravessam warp tiles igual o player.
- **Uniforme Tank**: paleta de cor do Royal Guard corrigida.

### Adicionado — Chat dos aventureiros em PT-BR

- ~295 linhas espontaneas dos aventureiros (cumprimentos, brincadeiras,
  reacoes) traduzidas em 14 categorias. Em vez de ingles, agora falam em
  portugues coloquial brasileiro.


## 2026-05-10

### Adicionado — Sistema de Aventureiros (mercenários)

- Novo NPC **Treinador** em `prt_in (44, 113)` que contrata um Aventureiro
  pra te seguir e ajudar na aventura. Cada personagem pode ter **dois
  aventureiros simultaneamente — um Suporte e um Tanker**, contratados
  separados.
- Os aventureiros vêm com classe e nível **espelhando o líder da party**
  (ou o seu, se solo), aparecem na barra de party com HP/SP visíveis e
  podem ser dispensados pelo NPC ou via `@dismiss tank` / `@dismiss support`.
- **Persistência completa**: dura quanto durou o contrato, sobrevive a
  logout, troca de mapa e restart do servidor — quando o servidor volta,
  os aventureiros vivos no momento do crash são restaurados automaticamente
  ao seu lado (sem precisar recontratar nem refund).

### Adicionado — Aventureiro Tanker (Royal Guard)

- Tanker linha de **Cavaleiro Real** (Sw → Crusader → Paladin → Royal
  Guard) com mount automática e auto-buffs (Auto Guard, Endure, Reflect
  Shield, Force of Vanguard, Prestige).
- **Posicionamento ativo**: fica 5 células à frente do líder usando
  predição de movimento, mas sai dessa formação assim que entra em
  combate pra cobrir o que o líder está atacando.
- **Geração de aggro sem auto-attack**: o tanker spamma **Provoke** pra
  pegar threat (pula em Undead/MVP, onde Provoke não funciona),
  **Over Brand** quando há 2+ mobs em volta do alvo, e **Banishing Point**
  pra single-target. Não dá tapa — toda a barra de damage vem das skills.
- **Defesa do líder**: usa **Devotion** pra absorver o dano, **Heal** se
  o HP do líder cai, e engaja no mob que machucou o líder mesmo que o
  líder não esteja em auto-attack (skills de dano também ativam o tank).

### Adicionado — Aventureiro Suporte (Arch Bishop)

- Suporte linha de **Acólito** com rotação de cura/buff completa: AL_HEAL,
  Blessing, Increase AGI, Kyrie Eleison, Aspersio, Magnificat, Assumptio,
  Sacrament, Expiatio, Highness Heal, Coluna do Senhor, etc.
- **Cura e buff em qualquer membro da party** (incluindo o tanker
  irmão): se o líder está OK mas o tanker está machucado, o suporte cura
  o tanker; se o líder não tem Blessing mas o tanker tem, o suporte vai
  buffar o tanker. **Resurrection** também — ressuscita líder, tanker, ou
  qualquer membro morto da party (no mesmo mapa).
- **Filtro de mapa**: só considera membros da party que estão no **mesmo
  mapa** que o aventureiro — não tenta curar quem está em outra área.

### Alterado — Visual dos Aventureiros

- Todos os aventureiros usam um uniforme fixo (independente da classe
  base): **Adventurer Hat** (top), **American S Hair** (mid), **Adventurer
  Map** (bottom), **Adventure Cat Bag** (manto).
- **Palette do corpo distingue a função**: Suporte usa palette 5,
  Tanker usa palette 3 — fácil identificar à distância no campo.

### Alterado — Comportamento de respawn ao morrer

- Quando o aventureiro morre, ele cai como cadáver no chão (não fica de
  pé sem vida) e só ressuscita após **30 segundos fora de combate** do
  líder.
- Se a party inteira no mapa estiver morta, o aventureiro **não
  ressuscita** — espera alguém da party voltar com HP > 0 antes de
  reaparecer (evita o tank renascer dentro do wipe e re-morrer).

## 2026-05-09

### Alterado — Recompensa de MVP em Nyangvine

- Cada **MVP derrotado** agora dá **1 Nyangvine** com mensagem de
  confirmação. Antes: a recompensa era anunciada como "1 a cada 100 MVPs",
  mas o gatilho nunca disparava nas linhas de spawn oficiais do rAthena
  (label vazia conflitando com sufixo `,1` de tamanho/AI). Trocado por
  detecção nativa via tipo de boss; agora funciona para todos os MVPs
  spawnados normalmente.
- O contador `#DRO_MVP_KILLS` continua somando por conta (histórico/
  futuro leaderboard) mas não é mais o gate da recompensa.

### Adicionado — Old Glast Heim Advanced (Hard Mode)

- Nova instância **Old Glast Heim Advanced** disponível para personagens
  nível **160+** que já completaram o OGH Normal pelo menos uma vez.
  Entrada por **Another Hugin** em `glast_01 (179, 283)`.
- Tempo limite **1h30m**. Cooldown de 3 dias (compartilhado com o Normal).
- Mesmo fluxo do OGH Normal — Varmundt, Heinrich, cutscenes, salas oeste e
  leste — mas com mobs e MVPs escalados:
  - 13 monstros novos (sufixo `_H`), level 160–180, 3-5x HP do Normal.
  - **Amdarais** (Lv 180, 42,9M HP) e **Corruption Root** (Lv 180, 18,2M HP)
    como MVPs.
  - Drops melhorados: **Temporal Crystal**, **Coagulated Spell**, **Polluted
    Spell**, **Patriotism Marks**, e cards próprios (`AmdaraisH_Card`,
    `CorruptionRootH_Card`).
- O Hugin original em `glast_01 (204, 273)` continua oferecendo o Normal —
  Another Hugin existe especificamente para o modo Hard.

## 2026-05-08

### Adicionado — Sistema de Economia (Cambista, Temporal Tina)

- **Quatro moedas oficializadas**: Zeny (dia-a-dia), Booster Coin (skill PvE,
  drop em instâncias), Nyangvine (lealdade/conveniência), Cash (cosméticos/QoL).
- **Cambista** (prontera 166,290): converte Booster Coin → Nyangvine
  (50:1, máximo 5 conversões/semana) e Nyangvine → Cash (1:10, sem cap).
  Cash não converte em outras moedas — comprar Cash gasta no Cash Shop.
- **Temporal Tina** (prontera 145,293): vende Episode Pass tickets em troca
  de Nyangvine via UI nativa de barter. Ep14/15: 8 Nyan, Ep16: 10 Nyan,
  Ep17: 12 Nyan.
- **Cash Shop** (ALT+M, aba Other): vende os mesmos tickets em Cash com
  preços paralelos (Ep14/15: 80 Cash, Ep16: 100, Ep17: 120).
- **Daily login bonus**: +2 Cash no primeiro login do dia (acumula no
  saldo da conta).
- **MVP counter para Nyangvine**: a cada 100 MVPs derrotados, o player
  ganha 1 Nyangvine. Determinístico, contador por conta.

### Adicionado — Valkyrie (Pular Episódios)

- **Novo NPC: Valkyrie** (prt_cas 373,77 — Castelo de Prontera). Aceita
  os **Passes de Episódio** (oficiais kRO/iRO) e marca todas as quests do
  episódio correspondente como concluídas, destravando NPCs, dungeons,
  shops e instâncias gateadas.
- Tickets aceitos: **Passe Ep 14** (Lighthalzen, Bifrost, Eclage, Dewata,
  Malangdo, Malaya), **Passe Ep 15** (Phantasmagorika, Reload), **Passe
  Ep 16** (Banquete dos Heróis, Charleston/Verus) e **Passe Ep 17**
  (Terra Gloria/Illusion; quando 17.2 estiver disponível, o mesmo passe
  cobrirá Mansion da Tragédia).
- O **skip não concede EXP, drops ou recompensas** das quests puladas —
  apenas marca como completas. Funciona **por personagem** (cada char usa
  seu próprio passe).
- Custo: **1 passe** por uso. Os passes podem ser obtidos via Cash Shop
  (ALT+M), via Temporal Tina em Prontera (Nyangvine), ou como drop de
  Booster Packs (a versão "Clear Ticket" é equivalente à "Pass Ticket"
  e a Valkyrie aceita ambas).

### Adicionado — Sistema de Aventureiros

- **Novo NPC: Treinador de Aventureiros** (prt_in 44,113). O líder da party
  pode contratar um Aventureiro de suporte para acompanhar o grupo.
- O Aventureiro é um companheiro controlado pelo servidor que **segue o
  contratante, cura, buffa, ressuscita e remove status negativos** da party
  inteira. **Não ataca por conta própria** — foco 100% em suporte.
- A **classe** do Aventureiro é definida automaticamente pelo nível do líder:
  - até 39 → Acólito
  - 40 a 69 → Sacerdote
  - 70 a 98 → Sumo Sacerdote
  - 99+ → Arcebispo
- O nível do Aventureiro **acompanha o nível do líder** (até 175).
- O Aventureiro **entra na party automaticamente** ao spawnar e segue você
  através de teleportes, portais e mudanças de mapa.
- Se o Aventureiro morrer, ele **espera ser ressuscitado**. Se o líder
  voltar ao ponto de retorno, o Aventureiro respawna ao seu lado.
- Se você deslogar, ele desaparece e **reaparece automaticamente no próximo
  login do mesmo personagem** — o tempo offline não conta no contrato.
- **Custo (zeny):** nível × 100z por minuto. Opções de 5, 30 ou 60 minutos.
- **Comandos:** `@dismiss` para dispensar antes do tempo acabar.

### Adicionado — Voucher de Aventureiro

- Novo item: **Voucher de Aventureiro** (id 1270150). Quando você tem um
  na bolsa, a opção de **60 minutos no Treinador de Aventureiros** vira
  pagamento por 1 Voucher (sem custo de zeny).
- O voucher é **untradeable** (não dropa, não troca, não vende), mas pode
  ser guardado no storage e cart.

## 2026-05-07

### Mudado — Drop de Gold

- **Tier comum (Dokebi, Mineral, Grand Peco, Mi Gao, Plasma, Miming, Pot Dofle, Cat o' Nine, Leib Olmai, Am Mut, Creepy Demon e variantes Solid/Ringleader/Furious/Elusive/coloridos):** drop de Gold subiu de 0,15% para **1,0%** (~6,7×). Vale também pros Mineral coloridos da Bio4.
- **Golden Thief Bug** agora dropa Gold a **50%** (era 100% capado).
- **Golden Savage** agora dropa Gold a **75%** (era 37,5%).
- **Por quê:** craftar Lux Anima (3 Gold + 3 Light Granule) era gargalo só com a Aurora (paramk, 1M zeny, ~20/dia, fechado quarta). Agora dá pra farmar.

### Adicionado — Resgate de Level Up Tickets

- A **Mega Booster Support** (prt_in 37,95) agora aceita os **Level Up Tickets** que saem dos Booster Boxes. Cada ticket sobe **+1 BaseLevel** dentro da faixa correspondente:
  - **Lv Up Ticket 80**: 80 ≤ BaseLevel ≤ 89
  - **Lv Up Ticket 90**: 90 ≤ BaseLevel ≤ 98
  - **Lv Up Ticket 150**: 150 ≤ BaseLevel ≤ 169
- Fora da faixa o ticket não funciona (sem stacking gratuito acima do limite). Pode acumular tickets e usar 1 por vez.

### Corrigido — Sunken Tower

- **Dimensional Device não trava mais para membros não-líder.** Antes, quando um membro da party que não era o líder clicava no Device, o NPC abria o diálogo e ficava sem opção de fechar. Agora ele recebe uma mensagem informando que apenas o líder avança o floor, e o diálogo fecha normal.
- **Cooldown de 3 dias agora é aplicado na criação da instance**, não na conclusão. Isso fecha o exploit de destruir a instance cedo pra resetar o cooldown. O cooldown é stampado em todos os membros online da party no momento em que o líder cria a instance com a Leticia.
- **Verificação de level dos membros da party no momento da criação.** A diferença de Base Level entre membros online tem que ser compatível com a regra de split de EXP (≤15 levels). Se houver alguém fora da faixa, a Leticia bloqueia a criação. Fecha o exploit de criar com um líder de level baixo e ter chars fortes clearando os mobs.
- Removida a opção de teste de GM do menu da Leticia.

## 2026-05-06

### Mudado
- **Booster Box e Premium Booster Box agora podem ser abertas em qualquer level.** A restrição original de Base Level 10 foi removida — o jogador pode segurar a caixa até quando quiser antes de abrir.

## [1.0.0] — 2026-04-30 (atualizado 2026-05-06)

### Booster pack rework + ep17.1 lock + VIP lockdown (2026-05-06)

- **Mega Booster Support NPC** (prt_in,37,95): reduzido a 1 item por char.
  VIP recebe 103048 (Booster Call Package especial), não-VIP recebe
  101538. Toda a lógica antiga de tier/variant/seleção foi removida.
- **Centro#booster** (prontera 166,300) slot 5: trocado `Boost_Up_2`
  (100044) por `Booster_W_Up_1` (100338, Booster Weapon Phase 1 Upgrade
  Package). Mesmo custo de 20 Booster_Coin.
- **Booster Pack distribution**: o `Booster_W_Up_3` saiu da box do nível
  100. As três phases agora vivem em boxes específicas:
  - Box 130 → **Booster_W_Up_1** (Phase 1, refine até +9, max base +8)
  - Box 150 → **Booster_W_Up_2** (Phase 2, refine até +11, max base +10)
  - Box 170 → **Booster_W_Up_3** (Final, refine até +13, max base +12)
  - Box 100 mantém o **Booster_W_Ticket** (selector das 18 armas Booster
    no barter Centro).
- **Episode 17.1 lock automatizado**: novo NPC `dro_ep17_1_lock_state`
  seta `$@ep17_1_lock=1` no OnInit. Carregado via
  `npc/re/scripts_post_ep17_1.conf` (mesmo conf que controla a parte
  script-side do lock). Item `Booster_Pack_170` (101498) foi gateado
  via `if (!$@ep17_1_lock)` para não entregar `Booster_Pack_180`
  enquanto o lock estiver ativo. Quando o lock for retirado (comentar
  o import em `scripts_main.conf`), a flag fica em 0 automaticamente
  e o pack do 180 volta a cair sem precisar reeditar nada.
- **Tickets VIP** (1270142/1270143/1270144 — 7d/15d/30d): completaram
  todas as 9 trade flags (NoDrop, NoTrade, TradePartner, NoSell, NoCart,
  NoStorage, NoGuildStorage, NoMail, NoAuction). Não-GMs não conseguem
  jogar/vender/storage/cart/etc. Itens client-side ganharam descrição
  custom + sprite VIP_Black_Card e foram adicionados ao
  `data\ItemMoveInfoV5.txt` para visualização correta da restrição
  no tooltip do inventário.

### Drop & EXP rate balance — wave 2 (2026-05-05 noite)

- **Common/Heal/Use**: 5x/5x/10x → **15x/15x/20x** (normal/mini/mvp).
- **Equip**: 5x/10x/15x → **20x/15x/10x** (invertido — mais drop em normal,
  menos em mvp). Justificativa: end-game equip era trivial em mvp; gear
  baseline (normal mobs) precisava de mais frequência.
- **`atcommand_mobinfo_type: 0`**: `@mi`/`@whodrops` agora mostram rate
  base do mob (sem `pc_level_penalty_mod` aplicado no display). Drop real
  continua afetado pelo level penalty Renewal — só o display que era
  confuso (mostrava rate efetivo pro char, parecia bug).
- **Cor Core (25723 / EP17_1_EVT39)** com `Ratio: 30000` em
  `mob_item_ratio.yml` — 300× o rate nativo (clampado em 100% via
  `item_drop_common_max`). Pra acelerar farm de Cor Core sem mexer no
  multiplier global.

### Sunken Tower (Ep.18 instance) — DimensionsRO custom

NPC `Leticia` em `alberta 214,74` cria uma instance no map `1@ch_u`, baseada
na spec oficial iRO Wiki/Sunken_Tower com ajustes pra DRO (cap lvl 175,
Booster Coin reward custom).

- **5 zonas walkable** detectadas no map_cache (X=4-51, 88-135, 176-223,
  262-309, 346-393, todas Y=14-63). Floor cycle `(floor-1) % 5` cicla pelas
  zonas — estilo EndlessTower.
- **Mob count por floor**: 25/30/35/40/50 (cycle), via `areamonster` confinada
  ao bounding box da zona ativa. Counter usa `mobcount(map, event_label)`
  (não contador manual) — `killmonster` em `OnNextFloor` limpa residuais.
- **11 ranks (R0–R10)** mapeados por BaseLevel oficial (40-249, ranks 7-10
  inacessíveis no cap DRO 175 mas mantidos pra futura expansão). Cada rank
  tem mob_db próprio (3960-3970) com level=range_min+14, BaseExp/JobExp=0,
  sem skills/drops, stats bakeados (HP/ATK/DEF/HIT/FLEE/STR escalonados).
- **Stats no mob_db** (não setunitdata): rAthena `status_calc_mob_` libera
  `base_status` quando flag=0 sem `mobs_level_up`, fazendo `setunitdata
  UMOB_*` virar lixo. Solução: 1 mob por rank com stats fixos. Apenas
  `UMOB_CLASS` (sprite, visual-only) randomizado — pool de 132 sprites
  oficiais documentados na iRO Wiki.
- **5 Dimensional Devices** (sprite `4_RUNESTONE`), um por zona. Disabled
  por default; habilitados em `OnMobDead` quando `mobcount < 1`. Sem check
  de `'st_floor_clear` no clique (impossível clicar invisível).
- **EXP custom** via `getexp` em `OnMobDead` (per-mob = `mob_exp_d1[rank]
  * (100 + (diff-1)*20) / 100`). Bonus de 5th floor: `bonus_d1[rank]
  * (100 + (diff-1)*100) / 100` + 1 Meteorite Dust garantido + roll de
  `dust_chance` pra extras (ranks 8-10) + `max(rank,1) * diff` Booster
  Coins.
- **Completion reward** (OnTimer1800000, 30min): `max(rank,1) * diff`
  Booster Coins por player no map. Warp pra alberta + destroy instance.
- **Difficulty 1-5**: upgrade custa 10 Meteorite Dust por nível (Diff 1
  sempre grátis). Multiplica EXP/Booster Coin/Dust chance.
- **Cooldown 3 dias** per-character (`#SunkenTower_LastRun`). GM Reset
  na Leticia bypassa pra teste.
- **Rank passing**: globals `$@ST_pending_rank/diff` setados antes de
  `instance_create`, lidos em `OnInstanceInit` (síncrono) — evita bug onde
  spawn fira antes do player carregar (quando dependia de OnPCLoadMapEvent).

### Booster Coin barter NPCs (Prontera)

Implementação dos NPCs oficiais kRO Booster Event via `barters.yml`
(BARTER_DB) — usa shop UI nativo com item-currency, sem cash shop dialog.

- **Centro (166,300)** sprite `4_F_TELEPORTER`: 31 itens. Booster Coin
  (1000254) → 12 tickets/upgrades/masks (Metal_W/7, Boost_A/W, Boost_Up,
  Booster_Mask A/B/C, Almighty, World_Tour_Ticket, RandomOpt P/M); Booster
  W Ticket → 18 booster weapons (todas as 3rd-class).
- **Guarani (148,282)** sprite `4_M_DST_MASTER`: 16 itens. Boost_A_Ticket
  → 4 armor crates (Atk/Ran/Ele/Defn); Metal_W_Ticket → 12 Metal weapons
  (Two_Hand_Sword, Lance, Mace, Two_Handed_Axe, Dagger, Book, Staff, Katar,
  Bow, Revolver, Huuma_Shuriken, Foxtail).

Release inicial consolidado da DimensionsRO. Esta versão agrega todas as
modificações desde o fork do upstream rAthena (`master` original) até o estado
atual em `develop`. Após este ponto, mudanças seguem o ciclo normal `develop →
main` com versionamento incremental (1.0.x patches, 1.x.0 features).

### Stylist (@stylist) — kRO 2024+ Compatibility

Cliente kRO Aug/2025 introduziu novo packet de Apply (`0x0bf7`) que rAthena
upstream ainda não parsea. Fix consome o packet, reproduz a UI sem cobrar por
categorias não alteradas e suporta o Costume Change.

- **Packet `CZ_REQ_STYLE_CHANGE3` (0xbf7) registrado** (`src/map/packets.hpp`,
  `src/map/clif_packetdb.hpp`): variable-length, layout `count + records[count]`
  de 8 bytes (`[category, _, value, _]`). Resolve o disconnect ao apertar Apply
  na janela do estilista em clientes 2024+.
- **Mapa de categorias kRO → rAthena**: 0=Hair_Color (palette), 1=Hair (style),
  2=Clothes_Color (palette), 3=Head_Top, 4=Head_Mid, 5=Head_Bottom, 9=Body2.
  Paletas (0/1/2) usam lookup por `Value`; acessórios (3/4/5) usam lookup por
  `Index` no DB.
- **Atomic 2-pass dispatch** (`clif.cpp:clif_parse_stylist_buy`): Pass 1 valida
  zeny/itens de todas categorias em dry-run; Pass 2 commita. Evita gastar zeny/
  cupom quando uma categoria posterior falha (a UI do kRO 2024+ envia o estado
  atual de todas categorias junto, então sem isso o jogador era cobrado por
  estilos que não pediu).
- **No-op skip**: categorias cuja `Value` já é a atual (`status.hair`,
  `hair_color`, `clothes_color`, `body`) retornam sem cobrar. Necessário pelo
  comportamento do cliente que sempre revalida tudo no Apply.
- **Costume Change (toggle de model alternativo, kRO category 9)**: para 3rd
  classes não-4th, alterna entre `status.class_` (default) e
  `alternate_outfits[0]` do `job_db.find()` (ex.: GC_T 4065 ↔
  JOB_GUILLOTINE_CROSS_2ND 4334). Consome 1 `Costume_Ticket` (id 6959) só ao
  trocar para o alternativo — voltar pra default é grátis.
- **Clothes_Color Value 1 adicionada ao DB** (`db/re/stylist.yml`): paleta 1
  estava ausente entre Value 0 e Value 2; cliente kRO aceita 0..6 mas a entrada
  intermediária faltava. Custo igual aos outros (`Clothing_Dye_Coupon`).
- **`max_cloth_color: 7 → 6`** (`conf/battle/client.conf`): valor é o
  índice máximo, não a contagem; range correto é 0..6 (7 paletas totais).

### NPCs (Player-Facing)

- **Job Master NPC ativado** em `prontera` (`npc/custom/jobmaster.txt`): NPC
  oficial rAthena, posicionado no spot do Valerie. Bloqueia advancement pra 4th
  jobs via runtime hook.
- **Eden Group iRO VIP NPCs habilitados** em `moc_para01`
  (`npc/re/quests/eden/eden_iro.txt`): npcs auxiliares (refine helpers, board
  proxies) que vinham comentados.
- **Adventurer Starter Kit reescrito** (`npc/custom/dro_starter_kit.txt`): 4
  tiers (1st/2nd/Trans/3rd), gear vanilla apropriado por classe (sem Booster
  endgame), validação de equipabilidade por classe, gating per-tier via flags
  (`DRO_STARTER_T1`/`T2`/`T3`/`TAKEN`) — cada tier só pode ser claimado uma
  vez. Sem consumíveis no kit.

### Infra

- **Map-server isolado em segunda instância Oracle** (136.248.101.89): login +
  char + DB ficam na primeira (163.176.144.14); map sobe na segunda e
  conversa com a DB via SSH tunnel (`/usr/local/bin/mysql-tunnel.sh`,
  systemd-managed) pelo VCN privado. Reduz contenção CPU em surtos de map-side.

### Server Identity & Scope

- **Episode 17.1 content lockdown** (`src/custom/ep17_1.hpp`,
  `npc/re/scripts_post_ep17_1.conf`): server trava em Ep 17.1. Sem 4th classes,
  sem quests/instances/items pós-17.1. `pc_jobchange` bloqueia advancement pra
  classes 4th com mensagem in-game.
- **3rd class caps travados pra match iRO oficial 17.1** (`db/re/job_exp.yml`,
  `conf/import-tmpl/battle_conf.txt`):
  - Base Level max: 175 (era 200 default rathena)
  - Job Level max: 60 (era 70 default)
  - Stat max: 120 (era 130, default rathena assumia progressão pra 4th)

### Rates & Drop Configuration

- **Mid-rate baseline** (`conf/import-tmpl/battle_conf.txt`):
  - EXP: 5x base / 5x job / 3x quest
  - Equip: 5x normal / 10x boss / 15x MVP
  - Cards: 3x normal / 3x boss / 2x MVP
  - Common/Heal/Use: 5x
  - MVP-specific drops (Old Card Album etc): 3x
  - Pace: ~30-40h casual to max 3rd class. Eventos (weekend +5x, anniversary
    +10x) elevam efetivo pra 10-20x para sessões curtas.

### Skill Rebalance — PvE Only (vs_players=0)

Estratégia tiered: 3rds são fracas pro conteúdo de 17.1, todas skills sobem,
escalado pela "fraqueza original". Aplicado via `db/import-tmpl/skill_damage_db.txt`.

**Tier C — skills "dead" (buff alto, +45% a +90%):**

| Skill | Buff | Classe |
|---|---|---|
| LG_BANISHINGPOINT | +65% | Royal Guard |
| LG_EARTHDRIVE | +65% | Royal Guard |
| NC_BOOSTKNUCKLE | +65% | Mechanic non-Mado |
| NC_ARMSCANNON | +45% | Mechanic |
| SR_KNUCKLEARROW | +45% | Sura |
| SC_TRIANGLESHOT | +65% | Shadow Chaser |
| WL_CHAINLIGHTNING_ATK | +45% | Warlock |
| PR_MAGNUS | +90% | Arch Bishop / Priest |
| KO_BAKURETSU | +45% | Kagerou |

**Tier A — main skills sub-tier (buff moderado, +30%):**

| Skill | Classe |
|---|---|
| LG_HESPERUSLIT | Royal Guard |
| RK_DRAGONBREATH | Rune Knight |
| RK_DRAGONBREATH_WATER | Rune Knight |
| NC_VULCANARM | Mechanic |
| NC_AXEBOOMERANG | Mechanic |
| SR_RAMPAGEBLASTER | Sura |
| GN_DEMONIC_FIRE | Genetic |
| AB_JUDEX | Arch Bishop |
| SC_FATALMENACE | Shadow Chaser |
| WL_TETRAVORTEX | Warlock |
| WL_JACKFROST | Warlock |
| RA_AIMEDBOLT | Ranger |

**Tier S — top-tier farming/burst skills (buff suave, +15%):**

| Skill | Classe |
|---|---|
| GN_CARTCANNON | Genetic |
| WL_COMET | Warlock |
| SR_TIGERCANNON | Sura |
| RA_ARROWSTORM | Ranger |
| AB_ADORAMUS | Arch Bishop |

PvP intacto — todas adjustments têm `vs_players=0`.

### Defensive Skill Buffs (source-level, requer recompile)

Skills defensivas universais buffadas pra player taxa de sobrevivência vs
conteúdo de 17.1. Aplicado em `src/map/battle.cpp`, `src/map/status.cpp` e
`db/re/skill_db.yml`.

- **HP_ASSUMPTIO**: agora dá -50% damage taken em PvE renewal (era só DEF
  buff). Mantém scaling por skill level. PvP fica em -33%.
- **MG_ENERGYCOAT**: damage reduction 6%/12%/18%/24%/30% → 10%/20%/30%/40%/50%
  por SP interval. SP cost por hit reduzido pela metade (1%+0.5% → 0.5%+0.3%).
- **LK_TENSIONRELAX**: HP regen rate 200 → 400. Adicionado SP regen 100 (skill
  agora é viável pra solo farm).
- **PR_MAGNIFICAT**: SP regen multiplier 100 → 150 (regen 2.5x base). Duração
  estendida 30-90s → 60-180s.
- **AL_BLESSING**: duração estendida x1.5 (lvl 10: 240s → 360s). Beneficia
  todas classes da Acolyte tree.
- **LG_KINGS_GRACE**: duração 5s → 8s. Cooldown reduzido pela metade
  (100s → 50s lvl 1, 60s → 30s lvl 5).
- **SC_AUTOGUARD**: block rate scaling +30% (lvl 10: 30% → 40%).
- **GC_HALLUCINATIONWALK**: physical Flee 50/lvl → 100/lvl, magic Flee
  10/lvl → 20/lvl.
- **SC__SHADOWFORM**: duração +30s em todos lvls (30-70s → 60-100s).
- **MI_RUSH_WINDMILL**: duração 3min → 4min.

**Wave 2 — defensivas avançadas:**

- **LK_PARRYING**: block chance scaling 20+3/lvl → 30+5/lvl (lvl 5: 35% → 55%).
- **CR_DEFENDER**: penalidade de aspd reduzida pela metade
  (val4 250-50/lvl → 125-25/lvl). Penalidade de movimento aliviada
  (`max(speed,200)` → `max(speed,175)`). Damage reduction (vs ranged weapon)
  inalterada (5+15/lvl, max 80%).
- **RK_MILLENNIUMSHIELD**: HP por shield 1000 → 2500. Escala melhor com
  conteúdo endgame onde MVPs batem >1k por hit.
- **SO_FIRE/WATER/WIND/EARTH_INSIGNIA**: cada insígnia agora dá -25% de
  resistência ao dano do elemento correspondente (somado ao bonus já existente
  em battle.cpp para dano de elemento oposto).
- **SR_GENTLETOUCH_REVITALIZE**: scaling buffado em todos os val:
  - MaxHP %: 2/lvl → 4/lvl (max 10% → 20%)
  - HP regen: 30/lvl+50 → 40/lvl+100 (lvl 5: 200% → 300%)
  - STAT DEF: 20/lvl → 30/lvl (max 100 → 150)
  - Duração: 240s → 360s (4 → 6 min)
- **GN_MANDRAGORA**: rate base 25+10/lvl → 35+12/lvl (max ~95% antes de
  mitigação por VIT/LUK). Duração 10-30s → 15-35s. Skill agora viável pra
  controle de cast em mobs/MVPs com mais consistência.
- **RA_CAMOUFLAGE**: removida a penalidade de DEF/DEF2 que crescia 5%/segundo
  (`status.cpp:7825-7826` e `:7909-7910`). Removido o aumento de 5%/seg de
  dano ranged recebido (`battle.cpp:4763-4769`). Bonus ofensivos (ATK +30/seg
  até 300, CRIT +100/seg até 1000) preservados — Camouflage agora é puro
  buff de prep.
- **NC_SHAPESHIFT**: duração 5min → 10min. Reduz o overhead de re-cast em
  Mado long-runs.
- **WA_MOONLIT_SERENADE / MI_RUSH_WINDMILL**: agora adicionam DEF/MDEF flat
  (10 por skill level, em ambos os lados). Antes só ATK/MATK; agora também
  endurecem a party.

### Server-Side Tuning

- **Input lag tuning Phase 1 + 2** (`conf/battle/battle.conf`,
  `conf/battle/client.conf`, `conf/battle/skill.conf`):
  - `damage_walk_delay_rate: 100 → 80` — recovery mais snappy após hits
  - `snap_dodge: yes` — snap skills (Asura/Body Relocation) dodge corretamente
  - `max_walk_path: 17 → 25` — menos pathfind recalcs em click-walks longos
  - `multihit_delay: 200 → 100` — menos stunlock pós multi-hit
  - `player_damage_delay_rate: 100 → 50` — walk-delay menor após dano
  - `default_walk_delay: 300 → 200` — "can't walk" reduzido pós-skill
- **`nc_madogear_no_fuel` battle config** (`src/map/battle.{cpp,hpp}`,
  `src/map/skill.cpp`, `conf/battle/skill.conf`): nova flag server-wide pra
  desabilitar consumo de Magic_Gear_Fuel. Default `no` (vanilla). Quando `yes`,
  remove o burden de fuel-management do Mechanic/Mado sem quebrar item bonuses
  per-player. Reusa infra `sd->special_state.no_mado_fuel`.

### Web / Infra

- **Web `GET /status` endpoint** (`src/web/status_controller.cpp`): retorna
  `{online: bool, players: int}` pro launcher consumir status do server.
- **CI: build automatizado** (`.github/workflows/build_dimensionsro_prod.yml`):
  builda binários rAthena no Oracle Linux 9 x86_64 pra prod deployment.
- **Dev environment isolation**: ports `72xx` (vs prod `62xx`),
  `PACKET_OBFUSCATION` off em map-server dev, DB `ragnarok_dev` separado.

### Decisões Estratégicas (não-implementadas, registradas)

- **Phase 4 nerfs revertidos**: estratégia inicial de cap nas top-tier skills
  (CartCannon -15%, Comet -20%, TigerCannon -25%, ArrowStorm -15%, Adoramus
  -15%) foi revertida. 3rds são fracas pro conteúdo de 17.1; cortar topo
  agravaria. Substituído pela strategy "all-buff progressivo" descrita em
  Skill Rebalance acima.
- **Phase 5.3 mob HP overrides**: scaffold pra reduzir HP/DEF de mobs
  Ep 17.1 (FACEWORM, TIMEHOLDER, HEART_HUNTER) abandonado. Decisão: nunca
  tocar em mobs do conteúdo, ajustes ficam só do lado do player.
- **Phase 6.1 Royal Guard Banding refactor**: deferred. Requer rewrite de
  `skill_check_pc_partner` semantics, risk de quebrar outras Royal Guard
  skills (Hesperus Lit, Inspiration, Banishing Buster) sem playtest. Phase 3
  buffs já endereçam parte do solo-pain do RG.
- **Phase 6.2 Shadow Chaser Reproduce**: investigado, OBSOLETO. Modern rAthena
  (`skill.cpp:801`) não tem o "lock" que a comunidade reportava em clientes
  antigos. Reproduce já copia continuamente skills do target.

### Client (DimensionsRO 1.0)

(Tracked em repo separado do client; resumo aqui)

- Vanilla/HD mode swap funcional
- Custom launcher com GitHub patcher
- Login point: Oracle Cloud `163.176.144.14:6266`
- Server name: DimensionsRO
- IP de prod escrito em `server.grf/data/clientinfo.xml`

---

## How to use this changelog

1. **Cada commit em `develop`** que introduz user-visible behavior (gameplay,
   balance, quest, instance, drop rate, exp, GM commands, client behavior)
   adiciona entry sob `[Unreleased]`.
2. **Internal changes** (refactors, build fixes que não mudam comportamento,
   doc-only) podem skipar o changelog.
3. **No merge `develop → main`**, bloco `[Unreleased]` vira nova versão.
   Bump por [SemVer](https://semver.org/):
   - **MAJOR** (1.x.x → 2.0.0): breaking (ex: wipe, char migration)
   - **MINOR** (1.0.x → 1.1.0): novas features (instances, classes, sistemas)
   - **PATCH** (1.0.0 → 1.0.1): fixes e balance tweaks
4. **Production release** = tag em `main` com a versão. Player-facing
   changelog (Discord announcement) derivado deste arquivo.

---

_Maintained by the DimensionsRO team. Last updated: 2026-05-02._
