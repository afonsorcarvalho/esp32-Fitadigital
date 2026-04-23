/* Extra pieces: StateBadgeInline, TempPressureChart, buildEventLog, PhaseBarShowcase */

const StateBadgeInline = ({ cycle }) => {
  const map = {
    in_progress: { cls: 'chip cyan', label: 'EM ANDAMENTO', dot: true, color: 'cyan' },
    overdue:     { cls: 'chip pink', label: 'ATRASADO',     dot: true, color: 'pink' },
    scheduled:   { cls: 'chip orange', label: 'PROGRAMADO', dot: true, color: 'orange' },
    completed:   { cls: 'chip green', label: 'CONCLUÍDO',   dot: false },
    aborted:     { cls: 'chip pink', label: 'ABORTADO',     dot: false },
  };
  const cfg = map[cycle.state];
  return (
    <span className={cfg.cls} style={{ fontSize: 11, padding: '5px 10px', display: 'inline-flex', alignItems: 'center', gap: 8 }}>
      {cfg.dot && <span className={`live-dot ${cfg.color}`} style={{ width: 6, height: 6 }} />}
      {cfg.label}
    </span>
  );
};

const TempPressureChart = ({ hist, cycle, tick }) => {
  const w = 800, h = 240;
  const temps = hist.temps;
  const pres = hist.pres;
  const tMin = Math.min(...temps) - 5;
  const tMax = Math.max(...temps) + 5;
  const pMin = Math.min(...pres) - 0.1;
  const pMax = Math.max(...pres) + 0.1;
  const toX = (i) => (i / (temps.length - 1)) * w;
  const toYt = (v) => h - ((v - tMin) / (tMax - tMin)) * (h - 24) - 12;
  const toYp = (v) => h - ((v - pMin) / (pMax - pMin)) * (h - 24) - 12;

  const tempPath = temps.map((v, i) => `${i === 0 ? 'M' : 'L'} ${toX(i).toFixed(1)} ${toYt(v).toFixed(1)}`).join(' ');
  const presPath = pres.map((v, i) => `${i === 0 ? 'M' : 'L'} ${toX(i).toFixed(1)} ${toYp(v).toFixed(1)}`).join(' ');
  const lastIdx = temps.length - 1;

  return (
    <svg width="100%" height="100%" viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none" style={{ overflow: 'visible' }}>
      <defs>
        <linearGradient id="temp-grad" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(16,185,129,0.35)" />
          <stop offset="100%" stopColor="rgba(16,185,129,0)" />
        </linearGradient>
      </defs>
      {/* grid */}
      {[0.25, 0.5, 0.75].map(f => (
        <line key={f} x1="0" x2={w} y1={h * f} y2={h * f} stroke="rgba(255,255,255,0.05)" strokeDasharray="3 4" />
      ))}
      {/* temp fill */}
      <path d={`${tempPath} L ${w} ${h} L 0 ${h} Z`} fill="url(#temp-grad)" />
      {/* temp line */}
      <path d={tempPath} fill="none" stroke="var(--neon-green)" strokeWidth="2" strokeLinejoin="round" strokeLinecap="round"
        style={{ filter: 'drop-shadow(0 0 6px rgba(16,185,129,0.5))' }} />
      {/* pressure line */}
      <path d={presPath} fill="none" stroke="var(--neon-blue)" strokeWidth="1.6" strokeLinejoin="round" strokeLinecap="round" strokeDasharray="4 3"
        style={{ filter: 'drop-shadow(0 0 4px rgba(0,212,255,0.4))' }} opacity="0.85" />
      {/* live indicator */}
      <line x1={toX(lastIdx)} x2={toX(lastIdx)} y1="0" y2={h} stroke="rgba(255,255,255,0.15)" strokeDasharray="2 3" />
      <circle cx={toX(lastIdx)} cy={toYt(temps[lastIdx])} r="3.5" fill="var(--neon-green)">
        <animate attributeName="r" values="3;5.5;3" dur="1.6s" repeatCount="indefinite" />
      </circle>
      <circle cx={toX(lastIdx)} cy={toYt(temps[lastIdx])} r="6" fill="none" stroke="var(--neon-green)" strokeWidth="1" opacity="0.5">
        <animate attributeName="r" values="5;14;5" dur="1.6s" repeatCount="indefinite" />
        <animate attributeName="opacity" values="0.6;0;0.6" dur="1.6s" repeatCount="indefinite" />
      </circle>
    </svg>
  );
};

function buildEventLog(cycle, phase) {
  const start = new Date(cycle.startedAt || Date.now() - 10000);
  const t = (addSec) => {
    const d = new Date(start.getTime() + addSec * 1000);
    return d.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  };
  const now = new Date();
  const logs = [];

  if (cycle.startedAt) {
    logs.push({ ts: t(0), level: 'ok', tag: 'OK', msg: `Ciclo iniciado por ${cycle.operator}` });
    logs.push({ ts: t(3), level: 'info', tag: 'INFO', msg: 'Porta travada · selo de segurança OK' });
    // Phase transitions
    let acc = 0;
    for (let i = 0; i < cycle.recipe.length; i++) {
      const phaseDurSec = cycle.recipe[i].minutes * 60;
      const phaseStartSec = acc;
      if (phase && phaseStartSec <= phase.elapsedSec) {
        logs.push({ ts: t(phaseStartSec), level: 'ok', tag: 'FASE', msg: `Fase "${cycle.recipe[i].label}" iniciada` });
      }
      acc += phaseDurSec;
      if (phase && acc <= phase.elapsedSec) {
        logs.push({ ts: t(acc), level: 'ok', tag: 'OK', msg: `Fase "${cycle.recipe[i].label}" concluída em ${cycle.recipe[i].minutes}min` });
      }
    }
  }
  if (cycle.state === 'overdue') {
    logs.push({ ts: now.toLocaleTimeString('pt-BR', { hour:'2-digit', minute:'2-digit', second:'2-digit' }), level: 'err', tag: 'ATRASO', msg: 'Tempo previsto excedido · verificar sensor de temperatura' });
  }
  if (cycle.state === 'aborted') {
    logs.push({ ts: t(40 * 60), level: 'err', tag: 'ABORTO', msg: cycle.abortReason });
  }
  if (cycle.state === 'completed') {
    logs.push({ ts: t(cycle.plannedMinutes * 60), level: 'ok', tag: 'FIM', msg: `Ciclo concluído · ${cycle.result}` });
    logs.push({ ts: t(cycle.plannedMinutes * 60 + 30), level: 'info', tag: 'INFO', msg: `Assinatura digital aplicada · op. ${cycle.operator}` });
  }

  // most recent first, cap
  return logs.reverse().slice(0, 14);
}

/* ============================================================
   Phase Bar Showcase — hero component kit page
   ============================================================ */
const PhaseBarShowcase = ({ tick }) => {
  const demoCycles = React.useMemo(() => {
    const base = window.AFR.makeCycles();
    return [
      base.find(c => c.id === 'c-0427'),   // in progress mid-way
      base.find(c => c.id === 'c-0428'),   // in progress early
      base.find(c => c.id === 'c-0425'),   // ETO long
      base.find(c => c.id === 'c-0421'),   // overdue
      base.find(c => c.id === 'c-0420'),   // completed
      base.find(c => c.id === 'c-0419'),   // aborted
    ];
  }, []);

  return (
    <div className="page-content scroll-y">
      <div className="page-inner">
        <div className="page-header">
          <div>
            <h1 className="page-title">CyclePhaseBar</h1>
            <div className="page-sub">
              Barra segmentada proporcional às fases planejadas da receita · 3 variantes · 5 estados visuais
            </div>
          </div>
        </div>

        {/* Anatomy */}
        <div className="showcase-section">
          <div className="t-micro-label" style={{ marginBottom: 12 }}>Anatomia (variante <span style={{ color: 'var(--neon-blue)' }}>full</span>)</div>
          <div className="showcase-card">
            <window.CyclePhaseBar cycle={demoCycles[0]} variant="full" tick={tick} />
            <div className="anatomy-notes">
              <div><span className="dot-cyan" /> <b>Label superior</b> — <span className="mono">phase-label-breathe</span> na fase ativa (opacidade + text-shadow, 3.6s)</div>
              <div><span className="dot-cyan" /> <b>Segmentos proporcionais</b> — largura = minutos planejados / total · gap 2px</div>
              <div><span className="dot-cyan" /> <b>Fill interno</b> — cresce dentro da fase ativa conforme o tempo avança</div>
              <div><span className="dot-cyan" /> <b>Shimmer</b> — varredura <span className="mono">phase-shimmer</span> a cada 2.6s no segmento atual</div>
              <div><span className="dot-cyan" /> <b>Tempo planejado</b> — tabular mono abaixo de cada fase</div>
            </div>
          </div>
        </div>

        {/* Variants */}
        <div className="showcase-section">
          <div className="t-micro-label" style={{ marginBottom: 12 }}>3 Variantes</div>
          <div className="variant-stack">
            <div className="variant-row">
              <div className="variant-label">
                <span className="variant-name">large</span>
                <span className="variant-hint">Parede de TVs · h 28px</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[0]} variant="large" tick={tick} />
            </div>
            <div className="variant-row">
              <div className="variant-label">
                <span className="variant-name">full</span>
                <span className="variant-hint">Detalhe · h 18px · com labels</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[0]} variant="full" tick={tick} />
            </div>
            <div className="variant-row">
              <div className="variant-label">
                <span className="variant-name">compact</span>
                <span className="variant-hint">Cards · h 10px · só a barra</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[0]} variant="compact" tick={tick} />
            </div>
          </div>
        </div>

        {/* States */}
        <div className="showcase-section">
          <div className="t-micro-label" style={{ marginBottom: 12 }}>5 Estados</div>
          <div className="state-grid">
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">Em andamento · fase intermediária</div>
                <span className="chip cyan"><span className="live-dot cyan" style={{ width: 6, height: 6 }} /> ATIVO</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[0]} variant="full" tick={tick} />
            </div>
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">Em andamento · fase inicial</div>
                <span className="chip cyan"><span className="live-dot cyan" style={{ width: 6, height: 6 }} /> ATIVO</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[1]} variant="full" tick={tick} />
            </div>
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">Atrasado · tempo excedido</div>
                <span className="chip pink"><span className="live-dot pink" style={{ width: 6, height: 6 }} /> ATRASO</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[3]} variant="full" tick={tick} />
            </div>
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">Concluído · todas as fases</div>
                <span className="chip green">✓ OK</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[4]} variant="full" tick={tick} />
            </div>
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">Abortado mid-phase</div>
                <span className="chip pink">✕ ABORTADO</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[5]} variant="full" tick={tick} />
            </div>
            <div className="state-card">
              <div className="flex jc-b ai-c" style={{ marginBottom: 12 }}>
                <div className="state-name">ETO · 5 fases longas</div>
                <span className="chip cyan"><span className="live-dot cyan" style={{ width: 6, height: 6 }} /> ATIVO</span>
              </div>
              <window.CyclePhaseBar cycle={demoCycles[2]} variant="full" tick={tick} />
            </div>
          </div>
        </div>

        {/* Fallback */}
        <div className="showcase-section">
          <div className="t-micro-label" style={{ marginBottom: 12 }}>Fallback · receita sem fases configuradas</div>
          <div className="showcase-card">
            <div className="mono" style={{ fontSize: 12, color: 'var(--white-40)', marginBottom: 10 }}>
              progresso linear clássico · 62%
            </div>
            <div style={{ height: 12, background: 'rgba(255,255,255,0.04)', border: '1px solid var(--hairline)', borderRadius: 3, overflow: 'hidden', position: 'relative' }}>
              <div style={{
                position: 'absolute', top: 0, left: 0, bottom: 0, width: '62%',
                background: 'linear-gradient(90deg, rgba(16,185,129,0.4), rgba(52,230,176,0.8))',
                boxShadow: '0 0 12px rgba(16,185,129,0.5)',
              }} />
            </div>
            <div style={{ fontSize: 12, color: 'var(--white-60)', marginTop: 10, fontFamily: 'var(--font-mono)' }}>
              Quando <code style={{ color: 'var(--neon-blue)' }}>afr.cycle.features.phases_planned</code> ainda não existe no Odoo, a UI cai para a barra simples sem quebrar nada.
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

window.StateBadgeInline = StateBadgeInline;
window.TempPressureChart = TempPressureChart;
window.buildEventLog = buildEventLog;
window.PhaseBarShowcase = PhaseBarShowcase;
