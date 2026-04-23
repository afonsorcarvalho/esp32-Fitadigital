/* Shared data + helpers, exposed on window */
(function(){
  // Equipment (autoclaves)
  const EQUIPMENT = [
    { id: 'AV-120', name: 'Autoclave Vapor AV-120', location: 'CME · Sala 03', type: 'vapor' },
    { id: 'AV-121', name: 'Autoclave Vapor AV-121', location: 'CME · Sala 04', type: 'vapor' },
    { id: 'AV-122', name: 'Autoclave Vapor AV-122', location: 'CME · Sala 05', type: 'vapor' },
    { id: 'AV-123', name: 'Autoclave Vapor AV-123', location: 'Lab. Central',  type: 'vapor' },
    { id: 'ET-60',  name: 'Autoclave ETO ET-60',    location: 'Bloco B · Sala 12', type: 'eto' },
    { id: 'ET-61',  name: 'Autoclave ETO ET-61',    location: 'Bloco B · Sala 14', type: 'eto' },
    { id: 'AV-124', name: 'Autoclave Vapor AV-124', location: 'Lab. Central',  type: 'vapor' },
    { id: 'ET-62',  name: 'Autoclave ETO ET-62',    location: 'Bloco B · Sala 15', type: 'eto' },
  ];

  // Phase recipes (minutes). Coming from Odoo afr.cycle.features in reality.
  const RECIPE_VAPOR_134 = [
    { key: 'pre-vacuo',      label: 'Pré-vácuo',      minutes: 6  },
    { key: 'aquecimento',    label: 'Aquecimento',    minutes: 10 },
    { key: 'esterilizacao',  label: 'Esterilização',  minutes: 7  },
    { key: 'secagem',        label: 'Secagem',        minutes: 12 },
    { key: 'resfriamento',   label: 'Resfriamento',   minutes: 5  },
  ];
  const RECIPE_VAPOR_121 = [
    { key: 'pre-vacuo',      label: 'Pré-vácuo',      minutes: 5  },
    { key: 'aquecimento',    label: 'Aquecimento',    minutes: 12 },
    { key: 'esterilizacao',  label: 'Esterilização',  minutes: 15 },
    { key: 'secagem',        label: 'Secagem',        minutes: 15 },
    { key: 'resfriamento',   label: 'Resfriamento',   minutes: 8  },
  ];
  const RECIPE_ETO = [
    { key: 'pre-condicion',  label: 'Pré-condicion.',  minutes: 45 },
    { key: 'injecao',        label: 'Injeção ETO',     minutes: 15 },
    { key: 'exposicao',      label: 'Exposição',       minutes: 180 },
    { key: 'lavagem',        label: 'Lavagem',         minutes: 30 },
    { key: 'aeracao',        label: 'Aeração',         minutes: 240 },
  ];

  function minutes(recipe) {
    return recipe.reduce((a, p) => a + p.minutes, 0);
  }

  // Cycles. startedAt is a real timestamp in the past so elapsed keeps running.
  // We create a factory so we can regenerate on palette or on "new event".
  function now() { return Date.now(); }
  function ago(sec) { return Date.now() - sec * 1000; }

  function makeCycles() {
    return [
      // 1. IN PROGRESS — currently in Esterilização, will move to Secagem
      {
        id: 'c-0427',
        lot: 'LOTE 24-0427',
        equipment: EQUIPMENT[0],
        recipe: RECIPE_VAPOR_121,
        recipeName: 'Vapor 121°C · instrumental',
        operator: 'J. Silva',
        state: 'in_progress',
        startedAt: ago( (5 + 12 + 6) * 60 ), // 6 min into Esterilização (15 min phase)
        plannedMinutes: minutes(RECIPE_VAPOR_121),
        material: 'Instrumental cirúrgico',
        tempNow: 121.4, pressureNow: 1.82,
      },
      // 2. IN PROGRESS — early in Aquecimento
      {
        id: 'c-0428',
        lot: 'LOTE 24-0428',
        equipment: EQUIPMENT[1],
        recipe: RECIPE_VAPOR_134,
        recipeName: 'Vapor 134°C · flash',
        operator: 'A. Costa',
        state: 'in_progress',
        startedAt: ago( (6 + 3) * 60 ), // 3 min into aquecimento
        plannedMinutes: minutes(RECIPE_VAPOR_134),
        material: 'Tecidos e campos',
        tempNow: 102.8, pressureNow: 0.9,
      },
      // 3. SCHEDULED TODAY
      {
        id: 'c-0429',
        lot: 'LOTE 24-0429',
        equipment: EQUIPMENT[2],
        recipe: RECIPE_VAPOR_121,
        recipeName: 'Vapor 121°C · instrumental',
        operator: 'M. Alves',
        state: 'scheduled',
        scheduledFor: '14:45',
        plannedMinutes: minutes(RECIPE_VAPOR_121),
        material: 'Pinças e tesouras',
      },
      // 4. IN PROGRESS — ETO long cycle, in Exposição
      {
        id: 'c-0425',
        lot: 'LOTE 24-0425',
        equipment: EQUIPMENT[4],
        recipe: RECIPE_ETO,
        recipeName: 'ETO · devices sensíveis',
        operator: 'R. Pinho',
        state: 'in_progress',
        startedAt: ago( (45 + 15 + 42) * 60 ),  // 42 min into exposicao (180)
        plannedMinutes: minutes(RECIPE_ETO),
        material: 'Endoscópios',
        tempNow: 54.8, pressureNow: 0.12, humidity: 62,
      },
      // 5. OVERDUE — should have finished; still shows "em andamento" but past planned end
      {
        id: 'c-0421',
        lot: 'LOTE 24-0421',
        equipment: EQUIPMENT[7],
        recipe: RECIPE_ETO,
        recipeName: 'ETO · devices sensíveis',
        operator: 'J. Silva',
        state: 'overdue',
        startedAt: ago( (45 + 15 + 180 + 30 + 240 + 18) * 60 ), // 18 min past planned end
        plannedMinutes: minutes(RECIPE_ETO),
        material: 'Cateteres',
        tempNow: 48.2, pressureNow: 0.08,
      },
      // 6. COMPLETED recently
      {
        id: 'c-0420',
        lot: 'LOTE 24-0420',
        equipment: EQUIPMENT[3],
        recipe: RECIPE_VAPOR_121,
        recipeName: 'Vapor 121°C · instrumental',
        operator: 'A. Costa',
        state: 'completed',
        startedAt: ago( (minutes(RECIPE_VAPOR_121) + 32) * 60 ),
        plannedMinutes: minutes(RECIPE_VAPOR_121),
        material: 'Instrumental odonto.',
        result: 'APROVADO',
        bi: 'Negativo',
      },
      // 7. SCHEDULED
      {
        id: 'c-0430',
        lot: 'LOTE 24-0430',
        equipment: EQUIPMENT[6],
        recipe: RECIPE_VAPOR_134,
        recipeName: 'Vapor 134°C · flash',
        operator: 'M. Alves',
        state: 'scheduled',
        scheduledFor: '15:20',
        plannedMinutes: minutes(RECIPE_VAPOR_134),
        material: 'Cubas metálicas',
      },
      // 8. ABORTED
      {
        id: 'c-0419',
        lot: 'LOTE 24-0419',
        equipment: EQUIPMENT[5],
        recipe: RECIPE_ETO,
        recipeName: 'ETO · devices sensíveis',
        operator: 'R. Pinho',
        state: 'aborted',
        startedAt: ago( 40 * 60 ),
        plannedMinutes: minutes(RECIPE_ETO),
        material: 'Laparoscópio',
        result: 'ABORTADO',
        abortReason: 'Pressão fora da faixa',
      },
    ];
  }

  /* Given a cycle, compute elapsed (sec), phase index, phase progress (0-1).
     For in_progress & overdue. */
  function computePhase(cycle, tick) {
    const elapsedSec = Math.max(0, (Date.now() - cycle.startedAt) / 1000);
    let remaining = elapsedSec;
    for (let i = 0; i < cycle.recipe.length; i++) {
      const phaseSec = cycle.recipe[i].minutes * 60;
      if (remaining < phaseSec) {
        return {
          elapsedSec,
          phaseIndex: i,
          phaseProgress: remaining / phaseSec,
          overtime: false,
        };
      }
      remaining -= phaseSec;
    }
    // past last phase
    return {
      elapsedSec,
      phaseIndex: cycle.recipe.length - 1,
      phaseProgress: 1,
      overtime: true,
      overtimeSec: remaining,
    };
  }

  function fmtHMS(totalSec) {
    totalSec = Math.max(0, Math.floor(totalSec));
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = totalSec % 60;
    if (h > 0) return `${h}h ${String(m).padStart(2,'0')}min ${String(s).padStart(2,'0')}s`;
    return `${String(m).padStart(2,'0')}min ${String(s).padStart(2,'0')}s`;
  }

  function fmtMMSS(totalSec) {
    totalSec = Math.max(0, Math.floor(totalSec));
    const m = Math.floor(totalSec / 60);
    const s = totalSec % 60;
    return `${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
  }

  function fmtHHMM(totalMin) {
    totalMin = Math.max(0, Math.floor(totalMin));
    const h = Math.floor(totalMin / 60);
    const m = totalMin % 60;
    if (h > 0) return `${h}h ${String(m).padStart(2,'0')}min`;
    return `${m}min`;
  }

  // Generate a rolling chart history for in-progress cycles (temp/pressure)
  function chartHistory(cycle, points = 60) {
    // Deterministic pseudo-random so chart is stable across re-renders
    const seed = cycle.id.charCodeAt(2) + cycle.id.charCodeAt(3);
    const rand = (i) => {
      const x = Math.sin((seed + i) * 12.9898) * 43758.5453;
      return x - Math.floor(x);
    };
    const phase = window.AFR.computePhase(cycle);
    const temps = []; const pres = [];
    const isETO = cycle.equipment.type === 'eto';
    const targetT = isETO ? 55 : (cycle.recipe.some(p => p.label === 'Esterilização') && phase.phaseIndex >= 2 ? 121 : 95);
    for (let i = 0; i < points; i++) {
      const t0 = (i / points);
      // Ramp up then plateau
      const ramp = Math.min(1, t0 * 2.5);
      const noise = (rand(i) - 0.5) * 1.2;
      temps.push( (isETO ? 30 : 20) + (targetT - (isETO ? 30 : 20)) * ramp + noise );
      pres.push( (isETO ? 0.05 : 0.2) + (isETO ? 0.1 : 1.6) * ramp + (rand(i+7) - 0.5) * 0.04 );
    }
    return { temps, pres };
  }

  window.AFR = {
    EQUIPMENT, RECIPE_VAPOR_121, RECIPE_VAPOR_134, RECIPE_ETO,
    makeCycles, computePhase, fmtHMS, fmtMMSS, fmtHHMM, chartHistory,
  };
})();
