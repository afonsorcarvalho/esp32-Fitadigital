/* Sidebar + Topbar + Alarm bar */

const Sidebar = ({ collapsed, onToggle, currentRoute, onNavigate }) => {
  const navItems = [
    { key: 'dashboard', label: 'Visão geral', route: 'list', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <rect x="3" y="3" width="8" height="10" rx="1.5" />
        <rect x="13" y="3" width="8" height="6" rx="1.5" />
        <rect x="13" y="11" width="8" height="10" rx="1.5" />
        <rect x="3" y="15" width="8" height="6" rx="1.5" />
      </svg>
    )},
    { key: 'cycles', label: 'Ciclos', route: 'list', badge: '5', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <path d="M21 12a9 9 0 1 1-3.6-7.2" /><path d="M21 4v5h-5" />
      </svg>
    )},
    { key: 'wall', label: 'Parede de TVs', route: 'wall', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <rect x="3" y="4" width="18" height="12" rx="1.5" />
        <path d="M8 20h8 M12 16v4" />
      </svg>
    )},
    { key: 'os', label: 'Ordens de serviço', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <path d="M9 4h6l1 2h3v14H5V6h3Z" /><path d="M9 13h6 M9 17h4" />
      </svg>
    )},
    { key: 'equip', label: 'Equipamentos', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <rect x="4" y="6" width="16" height="12" rx="2" /><circle cx="12" cy="12" r="3" /><path d="M4 10h16" />
      </svg>
    )},
    { key: 'contacts', label: 'Contatos', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <circle cx="12" cy="8" r="3.5" /><path d="M5 20c0-3.5 3.1-6 7-6s7 2.5 7 6" />
      </svg>
    )},
    { key: 'showcase', label: 'Phase Bar (kit)', route: 'showcase', icon: (
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6">
        <path d="M3 6h18 M3 12h18 M3 18h18" />
      </svg>
    )},
  ];

  const [sect, sectItems] = [
    [
      { label: 'OPERAÇÃO', items: navItems.slice(0,4) },
      { label: 'CADASTROS', items: navItems.slice(4,6) },
      { label: 'DESIGN', items: navItems.slice(6) },
    ]
  ];

  return (
    <aside className={`sidebar ${collapsed ? 'collapsed' : ''}`}>
      <div className="sidebar-top">
        <div className="brand-block">
          <div className="brand-mark">
            <svg viewBox="0 0 64 64" style={{ width: 32, height: 32 }}>
              <rect x="4" y="4" width="56" height="56" rx="8" fill="rgba(0,212,255,0.1)" stroke="rgba(0,212,255,0.5)" strokeWidth="1.2" />
              <path d="M18 46 L24 18 L30 18 L36 46 M20 38 L34 38" stroke="var(--neon-blue)" strokeWidth="2.5" fill="none" strokeLinecap="square" />
              <circle cx="48" cy="18" r="3" fill="var(--neon-green)">
                <animate attributeName="opacity" values="1;0.3;1" dur="1.8s" repeatCount="indefinite" />
              </circle>
            </svg>
          </div>
          {!collapsed && (
            <div>
              <div className="brand-name">AFR Soluções</div>
              <div className="brand-sub">CME · Hosp. São Lucas</div>
            </div>
          )}
        </div>
      </div>

      <nav className="sidebar-nav scroll-y">
        {[
          { label: 'OPERAÇÃO', items: navItems.slice(0, 4) },
          { label: 'CADASTROS', items: navItems.slice(4, 6) },
          { label: 'DESIGN SYSTEM', items: navItems.slice(6) },
        ].map(sect => (
          <div key={sect.label}>
            {!collapsed && <div className="nav-sect">{sect.label}</div>}
            {sect.items.map(item => (
              <button
                key={item.key}
                className={`nav-item ${currentRoute === item.route ? 'active' : ''}`}
                onClick={() => item.route && onNavigate(item.route)}
                title={collapsed ? item.label : ''}
              >
                <span className="nav-icon">{item.icon}</span>
                {!collapsed && <span className="nav-label">{item.label}</span>}
                {!collapsed && item.badge && <span className="nav-badge">{item.badge}</span>}
              </button>
            ))}
          </div>
        ))}
      </nav>

      <div className="sidebar-foot">
        {!collapsed ? (
          <>
            <div className="user-avatar">JS</div>
            <div className="user-info">
              <div className="user-name">João Silva</div>
              <div className="user-role">Engenheiro · 2847</div>
            </div>
            <div className="user-status" title="Online"><span className="live-dot" /></div>
          </>
        ) : (
          <div className="user-avatar" title="João Silva">JS</div>
        )}
      </div>
    </aside>
  );
};

const Topbar = ({ crumbs, collapsed, onToggleCollapse, onToggleDashboard, isDashboard, children }) => {
  const [time, setTime] = useState(new Date());
  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(id);
  }, []);

  return (
    <div className="topbar">
      <button
        className="topbar-btn"
        onClick={onToggleCollapse}
        title={collapsed ? 'Expandir sidebar' : 'Recolher sidebar'}
      >
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
          <path d={collapsed ? 'M4 6h16 M4 12h16 M4 18h16' : 'M6 4l-3 8 3 8 M21 4l3 8-3 8 M12 4v16'} />
        </svg>
      </button>

      <div className="crumbs">
        {crumbs.map((c, i) => (
          <React.Fragment key={i}>
            {i > 0 && <span className="crumb-sep">/</span>}
            <span className={i === crumbs.length - 1 ? 'crumb-active' : 'crumb'}>{c}</span>
          </React.Fragment>
        ))}
      </div>

      {children}

      <div className="topbar-right">
        <div className="live-indicator">
          <span className="live-dot" style={{ width: 6, height: 6 }} />
          <span className="mono" style={{ fontSize: 11, color: 'var(--white-60)', letterSpacing: '0.08em', textTransform: 'uppercase' }}>
            Sincronizado · SSE
          </span>
        </div>
        <div className="mono" style={{ fontSize: 13, color: 'var(--white-80)' }}>
          {time.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
        </div>
        <button
          className={`btn ${isDashboard ? 'btn-primary' : ''}`}
          onClick={onToggleDashboard}
          title="Modo Dashboard (F)"
        >
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
            <path d="M3 3h6v6H3z M15 3h6v6h-6z M3 15h6v6H3z M15 15h6v6h-6z" />
          </svg>
          {isDashboard ? 'Sair do modo' : 'Modo dashboard'}
        </button>
      </div>
    </div>
  );
};

window.Sidebar = Sidebar;
window.Topbar = Topbar;
