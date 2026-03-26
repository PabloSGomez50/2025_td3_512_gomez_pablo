"use client";
import { useEffect, useRef, useState } from "react";

// Minimal, opinionated helpers for a single-key websocket connection.
const buildWsBase = () => {
    const env = process.env.NEXT_PUBLIC_WS_URL || "";
    if (env)
        return env.replace(/\/$/, "");
    // if (typeof window !== "undefined") return `${window.location.protocol === "https:" ? "wss" : "ws"}://${window.location.host}`;
    return "ws://localhost:8000";
};

export function connectEgbSocket(key: string | null, onMessage: (m: any) => void) {
    const base = buildWsBase();
    const url = key ? `${base}?key=${encodeURIComponent(key)}` : base;
    const ws = new WebSocket(url);
    ws.onopen = () => console.info("EGB socket open", url);
    ws.onmessage = (ev) => {
        try {
            onMessage(JSON.parse(ev.data));
        } catch (e) {
            console.warn("EGB ws parse error", e);
        }
    };
    ws.onclose = () => console.info("EGB socket closed", url);
    ws.onerror = (e) => console.info("EGB socket error", e);
    return {
        close: () => { try { ws.close(); } catch (e) {} },
        send: (v: any) => { try { if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(v)); } catch (e) {} }
    };
}

export function useEgbSocket(key: string | null) {
    const [connected, setConnected] = useState(false);
    const [last, setLast] = useState<any>(null);
    const wsRef = useRef<WebSocket | null>(null);

    useEffect(() => {
        const base = buildWsBase();
        const url = key ? `${base}key=${encodeURIComponent(key)}` : base;
        const ws = new WebSocket(url);
        wsRef.current = ws;
        const onopen = () => setConnected(true);
        const onmsg = (ev: MessageEvent) => { try { setLast(JSON.parse(ev.data)); } catch (e) {} };
        const onclose = () => setConnected(false);
        ws.addEventListener("open", onopen);
        ws.addEventListener("message", onmsg);
        ws.addEventListener("close", onclose);
        ws.addEventListener("error", onclose);
        return () => {
            try {
                ws.removeEventListener("open", onopen);
                ws.removeEventListener("message", onmsg);
                ws.removeEventListener("close", onclose);
                ws.removeEventListener("error", onclose);
                ws.close();
            } catch (e) {
                
            }
            wsRef.current = null;
        };
    }, [key]);

    const send = (v: any) => { if (wsRef.current?.readyState === WebSocket.OPEN) wsRef.current.send(JSON.stringify(v)); };
    const close = () => { try { wsRef.current?.close(); } catch (e) {} };
    return { connected, last, send, close };
}