import { useCallback, useMemo, useState } from "react";
import { UploadCloud, AlertTriangle, CheckCircle2, FileAudio, Trash2, Download } from "lucide-react";
import { Card } from "@/components/ui/card";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { toast } from "sonner";
import {
  routeFile,
  generateManifests,
  type RoutedFile,
  type MultisamplePresetManifest,
} from "../../packages/preset-schema/src/multisampleRouter";

const CATEGORIES = [
  "Guitars", "Choirs", "Pianos", "Bells", "808s", "Strings", "Brass", "Uncategorized",
];

interface Row extends RoutedFile {
  id: string;
  file: File;
  overrideCategory?: string;
  overridePresetName?: string;
}

const MultisampleUploadPage = () => {
  const [rows, setRows] = useState<Row[]>([]);
  const [dragOver, setDragOver] = useState(false);
  const [manifests, setManifests] = useState<MultisamplePresetManifest[]>([]);

  const addFiles = useCallback((files: FileList | File[]) => {
    const next: Row[] = Array.from(files).map((f, i) => {
      const routed = routeFile(f.name);
      return { ...routed, id: `${Date.now()}-${i}-${f.name}`, file: f };
    });
    setRows((cur) => [...cur, ...next]);
  }, []);

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setDragOver(false);
    if (e.dataTransfer.files) addFiles(e.dataTransfer.files);
  };

  const removeRow = (id: string) => setRows((r) => r.filter((x) => x.id !== id));

  const replaceRow = (id: string, file: File) => {
    setRows((cur) =>
      cur.map((r) => (r.id === id ? { ...routeFile(file.name), id, file } : r)),
    );
  };

  const updateOverride = (id: string, patch: Partial<Row>) =>
    setRows((cur) => cur.map((r) => (r.id === id ? { ...r, ...patch } : r)));

  // Apply overrides into a fresh RoutedFile[] for manifest building.
  const effectiveRouted: RoutedFile[] = useMemo(
    () =>
      rows.map((r) => {
        if (!r.overrideCategory && !r.overridePresetName) return r;
        const parsed = { ...r.parsed };
        if (r.overridePresetName) parsed.instrumentName = r.overridePresetName;
        if (r.overrideCategory) parsed.category = r.overrideCategory;
        const safeInstrument = (parsed.instrumentName || "Unknown").replace(/\s+/g, "_");
        const safeNote = parsed.rootNote ?? "Unknown";
        const targetPath = `${parsed.category}/${safeInstrument}_${safeNote}.wav`;
        return {
          parsed,
          targetPath,
          presetKey: `${parsed.category}/${safeInstrument}`,
        };
      }),
    [rows],
  );

  const result = useMemo(() => generateManifests(effectiveRouted), [effectiveRouted]);

  const handleGenerate = () => {
    if (effectiveRouted.length === 0) {
      toast.error("Add some WAV files first.");
      return;
    }
    setManifests(result.manifests);
    toast.success(`Generated ${result.manifests.length} preset manifest(s).`);
  };

  const downloadManifests = () => {
    const blob = new Blob([JSON.stringify(manifests, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "multisample-manifests.json";
    a.click();
    URL.revokeObjectURL(url);
  };

  // Quick dup-lookup for row badges.
  const dupKeys = new Set<string>();
  for (const d of result.duplicates) {
    for (const f of d.files) dupKeys.add(`${d.presetKey}::${d.rootNote}::${f}`);
  }

  return (
    <div className="container mx-auto p-6 space-y-6">
      <div>
        <h1 className="text-3xl font-bold">Multisample Upload</h1>
        <p className="text-sm text-muted-foreground">
          Drop WAV files named like <code>Guitar_C3.wav</code>. They're parsed,
          routed by instrument, and grouped into auto-multisample presets.
        </p>
      </div>

      <Card
        onDragOver={(e) => { e.preventDefault(); setDragOver(true); }}
        onDragLeave={() => setDragOver(false)}
        onDrop={onDrop}
        className={`p-8 border-2 border-dashed text-center transition ${
          dragOver ? "border-primary bg-primary/5" : "border-border"
        }`}
      >
        <UploadCloud className="w-10 h-10 mx-auto text-muted-foreground" />
        <p className="mt-3 font-medium">Drop WAV files here</p>
        <p className="text-xs text-muted-foreground">
          Supported root notes: C / D# / F# / A across octaves 2–5
        </p>
        <div className="mt-4">
          <label>
            <input
              type="file"
              accept=".wav,audio/wav"
              multiple
              hidden
              onChange={(e) => e.target.files && addFiles(e.target.files)}
            />
            <Button asChild variant="outline">
              <span>Browse files</span>
            </Button>
          </label>
        </div>
      </Card>

      {rows.length > 0 && (
        <Card className="p-4 space-y-2">
          <div className="flex items-center justify-between">
            <h2 className="font-semibold">Files ({rows.length})</h2>
            <div className="flex gap-2">
              <Button variant="outline" onClick={() => setRows([])}>
                <Trash2 className="w-4 h-4 mr-2" /> Clear
              </Button>
              <Button onClick={handleGenerate}>Generate Manifest</Button>
            </div>
          </div>

          <div className="space-y-2 max-h-[28rem] overflow-y-auto">
            {rows.map((r) => {
              const dupBadge = dupKeys.has(
                `${r.presetKey}::${r.parsed.rootNote}::${r.parsed.originalName}`,
              );
              return (
                <div key={r.id} className="rounded-md border border-border p-3 space-y-2">
                  <div className="flex items-center gap-2">
                    <FileAudio className="w-4 h-4 shrink-0 text-muted-foreground" />
                    <span className="font-mono text-sm flex-1 truncate">{r.parsed.originalName}</span>
                    {r.parsed.ok ? (
                      <Badge variant="secondary" className="gap-1">
                        <CheckCircle2 className="w-3 h-3" /> {r.parsed.rootNote}
                      </Badge>
                    ) : (
                      <Badge variant="destructive" className="gap-1">
                        <AlertTriangle className="w-3 h-3" /> Invalid
                      </Badge>
                    )}
                    {dupBadge && <Badge variant="destructive">Duplicate root</Badge>}
                    <label>
                      <input
                        type="file"
                        accept=".wav,audio/wav"
                        hidden
                        onChange={(e) => e.target.files?.[0] && replaceRow(r.id, e.target.files[0])}
                      />
                      <Button asChild size="sm" variant="ghost">
                        <span>Replace</span>
                      </Button>
                    </label>
                    <Button size="sm" variant="ghost" onClick={() => removeRow(r.id)}>×</Button>
                  </div>

                  <div className="grid grid-cols-3 gap-2">
                    <div>
                      <Label className="text-xs">Preset name</Label>
                      <Input
                        value={r.overridePresetName ?? r.parsed.instrumentName}
                        onChange={(e) => updateOverride(r.id, { overridePresetName: e.target.value })}
                      />
                    </div>
                    <div>
                      <Label className="text-xs">Category</Label>
                      <Select
                        value={r.overrideCategory ?? r.parsed.category}
                        onValueChange={(v) => updateOverride(r.id, { overrideCategory: v })}
                      >
                        <SelectTrigger><SelectValue /></SelectTrigger>
                        <SelectContent>
                          {CATEGORIES.map((c) => <SelectItem key={c} value={c}>{c}</SelectItem>)}
                        </SelectContent>
                      </Select>
                    </div>
                    <div>
                      <Label className="text-xs">Target path</Label>
                      <Input readOnly value={
                        `${r.overrideCategory ?? r.parsed.category}/` +
                        `${(r.overridePresetName ?? r.parsed.instrumentName).replace(/\s+/g,"_")}_` +
                        `${r.parsed.rootNote ?? "?"}.wav`
                      } />
                    </div>
                  </div>

                  {(r.parsed.warnings.length > 0 || r.parsed.errors.length > 0) && (
                    <div className="text-xs space-y-1">
                      {r.parsed.errors.map((e, i) => (
                        <p key={`e${i}`} className="text-destructive">⚠ {e}</p>
                      ))}
                      {r.parsed.warnings.map((w, i) => (
                        <p key={`w${i}`} className="text-yellow-500">! {w}</p>
                      ))}
                    </div>
                  )}
                </div>
              );
            })}
          </div>
        </Card>
      )}

      {(result.duplicates.length > 0 || result.missingRoots.length > 0) && (
        <Card className="p-4 space-y-2">
          <h2 className="font-semibold flex items-center gap-2">
            <AlertTriangle className="w-4 h-4 text-yellow-500" /> Validation
          </h2>
          {result.duplicates.map((d, i) => (
            <p key={i} className="text-sm text-destructive">
              Duplicate root <strong>{d.rootNote}</strong> in <code>{d.presetKey}</code>: {d.files.join(", ")}
            </p>
          ))}
          {result.missingRoots.map((m, i) => (
            <p key={i} className="text-sm text-yellow-600">
              <code>{m.presetKey}</code> is missing roots: {m.missing.join(", ")}
            </p>
          ))}
        </Card>
      )}

      {manifests.length > 0 && (
        <Card className="p-4 space-y-3">
          <div className="flex items-center justify-between">
            <h2 className="font-semibold">Generated Manifests ({manifests.length})</h2>
            <Button variant="outline" onClick={downloadManifests}>
              <Download className="w-4 h-4 mr-2" /> Download JSON
            </Button>
          </div>
          <pre className="text-xs bg-muted p-3 rounded overflow-auto max-h-96">
            {JSON.stringify(manifests, null, 2)}
          </pre>
          <p className="text-xs text-muted-foreground">
            TODO: wire to pack-version system (Lovable Cloud) once enabled.
          </p>
        </Card>
      )}
    </div>
  );
};

export default MultisampleUploadPage;
