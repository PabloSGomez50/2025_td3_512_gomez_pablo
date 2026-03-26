"use client"

import { useState, useEffect } from "react"
import Image from "next/image"
import { Button } from "@/components/ui/button"
import { ChevronLeft, ChevronRight } from "lucide-react"
import Link from "next/link"
import { Card } from "@/components/ui/card"
import { Carousel } from "../ui/carousel"

const slides = [
  {
    id: 1,
    title: "¡BIENVENIDOS AL PORTAL DE CURSOS Y CARRERAS DE LA UTN DE AVELLANEDA!",
    image: "/background_facultad.jpg",
    buttonText: "CREAR UNA CUENTA",
    buttonLink: "/auth/login?type=register",
  },
  {
    id: 2,
    title: "CONOCÉ LA GRAN OFERTA DE IDIOMAS QUE TIENE LA UTN AVELLANEDA PARA VOS",
    image: "/background_facultad.jpg",
    buttonText: "MÁS INFORMACIÓN",
    buttonLink: "/idiomas",
  },
]

export default function News() {
  const [currentSlide, setCurrentSlide] = useState(0)

  const nextSlide = () => {
    setCurrentSlide((prev) => (prev === slides.length - 1 ? 0 : prev + 1))
  }

  const prevSlide = () => {
    setCurrentSlide((prev) => (prev === 0 ? slides.length - 1 : prev - 1))
  }

  useEffect(() => {
    const interval = setInterval(() => {
      nextSlide()
    }, 5000)
    return () => clearInterval(interval)
  }, [])

  return (
    <div className="relative w-full h-[16rem] overflow-hidden">
        {/* <Carousel>

        </Carousel> */}

      <button
        onClick={prevSlide}
        className="absolute left-4 top-1/2 -translate-y-1/2 z-30 bg-black/30 hover:bg-black/50 text-white p-2 rounded-full"
        aria-label="Anterior slide"
      >
        <ChevronLeft size={24} />
      </button>

      <button
        onClick={nextSlide}
        className="absolute right-4 top-1/2 -translate-y-1/2 z-30 bg-black/30 hover:bg-black/50 text-white p-2 rounded-full"
        aria-label="Siguiente slide"
      >
        <ChevronRight size={24} />
      </button>

      <div className="absolute bottom-4 left-1/2 -translate-x-1/2 z-30 flex space-x-2">
        {slides.map((_, index) => (
          <button
            key={index}
            onClick={() => setCurrentSlide(index)}
            className={`w-3 h-3 rounded-full ${index === currentSlide ? "bg-white" : "bg-white/50"}`}
            aria-label={`Ir al slide ${index + 1}`}
          />
        ))}
      </div>
    </div>
  )
}
