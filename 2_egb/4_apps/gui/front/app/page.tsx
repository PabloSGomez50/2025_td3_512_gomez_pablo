"use client";
import React, { useEffect, useState } from "react";
import { useEgb } from "@/hooks/useEgbContext";
import ParametersSelector from "@/components/egb/Parameter_selectors";
import PlotlyChart from "@/components/egb/graph";
import { Button } from "@/components/ui/button";
import axios from "axios";
import { useToast } from "@/hooks/use-toast";
import { Card, CardContent, CardTitle } from "@/components/ui/card";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";


const BASE_URL = process.env.NEXT_PUBLIC_BASE_URL || "";

const KEYNAMES = [
  "Voltage",
  "Current",
  "PWM Value",
  "Error",
  "Integral",
  "Derivative",
  "R_Target",
  "Temperature"
];

export default function Home() {
  
  const [keyName, setKeyName] = useState<string>("Current");
  const { fetchHistory, subscribeKey, history } = useEgb();
  const [data, setData] = useState<any[]>([]);

  const { toast } = useToast();

  useEffect(() => {
    console.log("Subscribing to key:", keyName);
    (async () => {
      try {
        const hist = await fetchHistory(keyName);
        setData(hist ?? []);
      } catch (e) {
        console.error(e);
      }
    })();
    const unsub = subscribeKey(keyName, (msg) => {
      setData(prev => {
        const next = [...prev, msg];
        // keep a reasonable max for UI
        if (next.length > 2000) next.splice(0, next.length - 2000);
        return next;
      });
    });
    return () => unsub();
  }, [keyName, fetchHistory, subscribeKey]);

  const handleSendCommand = async (command: string) => {
    try {
      const response = await axios.post(BASE_URL + "/egb/" + command);
      if (response.status === 200) {
        toast({
          title: "Comando enviado",
          description: `El comando ${command} se ha enviado correctamente.`,
          duration: 3000,
        });
      } else {
        throw new Error("Error en la respuesta del servidor");
      }
    } catch (error) {
      toast({
        title: "Error al enviar comando",
        description: `No se pudo enviar el comando ${command}.`,
        duration: 3000,
        variant: "destructive",
      });
    }
  }

  const handleKeynameChange = (value: string) => {
    setKeyName(value);
    setData([]); // clear data on key change
  };

  return (
    <div className="min-h-[50dvh] h-full flex flex-col items-center justify-items-center gap-4 pb-12 sm:pb-8">
      <ParametersSelector />
      <div className="w-full p-4">
        <h2 className="text-xl font-semibold mb-4">Grafico de rendimiento</h2>
        <Card className="p-4 w-fit">
          <CardTitle className="text-lg">Controles</CardTitle>
          <CardContent className="flex w-full gap-2 px-4 py-2">
            <Select
              value={keyName}
              onValueChange={handleKeynameChange}
            >
              <SelectTrigger className="min-w-32 bg-primary text-primary-foreground hover:bg-primary/90">
                <SelectValue placeholder="Selecciona la variable a medir" />
              </SelectTrigger>
              <SelectContent>
                {KEYNAMES.map((key) => (
                  <SelectItem key={key} value={key}>
                    {key}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
            <Button onClick={() => handleSendCommand("start")}>
              Iniciar control
            </Button>
            <Button onClick={() => handleSendCommand("stop")}>
              Frenar control
            </Button>
            <Button onClick={() => setData([])}>
              Limpiar valores
            </Button>
          </CardContent>
        </Card>

        <div className="flex w-full pt-4">
          <PlotlyChart data={data} keyName={keyName} />
        </div>
      </div>
    </div>
  );
}