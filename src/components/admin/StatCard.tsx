import { LucideIcon } from "lucide-react";

interface StatCardProps {
  label: string;
  value: string;
  change?: string;
  icon: LucideIcon;
  accent?: "purple" | "teal";
}

const StatCard = ({ label, value, change, icon: Icon, accent = "purple" }: StatCardProps) => (
  <div className={`glass-panel p-5 ${accent === "purple" ? "glow-purple" : "glow-teal"}`}>
    <div className="flex items-start justify-between">
      <div>
        <p className="text-sm text-muted-foreground">{label}</p>
        <p className="text-2xl font-bold mt-1">{value}</p>
        {change && <p className="text-xs text-secondary mt-1">{change}</p>}
      </div>
      <div className={`p-2.5 rounded-lg ${accent === "purple" ? "bg-primary/15 text-primary" : "bg-secondary/15 text-secondary"}`}>
        <Icon className="w-5 h-5" />
      </div>
    </div>
  </div>
);

export default StatCard;
