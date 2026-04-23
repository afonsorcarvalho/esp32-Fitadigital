/* CyclePhaseBar — the protagonist.
   Segments proportional to planned phase minutes.
   Full variant: label (top) + bar + planned time (bottom).
   Compact variant: 12px bar only. */

const CyclePhaseBar = ({ cycle, variant = 'full', tick }) => {
  const totalMin = cycle.recipe.reduce((a, p) => a + p.minutes, 0);
  const isActive = cycle.state === 'in_progress' || cycle.state === 'overdue';
  const phase = isActive ? window.AFR.computePhase(cycle) : null;

  // For completed: all segments done. For scheduled/aborted: all future.
  let currentIndex = -1;
  let currentProgress = 0;
  if (isActive) {
    currentIndex = phase.phaseIndex;
    currentProgress = phase.phaseProgress;
  } else if (cycle.state === 'completed') {
    currentIndex = cycle.recipe.length; // all past
  } else if (cycle.state === 'aborted') {
    // Aborted mid-way — use startedAt to find where
    const abortedPhase = window.AFR.computePhase(cycle);
    currentIndex = abortedPhase.phaseIndex;
    currentProgress = abortedPhase.phaseProgress;
  }

  const barHeight = variant === 'compact' ? 10 : variant === 'large' ? 28 : 18;

  return (
    <div className="phase-bar" style={{ width: '100%' }}>
      {/* Top row: labels */}
      {variant !== 'compact' && (
        <div className="phase-bar-labels">
          {cycle.recipe.map((p, i) => {
            const isCurrent = i === currentIndex && isActive;
            const isDone = i < currentIndex;
            const pct = (p.minutes / totalMin) * 100;
            return (
              <div
                key={p.key}
                className={`phase-bar-label ${isCurrent ? 'phase-label-breathe' : ''}`}
                style={{
                  width: pct + '%',
                  color: isCurrent ? 'var(--neon-green-hi)' :
                         isDone ? 'var(--white-60)' :
                         cycle.state === 'aborted' && i > currentIndex ? 'var(--white-25)' :
                         'var(--white-40)',
                  fontWeight: isCurrent ? 600 : 500,
                }}
              >
                {p.label}
              </div>
            );
          })}
        </div>
      )}

      {/* Bar itself */}
      <div
        className="phase-bar-track"
        style={{ height: barHeight + 'px' }}
      >
        {cycle.recipe.map((p, i) => {
          const isCurrent = i === currentIndex && isActive;
          const isDone = i < currentIndex;
          const isFuture = i > currentIndex;
          const pct = (p.minutes / totalMin) * 100;
          const isOvertime = isActive && phase.overtime && i === currentIndex;
          const isAbortedMid = cycle.state === 'aborted' && i === currentIndex;

          let segClass = 'phase-seg';
          if (isDone) segClass += ' done';
          else if (isCurrent && cycle.state === 'in_progress') segClass += ' current';
          else if (isCurrent && cycle.state === 'overdue') segClass += ' current overdue';
          else if (isAbortedMid) segClass += ' aborted';
          else if (isFuture) segClass += ' future';
          if (cycle.state === 'completed') segClass += ' completed';

          return (
            <div
              key={p.key}
              className={segClass}
              style={{ width: pct + '%' }}
            >
              {/* Fill interno crescendo na fase atual */}
              {isCurrent && cycle.state === 'in_progress' && (
                <div
                  className="phase-seg-fill"
                  style={{ width: Math.min(100, currentProgress * 100) + '%' }}
                />
              )}
              {/* Shimmer sweep */}
              {isCurrent && cycle.state === 'in_progress' && (
                <div className="phase-seg-shimmer" />
              )}
              {/* Tick mark at end of segment */}
              {i < cycle.recipe.length - 1 && <div className="phase-seg-tick" />}
            </div>
          );
        })}
      </div>

      {/* Bottom row: planned times */}
      {variant !== 'compact' && (
        <div className="phase-bar-times">
          {cycle.recipe.map((p, i) => {
            const isCurrent = i === currentIndex && isActive;
            const pct = (p.minutes / totalMin) * 100;
            return (
              <div
                key={p.key}
                className="phase-bar-time mono"
                style={{
                  width: pct + '%',
                  color: isCurrent ? 'var(--neon-green-hi)' : 'var(--white-40)',
                  fontSize: variant === 'large' ? 15 : 13,
                  fontWeight: isCurrent ? 600 : 500,
                }}
              >
                {p.minutes}min
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};

window.CyclePhaseBar = CyclePhaseBar;
