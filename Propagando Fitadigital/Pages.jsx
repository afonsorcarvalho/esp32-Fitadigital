/* Pages: List, Detail, Wall, Showcase */

const CycleList = ({ cycles, onOpen, tick, dashboard }) => {
  // Sort: in_progress, overdue, scheduled, then completed/aborted
  const order = { in_progress: 0, overdue: 1, scheduled: 2, completed: 3, aborted: 4 };
  const sorted = [...cycles].sort((a, b) => order[a.state] - order[b.state]);

  const groups = {
    active: sorted.filter(c => c.state === 'in_progress' || c.state === 'overdue'),
    scheduled: sorted.filter(c => c.state === 'scheduled'),
    done: sorted.filter(c => c.state === 'completed' || c.state === 'aborted'),
  };

  return (
    <div className="page-content scroll-y">
      <div className={`page-inner ${dashboard ? 'wide' : ''}`}>
        {/* Header row with KPI chips */}
        <div className="page-header">
          <div>
            <h1 className="page-title">Ciclos de esterilização</h1>
            <div className="page-sub">
              <span className="mono">{cycles.length} ciclos</span>
              <span className="dot-sep">·</span>
              <span>Sincronismo em tempo real via bus.bus / SSE</span>
              <span className="dot-sep">·</span>
              <span className="mono" style={{ color: 'var(--neon-green)' }}>
                <span className="live-dot" style={{ display: 'inline-block', width: 6, height: 6, marginRight: 6, verticalAlign: 'middle' }}></span>
                live
              </span>
            </div>
          </div>
          <div className="flex gap-2">
            <div className="kpi-chip">
              <div className="kpi-chip-label">EM ANDAMENTO</div>
              <div className="kpi-chip-value" style={{ color: 'var(--neon-blue)' }}>
                {groups.active.filter(c => c.state === 'in_progress').length}
              </div>
            </div>
            <div className="kpi-chip">
              <div className="kpi-chip-label">ATRASADOS</div>
              <div className="kpi-chip-value" style={{ color: 'var(--neon-pink)' }}>
                {cycles.filter(c => c.state === 'overdue').length}
              </div>
            </div>
            <div className="kpi-chip">
              <div className="kpi-chip-label">PROGRAMADOS</div>
              <div className="kpi-chip-value" style={{ color: 'var(--neon-orange)' }}>
                {groups.scheduled.length}
              </div>
            </div>
            <div className="kpi-chip">
              <div className="kpi-chip-label">CONCLUÍDOS HOJE</div>
              <div className="kpi-chip-value" style={{ color: 'var(--neon-green)' }}>
                27
              </div>
            </div>
          </div>
        </div>

        {/* Active section */}
        <div className="section-header">
          <div className="section-title">
            <span className="live-dot" style={{ width: 8, height: 8 }} />
            Em andamento
          </div>
          <div className="section-count mono">{groups.active.length} ciclos</div>
        </div>
        <div className="cycle-grid">
          {groups.active.map(c => (
            <window.CycleCard key={c.id} cycle={c} tick={tick} onOpen={onOpen} />
          ))}
        </div>

        {/* Scheduled */}
        {groups.scheduled.length > 0 && (
          <>
            <div className="section-header" style={{ marginTop: 28 }}>
              <div className="section-title">
                <span className="live-dot orange" style={{ width: 8, height: 8 }} />
                Programados para hoje
              </div>
              <div className="section-count mono">{groups.scheduled.length} ciclos</div>
            </div>
            <div className="cycle-grid">
              {groups.scheduled.map(c => (
                <window.CycleCard key={c.id} cycle={c} tick={tick} onOpen={onOpen} />
              ))}
            </div>
          </>
        )}

        {/* Done — hidden per user request */}
        {false && groups.done.length > 0 && (
          <>
            <div className="section-header" style={{ marginTop: 28 }}>
              <div className="section-title">
                <span className="live-dot idle" style={{ width: 8, height: 8 }} />
                Últimos concluídos
              </div>
              <div className="section-count mono">{groups.done.length} ciclos</div>
            </div>
            <div className="cycle-grid">
              {groups.done.map(c => (
                <window.CycleCard key={c.id} cycle={c} tick={tick} onOpen={onOpen} />
              ))}
            </div>
          </>
        )}
      </div>
    </div>
  );
};

/* ============================================================
   Detail page
   ============================================================ */
const CycleDetail = ({ cycle, cycles, onBack, onOpen, tick }) => {
  const isActive = cycle.state === 'in_progress' || cycle.state === 'overdue';
  const phase = isActive ? window.AFR.computePhase(cycle) : null;
  const idx = cycles.findIndex(c => c.id === cycle.id);
  const prev = cycles[idx - 1];
  const next = cycles[idx + 1];

  const hist = React.useMemo(() => window.AFR.chartHistory(cycle, 80), [cycle.id]);

  return (
    <div className="page-content scroll-y">
      <div className="page-inner">
        {/* sticky header */}
        <div className="detail-header">
          <button className="btn" onClick={onBack}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M15 18l-6-6 6-6" />
            </svg>
            Voltar
          </button>
          <div className="detail-crumbs">
            <span style={{ color: 'var(--white-40)' }}>Ciclos</span>
            <span className="crumb-sep">/</span>
            <span className="mono">{cycle.id.toUpperCase()}</span>
            <span className="crumb-sep">/</span>
            <span style={{ color: 'var(--white-95)' }}>{cycle.lot}</span>
          </div>
          <div className="flex gap-2" style={{ marginLeft: 'auto' }}>
            <button className="btn" disabled={!prev} onClick={() => prev && onOpen(prev)}>
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M15 18l-6-6 6-6" /></svg>
              Anterior
            </button>
            <span className="mono" style={{ fontSize: 12, color: 'var(--white-40)', alignSelf: 'center' }}>
              {idx + 1} / {cycles.length}
            </span>
            <button className="btn" disabled={!next} onClick={() => next && onOpen(next)}>
              Próximo
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M9 18l6-6-6-6" /></svg>
            </button>
          </div>
        </div>

        {/* Title block */}
        <div className={`detail-title-block ${isActive ? (cycle.state === 'overdue' ? 'overdue-glow' : 'in-progress-glow') : ''}`}>
          <div className="flex jc-b" style={{ alignItems: 'flex-start' }}>
            <div>
              <div className="flex gap-2 ai-c">
                <window.StateBadgeInline cycle={cycle} />
                <span className="mono" style={{ color: 'var(--white-40)', fontSize: 12, letterSpacing: '0.08em', textTransform: 'uppercase' }}>
                  {cycle.lot}
                </span>
              </div>
              <div className="detail-eq-name">{cycle.equipment.name}</div>
              <div className="detail-eq-sub">
                {cycle.equipment.location} · {cycle.recipeName} · op. {cycle.operator}
              </div>
            </div>

            {isActive && (
              <div className="detail-live-timer">
                <div className="flex gap-3 ai-b">
                  <div>
                    <div className="t-micro-label">Tempo decorrido</div>
                    <div
                      className="mono"
                      style={{
                        fontSize: 44,
                        fontWeight: 600,
                        color: cycle.state === 'overdue' ? 'var(--neon-pink)' : 'var(--neon-green-hi)',
                        lineHeight: 1,
                        letterSpacing: '-0.02em',
                        marginTop: 6,
                        textShadow: cycle.state === 'overdue'
                          ? '0 0 24px rgba(236,72,153,0.5)'
                          : '0 0 24px rgba(16,185,129,0.4)',
                      }}
                    >
                      {window.AFR.fmtMMSS(phase.elapsedSec)}
                    </div>
                  </div>
                  <div style={{ paddingBottom: 6 }}>
                    <div className="t-micro-label">/ previsto</div>
                    <div className="mono" style={{ fontSize: 22, fontWeight: 500, color: 'var(--white-40)', marginTop: 6, letterSpacing: '-0.01em' }}>
                      {window.AFR.fmtMMSS(cycle.plannedMinutes * 60)}
                    </div>
                  </div>
                </div>
                <div className="mono" style={{ fontSize: 13, color: 'var(--white-60)', marginTop: 12, letterSpacing: '0.02em' }}>
                  {window.AFR.fmtHMS(phase.elapsedSec)}{' '}
                  <span style={{ color: cycle.state === 'overdue' ? 'var(--neon-pink)' : 'var(--neon-green)' }}>
                    {cycle.state === 'overdue' ? '(atrasado +' + window.AFR.fmtMMSS(phase.overtimeSec) + ')' : '(em andamento)'}
                  </span>
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Phase bar full */}
        <div className="panel-block">
          <div className="panel-block-head">
            <div>
              <div className="t-micro-label">Progresso por fase</div>
              <div className="panel-title">
                {isActive ? (
                  <>Fase atual: <span style={{ color: cycle.state === 'overdue' ? 'var(--neon-pink)' : 'var(--neon-green-hi)' }}>{cycle.recipe[phase.phaseIndex].label}</span></>
                ) : cycle.state === 'completed' ? (
                  <>Todas as fases concluídas</>
                ) : cycle.state === 'aborted' ? (
                  <>Interrompido na fase <span style={{ color: 'var(--neon-pink)' }}>{cycle.recipe[window.AFR.computePhase(cycle).phaseIndex].label}</span></>
                ) : (
                  <>Aguardando início</>
                )}
              </div>
            </div>
            {isActive && (
              <div className="mono" style={{ fontSize: 13, color: 'var(--white-60)' }}>
                <span style={{ color: 'var(--neon-green-hi)' }}>{Math.round(phase.phaseProgress * 100)}%</span>
                {' '}da fase · restam <span style={{ color: 'var(--white-80)' }}>
                  {window.AFR.fmtMMSS((1 - phase.phaseProgress) * cycle.recipe[phase.phaseIndex].minutes * 60)}
                </span>
              </div>
            )}
          </div>
          <div style={{ padding: '24px 28px 28px' }}>
            <window.CyclePhaseBar cycle={cycle} variant="large" tick={tick} />
          </div>
        </div>

        {/* Chart + meta */}
        <div className="detail-grid">
          <div className="panel-block">
            <div className="panel-block-head">
              <div>
                <div className="t-micro-label">Tendência ao vivo</div>
                <div className="panel-title">Temperatura & Pressão</div>
              </div>
              <div className="flex gap-3">
                <div className="flex ai-c gap-2">
                  <span style={{ width: 10, height: 2, background: 'var(--neon-green)' }}></span>
                  <span className="mono" style={{ fontSize: 11, color: 'var(--white-60)' }}>
                    T · <span style={{ color: 'var(--neon-green-hi)' }}>{cycle.tempNow?.toFixed(1) || '—'} °C</span>
                  </span>
                </div>
                <div className="flex ai-c gap-2">
                  <span style={{ width: 10, height: 2, background: 'var(--neon-blue)' }}></span>
                  <span className="mono" style={{ fontSize: 11, color: 'var(--white-60)' }}>
                    P · <span style={{ color: 'var(--neon-blue)' }}>{cycle.pressureNow?.toFixed(2) || '—'} bar</span>
                  </span>
                </div>
              </div>
            </div>
            <div style={{ padding: '12px 20px 16px', height: 280 }}>
              <window.TempPressureChart hist={hist} cycle={cycle} tick={tick} />
            </div>
          </div>

          <div className="panel-block">
            <div className="panel-block-head">
              <div>
                <div className="t-micro-label">Registro & rastreabilidade</div>
                <div className="panel-title">Eventos do ciclo</div>
              </div>
            </div>
            <div className="event-log">
              {window.buildEventLog(cycle, phase).map((ev, i) => (
                <div key={i} className={`event-row ${ev.level}`}>
                  <span className="event-ts mono">{ev.ts}</span>
                  <span className={`event-level ${ev.level}`}>{ev.tag}</span>
                  <span className="event-msg">{ev.msg}</span>
                </div>
              ))}
            </div>
          </div>
        </div>

        {/* Cycle metadata */}
        <div className="panel-block">
          <div className="panel-block-head">
            <div className="panel-title">Parâmetros & assinatura digital</div>
          </div>
          <div className="meta-grid">
            <div className="meta-item">
              <div className="t-micro-label">Receita</div>
              <div className="meta-value">{cycle.recipeName}</div>
            </div>
            <div className="meta-item">
              <div className="t-micro-label">Material</div>
              <div className="meta-value">{cycle.material}</div>
            </div>
            <div className="meta-item">
              <div className="t-micro-label">Operador</div>
              <div className="meta-value">{cycle.operator}</div>
            </div>
            <div className="meta-item">
              <div className="t-micro-label">Equipamento</div>
              <div className="meta-value mono">{cycle.equipment.id}</div>
            </div>
            <div className="meta-item">
              <div className="t-micro-label">Lote</div>
              <div className="meta-value mono">{cycle.lot}</div>
            </div>
            <div className="meta-item">
              <div className="t-micro-label">Duração prevista</div>
              <div className="meta-value mono">{window.AFR.fmtHHMM(cycle.plannedMinutes)}</div>
            </div>
            {cycle.bi && (
              <div className="meta-item">
                <div className="t-micro-label">Indicador biológico</div>
                <div className="meta-value" style={{ color: 'var(--neon-green)' }}>✓ {cycle.bi}</div>
              </div>
            )}
            {cycle.result && (
              <div className="meta-item">
                <div className="t-micro-label">Resultado</div>
                <div className="meta-value" style={{
                  color: cycle.state === 'completed' ? 'var(--neon-green)' : 'var(--neon-pink)'
                }}>
                  {cycle.state === 'completed' ? '✓ ' : '✕ '}{cycle.result}
                </div>
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

/* ============================================================
   Wall mode — Parede de TVs
   ============================================================ */
const CycleWall = ({ cycles, tick }) => {
  const active = cycles.filter(c => c.state === 'in_progress' || c.state === 'overdue');

  return (
    <div className="page-content wall-mode scroll-y">
      <div className="wall-header">
        <div>
          <div className="t-micro-label">Modo parede</div>
          <h1 className="wall-title">Ciclos em andamento · CME São Lucas</h1>
        </div>
        <div className="flex gap-6 ai-c">
          <div className="flex ai-c gap-3">
            <span className="live-dot" />
            <span className="mono" style={{ fontSize: 14, color: 'var(--white-80)', letterSpacing: '0.08em', textTransform: 'uppercase' }}>
              {active.length} ciclos ativos
            </span>
          </div>
          <div className="mono wall-clock">
            {new Date().toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })}
          </div>
        </div>
      </div>

      <div className={`wall-grid wall-grid-${Math.min(active.length, 4)}`}>
        {active.map(c => {
          const phase = window.AFR.computePhase(c);
          const isOverdue = c.state === 'overdue';
          return (
            <div
              key={c.id}
              className={`wall-card ${isOverdue ? 'overdue-glow' : 'in-progress-glow'}`}
            >
              <div className="flex jc-b ai-b">
                <div>
                  <div className="mono" style={{
                    fontSize: 13, color: 'var(--white-40)',
                    letterSpacing: '0.1em', textTransform: 'uppercase',
                  }}>
                    {c.lot}
                  </div>
                  <div className="wall-eq-name">{c.equipment.name}</div>
                  <div className="wall-eq-loc">{c.equipment.location}</div>
                </div>
                <span className={`chip ${isOverdue ? 'pink' : 'cyan'}`} style={{ fontSize: 12, padding: '6px 12px' }}>
                  {isOverdue ? 'ATRASADO' : 'EM ANDAMENTO'}
                </span>
              </div>

              <div className="wall-timer-row">
                <div>
                  <div className="t-micro-label">Decorrido</div>
                  <div
                    className="mono wall-timer"
                    style={{ color: isOverdue ? 'var(--neon-pink)' : 'var(--neon-green-hi)' }}
                  >
                    {window.AFR.fmtMMSS(phase.elapsedSec)}
                  </div>
                </div>
                <div style={{ textAlign: 'right' }}>
                  <div className="t-micro-label">Fase atual</div>
                  <div
                    className="phase-label-breathe"
                    style={{
                      fontSize: 22, fontWeight: 600, marginTop: 8,
                      color: isOverdue ? 'var(--neon-pink)' : 'var(--neon-green-hi)',
                      letterSpacing: '0.06em', textTransform: 'uppercase',
                    }}
                  >
                    {c.recipe[phase.phaseIndex].label}
                  </div>
                </div>
              </div>

              <window.CyclePhaseBar cycle={c} variant="large" tick={tick} />

              <div className="wall-meta">
                <span className="chip">
                  {c.tempNow?.toFixed(1)} °C
                </span>
                <span className="chip">
                  {c.pressureNow?.toFixed(2)} bar
                </span>
                {c.humidity && <span className="chip">UR {c.humidity}%</span>}
                <span className="chip" style={{ marginLeft: 'auto', background: 'transparent', border: 'none', color: 'var(--white-40)' }}>
                  op. {c.operator}
                </span>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};

window.CycleList = CycleList;
window.CycleDetail = CycleDetail;
window.CycleWall = CycleWall;
