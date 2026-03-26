
import { Card, CardContent, CardDescription, CardFooter, CardTitle } from "@/components/ui/card";
import { Slider } from "@/components/ui/slider";
import { Button } from "@/components/ui/button";

export default function ParameterCard({
    section, item, sliderValues, handleSliderChange, handleSliderCommit, fetchParamValue
}) {
    
    return (
        <Card className="p-4 bg-background ">
            <CardTitle className="text-lg">{item.name}</CardTitle>
            <CardDescription>
                Valor: {item.value !== undefined ? String(item.value) : "Cargando..."}
            </CardDescription>
            {item?.limits && (
                <CardContent className="pt-4 flex gap-2">
                    <span>{item.limits ? item.limits[0] : ""}</span>
                    <Slider
                        value={sliderValues[item.name] !== undefined ? [sliderValues[item.name]] : (item.value !== undefined && typeof item.value === 'number' ? [item.value] : [0])}
                        min={item.limits ? item.limits[0] : 0}
                        max={item.limits ? item.limits[1] : 100}
                        step={item.limits ? (item.limits[1] - item.limits[0]) / 100 : 1}
                        onValueChange={(values) => handleSliderChange(item.name, values[0])}
                        onValueCommit={(values) => handleSliderCommit(section, item.name, values[0])}
                    />
                    <span>{item.limits ? item.limits[1] : ""}</span>
                </CardContent>
            )}
            <CardFooter className="flex items-center justify-between p-0">
                {(!item?.limits) ? (
                    <Button onClick={() => fetchParamValue(section, item.name)} className="mt-4">
                        Actualizar valor
                    </Button>
                ) :
                    <span className="text-sm">
                        Valor a setear: {sliderValues[item.name] !== undefined ? sliderValues[item.name] : (item.value !== undefined && typeof item.value === 'number' ? item.value : "N/A")}
                    </span>
                }
            </CardFooter>
        </Card>
    )
}