import { useEffect, useRef, useState } from "react";
import { Card } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { Slider } from "@/components/ui/slider";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Play, Square } from "lucide-react";

export interface SampleCropMetadata {
  cropStart: number;       // 0..1
  cropEnd: number;         // 0..1
  loopStart: number;       // 0..1
  loopEnd: number;         // 0..1
  loopCrossfadeMs: number; // 0..50
  autoLoop: boolean;
  oneShotMode: boolean;
  pitchTracking: boolean;
}

interface Props {
  /** Audio buffer source — File from <input type=file> or a URL */
  source: File | string | null;
  value: SampleCropMetadata;
  onChange: (next: SampleCropMetadata) => void;
}

const DEFAULTS: SampleCropMetadata = {
  cropStart: 0.0, cropEnd: 1.0,
  loopStart: 0.2, loopEnd: 0.95,
  loopCrossfadeMs: 20,
  autoLoop: true, oneShotMode: false, pitchTracking: true,
};

export function defaultCropMetadata(): SampleCropMetadata { return { ...DEFAULTS }; }

type Handle = "cropStart" | "cropEnd" | "loopStart" | "loopEnd" | null;

export const SampleCropEditor = ({ source, value, onChange }: Props) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [buffer, setBuffer] = useState<AudioBuffer | null>(null);
  const [drag, setDrag] = useState<Handle>(null);
  const ctxRef = useRef<AudioContext | null>(null);
  const sourceNodeRef = useRef<AudioBufferSourceNode | null>(null);
  const [playing, setPlaying] = useState(false);

  // Decode incoming source
  useEffect(() => {
    let cancelled = false;
    const decode = async () => {
      if (!source) { setBuffer(null); return; }
      try {
        const ac = ctxRef.current ?? new AudioContext();
        ctxRef.current = ac;
        const arr =
          typeof source === "string"
            ? await fetch(source).then(r => r.arrayBuffer())
            : await source.arrayBuffer();
        const buf = await ac.decodeAudioData(arr.slice(0));
        if (!cancelled) setBuffer(buf);
      } catch { if (!cancelled) setBuffer(null); }
    };
    decode();
    return () => { cancelled = true; };
  }, [source]);

  // Draw waveform + region overlays
  useEffect(() => {
    const c = canvasRef.current; if (!c) return;
    const ctx = c.getContext("2d"); if (!ctx) return;
    const w = c.width, h = c.height;
    ctx.fillStyle = "hsl(220 14% 7%)";
    ctx.fillRect(0, 0, w, h);

    if (buffer) {
      const ch = buffer.getChannelData(0);
      const step = Math.max(1, Math.floor(ch.length / w));
      ctx.strokeStyle = "hsl(180 88% 51% / 0.55)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let x = 0; x < w; x++) {
        let min = 1, max = -1;
        for (let i = 0; i < step; i++) {
          const s = ch[x * step + i] ?? 0;
          if (s < min) min = s; if (s > max) max = s;
        }
        ctx.moveTo(x, (1 - (max + 1) / 2) * h);
        ctx.lineTo(x, (1 - (min + 1) / 2) * h);
      }
      ctx.stroke();
    } else {
      ctx.fillStyle = "hsl(220 8% 50%)";
      ctx.font = "12px sans-serif";
      ctx.fillText(source ? "Decoding…" : "No audio loaded", 12, h / 2);
    }

    // Loop region (teal fill)
    ctx.fillStyle = "hsl(180 88% 51% / 0.12)";
    ctx.fillRect(value.loopStart * w, 0, (value.loopEnd - value.loopStart) * w, h);

    // Crop bounds (dimmed regions outside crop)
    ctx.fillStyle = "hsl(220 14% 4% / 0.65)";
    ctx.fillRect(0, 0, value.cropStart * w, h);
    ctx.fillRect(value.cropEnd * w, 0, w - value.cropEnd * w, h);

    // Markers
    const drawMarker = (x: number, color: string, label: string) => {
      ctx.strokeStyle = color; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
      ctx.fillStyle = color; ctx.fillRect(x - 4, 0, 8, 6);
      ctx.fillStyle = "hsl(220 14% 90%)"; ctx.font = "10px sans-serif";
      ctx.fillText(label, x + 4, 14);
    };
    drawMarker(value.cropStart * w, "hsl(180 88% 51%)", "Start");
    drawMarker(value.cropEnd * w,   "hsl(180 88% 51%)", "End");
    drawMarker(value.loopStart * w, "hsl(180 88% 71%)", "L◀");
    drawMarker(value.loopEnd * w,   "hsl(180 88% 71%)", "L▶");
  }, [buffer, value, source]);

  const xToFrac = (e: React.PointerEvent) => {
    const c = canvasRef.current!;
    const rect = c.getBoundingClientRect();
    return Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width));
  };

  const nearest = (frac: number): Handle => {
    const opts: { k: Handle; v: number }[] = [
      { k: "cropStart", v: value.cropStart },
      { k: "cropEnd", v: value.cropEnd },
      { k: "loopStart", v: value.loopStart },
      { k: "loopEnd", v: value.loopEnd },
    ];
    return opts.reduce((a, b) => Math.abs(b.v - frac) < Math.abs(a.v - frac) ? b : a).k;
  };

  const onDown = (e: React.PointerEvent) => {
    setDrag(nearest(xToFrac(e)));
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onMove = (e: React.PointerEvent) => {
    if (!drag) return;
    const f = xToFrac(e);
    const next = { ...value, [drag]: f } as SampleCropMetadata;
    if (next.cropEnd < next.cropStart) next.cropEnd = next.cropStart;
    if (next.loopEnd < next.loopStart) next.loopEnd = next.loopStart;
    onChange(next);
  };
  const onUp = () => setDrag(null);

  const stop = () => {
    sourceNodeRef.current?.stop();
    sourceNodeRef.current = null;
    setPlaying(false);
  };
  const preview = () => {
    if (!buffer || !ctxRef.current) return;
    stop();
    const ac = ctxRef.current;
    const node = ac.createBufferSource();
    node.buffer = buffer;
    if (!value.oneShotMode && value.autoLoop) {
      node.loop = true;
      node.loopStart = value.loopStart * buffer.duration;
      node.loopEnd   = value.loopEnd   * buffer.duration;
    }
    node.connect(ac.destination);
    const offset = value.cropStart * buffer.duration;
    node.start(0, offset);
    sourceNodeRef.current = node;
    setPlaying(true);
    node.onended = () => { setPlaying(false); sourceNodeRef.current = null; };
  };

  useEffect(() => () => stop(), []);

  return (
    <Card className="p-4 space-y-3 bg-card border-border">
      <canvas
        ref={canvasRef}
        width={780}
        height={130}
        className="w-full h-32 rounded-md border border-border touch-none cursor-ew-resize"
        onPointerDown={onDown}
        onPointerMove={onMove}
        onPointerUp={onUp}
        onPointerCancel={onUp}
      />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-3 text-xs">
        <NumberField label="Start" value={value.cropStart}
          onChange={v => onChange({ ...value, cropStart: v })} />
        <NumberField label="End" value={value.cropEnd}
          onChange={v => onChange({ ...value, cropEnd: v })} />
        <NumberField label="Loop Start" value={value.loopStart}
          onChange={v => onChange({ ...value, loopStart: v })} />
        <NumberField label="Loop End" value={value.loopEnd}
          onChange={v => onChange({ ...value, loopEnd: v })} />
      </div>

      <div className="space-y-2">
        <Label className="text-xs">Smooth Loop ({value.loopCrossfadeMs.toFixed(0)} ms)</Label>
        <Slider min={0} max={50} step={1} value={[value.loopCrossfadeMs]}
          onValueChange={([v]) => onChange({ ...value, loopCrossfadeMs: v })} />
      </div>

      <div className="flex flex-wrap gap-4 text-sm">
        <ToggleRow label="Auto Loop" checked={value.autoLoop}
          onChange={c => onChange({ ...value, autoLoop: c, oneShotMode: c ? false : value.oneShotMode })} />
        <ToggleRow label="Play Across Keys" checked={value.pitchTracking}
          onChange={c => onChange({ ...value, pitchTracking: c })} />
        <ToggleRow label="One-Shot Mode" checked={value.oneShotMode}
          onChange={c => onChange({ ...value, oneShotMode: c, autoLoop: c ? false : value.autoLoop })} />
      </div>

      <div className="flex gap-2">
        {!playing ? (
          <Button size="sm" onClick={preview} disabled={!buffer}
            className="bg-[hsl(180_88%_45%)] text-black hover:bg-[hsl(180_88%_55%)]">
            <Play className="w-4 h-4 mr-1" /> Preview
          </Button>
        ) : (
          <Button size="sm" variant="secondary" onClick={stop}>
            <Square className="w-4 h-4 mr-1" /> Stop
          </Button>
        )}
      </div>
    </Card>
  );
};

const NumberField = ({ label, value, onChange }: { label: string; value: number; onChange: (v: number) => void }) => (
  <div className="space-y-1">
    <Label className="text-xs text-muted-foreground">{label}</Label>
    <Input type="number" min={0} max={1} step={0.001}
      value={value.toFixed(3)}
      onChange={e => onChange(Math.min(1, Math.max(0, Number(e.target.value))))}
      className="h-8 text-xs" />
  </div>
);

const ToggleRow = ({ label, checked, onChange }: { label: string; checked: boolean; onChange: (c: boolean) => void }) => (
  <div className="flex items-center gap-2">
    <Switch checked={checked} onCheckedChange={onChange} />
    <span>{label}</span>
  </div>
);

export default SampleCropEditor;
