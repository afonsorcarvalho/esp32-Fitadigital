/* Main App — routing, realtime sim, tweaks */

const { useState, useEffect, useRef, useMemo } = React;

const TWEAKS_DEFAULT = /*EDITMODE-BEGIN*/{
  "dashboardMode": false,
  "sidebarCollapsed": false,
  "colorblindPalette": false
}/*EDITMODE-END*/;

const App = () => {
  const [route, setRoute] = useState(() => localStorage.getItem('afr.route') || 'list');
  const [selectedCycleId, setSelectedCycleId] = useState(() => localStorage.getItem('afr.cycle') || null);

  const [cycles, setCycles] = useState(() => window.AFR.makeCycles());
  const [tick, setTick] = useState(0);

  // Tweaks state
  const [dashboardMode, setDashboardMode] = useState(TWEAKS_DEFAULT.dashboardMode);
  const [sidebarCollapsed, setSidebarCollapsed] = useState(TWEAKS_DEFAULT.sidebarCollapsed);
  const [colorblind, setColorblind] = useState(TWEAKS_DEFAULT.colorblindPalette);
  const [showTweaks, setShowTweaks] = useState(false);

  // Live palette class
  useEffect(() => {
    document.documentElement.classList.toggle('palette-cb', colorblind);
  }, [colorblind]);

  // Persist route + cycle
  useEffect(() => { localStorage.setItem('afr.route', route); }, [route]);
  useEffect(() => { if (selectedCycleId) localStorage.setItem('afr.cycle', selectedCycleId); }, [selectedCycleId]);

  // Tick (1s) — drives timers, fills, shimmer refreshes
  useEffect(() => {
    const id = setInterval(() => setTick(t => t + 1), 1000);
    return () => clearInterval(id);
  }, []);

  // Simulated realtime bus — every ~15s, "something happens":
  //  - a scheduled cycle flips to in_progress
  //  - or a new cycle appears
  //  - or a completed cycle drops off
  useEffect(() => {
    const id = setInterval(() => {
      setCycles(prev => {
        const next = [...prev];
        // pick a scheduled one and start it
        const sched = next.find(c => c.state === 'scheduled');
        if (sched) {
          sched.state = 'in_progress';
          sched.startedAt = Date.now() - 30 * 1000; // 30s in
          sched.tempNow = 45 + Math.random() * 8;
          sched.pressureNow = 0.3 + Math.random() * 0.3;
          return [...next];
        }
        return next;
      });
    }, 18000);
    return () => clearInterval(id);
  }, []);

  // Simulate live temp/pressure drift on active cycles
  useEffect(() => {
    setCycles(prev => prev.map(c => {
      if (c.state !== 'in_progress' && c.state !== 'overdue') return c;
      const ph = window.AFR.computePhase(c);
      const isETO = c.equipment.type === 'eto';
      const phaseLabel = c.recipe[ph.phaseIndex].label.toLowerCase();
      const inSteril = phaseLabel.includes('esteril') || phaseLabel.includes('exposicao');
      const targetT = isETO ? 55 : (inSteril ? 121 : (ph.phaseIndex >= 1 ? 100 : 60));
      const drift = (Math.random() - 0.5) * 0.4;
      return {
        ...c,
        tempNow: c.tempNow ? c.tempNow * 0.98 + targetT * 0.02 + drift : targetT,
        pressureNow: c.pressureNow ? c.pressureNow * 0.98 + (isETO ? 0.12 : (inSteril ? 2.0 : 1.0)) * 0.02 + (Math.random() - 0.5) * 0.02 : 1.0,
      };
    }));
  }, [tick]);

  // keyboard: F = dashboard mode, Esc = back
  useEffect(() => {
    const h = (e) => {
      if (e.key === 'f' && !e.metaKey && !e.ctrlKey && e.target.tagName !== 'INPUT') {
        setDashboardMode(d => !d);
      }
      if (e.key === 'Escape' && route === 'detail') setRoute('list');
    };
    window.addEventListener('keydown', h);
    return () => window.removeEventListener('keydown', h);
  }, [route]);

  const persistTweak = (patch) => {
    try {
      window.parent.postMessage({ type: '__edit_mode_set_keys', edits: patch }, '*');
    } catch (e) {}
  };

  // Edit mode listener
  useEffect(() => {
    const handler = (e) => {
      if (e.data?.type === '__activate_edit_mode') setShowTweaks(true);
      if (e.data?.type === '__deactivate_edit_mode') setShowTweaks(false);
    };
    window.addEventListener('message', handler);
    window.parent.postMessage({ type: '__edit_mode_available' }, '*');
    return () => window.removeEventListener('message', handler);
  }, []);

  const openCycle = (c) => {
    setSelectedCycleId(c.id);
    setRoute('detail');
  };

  const currentCycle = cycles.find(c => c.id === selectedCycleId);

  const crumbs =
    route === 'list' ? ['CME São Lucas', 'Ciclos'] :
    route === 'detail' ? ['CME São Lucas', 'Ciclos', currentCycle?.lot || ''] :
    route === 'wall' ? ['CME São Lucas', 'Parede de TVs'] :
    route === 'showcase' ? ['Design System', 'CyclePhaseBar'] :
    ['CME São Lucas'];

  // Determine active nav
  const navRoute =
    route === 'detail' ? 'list' : route;

  return (
    <>
      <div className="bg-ambient" />
      <div className="bg-grid" />

      <div className={`app-shell ${sidebarCollapsed ? 'collapsed' : ''} ${dashboardMode ? 'dashboard' : ''}`}>
        <window.Sidebar
          collapsed={sidebarCollapsed}
          onToggle={() => setSidebarCollapsed(v => !v)}
          currentRoute={navRoute}
          onNavigate={(r) => setRoute(r)}
        />

        <div className="main-area">
          {route !== 'wall' && (
            <window.Topbar
              crumbs={crumbs}
              collapsed={sidebarCollapsed}
              onToggleCollapse={() => setSidebarCollapsed(v => !v)}
              onToggleDashboard={() => {
                setDashboardMode(v => !v);
                persistTweak({ dashboardMode: !dashboardMode });
              }}
              isDashboard={dashboardMode}
            />
          )}

          {route === 'list' && (
            <window.CycleList cycles={cycles} onOpen={openCycle} tick={tick} dashboard={dashboardMode} />
          )}
          {route === 'detail' && currentCycle && (
            <window.CycleDetail
              cycle={currentCycle}
              cycles={cycles}
              onBack={() => setRoute('list')}
              onOpen={openCycle}
              tick={tick}
            />
          )}
          {route === 'wall' && (
            <>
              <div className="topbar">
                <button className="topbar-btn" onClick={() => setRoute('list')}>
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
                    <path d="M15 18l-6-6 6-6" />
                  </svg>
                </button>
                <div className="crumbs">
                  <span className="crumb">CME São Lucas</span>
                  <span className="crumb-sep">/</span>
                  <span className="crumb-active">Parede de TVs</span>
                </div>
                <div className="topbar-right">
                  <div className="live-indicator">
                    <span className="live-dot" style={{ width: 6, height: 6 }} />
                    <span className="mono" style={{ fontSize: 11, color: 'var(--white-60)', letterSpacing: '0.08em', textTransform: 'uppercase' }}>
                      Wall mode
                    </span>
                  </div>
                </div>
              </div>
              <window.CycleWall cycles={cycles} tick={tick} />
            </>
          )}
          {route === 'showcase' && (
            <window.PhaseBarShowcase tick={tick} />
          )}
        </div>
      </div>

      {showTweaks && (
        <div className="tweaks-panel">
          <div className="tweaks-head">
            <span className="tweaks-title">Tweaks</span>
            <button
              className="topbar-btn"
              style={{ width: 24, height: 24 }}
              onClick={() => setShowTweaks(false)}
            >
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M6 6l12 12 M18 6L6 18" />
              </svg>
            </button>
          </div>

          <div className="tweak-row">
            <div>
              <div className="tweak-label">Modo dashboard</div>
              <div className="tweak-hint">Sidebar recolhida, conteúdo full-width</div>
            </div>
            <div
              className={`switch ${dashboardMode ? 'on' : ''}`}
              onClick={() => {
                setDashboardMode(v => !v);
                persistTweak({ dashboardMode: !dashboardMode });
              }}
            />
          </div>

          <div className="tweak-row">
            <div>
              <div className="tweak-label">Sidebar recolhida</div>
              <div className="tweak-hint">Mostra apenas ícones</div>
            </div>
            <div
              className={`switch ${sidebarCollapsed ? 'on' : ''}`}
              onClick={() => setSidebarCollapsed(v => !v)}
            />
          </div>

          <div className="tweak-row">
            <div>
              <div className="tweak-label">Paleta daltônica</div>
              <div className="tweak-hint">Azul/âmbar no lugar de verde/rosa</div>
            </div>
            <div
              className={`switch ${colorblind ? 'on' : ''}`}
              onClick={() => {
                setColorblind(v => !v);
                persistTweak({ colorblindPalette: !colorblind });
              }}
            />
          </div>

          <div style={{ borderTop: '1px solid var(--hairline)', marginTop: 8, paddingTop: 12 }}>
            <div className="tweak-label" style={{ marginBottom: 8 }}>Ir para</div>
            <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
              <button className={`btn ${route === 'list' ? 'btn-primary' : ''}`} style={{ fontSize: 11 }} onClick={() => setRoute('list')}>Lista</button>
              <button className={`btn ${route === 'detail' ? 'btn-primary' : ''}`} style={{ fontSize: 11 }} onClick={() => { if (!currentCycle) setSelectedCycleId(cycles[0].id); setRoute('detail'); }}>Detalhe</button>
              <button className={`btn ${route === 'wall' ? 'btn-primary' : ''}`} style={{ fontSize: 11 }} onClick={() => setRoute('wall')}>Wall</button>
              <button className={`btn ${route === 'showcase' ? 'btn-primary' : ''}`} style={{ fontSize: 11 }} onClick={() => setRoute('showcase')}>Phase Bar</button>
            </div>
          </div>
        </div>
      )}
    </>
  );
};

const root = ReactDOM.createRoot(document.getElementById('root'));
root.render(<App />);
