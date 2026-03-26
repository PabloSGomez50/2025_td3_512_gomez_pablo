import { useToast } from "@/hooks/use-toast";
import axios from "axios";
import { useEffect, useState } from "react";
import { Button } from "../ui/button";
import { RefreshCw } from "lucide-react";
import ParameterCard from "./parameter-card";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "../ui/tabs";

const BASE_URL = process.env.NEXT_PUBLIC_BASE_URL || "";

interface ParamData {
    index: number;
    key: string;
    name: string;
    value?: string | number;
    limits?: [number, number];
}

const parse_value_func = (value: string): string | number => {
    if (value.split(": ").length > 1) {
        const parts = value.split(": ");
        const parsedValue = parts[1].trim();
        return isNaN(Number(parsedValue)) ? parsedValue : Number(parsedValue);
    }
    return value;
}

export default function ParametersSelector({ }) {

    const { toast } = useToast();
    const [params, setParams] = useState<Record<string, ParamData[]>>({});
    const [sliderValues, setSliderValues] = useState<Record<string, number>>({});

    const [autoRefresh, setAutoRefresh] = useState<boolean>(false);

    const fetchParams = async () => {
        try {
            const response = await axios.get(`${BASE_URL}/egb/list`);
            if (response.status === 200) {
                const params = response.data;
                setParams(params);
                toast({
                    title: "Parameters fetched",
                    description: `Obtuvo ${Object.keys(params).length} parametros.`,
                    duration: 2000
                });
                return params;
            } else {
                throw new Error(`Failed to fetch parameters, status: ${response.status}`);
            }
        } catch (e) {
            toast({
                title: "Error fetching parameters",
                description: String(e),
                variant: "destructive",
                duration: 1000
            });
        }
    }
    const fetchParamValue = async (section: string, key: string) => {
        try {
            const response = await axios.post(`${BASE_URL}/egb/action`, {
                "action": "get",
                "name": key
            });
            if (response.status === 200 && response.data.data) {
                const value = response.data.data;
                if (value.split(": ").length > 1) {
                    const parsedValue = parse_value_func(value);
                    toast({
                        title: `Valor para ${key}`,
                        description: parsedValue,
                        duration: 2000
                    });
                    setParams(prev => ({
                        ...prev,
                        [section]: prev[section].map(item => {
                            if (item.name === key) {
                                return { ...item, value: parsedValue };
                            }
                            return item;
                        })
                    }))
                } else {
                    console.warn(`Unexpected value format for ${key}:`, value);
                    setParams(prev => ({
                        ...prev,
                        [section]: prev[section].map(item => {
                            if (item.name === key) {
                                return { ...item, value: "N/A" };
                            }
                            return item;
                        })
                    }))
                }
            }
        } catch (e) {
            toast({
                title: `Error fetching value for ${key}`,
                description: String(e),
                variant: "destructive",
                duration: 1000
            });
            return null;
        }
    }

    const handleSliderChange = (param: string, value: number) => {
        setSliderValues(prev => ({ ...prev, [param]: value }));
    };

    const handleSliderCommit = async (section: string, name: string, value: number) => {
        try {
            const response = await axios.post(`${BASE_URL}/egb/action`, {
                "action": "set",
                "name": name,
                "value": value
            });
            if (response.status === 200 && response.data.data) {
                const data = response.data.data;
                const parsedValue = parse_value_func(data);
                toast({
                    title: `Set ${name}`,
                    description: `Nuevo valor: ${parsedValue}`,
                    duration: 2000
                });
                setParams(prev => {
                    return {
                        ...prev,
                        [section]: prev[section].map(item => {
                            if (item.name === name) {
                                return { ...item, value: parsedValue };
                            }
                            return item;
                        })
                    }
                })
            }
        } catch (e) {
            toast({
                title: `Error setting value for ${name}`,
                description: String(e),
                variant: "destructive",
                duration: 1000
            });
        }
    }

    const fetchSequentialValues = async (local_params: Record<string, ParamData[]> | undefined) => {
        if (!local_params) {
            local_params = params;
        }
        Object.entries(local_params).forEach(([param, list]) =>{
            list.forEach(async item => {
                try {
                    await fetchParamValue(param, item.name);
                } catch (e) {
                    console.error(`Sequential fetch failed for ${param}:`, e);
                }
            });
        });
    };

    const fetchAll = async () => {
        const local = await fetchParams();
        setTimeout(() => {
            fetchSequentialValues(local);
        }, 500);
    }

    useEffect(() => {
        fetchAll();
    }, []);

    useEffect(() => {
        if (!autoRefresh)
            return

        fetchSequentialValues();
        const refreshTimer = setInterval(() => {
            fetchSequentialValues();
        }, 10000);

        return () => clearInterval(refreshTimer)
    }, [autoRefresh]);

    return (
        <div className="w-full min-h-48 mt-4 p-4 rounded-md border-b border-muted">
            <div className="flex items-center justify-start gap-4 mb-4">
                <h2 className="text-xl font-semibold mr-12">Selector de parametros</h2>
                <Button onClick={fetchAll}>
                    Solicitar todos
                </Button>
                <Button
                    variant="outline"
                    size="sm"
                    onClick={() => setAutoRefresh(!autoRefresh)}
                    className={autoRefresh ? "text-green-600" : ""}
                >
                    <RefreshCw className="h-4 w-4" />
                    Auto-actualizar
                </Button>
            </div>

            <section>
                <Tabs defaultValue="PID" className="w-full">
                    <TabsList  className="grid w-full grid-cols-3 mb-8">
                        {Object.keys(params).map((param, index) => (
                            <TabsTrigger key={index} value={param} className="capitalize">
                                {param}
                            </TabsTrigger>
                        ))}
                    </TabsList>
                    {Object.entries(params).map(([param, list], index) => (
                        <TabsContent key={index} value={param} className="grid grid-cols-3 lg:grid-cols-4 xl:grid-cols-5 gap-4">
                            {list.length > 0 && list.map((item, idx) => (
                                <ParameterCard
                                    key={idx}
                                    section={param}
                                    item={item}
                                    sliderValues={sliderValues}
                                    handleSliderChange={handleSliderChange}
                                    handleSliderCommit={handleSliderCommit}
                                    fetchParamValue={fetchParamValue}
                                />
                            ))}
                        </TabsContent>
                    ))}
                </Tabs>
                <div className="grid grid-cols-1 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 gap-4 mb-4">
                </div>
                
            </section>
        </div>
    )
}