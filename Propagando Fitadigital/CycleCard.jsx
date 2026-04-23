/* CycleCard: one card per cycle. Adapts by state. */

const { useState, useEffect, useRef } = React;

// Inline live mini-chart (temp curve) for in-progress cards
const MiniChart = ({ cycle, tick }) => {
  const hist = React.useMemo(() => window.AFR.chartHistory(cycle, 40), [cycle.id]);
  const w = 240, h = 40;
  const vals = hist.temps;
  const min = Math.min(...vals), max = Math.max(...vals);
  const range = max - min || 1;
  const pts = vals.map((v, i) => {
    const x = (i / (vals.length - 1)) * w;
    const y = h - ((v - min) / range) * (h - 4) - 2;
    return `${x.toFixed(1)},${y.toFixed(1)}`;
  }).join(' ');
  const last = vals[vals.length - 1];
  const lastX = w;
  const lastY = h - ((last - min) / range) * (h - 4) - 2;

  return (
    <svg className="card-mini-chart" viewBox={`0 0 ${w} ${h}`} preserveAspectRatio="none" style={{ width: '100%' }}>
      <defs>
        <linearGradient id={`mcg-${cycle.id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(0,212,255,0.25)" />
          <stop offset="100%" stopColor="rgba(0,212,255,0)" />
        </linearGradient>
      </defs>
      <polyline
        points={`0,${h} ${pts} ${w},${h}`}
        fill={`url(#mcg-${cycle.id})`}
        stroke="none"
      />
      <polyline
        points={pts}
        fill="none"
        stroke="var(--neon-blue)"
        strokeWidth="1.2"
        strokeLinejoin="round"
        strokeLinecap="round"
      />
      <circle cx={lastX - 2} cy={lastY} r="2.2" fill="var(--neon-blue)">
        <animate attributeName="r" values="2.2;3.5;2.2" dur="1.4s" repeatCount="indefinite" />
      </circle>
    </svg>
  );
};

const StateBadge = ({ cycle }) => {
  const map = {
    in_progress: { cls: 'chip cyan', label: 'EM ANDAMENTO', dot: 'cyan' },
    overdue:     { cls: 'chip pink', label: 'ATRASADO',     dot: 'pink' },
    scheduled:   { cls: 'chip orange', label: 'PROGRAMADO', dot: 'orange' },
    completed:   { cls: 'chip green', label: 'CONCLUÍDO',   dot: null },
    aborted:     { cls: 'chip pink', label: 'ABORTADO',     dot: null },
  };
  const cfg = map[cycle.state];
  return (
    <span className={cfg.cls} style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
      {cfg.dot && <span className={`live-dot ${cfg.dot}`} style={{ width: 6, height: 6 }} />}
      {cfg.label}
    </span>
  );
};

const CycleCard = ({ cycle, tick, onOpen, compact = false }) => {
  const isActive = cycle.state === 'in_progress' || cycle.state === 'overdue';
  const phase = isActive ? window.AFR.computePhase(cycle) : null;

  let glowClass = '';
  if (cycle.state === 'in_progress') glowClass = 'in-progress-glow';
  else if (cycle.state === 'overdue') glowClass = 'overdue-glow';
  else if (cycle.state === 'scheduled') glowClass = 'scheduled-today-glow';

  return (
    <div
      className={`cycle-card fade-slide ${glowClass} ${cycle.state}`}
      onClick={() => onOpen && onOpen(cycle)}
    >
      <div className="card-head">
        <div>
          <div className="eq-name">{cycle.equipment.name}</div>
          <div className="eq-loc">{cycle.equipment.location}</div>
          <div className="lot">{cycle.lot}</div>
        </div>
        <StateBadge cycle={cycle} />
      </div>

      <div className="recipe-name">
        <span style={{ color: 'var(--white-40)' }}>Receita · </span>
        {cycle.recipeName}
      </div>

      {/* State-specific body */}
      {isActive && (
        <>
          <div className="flex ai-b jc-b" style={{ gap: 12 }}>
            <div>
              <div className="timer" style={{
                color: cycle.state === 'overdue' ? 'var(--neon-pink)' : 'var(--neon-green-hi)'
              }}>
                {window.AFR.fmtHMS(phase.elapsedSec)}
              </div>
              <div style={{ marginTop: 4, fontSize: 11, color: 'var(--white-40)', fontFamily: 'var(--font-mono)' }}>
                de {window.AFR.fmtHHMM(cycle.plannedMinutes)} previstos
                {cycle.state === 'overdue' && phase.overtime && (
                  <> · <span style={{ color: 'var(--neon-pink)' }}>+{window.AFR.fmtMMSS(phase.overtimeSec)} atraso</span></>
                )}
              </div>
            </div>
            <div style={{ textAlign: 'right' }}>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Fase atual</div>
              <div
                className="mono phase-label-breathe"
                style={{
                  fontSize: 14, fontWeight: 600,
                  color: cycle.state === 'overdue' ? 'var(--neon-pink)' : 'var(--neon-green-hi)',
                  marginTop: 4,
                  textTransform: 'uppercase', letterSpacing: '0.08em',
                }}
              >
                {cycle.recipe[phase.phaseIndex].label}
              </div>
            </div>
          </div>

          <window.CyclePhaseBar cycle={cycle} variant="compact" tick={tick} />

          <MiniChart cycle={cycle} tick={tick} />

          <div className="meta-row">
            <span className="chip">
              <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                <path d="M12 2 L12 8 M12 8 L16 12 M12 8 L8 12 M4 18 L20 18" />
              </svg>
              {cycle.tempNow?.toFixed(1)} °C
            </span>
            <span className="chip">
              <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                <circle cx="12" cy="12" r="8" /><path d="M12 8 L12 12 L15 13" />
              </svg>
              {cycle.pressureNow?.toFixed(2)} bar
            </span>
            {cycle.humidity && <span className="chip">UR {cycle.humidity}%</span>}
            <span className="chip" style={{ marginLeft: 'auto', background: 'transparent', border: 'none', color: 'var(--white-40)' }}>
              op. {cycle.operator}
            </span>
          </div>
        </>
      )}

      {cycle.state === 'scheduled' && (
        <>
          <div className="flex ai-b jc-b">
            <div>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Início previsto</div>
              <div className="mono" style={{ fontSize: 28, fontWeight: 600, color: 'var(--neon-orange)', marginTop: 6, letterSpacing: '-0.01em' }}>
                {cycle.scheduledFor}
              </div>
            </div>
            <div style={{ textAlign: 'right' }}>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Duração prevista</div>
              <div className="mono" style={{ fontSize: 15, fontWeight: 600, color: 'var(--white-80)', marginTop: 6 }}>
                {window.AFR.fmtHHMM(cycle.plannedMinutes)}
              </div>
            </div>
          </div>
          <window.CyclePhaseBar cycle={cycle} variant="compact" tick={tick} />
          <div className="meta-row">
            <span className="chip">{cycle.material}</span>
            <span className="chip" style={{ marginLeft: 'auto', background: 'transparent', border: 'none', color: 'var(--white-40)' }}>
              op. {cycle.operator}
            </span>
          </div>
        </>
      )}

      {cycle.state === 'completed' && (
        <>
          <div className="flex ai-b jc-b">
            <div>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Duração total</div>
              <div className="mono" style={{ fontSize: 22, fontWeight: 600, color: 'var(--neon-green)', marginTop: 6, letterSpacing: '-0.01em' }}>
                {window.AFR.fmtHHMM(cycle.plannedMinutes)}
              </div>
            </div>
            <div style={{ textAlign: 'right' }}>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Resultado</div>
              <div style={{ fontSize: 15, fontWeight: 600, color: 'var(--neon-green)', marginTop: 6 }}>
                ✓ {cycle.result}
              </div>
            </div>
          </div>
          <window.CyclePhaseBar cycle={cycle} variant="compact" tick={tick} />
          <div className="meta-row">
            <span className="chip green">IB {cycle.bi}</span>
            <span className="chip">{cycle.material}</span>
            <span className="chip" style={{ marginLeft: 'auto', background: 'transparent', border: 'none', color: 'var(--white-40)' }}>
              assin. {cycle.operator}
            </span>
          </div>
        </>
      )}

      {cycle.state === 'aborted' && (
        <>
          <div className="flex ai-b jc-b">
            <div>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Abortado após</div>
              <div className="mono" style={{ fontSize: 22, fontWeight: 600, color: 'var(--neon-pink)', marginTop: 6, letterSpacing: '-0.01em' }}>
                {window.AFR.fmtHHMM(40)}
              </div>
            </div>
            <div style={{ textAlign: 'right', maxWidth: '60%' }}>
              <div style={{ fontSize: 11, color: 'var(--white-40)', letterSpacing: '0.08em', textTransform: 'uppercase', fontWeight: 500 }}>Motivo</div>
              <div style={{ fontSize: 13, fontWeight: 500, color: 'var(--neon-pink)', marginTop: 6 }}>
                {cycle.abortReason}
              </div>
            </div>
          </div>
          <window.CyclePhaseBar cycle={cycle} variant="compact" tick={tick} />
          <div className="meta-row">
            <span className="chip pink">{cycle.result}</span>
            <span className="chip">{cycle.material}</span>
            <span className="chip" style={{ marginLeft: 'auto', background: 'transparent', border: 'none', color: 'var(--white-40)' }}>
              op. {cycle.operator}
            </span>
          </div>
        </>
      )}

      <div className="card-detail-cta">Ver detalhes →</div>
    </div>
  );
};

window.CycleCard = CycleCard;
