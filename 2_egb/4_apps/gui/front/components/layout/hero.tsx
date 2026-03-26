"use client"

import { useState, useEffect } from "react"
import Image from "next/image"
import { Button } from "@/components/ui/button"
import { ChevronLeft, ChevronRight } from "lucide-react"
import Link from "next/link"

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

export default function Hero() {
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
    <div className="relative w-full h-[500px] overflow-hidden">
      {slides.map((slide, index) => (
        <div
          key={slide.id}
          className={`absolute inset-0 transition-opacity duration-1000 ${
            index === currentSlide ? "opacity-100" : "opacity-0"
          }`}
        >
          <div className="absolute inset-0 bg-black/30 z-10" />
          <Image
            src={slide.image || "/background_facultad.jpg"}
            alt={slide.title}
            fill
            className="object-cover"
            priority={index === 0}
          />
          <div className="absolute inset-0 flex flex-col justify-center items-start z-20 px-8 md:px-16 lg:px-24">
            <div className="max-w-2xl">
              <h1 className="text-3xl md:text-4xl lg:text-5xl font-bold text-white mb-6">{slide.title}</h1>
              <Button
                className="bg-teal-600 hover:bg-teal-700 text-white"
                asChild
              >
                <Link href={slide.buttonLink} className="flex items-center">
                  {slide.buttonText}
                </Link>
              </Button>
            </div>
          </div>
        </div>
      ))}

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
