import { CropLibrary } from "@/components/admin/CropLibrary";

const FACTORY_PRESETS = [
  "Chicago_Drill_Sub","Choir_Lead","Cinematic_Hybrid_Hit","Crystal_Pluck","Dark_Air_Pad",
  "Dark_FM_Bass","Dark_Music_Box","Dark_Stringer","Digital_Cry_Lead","Dirty_Reese",
  "Dream_Bell","Ethereal_Pad","Evil_Square_Lead","FM_Marimba","Frozen_Choir",
  "Glass_EP","Glo_Alien_Lead","Hollow_Bell","Mellow_Tape_Pad","Metallic_Pluck",
  "Pluck_Bass","Sampled_Brass","Soft_Pluck","Space_Organ","Staccato_Synth_Strings",
  "Synth_Brass_Stack","Tape_Keys","Tight_Synth_Pluck","Warm_Analog_Bass",
  "Wide_Analog_Pad","Wide_Portamento_Lead",
];

export default function FactorySamplesPage() {
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-bold">Factory One-Shots</h1>
        <p className="text-sm text-muted-foreground">
          Crop and loop the one-shot used by each factory preset. Attach a WAV
          to any preset, then set crop / loop / smooth-loop / one-shot mode.
        </p>
      </div>
      <CropLibrary
        storageKey="dida.factoryCrop.v1"
        seed={FACTORY_PRESETS.map(name => ({ id: `factory:${name}`, name, locked: true }))}
        allowUpload={false}
        emptyMessage="No factory presets match your search."
      />
    </div>
  );
}
