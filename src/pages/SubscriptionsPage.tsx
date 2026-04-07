import StatCard from "@/components/admin/StatCard";
import { CreditCard, TrendingUp, AlertTriangle, DollarSign } from "lucide-react";

const SubscriptionsPage = () => (
  <div className="space-y-6">
    <div>
      <h2 className="text-2xl font-bold">Subscriptions</h2>
      <p className="text-muted-foreground text-sm mt-1">Manage Stripe subscriptions and billing</p>
    </div>
    <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
      <StatCard icon={CreditCard} label="Active" value="834" accent="teal" />
      <StatCard icon={TrendingUp} label="MRR" value="$12,510" accent="purple" />
      <StatCard icon={AlertTriangle} label="Past Due" value="18" accent="purple" />
      <StatCard icon={DollarSign} label="Churn Rate" value="3.2%" accent="teal" />
    </div>
    <div className="glass-panel p-8 text-center text-muted-foreground">
      <p className="text-sm">Connect Stripe to view live subscription data.</p>
      <p className="text-xs mt-2">Enable Lovable Cloud → Stripe integration to proceed.</p>
    </div>
  </div>
);

export default SubscriptionsPage;
