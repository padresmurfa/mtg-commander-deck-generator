export interface ThemeQuery {
  primary: string;
  secondary?: string;
  keywords: string[];
}

/**
 * Maps EDHREC theme names to Scryfall queries and scoring keywords.
 * Theme names are case-insensitive.
 */
const THEME_QUERY_MAP: Record<string, ThemeQuery> = {
  // Token strategies
  tokens: {
    primary: 'o:"create" o:"token"',
    secondary: 'o:"populate" OR o:"go wide"',
    keywords: ['create', 'token', 'populate', 'creatures you control'],
  },
  'go wide': {
    primary: 'o:"creatures you control" OR o:"each creature you control"',
    keywords: ['creatures you control', 'anthem', 'overrun'],
  },

  // Counter strategies
  '+1/+1 counters': {
    primary: 'o:"+1/+1 counter"',
    secondary: 'o:"proliferate"',
    keywords: ['+1/+1 counter', 'proliferate', 'enters with', 'counter on'],
  },
  '-1/-1 counters': {
    primary: 'o:"-1/-1 counter"',
    keywords: ['-1/-1 counter', 'wither', 'persist'],
  },
  counters: {
    primary: 'o:"counter on" OR o:"proliferate"',
    keywords: ['counter', 'proliferate'],
  },
  proliferate: {
    primary: 'o:"proliferate"',
    keywords: ['proliferate', 'counter'],
  },

  // Sacrifice/Death strategies
  aristocrats: {
    primary: 'o:"sacrifice" OR (o:"when" o:"dies")',
    secondary: 'o:"drain" OR o:"each opponent loses"',
    keywords: ['sacrifice', 'dies', 'drain', 'blood artist'],
  },
  sacrifice: {
    primary: 'o:"sacrifice"',
    keywords: ['sacrifice', 'sac outlet', 'fodder'],
  },

  // Equipment/Aura strategies
  voltron: {
    primary: 't:equipment OR t:aura',
    secondary: 'o:"equipped creature" OR o:"enchanted creature"',
    keywords: ['equipment', 'aura', 'equip', 'attach', 'hexproof', 'protection'],
  },
  equipment: {
    primary: 't:equipment OR o:"equipped creature"',
    keywords: ['equipment', 'equip', 'attach'],
  },
  auras: {
    primary: 't:aura OR o:"enchanted creature"',
    keywords: ['aura', 'enchant', 'enchanted creature'],
  },

  // Spell strategies
  spellslinger: {
    primary: '(t:instant OR t:sorcery) o:"whenever you cast"',
    secondary: 'o:"magecraft" OR o:"prowess"',
    keywords: ['instant', 'sorcery', 'whenever you cast', 'magecraft', 'prowess'],
  },
  'instants matter': {
    primary: 't:instant OR o:"instant"',
    keywords: ['instant', 'flash'],
  },
  storm: {
    primary: 'o:"storm" OR o:"cost" o:"less"',
    secondary: 'o:"add" o:"mana"',
    keywords: ['storm', 'cost less', 'ritual', 'mana'],
  },
  cantrips: {
    primary: 'o:"draw a card" cmc<=2',
    keywords: ['draw a card', 'cantrip'],
  },

  // Planeswalker strategies
  superfriends: {
    primary: 't:planeswalker',
    secondary: 'o:"planeswalker" OR o:"loyalty"',
    keywords: ['planeswalker', 'loyalty', 'ultimate'],
  },
  planeswalkers: {
    primary: 't:planeswalker',
    keywords: ['planeswalker', 'loyalty'],
  },

  // Graveyard strategies
  reanimator: {
    primary: 'o:"graveyard" o:"return"',
    secondary: 'o:"reanimate" OR o:"unearth"',
    keywords: ['graveyard', 'return', 'reanimate', 'unearth'],
  },
  graveyard: {
    primary: 'o:"graveyard"',
    keywords: ['graveyard', 'mill', 'discard'],
  },
  mill: {
    primary: 'o:"mill" OR (o:"puts" o:"graveyard")',
    keywords: ['mill', 'graveyard', 'library'],
  },
  'self-mill': {
    primary: 'o:"mill" OR o:"discard"',
    keywords: ['mill', 'discard', 'graveyard'],
  },
  flashback: {
    primary: 'o:"flashback"',
    keywords: ['flashback', 'graveyard'],
  },
  dredge: {
    primary: 'o:"dredge"',
    keywords: ['dredge', 'graveyard', 'mill'],
  },

  // Hand manipulation
  wheels: {
    primary: 'o:"each player" (o:"discards" OR o:"draws")',
    secondary: 'o:"wheel" OR o:"discard" o:"draw"',
    keywords: ['wheel', 'discard', 'draw', 'each player'],
  },
  discard: {
    primary: 'o:"discard"',
    keywords: ['discard', 'madness'],
  },

  // Blink/Flicker
  blink: {
    primary: 'o:"exile" o:"return" o:"battlefield"',
    secondary: 'o:"flicker" OR o:"enters the battlefield"',
    keywords: ['exile', 'return', 'flicker', 'enters the battlefield'],
  },
  flicker: {
    primary: 'o:"exile" o:"return" o:"battlefield"',
    keywords: ['exile', 'return', 'enters'],
  },
  etb: {
    primary: 'o:"enters the battlefield"',
    keywords: ['enters the battlefield', 'etb'],
  },

  // Clone/Copy
  clones: {
    primary: 'o:"copy" OR o:"becomes a copy"',
    keywords: ['copy', 'clone', 'becomes'],
  },
  copy: {
    primary: 'o:"copy"',
    keywords: ['copy', 'clone'],
  },

  // Land strategies
  landfall: {
    primary: 'o:"landfall" OR (o:"land" o:"enters")',
    secondary: 'o:"play" o:"additional land"',
    keywords: ['landfall', 'land enters', 'extra land'],
  },
  lands: {
    primary: 'o:"land"',
    keywords: ['land', 'landfall'],
  },
  'lands matter': {
    primary: 'o:"landfall" OR o:"lands you control"',
    keywords: ['landfall', 'lands you control'],
  },

  // Artifact strategies
  artifacts: {
    primary: 't:artifact OR (o:"artifact" o:"enters")',
    secondary: 'o:"affinity" OR o:"metalcraft"',
    keywords: ['artifact', 'affinity', 'metalcraft', 'improvise'],
  },
  'artifact tokens': {
    primary: 'o:"create" o:"artifact token"',
    keywords: ['artifact token', 'treasure', 'clue', 'food'],
  },
  treasures: {
    primary: 'o:"treasure"',
    keywords: ['treasure', 'create', 'artifact'],
  },
  food: {
    primary: 'o:"food"',
    keywords: ['food', 'create', 'gain life'],
  },
  clues: {
    primary: 'o:"clue"',
    keywords: ['clue', 'investigate', 'draw'],
  },

  // Enchantment strategies
  enchantress: {
    primary: 't:enchantment OR (o:"enchantment" o:"draw")',
    secondary: 'o:"constellation"',
    keywords: ['enchantment', 'constellation', 'enchant'],
  },
  enchantments: {
    primary: 't:enchantment',
    keywords: ['enchantment', 'aura'],
  },
  constellation: {
    primary: 'o:"constellation"',
    keywords: ['constellation', 'enchantment'],
  },

  // Combat strategies
  aggro: {
    primary: 'o:"haste" OR o:"attack"',
    keywords: ['haste', 'attack', 'combat'],
  },
  combat: {
    primary: 'o:"combat" OR o:"attack"',
    keywords: ['combat', 'attack', 'damage'],
  },
  'extra combat': {
    primary: 'o:"additional combat"',
    keywords: ['additional combat', 'untap', 'attack'],
  },
  'attack triggers': {
    primary: 'o:"whenever" o:"attacks"',
    keywords: ['attacks', 'combat', 'attack triggers'],
  },

  // Control strategies
  control: {
    primary: 'o:"counter target" OR o:"destroy target"',
    secondary: 'o:"exile target" OR o:"return" o:"hand"',
    keywords: ['counter', 'destroy', 'exile', 'removal'],
  },
  stax: {
    primary: 'o:"can\'t" OR o:"opponents" o:"sacrifice"',
    keywords: ["can't", 'sacrifice', 'tax'],
  },
  pillowfort: {
    primary: 'o:"can\'t attack" OR o:"propaganda"',
    keywords: ["can't attack", 'tax', 'protection'],
  },

  // Life strategies
  lifegain: {
    primary: 'o:"gain" o:"life"',
    secondary: 'o:"whenever you gain life"',
    keywords: ['gain life', 'lifegain', 'life total'],
  },
  'life gain': {
    primary: 'o:"gain" o:"life"',
    keywords: ['gain life', 'lifegain'],
  },
  lifedrain: {
    primary: 'o:"lose" o:"life" OR o:"drain"',
    keywords: ['lose life', 'drain', 'each opponent'],
  },

  // Special mechanics
  infect: {
    primary: 'o:"infect" OR o:"poison counter"',
    keywords: ['infect', 'poison', 'toxic'],
  },
  poison: {
    primary: 'o:"poison counter" OR o:"toxic"',
    keywords: ['poison', 'toxic', 'infect'],
  },
  energy: {
    primary: 'o:"energy counter"',
    keywords: ['energy', 'counter'],
  },
  cascade: {
    primary: 'o:"cascade"',
    keywords: ['cascade', 'free spell'],
  },
  chaos: {
    primary: 'o:"random" OR o:"flip a coin"',
    keywords: ['random', 'coin', 'chaos'],
  },

  // Group strategies
  'group hug': {
    primary: 'o:"each player" (o:"draws" OR o:"gains")',
    keywords: ['each player', 'draw', 'ramp'],
  },
  'group slug': {
    primary: 'o:"each player" o:"loses" o:"life"',
    keywords: ['each player', 'loses life', 'damage'],
  },
  politics: {
    primary: 'o:"vote" OR o:"monarch"',
    keywords: ['vote', 'monarch', 'politics'],
  },
  monarch: {
    primary: 'o:"monarch"',
    keywords: ['monarch', 'draw'],
  },

  // Draw strategies
  'card draw': {
    primary: 'o:"draw" o:"card"',
    keywords: ['draw', 'cards'],
  },

  // Combo enablers
  combo: {
    primary: 'o:"untap" OR o:"infinite"',
    secondary: 'o:"each" o:"add"',
    keywords: ['untap', 'combo', 'infinite'],
  },
  'infinite combos': {
    primary: 'o:"untap" OR o:"whenever"',
    keywords: ['untap', 'infinite', 'combo'],
  },
  tutors: {
    primary: 'o:"search your library"',
    keywords: ['search', 'tutor', 'library'],
  },

  // Creature type themes (tribal)
  tribal: {
    primary: 't:creature',
    keywords: ['tribal', 'creature type'],
  },
  elves: {
    primary: 't:elf',
    keywords: ['elf', 'elves'],
  },
  goblins: {
    primary: 't:goblin',
    keywords: ['goblin', 'goblins'],
  },
  zombies: {
    primary: 't:zombie',
    keywords: ['zombie', 'zombies'],
  },
  vampires: {
    primary: 't:vampire',
    keywords: ['vampire', 'vampires', 'blood'],
  },
  dragons: {
    primary: 't:dragon',
    keywords: ['dragon', 'dragons'],
  },
  angels: {
    primary: 't:angel',
    keywords: ['angel', 'angels'],
  },
  demons: {
    primary: 't:demon',
    keywords: ['demon', 'demons'],
  },
  wizards: {
    primary: 't:wizard',
    keywords: ['wizard', 'wizards'],
  },
  warriors: {
    primary: 't:warrior',
    keywords: ['warrior', 'warriors'],
  },
  rogues: {
    primary: 't:rogue',
    keywords: ['rogue', 'rogues'],
  },
  clerics: {
    primary: 't:cleric',
    keywords: ['cleric', 'clerics'],
  },
  soldiers: {
    primary: 't:soldier',
    keywords: ['soldier', 'soldiers'],
  },
  knights: {
    primary: 't:knight',
    keywords: ['knight', 'knights'],
  },
  merfolk: {
    primary: 't:merfolk',
    keywords: ['merfolk', 'islandwalk'],
  },
  spirits: {
    primary: 't:spirit',
    keywords: ['spirit', 'spirits'],
  },
  dinosaurs: {
    primary: 't:dinosaur',
    keywords: ['dinosaur', 'dinosaurs', 'enrage'],
  },
  pirates: {
    primary: 't:pirate',
    keywords: ['pirate', 'pirates', 'treasure'],
  },
  cats: {
    primary: 't:cat',
    keywords: ['cat', 'cats'],
  },
  dogs: {
    primary: 't:dog',
    keywords: ['dog', 'dogs'],
  },
  beasts: {
    primary: 't:beast',
    keywords: ['beast', 'beasts'],
  },
  elementals: {
    primary: 't:elemental',
    keywords: ['elemental', 'elementals'],
  },
  slivers: {
    primary: 't:sliver',
    keywords: ['sliver', 'slivers'],
  },
  allies: {
    primary: 't:ally',
    keywords: ['ally', 'allies'],
  },
  humans: {
    primary: 't:human',
    keywords: ['human', 'humans'],
  },
  faeries: {
    primary: 't:faerie',
    keywords: ['faerie', 'faeries', 'flash', 'flying'],
  },
  eldrazi: {
    primary: 't:eldrazi',
    keywords: ['eldrazi', 'colorless', 'annihilator'],
  },
  horrors: {
    primary: 't:horror',
    keywords: ['horror', 'horrors'],
  },
  insects: {
    primary: 't:insect',
    keywords: ['insect', 'insects'],
  },
  tyranids: {
    primary: 't:tyranid',
    keywords: ['tyranid', '+1/+1 counter'],
  },
  hydras: {
    primary: 't:hydra',
    keywords: ['hydra', '+1/+1 counter'],
  },
  werewolves: {
    primary: 't:werewolf OR t:wolf',
    keywords: ['werewolf', 'wolf', 'transform'],
  },
  wolves: {
    primary: 't:wolf',
    keywords: ['wolf', 'wolves'],
  },
  rats: {
    primary: 't:rat',
    keywords: ['rat', 'rats'],
  },
  squirrels: {
    primary: 't:squirrel',
    keywords: ['squirrel', 'squirrels'],
  },
};

/**
 * Get Scryfall query for an EDHREC theme
 */
export function getQueryForTheme(themeName: string): ThemeQuery | null {
  const normalized = themeName.toLowerCase().trim();
  return THEME_QUERY_MAP[normalized] || null;
}

/**
 * Get keywords for scoring cards based on theme
 */
export function getKeywordsForTheme(themeName: string): string[] {
  const query = getQueryForTheme(themeName);
  return query?.keywords || [];
}

/**
 * Build combined queries from multiple selected themes
 */
export function buildQueriesFromThemes(themeNames: string[]): {
  creatureQuery: string;
  synergyQuery: string;
  keywords: string[];
} {
  const queries = themeNames
    .map((name) => getQueryForTheme(name))
    .filter((q): q is ThemeQuery => q !== null);

  if (queries.length === 0) {
    return {
      creatureQuery: 't:creature',
      synergyQuery: '',
      keywords: [],
    };
  }

  // Collect all keywords
  const keywords = [...new Set(queries.flatMap((q) => q.keywords))];

  // Build synergy query from primaries
  const primaryQueries = queries.map((q) => `(${q.primary})`);
  const synergyQuery = primaryQueries.join(' OR ');

  // For creatures, check if any theme is tribal
  const tribalQueries = queries.filter((q) => q.primary.startsWith('t:'));
  const creatureQuery =
    tribalQueries.length > 0
      ? tribalQueries.map((q) => q.primary).join(' OR ')
      : 't:creature';

  return {
    creatureQuery,
    synergyQuery,
    keywords,
  };
}

/**
 * Get all available theme names (for autocomplete/suggestions)
 */
export function getAllThemeNames(): string[] {
  return Object.keys(THEME_QUERY_MAP);
}
