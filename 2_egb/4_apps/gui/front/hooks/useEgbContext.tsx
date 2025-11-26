"use client";
import React, { createContext, useContext, useEffect, useState, useCallback, useRef } from "react";
import { connectEgbSocket } from "./useEgbSocket";
import axios from "axios";

const BASE_URL = process.env.NEXT_PUBLIC_BASE_URL || "";

type EgbContextValue = {
  history: Record<string, any[]>;
  subscribeKey: (key: string, cb: (m: any) => void) => () => void;
  fetchHistory: (key: string) => Promise<any[]>;
};

const EgbContext = createContext<EgbContextValue | null>(null);

export const EgbProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [history, setHistory] = useState<Record<string, any[]>>({});
  const historyRef = useRef(history);
  // keep ref in sync
  useEffect(() => {
    historyRef.current = history
  }, [history]);

  const fetchHistory = useCallback(async (key: string) => {
    const res = await axios.get(`${BASE_URL}/egb/history?key=${encodeURIComponent(key)}`);
    if (res.status !== 200) throw new Error("Failed fetching history");
    const data = res.data;
    console.log("Fetched history for", key, data);
    setHistory(prev => ({ ...prev, [key]: data.history ?? data }));
    return data.history ?? data;
  }, []);

  const subscribeKey = useCallback((key: string, cb: (m: any) => void) => {
    const snap = historyRef.current[key];
    if (snap)
      snap.forEach(cb);

    // open a dedicated websocket for this key. connectEgbSocket will call our handler
    const socket = connectEgbSocket(key, (m: any) => {
      // server message shape may be { key, msg } or similar; normalize to the payload
      const value = (m && (m.msg !== undefined ? m.msg : (m.value !== undefined ? m.value : m))) ?? m;
      // update shared history snapshot
      setHistory(prev => {
        const arr = prev[key] ? [...prev[key]] : [];
        arr.push(value);
        const max = 2000;
        if (arr.length > max) arr.splice(0, arr.length - max);
        const next = { ...prev, [key]: arr };
        historyRef.current = next;
        return next;
      });
      try { cb(value); } catch (e) { /* swallow listener errors */ }
    });

    return () => { try { socket.close(); } catch (e) {} };
  }, []);

  return <EgbContext.Provider value={{ history, subscribeKey, fetchHistory }}>{children}</EgbContext.Provider>;
};

export const useEgb = () => {
  const ctx = useContext(EgbContext);
  if (!ctx) throw new Error("useEgb must be used inside EgbProvider");
  return ctx;
};