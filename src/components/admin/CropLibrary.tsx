import { useEffect, useMemo, useRef, useState } from "react";
import { Upload, Trash2, Scissors, Save, FileAudio } from "lucide-react";
import { Card } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { toast } from "@/hooks/use-toast";
import {
  SampleCropEditor,
  defaultCropMetadata,
  type SampleCropMetadata,
} from "@/components/admin/SampleCropEditor";

export interface CropLibraryItem {
  id: string;
  name: string;
  /** base64 data URL of audio, or null if no audio attached yet */
  dataUrl: string | null;
  size: number;
  addedAt: number;
  crop: SampleCropMetadata;
  /** factory items can't be deleted, only re-cropped/replaced */
  locked?: boolean;
}

interface Props {
  storageKey: string;
  /** Seeded items merged in on first load (e.g. factory preset names). */
  seed?: Omit<CropLibraryItem, "addedAt" | "crop" | "dataUrl" | "size">[];
  /** Allow user to add new audio files to this library. */
  allowUpload: boolean;
  emptyMessage: string;
}

const fileToDataUrl = (f: File) =>
  new Promise<string>((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(String(r.result));
    r.onerror = reject;
    r.readAsDataURL(f);
  });

const fmtSize = (n: number) =>
  n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : n > 0 ? `${Math.round(n / 1024)} KB` : "—";

export function CropLibrary({ storageKey, seed = [], allowUpload, emptyMessage }: Props) {
  const [items, setItems] = useState<CropLibraryItem[]>([]);
  const [activeId, setActiveId] = useState<string | null>(null);
  const [filter, setFilter] = useState("");
  const fileInput = useRef<HTMLInputElement>(null);
  const replaceInput = useRef<HTMLInputElement>(null);

  // Load + merge seed
  useEffect(() => {
    let stored: CropLibraryItem[] = [];
    try { stored = JSON.parse(localStorage.getItem(storageKey) || "[]"); } catch { /* ignore */ }
    const byId = new Map(stored.map(i => [i.id, i]));
    for (const s of seed) {
      if (!byId.has(s.id)) {
        byId.set(s.id, {
          ...s,
          addedAt: Date.now(),
          crop: defaultCropMetadata(),
          dataUrl: null,
          size: 0,
        });
      }
    }
    const merged = Array.from(byId.values());
    setItems(merged);
    localStorage.setItem(storageKey, JSON.stringify(merged));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [storageKey]);

  const persist = (next: CropLibraryItem[]) => {
    setItems(next);
    localStorage.setItem(storageKey, JSON.stringify(next));
  };

  const active = useMemo(() => items.find(i => i.id === activeId) ?? null, [items, activeId]);

  const updateActive = (patch: Partial<CropLibraryItem>) => {
    persist(items.map(i => i.id === activeId ? { ...i, ...patch } : i));
  };

  const onAdd = async (files: FileList | null) => {
    if (!files?.length) return;
    const additions: CropLibraryItem[] = [];
    for (const f of Array.from(files)) {
      try {
        const dataUrl = await fileToDataUrl(f);
        additions.push({
          id: `${Date.now()}-${Math.random().toString(36).slice(2, 7)}`,
          name: f.name,
          size: f.size,
          addedAt: Date.now(),
          dataUrl,
          crop: defaultCropMetadata(),
        });
      } catch {
        toast({ title: "Failed to read file", description: f.name, variant: "destructive" });
      }
    }
    persist([...items, ...additions]);
    if (additions[0]) setActiveId(additions[0].id);
  };

  const onReplaceActive = async (files: FileList | null) => {
    if (!files?.[0] || !active) return;
    try {
      const dataUrl = await fileToDataUrl(files[0]);
      updateActive({ dataUrl, size: files[0].size, name: active.locked ? active.name : files[0].name });
      toast({ title: "Audio attached", description: files[0].name });
    } catch {
      toast({ title: "Failed to load audio", variant: "destructive" });
    }
  };

  const onDelete = (id: string) => {
    const item = items.find(i => i.id === id);
    if (item?.locked) return;
    persist(items.filter(i => i.id !== id));
    if (activeId === id) setActiveId(null);
  };

  const filtered = items.filter(i => i.name.toLowerCase().includes(filter.toLowerCase()));

  return (
    <div className="grid grid-cols-1 lg:grid-cols-[320px_1fr] gap-4">
      <Card className="p-3 bg-card border-border space-y-2">
        <div className="flex gap-2">
          <Input placeholder="Search…" value={filter}
            onChange={e => setFilter(e.target.value)} className="h-8" />
          {allowUpload && (
            <>
              <Button size="sm" variant="outline" onClick={() => fileInput.current?.click()}>
                <Upload className="w-4 h-4" />
              </Button>
              <input ref={fileInput} type="file" accept="audio/*" multiple hidden
                onChange={e => onAdd(e.target.files)} />
            </>
          )}
        </div>
        <div className="space-y-1 max-h-[60vh] overflow-y-auto">
          {filtered.length === 0 && (
            <p className="text-xs text-muted-foreground px-2 py-6 text-center">{emptyMessage}</p>
          )}
          {filtered.map(s => (
            <button
              key={s.id}
              onClick={() => setActiveId(s.id)}
              className={`w-full text-left rounded-md px-3 py-2 text-sm border transition-colors ${
                activeId === s.id
                  ? "border-[hsl(180_88%_51%)] bg-[hsl(180_88%_51%/0.08)]"
                  : "border-border bg-background hover:bg-muted/40"
              }`}
            >
              <div className="flex items-center justify-between gap-2">
                <span className="font-mono text-xs truncate">{s.name}</span>
                {!s.locked && (
                  <Trash2
                    className="w-3.5 h-3.5 text-muted-foreground hover:text-destructive shrink-0"
                    onClick={(e) => { e.stopPropagation(); onDelete(s.id); }}
                  />
                )}
              </div>
              <div className="flex items-center gap-2 mt-1 flex-wrap">
                {s.locked && <Badge variant="outline" className="text-[10px]">factory</Badge>}
                <Badge variant="secondary" className="text-[10px]">{fmtSize(s.size)}</Badge>
                {!s.dataUrl && <Badge variant="outline" className="text-[10px]">no audio</Badge>}
                {s.crop.autoLoop && s.dataUrl && <Badge className="text-[10px] bg-[hsl(180_88%_45%)] text-black">loop</Badge>}
                {s.crop.oneShotMode && <Badge variant="outline" className="text-[10px]">one-shot</Badge>}
              </div>
            </button>
          ))}
        </div>
      </Card>

      <div className="space-y-3">
        {active ? (
          <>
            <Card className="p-3 flex items-center justify-between gap-3 bg-card border-border">
              <div className="min-w-0">
                <div className="flex items-center gap-2">
                  <Scissors className="w-4 h-4 text-[hsl(180_88%_51%)]" />
                  <span className="font-mono text-sm truncate">{active.name}</span>
                  {active.locked && <Badge variant="outline" className="text-[10px]">factory</Badge>}
                </div>
                <p className="text-xs text-muted-foreground">
                  {active.dataUrl
                    ? "Edits are saved automatically."
                    : "Attach an audio file to this entry to start cropping."}
                </p>
              </div>
              <div className="flex gap-2 shrink-0">
                <Button size="sm" variant="outline" onClick={() => replaceInput.current?.click()}>
                  <FileAudio className="w-4 h-4 mr-1" />
                  {active.dataUrl ? "Replace audio" : "Attach audio"}
                </Button>
                <input ref={replaceInput} type="file" accept="audio/*" hidden
                  onChange={e => { onReplaceActive(e.target.files); e.target.value = ""; }} />
                <Button size="sm" variant="outline"
                  onClick={() => toast({ title: "Saved", description: active.name })}>
                  <Save className="w-4 h-4 mr-1" /> Saved
                </Button>
              </div>
            </Card>
            {active.dataUrl ? (
              <SampleCropEditor
                source={active.dataUrl}
                value={active.crop}
                onChange={(crop) => updateActive({ crop })}
              />
            ) : (
              <Card className="p-12 text-center bg-card border-border">
                <FileAudio className="w-8 h-8 mx-auto text-muted-foreground mb-2" />
                <p className="text-sm text-muted-foreground">
                  No audio attached to this preset yet. Click "Attach audio" above.
                </p>
              </Card>
            )}
          </>
        ) : (
          <Card className="p-12 text-center bg-card border-border">
            <p className="text-sm text-muted-foreground">
              Select an item on the left to start cropping.
            </p>
          </Card>
        )}
      </div>
    </div>
  );
}
