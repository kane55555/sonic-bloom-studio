import { useState } from "react";
import { Save, RotateCcw } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Slider } from "@/components/ui/slider";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { ALL_CATEGORIES, type PresetCategory } from "../../../packages/preset-schema/src";

export type CategoryTemplate = {
  category: PresetCategory;
  defaultRootMidi: number;
  defaultAttack: number;
  defaultRelease: number;
  defaultFilterCutoff: number;
  defaultReverbMix: number;
  pitchTracking: boolean;
};

const DEFAULT_TEMPLATE: CategoryTemplate = {
  category: "DrillBells",
  defaultRootMidi: 72,
  defaultAttack: 0.005,
  defaultRelease: 1.2,
  defaultFilterCutoff: 8000,
  defaultReverbMix: 0.25,
  pitchTracking: true,
};

interface Props {
  initial?: Partial<CategoryTemplate>;
  onSave?: (t: CategoryTemplate) => void;
}

export const PresetTemplateEditor = ({ initial, onSave }: Props) => {
  const [tpl, setTpl] = useState<CategoryTemplate>({ ...DEFAULT_TEMPLATE, ...initial });

  const update = <K extends keyof CategoryTemplate>(k: K, v: CategoryTemplate[K]) =>
    setTpl(t => ({ ...t, [k]: v }));

  return (
    <Card className="p-6 space-y-4 bg-card border-border">
      <div className="flex items-center justify-between">
        <h3 className="text-lg font-semibold">Category Template Editor</h3>
        <div className="flex gap-2">
          <Button variant="outline" size="sm" onClick={() => setTpl({ ...DEFAULT_TEMPLATE, ...initial })}>
            <RotateCcw className="w-4 h-4 mr-2" />Reset
          </Button>
          <Button size="sm" onClick={() => onSave?.(tpl)}>
            <Save className="w-4 h-4 mr-2" />Save
          </Button>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-4">
        <div className="space-y-2">
          <Label>Category</Label>
          <Select value={tpl.category} onValueChange={v => update("category", v as PresetCategory)}>
            <SelectTrigger><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_CATEGORIES.map(c => <SelectItem key={c} value={c}>{c}</SelectItem>)}
            </SelectContent>
          </Select>
        </div>

        <div className="space-y-2">
          <Label>Default Root MIDI</Label>
          <Input type="number" min={0} max={127} value={tpl.defaultRootMidi}
            onChange={e => update("defaultRootMidi", Number(e.target.value))} />
        </div>

        <div className="space-y-2">
          <Label>Attack ({tpl.defaultAttack.toFixed(3)}s)</Label>
          <Slider value={[tpl.defaultAttack]} min={0} max={2} step={0.001}
            onValueChange={([v]) => update("defaultAttack", v)} />
        </div>

        <div className="space-y-2">
          <Label>Release ({tpl.defaultRelease.toFixed(2)}s)</Label>
          <Slider value={[tpl.defaultRelease]} min={0} max={8} step={0.01}
            onValueChange={([v]) => update("defaultRelease", v)} />
        </div>

        <div className="space-y-2">
          <Label>Filter Cutoff ({tpl.defaultFilterCutoff.toFixed(0)} Hz)</Label>
          <Slider value={[tpl.defaultFilterCutoff]} min={20} max={20000} step={1}
            onValueChange={([v]) => update("defaultFilterCutoff", v)} />
        </div>

        <div className="space-y-2">
          <Label>Reverb Mix ({(tpl.defaultReverbMix * 100).toFixed(0)}%)</Label>
          <Slider value={[tpl.defaultReverbMix]} min={0} max={1} step={0.01}
            onValueChange={([v]) => update("defaultReverbMix", v)} />
        </div>

        <div className="flex items-center gap-2 col-span-2">
          <input
            id="pitchTrack"
            type="checkbox"
            checked={tpl.pitchTracking}
            onChange={e => update("pitchTracking", e.target.checked)}
            className="w-4 h-4"
          />
          <Label htmlFor="pitchTrack">Pitch tracking enabled (uncheck for FX/Risers)</Label>
        </div>
      </div>
    </Card>
  );
};

export default PresetTemplateEditor;
