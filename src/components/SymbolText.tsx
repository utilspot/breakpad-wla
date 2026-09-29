import { useLayoutEffect, useRef, useState } from 'react';

/** Must match `.symbol-text pre` line-height in styles.css. */
const LINE_HEIGHT = 18;
/** Lines rendered above and below the visible ones, so fast scrolling does not flash blank. */
const OVERSCAN = 30;
/**
 * Tallest scroll area used. Browsers cap element heights (Firefox near 17.9M
 * px), and a big module has millions of lines; past this, the scrollbar maps
 * onto the lines proportionally instead of one pixel per pixel.
 */
const MAX_SCROLL_HEIGHT = 15_000_000;

/**
 * Every line of a symbol file in a scrolling box, however many there are:
 * only the lines in view are in the page, so a file of millions of lines
 * scrolls as easily as a short one. The browser's find therefore only sees
 * the lines on screen; the download has the whole file.
 */
export default function SymbolText({ lines }: { lines: string[] }) {
  const box = useRef<HTMLDivElement>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewHeight, setViewHeight] = useState(0);

  useLayoutEffect(() => {
    const element = box.current;
    if (!element) return;
    setViewHeight(element.clientHeight);
    const observer = new ResizeObserver(() => setViewHeight(element.clientHeight));
    observer.observe(element);
    return () => observer.disconnect();
  }, []);

  // A new file starts at its top.
  useLayoutEffect(() => {
    if (box.current) box.current.scrollTop = 0;
    setScrollTop(0);
  }, [lines]);

  const fullHeight = lines.length * LINE_HEIGHT;
  const scrollHeight = Math.min(fullHeight, MAX_SCROLL_HEIGHT);
  // How far into the full text the scroll position is; 1:1 unless the file is taller than the cap.
  const ratio = scrollHeight > viewHeight ? Math.max(1, (fullHeight - viewHeight) / (scrollHeight - viewHeight)) : 1;
  const textTop = scrollTop * ratio;

  const first = Math.max(0, Math.floor(textTop / LINE_HEIGHT) - OVERSCAN);
  const last = Math.min(lines.length, Math.ceil((textTop + viewHeight) / LINE_HEIGHT) + OVERSCAN);
  // Where line `first` sits in the scroll area so the visible lines land in view.
  const offset = scrollTop - (textTop - first * LINE_HEIGHT);

  return (
    <div ref={box} className="symbol-text" onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}>
      <div className="symbol-text-space" style={{ height: scrollHeight }}>
        <pre style={{ transform: `translateY(${offset}px)` }}>{lines.slice(first, last).join('\n')}</pre>
      </div>
    </div>
  );
}
