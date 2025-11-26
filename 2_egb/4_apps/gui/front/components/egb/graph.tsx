"use client";
import React, { useEffect, useState } from "react";

interface PlotlyChartProps {
  data: any[];
  keyName?: string;
}

export default function PlotlyChart({ data, keyName }: PlotlyChartProps) {
  if (!data || data.length === 0) {
    return <p className="w-full py-8 text-center text-gray-500">No data available to display.</p>;
  }

  const [Plot, setPlot] = useState<any | null>(null);

  useEffect(() => {
    let mounted = true;
    import("react-plotly.js")
      .then((mod) => {
        if (mounted) setPlot(() => mod.default || mod);
      })
      .catch((err) => {
        console.error("Failed to load react-plotly.js:", err);
      });
    return () => {
      mounted = false;
    };
  }, []);

  if (!Plot) {
    return <p className="text-center text-gray-500">Loading chart...</p>;
  }

  // Detectamos el formato automáticamente
  const first = data[0];

  let x: any[] = [];
  let y: any[] = [];

  if (typeof first === "number") {
    x = data.map((_, i) => i);
    y = data;
  } else if (typeof first === "object") {
    const getX = (d: any, i: number) => d.timestamp ?? d.time ?? i;
    const getY = (d: any) =>
      d.value ??
      d.voltage ??
      d.y ??
      (typeof d === "number" ? d : undefined);

    x = data.map(getX);
    y = data.map(getY);
  }

  const validPoints = x.length > 0 && y.length > 0 && !y.every((v) => v === undefined);
  if (!validPoints) {
    return <p className="text-center text-gray-500">Invalid or empty data format.</p>;
  }

  return (
    <div className="w-full max-w-4xl mx-auto">
      <Plot
        data={[
          {
            x,
            y,
            type: "scatter",
            mode: "lines+markers",
            marker: { size: 4 },
            line: { width: 2 },
            name: keyName ?? "Data",
          },
        ]}
        layout={{
          title: keyName ?? "Realtime Data",
          xaxis: { title: "Index / Time", showgrid: false },
          yaxis: { title: "Value", showline: true },
          autosize: true,
          margin: { l: 50, r: 20, t: 40, b: 40 },
        }}
        config={{ responsive: true }}
        style={{ width: "100%", height: "400px" }}
      />
    </div>
  );
}