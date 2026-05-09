import { useEffect, useMemo, useRef, useState } from "react";
import { Upload, Trash2, Scissors, Save } from "lucide-react";
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

interface StoredSample {
  id: string;
  name: string;
  size: number;
  addedAt: number;
  /** base64 data URL — kept here so cropping persists across reloads */
  dataUrl: string;
  crop: SampleCropMetadata;
}

const STORAGE_KEY = "dida.sampleLibrary.v1";

const loadAll = (): StoredSample[] => {
  try { return JSON.parse(localStorage.getItem(STORAGE_KEY) || "[]"); }
  catch { return []; }
};
const saveAll = (items: StoredSample[]) =>
  localStorage.setItem(STORAGE_KEY, JSON.stringify(items));

const fileToDataUrl = (f: File) =>
  new Promise<string>((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(String(r.result));
    r.onerror = reject;
    r.readAsDataURL(f);
  });

const fmtSize = (n: number) =>
  n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`;

export default function SampleLibraryPage() {
  const [items, setItems] = useState<StoredSample[]>([]);
  const [activeId, setActiveId] = useState<string | null>(null);
  const [filter, setFilter] = useState("");
  const fileInput = useRef<HTMLInputElement>(null);

  useEffect(() => { setItems(loadAll()); }, []);

  const active = useMemo(() => items.find(i => i.id === activeId) ?? null, [items, activeId]);

  const updateActive = (patch: Partial<StoredSample>) => {
    setItems(curr => {
      const next = curr.map(i => i.id === activeId ? { ...i, ...patch } : i);
      saveAll(next);
      return next;
    });
  };

  const onAdd = async (files: FileList | null) => {
    if (!files?.length) return;
    const additions: StoredSample[] = [];
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
    setItems(curr => {
      const next = [...curr, ...additions];
      saveAll(next);
      return next;
    });
    if (additions[0]) setActiveId(additions[0].id);
  };

  const onDelete = (id: string) => {
    setItems(curr => {
      const next = curr.filter(i => i.id !== id);
      saveAll(next);
      return next;
    });
    if (activeId === id) setActiveId(null);
  };

  const filtered = items.filter(i =>
    i.name.toLowerCase().includes(filter.toLowerCase())
  );

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-bold">Sample Library</h1>
          <p className="text-sm text-muted-foreground">
            Crop, loop, and edit any imported user sample. Changes persist in your browser.
          </p>
        </div>
        <div className="flex gap-2">
          <Button variant="outline" onClick={() => fileInput.current?.click()}>
            <Upload className="w-4 h-4 mr-2" /> Add Samples
          </Button>
          <input ref={fileInput} type="file" accept="audio/*" multiple hidden
            onChange={e => onAdd(e.target.files)} />
        </div>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[320px_1fr] gap-4">
        <Card className="p-3 bg-card border-border space-y-2">
          <Input placeholder="Search…" value={filter}
            onChange={e => setFilter(e.target.value)} className="h-8" />
          <div className="space-y-1 max-h-[60vh] overflow-y-auto">
            {filtered.length === 0 && (
              <p className="text-xs text-muted-foreground px-2 py-6 text-center">
                No samples yet. Click "Add Samples" to import.
              </p>
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
                  <Trash2
                    className="w-3.5 h-3.5 text-muted-foreground hover:text-destructive shrink-0"
                    onClick={(e) => { e.stopPropagation(); onDelete(s.id); }}
                  />
                </div>
                <div className="flex items-center gap-2 mt-1">
                  <Badge variant="secondary" className="text-[10px]">{fmtSize(s.size)}</Badge>
                  {s.crop.autoLoop && <Badge className="text-[10px] bg-[hsl(180_88%_45%)] text-black">loop</Badge>}
                  {s.crop.oneShotMode && <Badge variant="outline" className="text-[10px]">one-shot</Badge>}
                </div>
              </button>
            ))}
          </div>
        </Card>

        <div className="space-y-3">
          {active ? (
            <>
              <Card className="p-3 flex items-center justify-between bg-card border-border">
                <div>
                  <div className="flex items-center gap-2">
                    <Scissors className="w-4 h-4 text-[hsl(180_88%_51%)]" />
                    <span className="font-mono text-sm">{active.name}</span>
                  </div>
                  <p className="text-xs text-muted-foreground">
                    Edits are saved automatically.
                  </p>
                </div>
                <Button size="sm" variant="outline"
                  onClick={() => toast({ title: "Saved", description: active.name })}>
                  <Save className="w-4 h-4 mr-1" /> Saved
                </Button>
              </Card>
              <SampleCropEditor
                source={active.dataUrl}
                value={active.crop}
                onChange={(crop) => updateActive({ crop })}
              />
            </>
          ) : (
            <Card className="p-12 text-center bg-card border-border">
              <p className="text-sm text-muted-foreground">
                Select a sample on the left to start cropping, or import new audio.
              </p>
            </Card>
          )}
        </div>
      </div>
    </div>
  );
}
