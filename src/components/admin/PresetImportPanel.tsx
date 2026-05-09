import { useMemo, useRef, useState } from "react";
import { Upload, Folder, CheckCircle2, AlertCircle, ChevronDown, ChevronUp } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { ALL_CATEGORIES, type PresetCategory } from "../../../packages/preset-schema/src";
import logo from "@/assets/diditagain-logo.png";
import { SampleCropEditor, defaultCropMetadata, type SampleCropMetadata } from "./SampleCropEditor";

export type ImportCandidate = {
  id: string;
  filename: string;
  detectedCategory: PresetCategory;
  detectedRootMidi: number;
  detectedVelocity: "soft" | "medium" | "hard";
  needsReview: boolean;
  file?: File;
  crop: SampleCropMetadata;
};

const seed = (): ImportCandidate[] => [
  { id: "a", filename: "Dark_Bell_C5.wav",   detectedCategory: "DrillBells", detectedRootMidi: 72, detectedVelocity: "medium", needsReview: false, crop: { ...defaultCropMetadata(), autoLoop: true } },
  { id: "b", filename: "808_sustained.wav",  detectedCategory: "Bass808",    detectedRootMidi: 36, detectedVelocity: "hard",   needsReview: false, crop: { ...defaultCropMetadata(), autoLoop: false, pitchTracking: true } },
  { id: "c", filename: "weird_thing.wav",    detectedCategory: "AlienLeads", detectedRootMidi: 60, detectedVelocity: "medium", needsReview: true,  crop: { ...defaultCropMetadata(), autoLoop: true } },
];

const midiToName = (m: number) => {
  const names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
  return `${names[m % 12]}${Math.floor(m / 12) - 1}`;
};

interface Props { onFinalize?: (items: ImportCandidate[]) => void; }

export const PresetImportPanel = ({ onFinalize }: Props) => {
  const [queue, setQueue] = useState<ImportCandidate[]>(seed());
  const [folderPath, setFolderPath] = useState("Documents/DIDITAGAIN STUDIO/Inbox");
  const [expanded, setExpanded] = useState<string | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

  const reviewCount = useMemo(() => queue.filter(q => q.needsReview).length, [queue]);

  const updateItem = (id: string, patch: Partial<ImportCandidate>) =>
    setQueue(q => q.map(i => i.id === id ? { ...i, ...patch, needsReview: false } : i));

  const updateCrop = (id: string, crop: SampleCropMetadata) =>
    setQueue(q => q.map(i => i.id === id ? { ...i, crop } : i));

  const removeItem = (id: string) => setQueue(q => q.filter(i => i.id !== id));

  const addFiles = (files: FileList | null) => {
    if (!files) return;
    const next: ImportCandidate[] = Array.from(files).map((f, idx) => ({
      id: `${Date.now()}-${idx}`,
      filename: f.name,
      detectedCategory: "Uncategorized",
      detectedRootMidi: 60,
      detectedVelocity: "medium",
      needsReview: true,
      file: f,
      crop: defaultCropMetadata(),
    }));
    setQueue(q => [...q, ...next]);
  };

  return (
    <Card className="p-6 space-y-4 bg-card border-border">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <img src={logo} alt="DIDITAGAIN STUDIO" className="h-9 w-auto" />
          <div>
            <div className="flex items-center gap-2">
              <Upload className="w-5 h-5 text-[hsl(180_88%_51%)]" />
              <h3 className="text-lg font-semibold">Import Review</h3>
              {reviewCount > 0 && (
                <Badge variant="destructive" className="ml-2">{reviewCount} need review</Badge>
              )}
            </div>
            <p className="text-xs text-muted-foreground">Crop, loop, and finalize one-shots into hybrid presets.</p>
          </div>
        </div>
        <Button onClick={() => onFinalize?.(queue)} disabled={queue.length === 0}
          className="bg-[hsl(180_88%_45%)] text-black hover:bg-[hsl(180_88%_55%)]">
          <CheckCircle2 className="w-4 h-4 mr-2" />
          Finalize Import ({queue.length})
        </Button>
      </div>

      <div className="flex gap-2">
        <Folder className="w-5 h-5 text-muted-foreground self-center" />
        <Input value={folderPath} onChange={e => setFolderPath(e.target.value)} className="flex-1" />
        <Button variant="outline" onClick={() => fileInput.current?.click()}>Add Files</Button>
        <input ref={fileInput} type="file" accept="audio/*" multiple hidden
          onChange={e => addFiles(e.target.files)} />
      </div>

      <div className="space-y-2 max-h-[36rem] overflow-y-auto">
        {queue.map(item => (
          <div key={item.id} className="rounded-md border border-border bg-background">
            <div className="flex items-center gap-2 p-2">
              {item.needsReview ? (
                <AlertCircle className="w-4 h-4 text-destructive shrink-0" />
              ) : (
                <CheckCircle2 className="w-4 h-4 text-[hsl(180_88%_51%)] shrink-0" />
              )}
              <span className="font-mono text-sm flex-1 truncate">{item.filename}</span>

              <Select value={item.detectedCategory} onValueChange={(v) => updateItem(item.id, { detectedCategory: v as PresetCategory })}>
                <SelectTrigger className="w-40"><SelectValue /></SelectTrigger>
                <SelectContent>
                  {ALL_CATEGORIES.map(c => <SelectItem key={c} value={c}>{c}</SelectItem>)}
                </SelectContent>
              </Select>

              <Input type="number" min={0} max={127}
                value={item.detectedRootMidi}
                onChange={e => updateItem(item.id, { detectedRootMidi: Number(e.target.value) })}
                className="w-20" />
              <span className="text-xs text-muted-foreground w-10">{midiToName(item.detectedRootMidi)}</span>

              <Select value={item.detectedVelocity} onValueChange={(v) => updateItem(item.id, { detectedVelocity: v as ImportCandidate["detectedVelocity"] })}>
                <SelectTrigger className="w-24"><SelectValue /></SelectTrigger>
                <SelectContent>
                  <SelectItem value="soft">soft</SelectItem>
                  <SelectItem value="medium">medium</SelectItem>
                  <SelectItem value="hard">hard</SelectItem>
                </SelectContent>
              </Select>

              <Button size="sm" variant="ghost"
                onClick={() => setExpanded(expanded === item.id ? null : item.id)}>
                {expanded === item.id ? <ChevronUp className="w-4 h-4" /> : <ChevronDown className="w-4 h-4" />}
                Edit
              </Button>
              <Button size="sm" variant="ghost" onClick={() => removeItem(item.id)}>×</Button>
            </div>
            {expanded === item.id && (
              <div className="p-3 border-t border-border">
                <SampleCropEditor
                  source={item.file ?? null}
                  value={item.crop}
                  onChange={(crop) => updateCrop(item.id, crop)}
                />
              </div>
            )}
          </div>
        ))}
        {queue.length === 0 && (
          <div className="text-center text-muted-foreground text-sm py-8">No samples queued. Add files above.</div>
        )}
      </div>
    </Card>
  );
};

export default PresetImportPanel;
